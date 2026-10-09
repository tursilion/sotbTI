#!/usr/bin/env python3
"""Render what the TI draws for each character (two single-colour 16x16
sprite layers composed from the NES CHR sheets) as an enlarged PNG, next to
the original NES-coloured art, so sprite/colour changes can be checked
without a Classic99 round trip.

usage: python3 tools/sprite_preview.py out.png
Character table below mirrors entity.c ent_spawn (keep them in sync).
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import convert as c
from PIL import Image

# TMS9918 palette (index 0 transparent)
TI_RGB = [None, (0,0,0), (33,200,66), (94,220,120), (84,85,237), (125,118,252),
          (212,82,77), (66,235,245), (252,85,84), (255,121,120), (212,193,84),
          (230,206,128), (33,176,59), (201,91,186), (204,204,204), (255,255,255)]
GROUND = (60, 110, 60)

_sheets = {}
def sheet(bank):
    """bank -> 128x128 list of NES pixel indices (0,1,2 colours; 3 transparent)"""
    if bank not in _sheets:
        banks, _ = c.parse_game()
        w, h, rgba = c.load_png_rgba(c.chr_png_for(banks[bank]))
        _sheets[bank] = [[c.px_index(rgba[(y*w+x)*4:(y*w+x)*4+4]) for x in range(128)]
                         for y in range(128)]
    return _sheets[bank]

def tile_px(bank, t, row, col):
    t &= 0xff                     # tile numbers wrap like the TI compose
    s = sheet(bank)
    return s[(t // 16) * 8 + row][(t % 16) * 8 + col]

def nes_cell(bank, base, rows=3):
    """the original 16 x (8*rows) cell: tiles base+16*r, base+1+16*r"""
    return [[tile_px(bank, base + 16*(y//8) + (x//8), y % 8, x % 8)
             for x in range(16)] for y in range(8*rows)]

def draw(img, ox, oy, grid, colour_of, scale):
    for y, line in enumerate(grid):
        for x, k in enumerate(line):
            col = colour_of(k)
            if col is None: continue
            for dy in range(scale):
                for dx in range(scale):
                    img.putpixel((ox + x*scale + dx, oy + y*scale + dy), col)

def nes_rgb(n):
    v = c.NES_COLORS[n & 63]
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)

def pony_tiles(body_index, hair_index, d):
    body = body_index*56 + 8*d + (32 if ((d > 1 and body_index == 0) or (d > 0 and body_index == 1)) else 0)
    hair = 144 + hair_index*6 + (36 if hair_index > 1 else 0) + 2*d
    return body, hair

def ti_layer(cell, rows, mask):
    """16x16 from the chosen source rows of a 16x24 cell; pixel on if index in mask"""
    return [[1 if cell[r][x] in mask else 0 for x in range(16)] for r in rows]

ROWS_CURRENT = [r for r in range(24) if r % 3 != 1]     # what compose16 samples
ROWS_OLD = list(range(0, 8)) + list(range(16, 24))      # pre-2026-10-08: T + T+32

def render(chars, out, rows=ROWS_CURRENT, scale=4):
    cw = 16*scale + 12
    img = Image.new('RGB', (cw*len(chars) + 8, (24 + 16)*scale + 40), GROUND)
    for i, ch in enumerate(chars):
        ox = 6 + i*cw
        bank, body, hair, bpal, hpal, tib, tih, bmask, hmask = ch[1:10]
        nb, nh = nes_cell(bank, body), nes_cell(bank, hair)
        # NES original: body, then hair in front
        draw(img, ox, 6, nb, lambda k: None if k == 3 else nes_rgb(bpal[k]), scale)
        draw(img, ox, 6, nh, lambda k: None if k == 3 else nes_rgb(hpal[k]), scale)
        # TI: body layer, hair layer in front
        ty = 6 + 24*scale + 12
        if body is not None:
            draw(img, ox, ty, ti_layer(nb, rows, bmask), lambda k: TI_RGB[tib] if k else None, scale)
        if hair is not None:
            draw(img, ox, ty, ti_layer(nh, rows, hmask), lambda k: TI_RGB[tih] if k else None, scale)
    img.save(out)

# NES sprite palettes are GLOBAL in the original (4 shared sub-palettes), so a
# mane often uses colours another character set - these are the resolved ones.
PAL = {'abB': (56,47,39), 'abH': (22,20,48), 'gh': (16,63,0), 'st': (32,63,17),
       'gl': (24,63,8), 'ze': (15,40,32), 'twB': (51,63,35), 'twH': (3,20,48),
       'nm': (31,31,31)}
ALL = {0, 1, 2}
DKBLUE = 4

def characters(d=1):
    ti = lambda n: c.NES_TO_TI[n]
    def pony(name, bank, bi, hi, bp, hp, tb, th):
        b, h = pony_tiles(bi, hi, d)
        return (name, bank, b, h, PAL[bp], PAL[hp], tb, th, ALL, ALL)
    # (keep in sync with entity.c ent_spawn bodyCol/hairCol)
    return [pony('AB',        1, 1, 1, 'abB', 'abH', ti(56), ti(22)),
            pony('GreyHoof',  2, 0, 0, 'gh',  'gh',  ti(16), ti(63)),
            pony('Starlet',   2, 0, 2, 'st',  'abH', ti(32), ti(22)),
            pony('Roneo',     2, 0, 3, 'abB', 'st',  ti(56), ti(17)),
            pony('Gladstone', 3, 0, 2, 'gl',  'abB', ti(24), ti(39)),
            pony('Ruby',      3, 1, 3, 'gh',  'abB', ti(16), ti(56)),
            pony('Zecora',    1, 0, 2, 'gh',  'ze',  ti(16), ti(15)),
            pony('Twilight',  1, 0, 0, 'twB', 'twH', ti(51), ti(3)),
            pony('Nightmare', 4, 0, 0, 'nm',  'nm',  ti(31), DKBLUE)]

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'sprites_preview.png'
    # second arg 'old' previews the previous rows 0-7 + 16-23 composition
    rows = ROWS_OLD if 'old' in sys.argv[2:] else ROWS_CURRENT
    render(characters(), out, rows)
    print('wrote', out, '(top: NES original, bottom: TI two-sprite render)')
