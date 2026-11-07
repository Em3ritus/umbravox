#!/usr/bin/env python3
"""Rebuild the four 64px crew emblems from their RGBA sources (Pillow)."""
from pathlib import Path
from PIL import Image,ImageOps
root=Path(__file__).resolve().parents[1]
for folder in ['slick','z-skuld','z-baph0mau5','z-em3ritus']:
 d=root/'packs'/folder
 src=Image.open(d/'logo-simplified.png').convert('RGBA')
 # Normalize to the firmware texture format; retain aspect and transparent edges.
 scaled=ImageOps.contain(src,(64,64),Image.Resampling.LANCZOS)
 img=Image.new('RGBA',(64,64),(0,0,0,255));img.alpha_composite(scaled,((64-scaled.width)//2,(64-scaled.height)//2))
 q=img.convert('RGB').quantize(colors=12,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
 data=bytes(q.get_flattened_data());pal=q.getpalette();n=max(data)+1;colors=[]
 for i in range(n):
  r,g,b=pal[i*3:i*3+3]
  if max(r,g,b)<20:r=g=b=0
  colors.append(((r&248)<<8)|((g&252)<<3)|(b>>3))
 (d/'emblem.bin').write_bytes(b'UVE\x01'+bytes([64,64,n,0])+b''.join(c.to_bytes(2,'little') for c in colors)+data)
