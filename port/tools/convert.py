#!/usr/bin/env python3
"""
Story of the Blanks -> TI-99/4A asset converter.

Reads the decompiled AS3 (extracted/) and produces C data for the TI port:
  gen/chr.c / chr.h      - CHR pattern banks converted to TMS9918 8x8 patterns
                           (NES color index 3 -> off bit, indices 0/1/2 -> on bit)
                           plus a suggested single VDP color per tile.
  gen/maps.c / maps.h    - per-map RLE data (as stored in the SWF) + a table,
                           decoded at runtime on the TI exactly like the original.
  gen/palette.h          - NES 64-color master palette -> nearest TI VDP color LUT.

The map byte stream (after RLE+delta decode) is, in order:
  [0]            image pattern (CHR bank) index
  [1..16]        16 palette color indices (into the NES master palette)
  [64*60]        nametable tile indices   (decode order x outer 0..63, y inner 0..59)
  [32*30]        attribute palette indices (per 2x2 block; x outer 0..31, y inner 0..29)
  [64*60]        collision flags (BlockedTable)
  [4*n]          entity spawn quads: (entityType, tileX, tileY, selectedKey)

This script is the single source of truth for that layout; the C runtime
decoder in src/maps.c mirrors it.
"""
import os, re, sys, struct, zlib, glob

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXTRACT = os.path.join(ROOT, "extracted")
GEN = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "gen"))
os.makedirs(GEN, exist_ok=True)

SCROLL_X, SCROLL_Y = 64, 60        # ScrollTileRes
SCREEN_X, SCREEN_Y = 32, 30        # ScreenTileRes (NES); TI shows 32x24 of this
ATTR_W, ATTR_H = SCROLL_X // 2, SCROLL_Y // 2   # 32 x 30

# ---- NES master palette (orgColors from NESEngine.as), 0xRRGGBB -------------
NES_COLORS = [8158332,252,188,4466876,9699460,11010080,11014144,8918016,5255168,
 30720,26624,22528,16472,0,0,0,12369084,30968,22776,6833404,14155980,14942296,
 16267264,14965776,11303936,47104,43008,43076,34952,0,0,0,16316664,3980540,
 6850812,9992440,16283896,16275608,16283736,16556100,16300032,12122136,5822548,
 5830808,59608,7895160,0,0,16579836,10806524,12105976,14203128,16300280,16295104,
 15782064,16572584,16308344,14219384,12122296,12122328,64764,16308472,0,0]

# ---- TMS9918A palette RGB (legacy "TI" values), index 0 = transparent -------
TI_RGB = {
 0:None, 1:(0,0,0), 2:(33,200,66), 3:(94,220,120), 4:(84,85,237), 5:(125,118,252),
 6:(212,82,77), 7:(66,235,245), 8:(252,85,84), 9:(255,121,120), 10:(212,193,84),
 11:(230,206,128), 12:(33,176,59), 13:(201,91,186), 14:(204,204,204), 15:(255,255,255),
}
TI_NAMES = {0:"TRANS",1:"BLACK",2:"MEDGREEN",3:"LTGREEN",4:"DKBLUE",5:"LTBLUE",
 6:"DKRED",7:"CYAN",8:"MEDRED",9:"LTRED",10:"DKYELLOW",11:"LTYELLOW",12:"DKGREEN",
 13:"MAGENTA",14:"GRAY",15:"WHITE"}

def nearest_ti(rgb24):
    r=(rgb24>>16)&255; g=(rgb24>>8)&255; b=rgb24&255
    best=1; bd=1<<30
    for i,v in TI_RGB.items():
        if v is None: continue
        d=(r-v[0])**2+(g-v[1])**2+(b-v[2])**2
        if d<bd: bd=d; best=i
    return best

# NES color index (0..63) -> TI color index (1..15)
NES_TO_TI = [nearest_ti(c) for c in NES_COLORS]

# ---- minimal PNG loader (8-bit RGBA, colortype 6) --------------------------
def load_png_rgba(fn):
    d=open(fn,"rb").read(); assert d[:8]==b'\x89PNG\r\n\x1a\n', fn
    pos=8; w=h=ct=bitd=0; idat=b''
    while pos<len(d):
        ln=struct.unpack(">I",d[pos:pos+4])[0]; typ=d[pos+4:pos+8]
        chunk=d[pos+8:pos+8+ln]; pos+=12+ln
        if typ==b'IHDR': w,h,bitd,ct=struct.unpack(">IIBB",chunk[:10])
        elif typ==b'IDAT': idat+=chunk
        elif typ==b'IEND': break
    assert ct==6 and bitd==8, (fn,ct,bitd)
    raw=zlib.decompress(idat); bpp=4; stride=w*bpp
    out=bytearray(); prev=bytearray(stride); p=0
    for y in range(h):
        ft=raw[p]; p+=1; line=bytearray(raw[p:p+stride]); p+=stride
        if ft==1:
            for i in range(bpp,stride): line[i]=(line[i]+line[i-bpp])&255
        elif ft==2:
            for i in range(stride): line[i]=(line[i]+prev[i])&255
        elif ft==3:
            for i in range(stride): line[i]=(line[i]+((prev[i]+(line[i-bpp] if i>=bpp else 0))>>1))&255
        elif ft==4:
            for i in range(stride):
                a=line[i-bpp] if i>=bpp else 0; b=prev[i]; c=prev[i-bpp] if i>=bpp else 0
                pp=a+b-c; pa=abs(pp-a); pb=abs(pp-b); pc=abs(pp-c)
                pr=a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[i]=(line[i]+pr)&255
        out+=line; prev=line
    return w,h,out

def px_index(rgba):
    """NES CHR 2bpp color index from an RGBA pixel."""
    r,g,b,a=rgba
    if r>200 and g<80 and b<80:   return 0   # red
    if r>200 and g>200 and b>200: return 1   # white
    if r<80  and g<80  and b>200: return 2   # blue
    return 3                                  # black/transparent

# ---- parse Game.as: pattern-bank load order + map RLE strings --------------
def parse_game():
    src=open(os.path.join(EXTRACT,"script/scripts/blank/Game.as"),errors="replace").read()
    banks={}
    for m in re.finditer(r'AddPatternTable\(new\s+(\w+)\(\),\s*(\d+)\)',src):
        banks[int(m.group(2))]=m.group(1)
    maps={}
    for m in re.finditer(r'AddMap\("([-0-9,]*)"\s*,\s*(\d+)\)',src):
        maps[int(m.group(2))]=[int(x) for x in m.group(1).split(",") if x!=""]
    return banks, maps

# symbolN -> exported CHR png (128x128) via the DefineSprite_N_<Class> folder
def chr_png_for(classname):
    g=glob.glob(os.path.join(EXTRACT,"assets/sprites","DefineSprite_*_%s"%classname,"1.png"))
    return g[0] if g else None

def convert_chr(classname):
    """Return (spr_pats, bg_pats): two sets of 256 8-byte TI patterns.
    spr_pats: sprite silhouette - transparent(index3) -> off, else on.
    bg_pats:  background texture - each tile's DOMINANT colour -> off (so a
              mostly-uniform tile reads as flat background), the rest -> on.
              This avoids the 'solid block' look of the naive reduction."""
    fn=chr_png_for(classname)
    if fn is None: return None,None
    w,h,rgba=load_png_rgba(fn)
    assert (w,h)==(128,128), (fn,w,h)
    spr=[]; bg=[]
    for ty in range(16):
        for tx in range(16):
            # first pass: pixel indices + counts for this tile
            idxs=[]; counts=[0,0,0,0]
            for row in range(8):
                for col in range(8):
                    x=tx*8+col; y=ty*8+row; o=(y*w+x)*4
                    k=px_index(rgba[o:o+4]); idxs.append(k); counts[k]+=1
            dom=counts.index(max(counts))      # dominant colour index
            spat=[]; bpat=[]
            for row in range(8):
                sb=0; bb=0
                for col in range(8):
                    k=idxs[row*8+col]
                    if k!=3:  sb|=(0x80>>col)   # sprite: non-transparent = on
                    if k!=dom: bb|=(0x80>>col)  # bg: non-dominant = on (detail)
                spat.append(sb); bpat.append(bb)
            spr.append(spat); bg.append(bpat)
    return spr,bg

# ---- RLE+delta decode (matches Maps.LoadMap) -------------------------------
def decode_map(toks):
    flat=[]
    for i in range(0,len(toks)-1,2):
        flat.extend([toks[i+1]]*toks[i])
    for i in range(1,len(flat)): flat[i]=flat[i-1]+flat[i]
    p=0
    img=flat[p]; p+=1
    pal=flat[p:p+16]; p+=16
    nt=flat[p:p+SCROLL_X*SCROLL_Y]; p+=SCROLL_X*SCROLL_Y
    attr=flat[p:p+ATTR_W*ATTR_H]; p+=ATTR_W*ATTR_H
    blk=flat[p:p+SCROLL_X*SCROLL_Y]; p+=SCROLL_X*SCROLL_Y
    ents=[]
    while p+3 < len(flat)+1 and p+4<=len(flat):
        ents.append(tuple(flat[p:p+4])); p+=4
    return img,pal,nt,attr,blk,ents

def parse_dialogues():
    """Parse blank/entities/Dialogue.as StartDialogue cases into
    {id: (firstName, secondName, [messages])}. Messages keep their markers
    (< > digit / - *) which the C runtime interprets exactly like the AS3."""
    src=open(os.path.join(EXTRACT,"script/scripts/blank/entities/Dialogue.as"),
             errors="replace").read()
    # isolate the big switch body
    body=src[src.index("switch(param1)"):]
    body=body[:body.index("this.ClearDialogueBox")]
    out={}
    # split into case blocks
    parts=re.split(r'\bcase\s+(\d+)\s*:', body)
    # parts: [pre, id, block, id, block, ...]
    def unquote(concat):
        # join "..."+"..." string literals, decode AS escapes
        s="".join(re.findall(r'"((?:[^"\\]|\\.)*)"', concat))
        s=s.replace('\\"','"').replace("\\'","'").replace("\\\\","\\")
        return s
    for i in range(1,len(parts),2):
        cid=int(parts[i]); blk=parts[i+1]
        fn=re.search(r'firstName\s*=\s*"((?:[^"\\]|\\.)*)"',blk)
        sn=re.search(r'secondName\s*=\s*"((?:[^"\\]|\\.)*)"',blk)
        first=unquote('"%s"'%fn.group(1)) if fn else ""
        second=unquote('"%s"'%sn.group(1)) if sn else ""
        msgs=[]
        for m in re.finditer(r'messages\.push\((.*?)\);',blk,re.S):
            msgs.append(unquote(m.group(1)))
        out[cid]=(first,second,msgs)
    return out

def cstr(s):
    return '"'+s.replace('\\','\\\\').replace('"','\\"')+'"'

def emit_dialogues(dlg, want=None):
    if want is not None:
        dlg={k:v for k,v in dlg.items() if k in want}
    hh=open(os.path.join(GEN,"dialogues.h"),"w")
    cc=open(os.path.join(GEN,"dialogues.c"),"w")
    maxid=(max(dlg)+1) if dlg else 1
    hh.write("/* generated by convert.py - dialogue text (markers interpreted at runtime) */\n")
    hh.write("#ifndef GEN_DIALOGUES_H\n#define GEN_DIALOGUES_H\n")
    hh.write("#define DIALOGUE_COUNT %d\n"%maxid)
    hh.write("typedef struct { const char *first, *second; "
             "const char *const *msgs; unsigned char nmsgs; } DialogueDef;\n")
    hh.write("extern const DialogueDef dialogues[DIALOGUE_COUNT];\n#endif\n")
    cc.write('#include "dialogues.h"\n')
    for cid in sorted(dlg):
        first,second,msgs=dlg[cid]
        if not msgs: continue
        cc.write("static const char *const dmsg%d[]={%s};\n"%(
                 cid, ",".join(cstr(m) for m in msgs)))
    cc.write("const DialogueDef dialogues[DIALOGUE_COUNT]={\n")
    for cid in range(maxid):
        if cid in dlg and dlg[cid][2]:
            first,second,msgs=dlg[cid]
            cc.write("  [%d]={%s,%s,dmsg%d,%d},\n"%(
                     cid,cstr(first),cstr(second),cid,len(msgs)))
        else:
            cc.write("  [%d]={0,0,0,0},\n"%cid)
    cc.write("};\n")
    hh.close(); cc.close()

def emit_palette_h():
    with open(os.path.join(GEN,"palette.h"),"w") as f:
        f.write("/* generated by convert.py - NES master palette -> nearest TI VDP color */\n")
        f.write("#ifndef GEN_PALETTE_H\n#define GEN_PALETTE_H\n")
        f.write("/* index by NES color (0..63), yields TI COLOR_* value (1..15) */\n")
        f.write("extern const unsigned char nes_to_ti[64];\n")
        f.write("/* ti_near[c] = the other 14 TI colours, nearest (RGB) to c first;\n"
                " * used to move room colours off colours the sprites use */\n")
        f.write("extern const unsigned char ti_near[16][14];\n#endif\n")
    with open(os.path.join(GEN,"palette.c"),"w") as f:
        f.write('#include "palette.h"\n')
        f.write("const unsigned char nes_to_ti[64]={\n ")
        f.write(",".join(str(v) for v in NES_TO_TI))
        f.write("\n};\n")
        f.write("const unsigned char ti_near[16][14]={\n")
        for c in range(16):
            if c == 0:
                row = list(range(1, 15))
            else:
                cr = TI_RGB[c]
                # weighted RGB (G > B > R, roughly perceptual) - plain RGB
                # distance ranks e.g. magenta as the nearest colour to white
                row = sorted((o for o in range(1, 16) if o != c),
                             key=lambda o: sum(w*(a-b)**2 for w, a, b in
                                               zip((2, 4, 3), cr, TI_RGB[o])))
            f.write(" {%s},\n" % ",".join(str(v) for v in row))
        f.write("};\n")

def emit_chr(banks, spr_banks, bg_banks):
    hh=open(os.path.join(GEN,"chr.h"),"w")
    cc=open(os.path.join(GEN,"chr.c"),"w")
    hh.write("/* generated by convert.py - CHR banks as TMS9918 8x8 patterns */\n")
    hh.write("#ifndef GEN_CHR_H\n#define GEN_CHR_H\n")
    cc.write('#include "chr.h"\n')
    want=sorted(set(spr_banks)|set(bg_banks))
    for idx in want:
        cls=banks.get(idx)
        if not cls: continue
        spr,bg=convert_chr(cls)
        if spr is None:
            print("  WARN no CHR png for bank %d (%s)"%(idx,cls)); continue
        variants=[]
        if idx in spr_banks: variants.append(("",spr))
        if idx in bg_banks:  variants.append(("_bg",bg))
        for suffix,pats in variants:
            name="chr_bank%d%s"%(idx,suffix)
            hh.write("extern const unsigned char %s[256*8]; /* %s %s */\n"%(
                     name,cls,"sprite" if suffix=="" else "background"))
            cc.write("/* bank %d %s: %s */\nconst unsigned char %s[256*8]={\n"%(
                     idx,"sprite" if suffix=="" else "bg",cls,name))
            for pat in pats:
                cc.write(" "+",".join(str(b) for b in pat)+",\n")
            cc.write("};\n")
    hh.write("#endif\n"); hh.close(); cc.close()

def byte_rle(arr):
    """(count 1..255, value) pairs covering arr exactly."""
    out=[]; i=0; n=len(arr)
    while i<n:
        v=arr[i]; run=1
        while i+run<n and arr[i+run]==v and run<255: run+=1
        out.append(run); out.append(v); i+=run
    return out

def compact_map(toks):
    """Re-encode one map into the TI-native byte stream:
       chr(1), pal[16], nametable byte-RLE (3840 cells, storage order x+y*64),
       collision[480] bitpacked, nent(1), then nent*(type,tx,ty,key).
       The NES attribute table is dropped (Graphics I doesn't use it)."""
    img,pal,nt,attr,blk,ents=decode_map(toks)   # nt/blk in decode order (x outer,y inner)
    N=SCROLL_X*SCROLL_Y
    nt =(list(nt) +[0]*N)[:N]                    # zero-pad short (near-empty) maps
    blk=(list(blk)+[0]*N)[:N]
    pal=(list(pal)+[0]*16)[:16]
    nts=[0]*N; coll=bytearray(N//8)
    for j in range(N):
        x=j//SCROLL_Y; y=j%SCROLL_Y; idx=x+y*SCROLL_X
        nts[idx]=nt[j]&0xff
        if blk[j]: coll[idx>>3]|=1<<(idx&7)
    out=bytearray()
    out.append(img&0xff)
    out+=bytes(((p&0xff) for p in pal))
    out+=bytes(byte_rle(nts))
    out+=coll
    out.append(len(ents)&0xff)
    for (t,tx,ty,k) in ents:
        out+=bytes((t&0xff, tx&0xff, ty&0xff, k&0xff))
    return out

def compact_map_bg(toks, m, bg):
    """Banked-cart map stream (halftone backgrounds, see bgdither.py):
       bank(1), border colour(1), colour table[32] (TI fg<<4|bg per 8-tile
       group), nametable byte-RLE of per-map tile ids (storage x+y*64),
       collision[480] bitpacked, nent(1), nent*(type,tx,ty,key)."""
    img,pal,nt,attr,blk,ents=decode_map(toks)
    N=SCROLL_X*SCROLL_Y
    blk=(list(blk)+[0]*N)[:N]
    nts=[0]*N; coll=bytearray(N//8)
    for j in range(N):
        x=j//SCROLL_Y; y=j%SCROLL_Y; idx=x+y*SCROLL_X
        nts[idx]=bg["nt"][y][x]
        if blk[j]: coll[idx>>3]|=1<<(idx&7)
    out=bytearray()
    out.append(bg["bank"]&0xff)
    out.append(bg["border"]&0xff)
    out+=bytes(bg["colors"])
    out+=bytes(byte_rle(nts))
    out+=coll
    out.append(len(ents)&0xff)
    for (t,tx,ty,k) in ents:
        out+=bytes((t&0xff, tx&0xff, ty&0xff, k&0xff))
    return out

def emit_maps(maps, which):
    hh=open(os.path.join(GEN,"maps.h"),"w")
    cc=open(os.path.join(GEN,"maps.c"),"w")
    MAXMAPS = 32
    hh.write("/* generated by convert.py - compact TI map streams (decoded at runtime) */\n")
    hh.write("#ifndef GEN_MAPS_H\n#define GEN_MAPS_H\n")
    hh.write("#define MAP_COUNT %d\n"%MAXMAPS)
    hh.write("#define SCROLL_X %d\n#define SCROLL_Y %d\n"%(SCROLL_X,SCROLL_Y))
    hh.write("extern const unsigned char *const maps[MAP_COUNT]; /* 0 if absent */\n#endif\n")
    cc.write('#include "maps.h"\n')
    for idx in which:
        data=compact_map(maps.get(idx,[]))
        cc.write("static const unsigned char map_%d[]={\n "%idx)
        cc.write(",".join(str(b) for b in data))
        cc.write("\n};\n")
    cc.write("const unsigned char *const maps[MAP_COUNT]={\n")
    for idx in range(MAXMAPS):
        cc.write("  [%d]=%s,\n"%(idx, ("map_%d"%idx) if idx in which else "0"))
    cc.write("};\n")
    hh.close(); cc.close()

BANK_CAP = 8146          # 8192 - 46-byte cart header
FIRST_PHYS = 3           # user data banks start at physical bank 3

def carr(sym, data):
    """C definition of a const byte array + its size."""
    body=",".join(str(b) for b in data)
    return "const unsigned char %s[]={%s};\n"%(sym,body), len(data)

def dialogue_blob(cid, first, second, msgs):
    """C text + byte size for one dialogue (strings, pointer array, names).
       All of it stays in one bank so a single switch covers the conversation."""
    lines=[]; size=0
    for i,m in enumerate(msgs):
        lines.append("static const char d%d_%d[]=%s;\n"%(cid,i,cstr(m)))
        size+=len(m)+1
    lines.append("const char *const dmsg%d[]={%s};\n"%(
        cid, ",".join("d%d_%d"%(cid,i) for i in range(len(msgs)))))
    size+=2*len(msgs)
    lines.append("const char dfirst%d[]=%s;\n"%(cid,cstr(first)))
    lines.append("const char dsecond%d[]=%s;\n"%(cid,cstr(second)))
    size+=len(first)+1+len(second)+1
    return "".join(lines), (size+1)&~1

def skeleton_chars():
    """The fireplace skeleton (dialogue 24's portrait: face 6 of pattern
    bank 13, a 32x32 two-layer picture) as TI character graphics: the bone
    pixels (CHR 'white', the colour the scare palette shows) doubled to
    64x64 = 8x8 characters, row-major, 8 bytes each."""
    import bgdither
    px = bgdither.bank_pixels(13)
    face = 6
    base = 64 + (face - 1) * 8 + ((face - 1) // 2) * 48
    bone = [[0] * 32 for _ in range(32)]
    for layer in (0, 4):
        for ty in range(4):
            for tx in range(4):
                t = base + layer + ty * 16 + tx
                for y in range(8):
                    for x in range(8):
                        if px[(t // 16) * 8 + y][(t % 16) * 8 + x] == 1:
                            bone[ty * 8 + y][tx * 8 + x] = 1
    out = []
    for cy in range(8):
        for cx in range(8):
            for r in range(8):
                b = 0
                for c2 in range(8):
                    if bone[(cy * 8 + r) // 2][(cx * 8 + c2) // 2]: b |= 0x80 >> c2
                out.append(b)
    return out

def emit_banked(banks, spr_banks, bg_banks, want_maps, dlg):
    MAXMAPS=32; DCOUNT=(max(dlg)+1) if dlg else 1
    # ---- build blobs: (size, ctext, kind, key, symbol) ----
    blobs=[]
    chr_cache={}
    def getchr(b):
        if b not in chr_cache: chr_cache[b]=convert_chr(banks[b])
        return chr_cache[b]
    for b in spr_banks:
        spr,bg=getchr(b)
        flat=[x for pat in spr for x in pat]
        ct,sz=carr("chr_bank%d"%b, flat)
        blobs.append([sz,ct,"chrs",b,"chr_bank%d"%b])
    import bgdither
    bginfo={}
    for m in want_maps:
        bg=bginfo[m]=bgdither.build(m)
        data=compact_map_bg(maps.get(m,[]), m, bg)
        ct,sz=carr("map_%d"%m, data)
        blobs.append([sz,ct,"map",m,"map_%d"%m])
        ct,sz=carr("map_bg_%d"%m, list(bg["chr"]))
        blobs.append([sz,ct,"bgc",m,"map_bg_%d"%m])
        if "alt_chr" in bg:
            # variant blob: border(1), colours[32], then its CHR
            ct,sz=carr("map_bg2_%d"%m, [bg["alt_border"]]+list(bg["alt_colors"])+list(bg["alt_chr"]))
            blobs.append([sz,ct,"bgc2",m,"map_bg2_%d"%m])
    ct,sz=carr("skel_chr", skeleton_chars())
    blobs.append([sz,ct,"skel",0,"skel_chr"])
    for cid in sorted(dlg):
        first,second,msgs=dlg[cid]
        if not msgs: continue
        ct,sz=dialogue_blob(cid,first,second,msgs)
        blobs.append([sz,ct,"dlg",cid,"dmsg%d"%cid])
    # ---- first-fit-decreasing pack into banks ----
    for bl in blobs:
        if bl[0]>BANK_CAP: raise SystemExit("blob %s is %d bytes > bank cap %d"%(bl[4],bl[0],BANK_CAP))
    blobs.sort(key=lambda x:-x[0])
    bins=[]   # each: [used, [blobs]]
    for bl in blobs:
        for b in bins:
            if b[0]+bl[0]<=BANK_CAP: b[0]+=bl[0]; b[1].append(bl); break
        else:
            bins.append([bl[0],[bl]])
    # physbank per bin
    phys_of={}
    for i,b in enumerate(bins):
        phys=FIRST_PHYS+i
        for bl in b[1]: phys_of[(bl[2],bl[3])]=phys
    # ---- emit per-bank .c (drop stale banks from a bigger earlier build) ----
    for old in glob.glob(os.path.join(GEN,"bk[0-9][0-9].[co]")): os.remove(old)
    for i,b in enumerate(bins):
        phys=FIRST_PHYS+i
        with open(os.path.join(GEN,"bk%02d.c"%phys),"w") as f:
            f.write('#include "banks.h"\n/* physical bank %d */\n'%phys)
            for bl in b[1]: f.write(bl[1])
    # ---- banks.h ----
    with open(os.path.join(GEN,"banks.h"),"w") as f:
        f.write("/* generated by convert.py - banked resource manifest */\n")
        f.write("#ifndef GEN_BANKS_H\n#define GEN_BANKS_H\n")
        f.write("#define SCROLL_X %d\n#define SCROLL_Y %d\n"%(SCROLL_X,SCROLL_Y))
        f.write("#define MAP_COUNT %d\n#define DIALOGUE_COUNT %d\n"%(MAXMAPS,DCOUNT))
        f.write("typedef struct { const char *first,*second; const char *const *msgs; unsigned char nmsgs; } DialogueDef;\n")
        for bl in blobs:
            if bl[2] in ("chrs","map","bgc","bgc2"): f.write("extern const unsigned char %s[];\n"%bl[4])
            elif bl[2]=="skel": pass
            else:
                cid=bl[3]; f.write("extern const char *const dmsg%d[];\n"%cid)
                f.write("extern const char dfirst%d[],dsecond%d[];\n"%(cid,cid))
        f.write("extern const unsigned char *const chr_spr[16];\n")
        f.write("extern const unsigned char chr_spr_phys[16];\n")
        f.write("/* per-map halftone background CHR (+ the BoneFiend variant) */\n")
        f.write("extern const unsigned char *const map_bg[MAP_COUNT];\n")
        f.write("extern const unsigned char map_bg_phys[MAP_COUNT];\n")
        f.write("extern const unsigned int map_bg_len[MAP_COUNT];\n")
        f.write("extern const unsigned char *const map_bg2[MAP_COUNT];\n")
        f.write("extern const unsigned char map_bg2_phys[MAP_COUNT];\n")
        f.write("extern const unsigned char map_bg2_bank[MAP_COUNT];\n")
        f.write("extern const unsigned char *const maps[MAP_COUNT];\n")
        f.write("extern const unsigned char map_phys[MAP_COUNT];\n")
        f.write("extern const DialogueDef dialogues[DIALOGUE_COUNT];\n")
        f.write("extern const unsigned char dlg_phys[DIALOGUE_COUNT];\n")
        f.write("/* fireplace skeleton portrait: 8x8 chars (64x64 px), row-major */\n")
        f.write("extern const unsigned char skel_chr[];\n")
        f.write("extern const unsigned char skel_phys;\n")
        f.write("#endif\n")
    # ---- tables.c (lives in fixed) ----
    with open(os.path.join(GEN,"tables.c"),"w") as f:
        f.write('#include "banks.h"\n')
        def tbl(name,n,fn):
            f.write("const unsigned char *const %s[%d]={"%(name,n))
            f.write(",".join(fn(i) for i in range(n))); f.write("};\n")
        tbl("chr_spr",16, lambda i:("chr_bank%d"%i) if i in spr_banks else "0")
        f.write("const unsigned char chr_spr_phys[16]={%s};\n"%
                ",".join(str(phys_of.get(("chrs",i),0)) for i in range(16)))
        tbl("maps",MAXMAPS, lambda i:("map_%d"%i) if i in want_maps else "0")
        tbl("map_bg",MAXMAPS, lambda i:("map_bg_%d"%i) if i in bginfo else "0")
        f.write("const unsigned char map_bg_phys[MAP_COUNT]={%s};\n"%
                ",".join(str(phys_of.get(("bgc",i),0)) for i in range(MAXMAPS)))
        f.write("const unsigned int map_bg_len[MAP_COUNT]={%s};\n"%
                ",".join(str(len(bginfo[i]["chr"]) if i in bginfo else 0) for i in range(MAXMAPS)))
        tbl("map_bg2",MAXMAPS, lambda i:("map_bg2_%d"%i) if i in bginfo and "alt_chr" in bginfo[i] else "0")
        f.write("const unsigned char map_bg2_phys[MAP_COUNT]={%s};\n"%
                ",".join(str(phys_of.get(("bgc2",i),0)) for i in range(MAXMAPS)))
        f.write("const unsigned char map_bg2_bank[MAP_COUNT]={%s};\n"%
                ",".join(str(bginfo[i].get("alt_bank",0) if i in bginfo else 0) for i in range(MAXMAPS)))
        f.write("const unsigned char map_phys[MAP_COUNT]={%s};\n"%
                ",".join(str(phys_of.get(("map",i),0)) for i in range(MAXMAPS)))
        f.write("const DialogueDef dialogues[DIALOGUE_COUNT]={\n")
        for cid in range(DCOUNT):
            if cid in dlg and dlg[cid][2]:
                f.write("  [%d]={dfirst%d,dsecond%d,dmsg%d,%d},\n"%(cid,cid,cid,cid,len(dlg[cid][2])))
            else: f.write("  [%d]={0,0,0,0},\n"%cid)
        f.write("};\n")
        f.write("const unsigned char dlg_phys[DIALOGUE_COUNT]={%s};\n"%
                ",".join(str(phys_of.get(("dlg",i),0)) for i in range(DCOUNT)))
        f.write("const unsigned char skel_phys=%d;\n"%phys_of[("skel",0)])
    # ---- cart.ld ----
    nb=len(bins)
    with open(os.path.join(GEN,"cart.ld"),"w") as f:
        f.write("OUTPUT_FORMAT(\"elf32-tms9900\",\"elf32-tms9900\",\"elf32-tms9900\")\n")
        f.write("OUTPUT_ARCH(tms9900)\nENTRY(_start)\n")
        f.write("MEMORY {\n")
        f.write("  INIT  (xr)  : ORIGIN = 0x6000, LENGTH = 512\n")
        # crt0 copies only banks 0-2 minus the cart header, loader and .data
        # (~0x5e62 bytes) into >A000 - a longer fixed region would silently
        # lose its tail, so the linker gets the real limit (with headroom)
        f.write("  FIXED (xrw) : ORIGIN = 0xA000, LENGTH = 0x5e00\n")
        f.write("  STACK (xrw) : ORIGIN = 0x2000, LENGTH = 8K\n")
        for i in range(nb):
            f.write("  CART%-3d (XR) : ORIGIN = 0x602e, LENGTH = 8146\n"%(FIRST_PHYS+i))
        f.write("  FLASH (XR)  : ORIGIN = 0, LENGTH = 16M\n}\n")
        f.write("SECTIONS {\n  PROVIDE (__executable_start = 0xA000);\n  . = 0x6000;\n")
        f.write("  loader : { crt0_cart.o } > INIT AT> FLASH\n  . = 0xA000;\n")
        f.write("  fixed : {\n")
        f.write("    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .text)\n")
        f.write("    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .text.*)\n")
        f.write("    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .rodata)\n")
        f.write("    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .rodata.*)\n")
        f.write("  } > FIXED AT> FLASH\n")
        for i in range(nb):
            phys=FIRST_PHYS+i
            f.write("  bank%d : { *bk%02d.o(.text .text.* .rodata .rodata.*) } > CART%d AT> FLASH\n"%(phys,phys,phys))
        f.write("  . = 0x2080;\n  .data : { *(.data) *(.data.*) } > STACK AT> FLASH\n")
        f.write("  .bss : { *(.bss) *(.bss.*) *(COMMON) } > STACK AT> FLASH\n")
        f.write("  _end = .;\n}\n")
    print("  packed %d blobs into %d banks (phys %d..%d), cart data banks ready"%(
          len(blobs),nb,FIRST_PHYS,FIRST_PHYS+nb-1))
    for i,b in enumerate(bins):
        print("    bank%d: %5d/%d used: %s"%(FIRST_PHYS+i,b[0],BANK_CAP,
              ",".join(bl[4] for bl in b[1])))

if __name__=="__main__":
    banks,maps=parse_game()
    def parselist(v): return [int(x) for x in v.split(",") if x!=""]
    # SPR=banks needing sprite patterns, BG=banks needing background patterns
    spr_banks=parselist(os.environ.get("SPR","1"))
    bg_banks=parselist(os.environ.get("BG","6"))
    want_maps=parselist(os.environ.get("MAPS","4"))
    if os.environ.get("ALL"):
        spr_banks=bg_banks=sorted(banks.keys()); want_maps=sorted(maps.keys())
    dlg=parse_dialogues()
    dlg_want=dlg
    if not os.environ.get("ALL") and os.environ.get("DLG"):
        w=set(parselist(os.environ.get("DLG")))
        dlg_want={k:v for k,v in dlg.items() if k in w}
    emit_palette_h()
    print("pattern banks:",banks)
    if os.environ.get("BANKED"):
        print("BANKED build: spr",spr_banks,"bg",bg_banks,"maps",len(want_maps),"dlg",len(dlg_want))
        emit_banked(banks, spr_banks, bg_banks, want_maps, dlg_want)
    else:
        emit_chr(banks, spr_banks, bg_banks)
        emit_maps(maps, want_maps)
        emit_dialogues(dlg, set(dlg_want))
        print("FLAT build: spr",spr_banks,"bg",bg_banks,"maps",want_maps,"dlg",sorted(dlg_want))
    print("wrote gen/ ->",GEN)
