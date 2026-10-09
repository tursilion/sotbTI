#!/usr/bin/env python3
"""Render the TI screen from a cartsim VRAM dump (Graphics I + 16x16 sprites).
usage: tools/cartsim/run.sh N ; python3 tools/cartsim/screenshot.py out.png"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import convert as c
from PIL import Image
v = open(os.path.join(HERE, 'vram.bin'), 'rb').read()
r = open(os.path.join(HERE, 'vregs.bin'), 'rb').read()
SIT, CT, PDT, SAL = r[2] * 0x400, r[3] * 0x40, (r[4] & 7) * 0x800, (r[5] & 0x7f) * 0x80
SDT, back = (r[6] & 7) * 0x800, r[7] & 15
rgb = lambda i, d: c.TI_RGB[i] if i else c.TI_RGB[back] or (0, 0, 0)
img = Image.new('RGB', (256, 192))
px = img.load()
for row in range(24):
    for col in range(32):
        ch = v[SIT + row * 32 + col]
        cc = v[CT + ch // 8]
        for y in range(8):
            b = v[PDT + ch * 8 + y]
            for x in range(8):
                px[col * 8 + x, row * 8 + y] = rgb(cc >> 4 if b & (0x80 >> x) else cc & 15, 0)
sprites = []
for n in range(32):
    y, x, name, col = v[SAL + n * 4:SAL + n * 4 + 4]
    if y == 0xD0: break
    sprites.append((y, x, name, col))
for (y, x, name, col) in reversed(sprites):   # slot 0 drawn last = in front
    if col & 15 == 0: continue
    for yy in range(16):
        for xx in range(16):
            byte = v[SDT + name * 8 + (xx // 8) * 16 + yy]
            sx, sy = x + xx, (y + 1 + yy) & 0xff
            if byte & (0x80 >> (xx % 8)) and sx < 256 and sy < 192:
                px[sx, sy] = c.TI_RGB[col & 15]
img.resize((768, 576), Image.NEAREST).save(sys.argv[1])
print("SIT %04x CT %04x PDT %04x SAL %04x SDT %04x backdrop %d, %d sprites" %
      (SIT, CT, PDT, SAL, SDT, back, len(sprites)))
