#!/usr/bin/env python3
"""Rebuild selected native symbol assets. Requires Pillow; presentation only."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from collections import deque
ROOT=Path(__file__).resolve().parents[2]
art=ROOT/'assets/art';target=ROOT/'headless/prosperoeden/ui/art'
font=ImageFont.truetype(str(ROOT/'assets/fonts/Nintendo-Symbols.ttf'),256)
def glyph(cp,size):
 image=Image.new('RGBA',(512,512));draw=ImageDraw.Draw(image)
 draw.text((256,256),chr(cp),font=font,fill='white',anchor='mm')
 box=image.getchannel('A').getbbox()
 if not box: raise ValueError(f'Missing symbol {cp:x}')
 image=image.crop(box);image.thumbnail(size,Image.Resampling.LANCZOS)
 canvas=Image.new('RGBA',size)
 canvas.alpha_composite(image,((size[0]-image.width)//2,(size[1]-image.height)//2))
 return canvas
atlas=Image.open(art/'stock-icons.png').convert('RGBA')
for cp,x in [(0xe041,0),(0xe0d5,32),(0xe0d4,64),(0xe0b5,96),(0xe14b,128),(0xe14d,160)]:
 atlas.paste(glyph(cp,(32,32)),(x,64))
# Fill the letter cutout in the native filled-A glyph, retaining its antialiased outline.
base=glyph(0xe0e0,(32,32));alpha=base.getchannel('A');pixels=alpha.load()
seen=set();todo=deque([(x,edge) for x in range(32) for edge in (0,31)]+[(edge,y) for y in range(32) for edge in (0,31)])
while todo:
 x,y=todo.popleft()
 if (x,y) in seen or not (0<=x<32 and 0<=y<32) or pixels[x,y]>=128: continue
 seen.add((x,y));todo.extend([(x-1,y),(x+1,y),(x,y-1),(x,y+1)])
for y in range(32):
 for x in range(32):
  if (x,y) not in seen: pixels[x,y]=255
assert all(pixels[x,y]<128 for x,y in [(0,0),(31,0),(0,31),(31,31)]), 'Button outline must retain transparent corners'
base=Image.new('RGBA',base.size,'white');base.putalpha(alpha);base.save(art/'stock-button-base.png');atlas.paste(base,(192,64))
atlas.save(art/'stock-icons.png');atlas.save(target/'stock-icons.tga',orientation=1)
controller=glyph(0xe12c,(144,100))
controller.save(art/'pro-controller.png');controller.save(target/'controller.tga',orientation=1)
