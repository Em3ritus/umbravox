#!/usr/bin/env python3
"""Convert a square source PNG to the bounded UVE indexed RGB565 format.
Usage: python tools/png_to_emblem.py source.png emblem.bin
Requires Pillow; intended for host asset preparation only. No cropping or redraw.
"""
import sys
from pathlib import Path
from PIL import Image

def convert(source, target):
    image=Image.open(source).convert('RGB')
    if image.width != image.height:
        raise ValueError('source must already be square')
    image=image.resize((96,96),Image.Resampling.LANCZOS)
    indexed=image.quantize(colors=16,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    palette=indexed.getpalette(); data=bytes(indexed.getdata())
    count=max(data)+1
    if not 2<=count<=16: raise ValueError('invalid palette size')
    colors=[]
    for i in range(count):
        r,g,b=palette[3*i:3*i+3]
        # Reserve near-black as transparent in the radar-emblem renderer.
        if max(r,g,b)<20:r=g=b=0
        colors.append(((r&248)<<8)|((g&252)<<3)|(b>>3))
    result=b'UVE\x01'+bytes([96,96,count,0])+b''.join(c.to_bytes(2,'little') for c in colors)+data
    assert len(result)<=9728
    Path(target).write_bytes(result)
    print(f'{target}: 96x96, {count} colors, {len(result)} bytes')
if __name__=='__main__':convert(*sys.argv[1:])
