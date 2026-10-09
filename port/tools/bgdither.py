#!/usr/bin/env python3
"""Background tiles for the TI: per-map halftone (grey-scale dither).

The NES colours a background pixel from its CHR index and the cell's
attribute sub-palette (NESEngine: red/white/blue -> sub-palette slots 0/1/2,
black -> the universal backdrop pal[3]). For each map we:
  1. resolve every (tile, sub-palette) the map uses to real NES colours,
  2. give that tile a colour pair: the two TI colours covering most of its
     pixels, drawn exactly (so a colour looks the same in every tile - e.g.
     leaves don't flip where a trunk crosses them). Graphics I has 32 colour
     groups of 8 tiles; tiles sharing a pair are packed into one group,
  3. halftone any remaining colours by luminance between the pair with a 4x4
     clustered-dot screen, quantised DOWN to 0/25/50/75/100% (light = set
     pixel; rounding down keeps light shades sparse, and 25% is a 2x2 clump,
     never a lone dot). Font tiles (letters) never halftone: leftovers snap
     to the nearer colour so text stays crisp,
  4. dedupe the 8x8 results into a per-map CHR and remap the nametable.
A second CHR variant can be built on the same tile ids (maps 11-16 switch to
the nightmare background when the BoneFiends wake).

usage (preview): python3 tools/bgdither.py MAP out.png  -> NES vs TI render
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import convert as c

# 4x4 clustered-dot threshold matrix: grows from the centre 2x2 outwards
CLUSTER = [[12, 5, 6, 13],
           [4,  0, 1, 7],
           [11, 3, 2, 8],
           [15, 10, 9, 14]]
LEVELS = 4                      # coverage steps: 0, 1/4, 1/2, 3/4, 1

def luma(nes):
    v = c.NES_COLORS[nes & 63]
    return 0.299 * ((v >> 16) & 255) + 0.587 * ((v >> 8) & 255) + 0.114 * (v & 255)

_pix = {}
def bank_pixels(bank):
    """128x128 CHR indices (0,1,2 = sub-palette slots, 3 = backdrop)"""
    if bank not in _pix:
        banks, _ = c.parse_game()
        w, h, rgba = c.load_png_rgba(c.chr_png_for(banks[bank]))
        _pix[bank] = [[c.px_index(rgba[(y * w + x) * 4:(y * w + x) * 4 + 4])
                       for x in range(128)] for y in range(128)]
    return _pix[bank]

def tile_colors(bank, tile, sub, pal):
    """8x8 NES colours of one tile drawn with sub-palette `sub`"""
    px = bank_pixels(bank)
    ty, tx = (tile // 16) * 8, (tile % 16) * 8
    return [[pal[3] if k == 3 else pal[sub * 4 + k]
             for k in px[ty + r][tx:tx + 8]] for r in range(8)]

# per-map adjustments the original makes at run time before you can see the
# map (Applebloom's intro switches map 0 to the forest tiles + recolours it)
MAP_BANK = {0: 6}
MAP_PAL = {0: {3: 23, 12: 11, 13: 10, 14: 9}}
# second background variant: (bank, palette changes) - BoneFiend.cycle0
ALT = {m: (8, {2: 7, 3: 7, 4: 7, 5: 7}) for m in range(11, 17)}

# NESEngine.asciiToTileIndex (background text uses these tiles)
ASCII_TILE = {' ': 32, '=': 213, '!': 224, '?': 225, '.': 226, ',': 227}
ASCII_TILE.update({str(d): 214 + d for d in range(1, 10)})
ASCII_TILE.update({chr(ord('A') + i): 230 + i for i in range(26)})

# tutorial hints rewritten for the TI's controls (same lengths, same boxes)
TEXT_PATCH = {0: [("USE ARROWS TO MOVE", "USE JOYST TO MOVE ")],
              1: [("USE Z TO INTERACT", "FIRE TO INTERACT ")]}

def patch_text(m, cells):
    for old, new in TEXT_PATCH.get(m, []):
        want = [ASCII_TILE[ch] for ch in old]
        hit = False
        for y, row in enumerate(cells):
            tiles = [t for t, s in row]
            for x in range(len(tiles) - len(want) + 1):
                if tiles[x:x + len(want)] == want:
                    for i, ch in enumerate(new):
                        row[x + i] = (ASCII_TILE[ch], row[x + i][1])
                    hit = True
        if not hit:
            raise SystemExit("map %d: text %r not found" % (m, old))

def map_cells(m):
    """decode map m -> (bank, pal, cells) with cells[y][x] = (tile, sub)"""
    banks, maps = c.parse_game()
    img, pal, nt, attr, blk, ents = c.decode_map(maps[m])
    N = c.SCROLL_X * c.SCROLL_Y
    nt = (list(nt) + [0] * N)[:N]
    attr = (list(attr) + [0] * (c.ATTR_W * c.ATTR_H))[:c.ATTR_W * c.ATTR_H]
    pal = (list(pal) + [0] * 16)[:16]
    for i, v in MAP_PAL.get(m, {}).items(): pal[i] = v
    bank = MAP_BANK.get(m, img)
    cells = [[(nt[x * c.SCROLL_Y + y] & 0xff,
               attr[(x // 2) * c.ATTR_H + (y // 2)] & 3)
              for x in range(c.SCROLL_X)] for y in range(c.SCROLL_Y)]
    patch_text(m, cells)
    return bank, pal, cells

FONT_TILES = range(224, 256)    # NESEngine.asciiToTileIndex: ! ? . , A-Z (213-223
                                # hold box-frame pieces in the bg banks)

def dither_tile(cols, ld, ll, crisp=False, fixed=None):
    """8 pattern bytes, light pixels set. fixed maps a NES colour straight to
    0/1 (the tile's two exact colours); anything else is halftoned by
    luminance between ld and ll (clustered screen, rounded down), or snapped
    to the nearer end when crisp (font tiles)."""
    out = []
    span = (ll - ld) or 1.0
    for r in range(8):
        b = 0
        for x in range(8):
            col = cols[r][x]
            if fixed and col in fixed:
                on = fixed[col]
            else:
                cov = max(0.0, min(1.0, (luma(col) - ld) / span))
                if crisp:
                    on = cov >= 0.5
                else:
                    n = int(cov * LEVELS + 1e-6) * (16 // LEVELS)
                    on = CLUSTER[r & 3][x & 3] < n
            if on:
                b |= 0x80 >> x
        out.append(b)
    return tuple(out)

NEUTRAL = (1, 14, 15)            # TI black, grey, white

def nes_rgb(nes):
    v = c.NES_COLORS[nes & 63]
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)

def is_grey(rgb):
    return max(rgb) - min(rgb) < 32

def ti(nes):
    """NES -> TI colour for backgrounds. Greys stay neutral (the TI has no
    mid grey, and plain nearest-colour turns NES grey into light green).
    Plain nearest-colour also sends the NES dark green / dark teal / dark
    brown to TI black, which erases grass and forest; a clearly coloured
    dark keeps its hue's TI colour instead."""
    t = c.NES_TO_TI[nes & 63]
    rgb = nes_rgb(nes)
    if is_grey(rgb):
        l = luma(nes)
        return min(NEUTRAL, key=lambda i: abs(tluma(i) - l))
    if t != 1 or max(rgb) < 40:
        return t
    k = 180.0 / max(rgb)
    lit = [x * k for x in rgb]
    return min((i for i in range(2, 14)),
               key=lambda i: sum((a - b) ** 2 for a, b in zip(lit, c.TI_RGB[i])))

def tluma(t):
    r, g, b = c.TI_RGB[t]
    return 0.299 * r + 0.587 * g + 0.114 * b

def darker(nes, fg):
    """nearest TI colour to a NES colour that is clearly darker than fg
    (greys only to darker neutrals)"""
    rgb = nes_rgb(nes)
    pool = NEUTRAL if is_grey(rgb) else range(1, 16)
    cand = [i for i in pool if tluma(i) < tluma(fg) - 20] or [1]
    return min(cand, key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb, c.TI_RGB[i])))

def lighter(nes, t):
    """nearest TI colour to a NES colour that is clearly lighter than t"""
    rgb = nes_rgb(nes)
    pool = NEUTRAL if is_grey(rgb) else range(1, 16)
    cand = [i for i in pool if tluma(i) > tluma(t) + 20] or [15]
    return min(cand, key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb, c.TI_RGB[i])))

def colour_map(used):
    """NES colour -> (light TI, dark TI, coverage of light 0..16) for one map,
    decided once so a colour looks the same in every tile. used = {colour:
    pixel count}. NES shades that collapse onto one TI colour but differ in
    brightness (dark leaves on grass, dark text on a green box) are spread
    around the group's most-used shade, which stays solid: darker shades
    blend toward a darker TI colour, lighter ones toward a lighter one, each
    a halftone rounded down (the extremes become solid)."""
    tmap, groups = {}, {}
    for col in used:
        groups.setdefault(ti(col), []).append(col)
    for t, cs in groups.items():
        anchor = max(cs, key=lambda x: used[x])
        la = luma(anchor)
        darks = [x for x in cs if luma(x) < la - 10]
        lights = [x for x in cs if luma(x) > la + 10]
        dk = darker(min(darks, key=luma), t) if darks else t
        lt = lighter(max(lights, key=luma), t) if lights else t
        lo = min([luma(x) for x in darks] or [la])
        hi = max([luma(x) for x in lights] or [la])
        for col in cs:
            l = luma(col)
            if col in darks:                 # t over dk
                cov = (l - lo) / (la - lo)
                tmap[col] = (t, dk, int(cov * LEVELS + 1e-6) * (16 // LEVELS))
            elif col in lights:              # lt over t
                cov = (l - la) / (hi - la)
                tmap[col] = (lt, t, int(cov * LEVELS + 1e-6) * (16 // LEVELS))
            else:
                tmap[col] = (t, t, 16)
    return tmap

def tile_plan(cols, crisp, tmap):
    """-> ((fg, bg) TI colours, pattern). The two TI colours carrying most
    of the tile are used; each colour is drawn as its map-wide rendering
    when the pair allows it, otherwise halftoned by luminance."""
    nes_n = {}
    for row in cols:
        for col in row: nes_n[col] = nes_n.get(col, 0) + 1
    weight, rep = {}, {}
    for col, k in nes_n.items():
        lt, dk, cov = tmap[col]
        for t, w in ((lt, k * cov / 16.0), (dk, k * (16 - cov) / 16.0)):
            if w <= 0: continue
            weight[t] = weight.get(t, 0) + w
            if t not in rep or w > rep[t][1]: rep[t] = (col, w)
    top = sorted(weight, key=lambda t: -weight[t])[:2]
    if len(top) == 1:
        return (top[0], top[0]), (0,) * 8
    a, b = top
    if tluma(a) < tluma(b): a, b = b, a                  # a = light (fg)
    out_a, out_b = a, b
    if crisp and tluma(a) - tluma(b) < 80:               # letters must read
        if tluma(a) > 100: out_b = 1
        else: out_a = 15
    ll, ld = luma(rep[a][0]), luma(rep[b][0])
    if ll <= ld: ll, ld = tluma(a), tluma(b)
    out = []
    for r in range(8):
        byte = 0
        for x in range(8):
            col = cols[r][x]
            lt, dk, cov = tmap[col]
            if lt == a and (dk == b or cov == 16):
                n = cov                                  # its own rendering
            elif cov == 0 and dk == b:
                n = 0
            elif lt == b and cov == 16:
                n = 0
            else:                                        # leftover colour
                f = max(0.0, min(1.0, (luma(col) - ld) / ((ll - ld) or 1.0)))
                n = 16 if (crisp and f >= 0.5) else (0 if crisp else
                    int(f * LEVELS + 1e-6) * (16 // LEVELS))
            if crisp and 0 < n < 16:
                n = 16 if n >= 8 else 0                  # letters stay crisp
            if CLUSTER[r & 3][x & 3] < n:
                byte |= 0x80 >> x
        out.append(byte)
    return (out_a, out_b), tuple(out)

def rgbd(a, b):
    return sum((x - y) ** 2 for x, y in zip(c.TI_RGB[a], c.TI_RGB[b]))

def build(m):
    """-> dict(bank, nt[y][x] tile ids, chr bytes, colors[32] (fg<<4|bg),
               border, n tiles, groups, [alt_bank, alt_chr, alt_colors,
               alt_border])"""
    bank, pal, cells = map_cells(m)
    counts = {}
    for row in cells:
        for p in row: counts[p] = counts.get(p, 0) + 1
    alt = ALT.get(m)
    variants = [(bank, pal)]
    if alt:                       # the nightmare background on the same ids
        apal = list(pal)
        for i, v in alt[1].items(): apal[i] = v
        variants.append((alt[0], apal))
    # per (tile, sub-palette) and per variant: TI colour pair + pattern. Each
    # variant makes its own map-wide colour decision.
    info = {p: [(), ()] for p in counts}
    for vbank, vpal in variants:
        used = {}
        for p, k in counts.items():
            for row in tile_colors(vbank, p[0], p[1], vpal):
                for col in row: used[col] = used.get(col, 0) + k
        tmap = colour_map(used)
        for p in counts:
            crisp = p[0] in FONT_TILES
            pr, pat = tile_plan(tile_colors(vbank, p[0], p[1], vpal), crisp, tmap)
            if not any(pat): pr = (pr[1], pr[1])         # all background
            elif all(b == 0xff for b in pat):            # all foreground
                pat = (0,) * 8; pr = (pr[0], pr[0])
            info[p][0] += (pr,)
            info[p][1] += (pat,)
    # usage per colour key (the pair in every variant); merge the rarest into
    # their nearest neighbour until every key's tiles fit the 32 groups of 8
    def plan():
        uses, tiles = {}, {}
        for p, (prs, pats) in info.items():
            uses[prs] = uses.get(prs, 0) + counts[p]
            tiles.setdefault(prs, set()).add(pats)
        return uses, tiles
    def dist(a, b):
        return sum(rgbd(x[0], y[0]) + rgbd(x[1], y[1]) for x, y in zip(a, b))
    uses, tiles = plan()
    while sum((len(t) + 7) // 8 for t in tiles.values()) > 32:
        rare = min(uses, key=uses.get)
        near = min((q for q in uses if q != rare), key=lambda q: dist(q, rare))
        for v in info.values():
            if v[0] == rare: v[0] = near
        uses, tiles = plan()
    # lay out: each key owns whole groups; tile id = 8 * group + slot
    nv = len(variants)
    ids, chrs = {}, [{} for _ in range(nv)]
    colors = [[0x11] * 32 for _ in range(nv)]
    g = 0
    for prs in sorted(uses, key=lambda q: -uses[q]):
        for k, pats in enumerate(sorted(tiles[prs])):
            if k % 8 == 0:
                for v in range(nv):
                    colors[v][g] = (prs[v][0] << 4) | prs[v][1]
                g += 1
            tid = (g - 1) * 8 + k % 8
            ids[(prs, pats)] = tid
            for v in range(nv): chrs[v][tid] = pats[v]
    n = g * 8
    nt = [[ids[(info[p][0], info[p][1])] for p in row] for row in cells]
    # Graphics I doesn't care which colour is "set": make each group's
    # background nibble the colour covering most of the map, so the run-time
    # sprite-colour protection (which moves backgrounds) acts on what fills
    # the screen (e.g. the red plank floor in Roneo's house).
    for v in range(nv):
        on = [0] * 32; tot = [0] * 32
        for row in nt:
            for tid in row:
                on[tid // 8] += sum(bin(b).count("1") for b in chrs[v][tid])
                tot[tid // 8] += 64
        for grp in range(g):
            if tot[grp] and on[grp] * 2 > tot[grp]:
                cc = colors[v][grp]
                colors[v][grp] = ((cc & 15) << 4) | (cc >> 4)
                for tid in range(grp * 8, grp * 8 + 8):
                    if tid in chrs[v]:
                        chrs[v][tid] = tuple(b ^ 0xff for b in chrs[v][tid])
    def flat(d): return bytes(b for t in range(n) for b in d.get(t, (0,) * 8))
    common = ids[(info[max(counts, key=counts.get)][0],
                  info[max(counts, key=counts.get)][1])]
    res = {"bank": bank, "nt": nt, "n": n, "colors": colors[0],
           "border": colors[0][common // 8] & 15, "chr": flat(chrs[0]), "groups": g}
    if alt:
        res["alt_bank"] = alt[0]
        res["alt_chr"] = flat(chrs[1])
        res["alt_colors"] = colors[1]
        res["alt_border"] = colors[1][common // 8] & 15
    return res

def preview(m, out, scale=2):
    """NES render (top) vs TI halftone render (bottom) of the whole map"""
    from PIL import Image
    bank, pal, cells = map_cells(m)
    r = build(m)
    W, H = c.SCROLL_X * 8, c.SCROLL_Y * 8
    img = Image.new('RGB', (W * scale, (H * 2 + 8) * scale), (40, 40, 40))
    def put(x, y, rgb):
        for dy in range(scale):
            for dx in range(scale):
                img.putpixel((x * scale + dx, y * scale + dy), rgb)
    def rgb(n):
        v = c.NES_COLORS[n & 63]; return ((v >> 16) & 255, (v >> 8) & 255, v & 255)
    tirgb = lambda n: c.TI_RGB[c.NES_TO_TI[n & 63]]
    chr_ = r["chr"]
    for ty in range(c.SCROLL_Y):
        for tx in range(c.SCROLL_X):
            t, s = cells[ty][tx]
            cols = tile_colors(bank, t, s, pal)
            tid = r["nt"][ty][tx]
            for yy in range(8):
                byte = chr_[tid * 8 + yy]
                for xx in range(8):
                    put(tx * 8 + xx, ty * 8 + yy, rgb(cols[yy][xx]))
                    pair = r["colors"][tid // 8]
                    put(tx * 8 + xx, H + 8 + ty * 8 + yy,
                        c.TI_RGB[pair >> 4 if byte & (0x80 >> xx) else pair & 15]
                        or (0, 0, 0))
    img.save(out)
    return r

if __name__ == "__main__":
    r = preview(int(sys.argv[1]), sys.argv[2])
    print("map %s: bank %d, %d tile slots in %d colour groups" %
          (sys.argv[1], r["bank"], r["n"], r["groups"]))
