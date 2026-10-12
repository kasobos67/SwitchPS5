#!/usr/bin/env python3
"""Bake selective decoded original UI pixels, without a firmware/key dependency.
Original mirror wrapping and part placement assemble the stock pictograms.
Rotation sprite sheets preserve authored wait/cursor texture motion without
requiring changes to the emulator's graphics pipeline.
"""
from pathlib import Path
import json,hashlib,math,io
from PIL import Image,ImageOps,ImageDraw,ImageChops
ROOT=Path(__file__).resolve().parents[2]; art=ROOT/'assets/art';target=ROOT/'headless/prosperoeden/ui/art'
manifest=json.loads((ROOT/'assets/firmware-ui-pixels.json').read_text());input_dir=art/'native'
def tex(group,name):return Image.open(input_dir/(group+'-'+name+'.png')).convert('RGBA')
def mirror_h(im):
 out=Image.new('RGBA',(im.width*2,im.height));out.alpha_composite(im);out.alpha_composite(ImageOps.mirror(im),(im.width,0));return out
def mirror_hv(im):
 im=mirror_h(im);out=Image.new('RGBA',(im.width,im.height*2));out.alpha_composite(im);out.alpha_composite(ImageOps.flip(im),(0,im.height));return out
def tint_mask(im):
 out=Image.new('RGBA',im.size,'white');out.putalpha(im.getchannel('A'));return out
def icon(im,x=0,y=0):
 im=tint_mask(im).resize((im.width*2,im.height*2),Image.Resampling.LANCZOS)
 out=Image.new('RGBA',(128,128));out.alpha_composite(im,((128-im.width)//2+int(x*2),(128-im.height)//2-int(y*2)));return out
def inset_sprite(im,rect):
 im=im.resize((rect[2],rect[3]),Image.Resampling.LANCZOS);atlas.alpha_composite(im,(rect[0],rect[1]));return rect
def shortcut_mask(name,colour):
 # Shortcut PNGs include a grey circular button. Recover only their coloured
 # foreground coverage; the existing HOME button draws its theme background.
 source=Image.open(art/'home-shortcuts'/name).convert('RGBA')
 channel=max(range(3),key=lambda i:abs(colour[i]-80))
 denominator=colour[channel]-80
 alpha=Image.new('L',source.size)
 alpha.putdata([max(0,min(255,round((pixel[channel]-80)*pixel[3]/denominator)))
                for pixel in source.getdata()])
 out=Image.new('RGBA',source.size,'white');out.putalpha(alpha)
 return out.resize((128,128),Image.Resampling.LANCZOS)
def write_changed(path,data):
 if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
def save_changed(im,path,**kwargs):
 output=io.BytesIO();im.save(output,format=path.suffix[1:],**kwargs)
 write_changed(path,output.getvalue())
atlas=Image.new('RGBA',(2048,1024));rects={}
news_icon=shortcut_mask('info.png',(255,77,77))
shop=shortcut_mask('game-files.png',(255,174,46))
gear=icon(mirror_hv(tex('ResidentMenu','RdtIcoSet_s')))
power=icon(mirror_h(tex('ResidentMenu','RdtIcoPwrForm_s')))
multiplayer=shortcut_mask('multiplayer.png',(74,155,219))
controller=shortcut_mask('controller.png',(255,255,255))
for i,(name,im) in enumerate([('news',news_icon),('shop',shop),('multiplayer',multiplayer),('joycon',controller),('settings',gear),('power',power)]):rects[name]=inset_sprite(im,(i*132+2,2,128,128))
for i,(name,group,pixel,size) in enumerate([('balloon_tip','Flaunch','BalloonTip_t',(22,26)),('balloon_shadow','Flaunch','BalloonTipShdw_t',(24,40)),('launch_effect','ResidentMenu','EffectLaunch_00_t',(220,220)),('plus','common','IcoBtnPlusRev_24_s',(48,48)),('check','common','IcoCheck_72_t',(72,72))]):
 im=tex(group,pixel)
 # P_EffectDecide uses UV 0..2 with mirror wrapping on both axes. The
 # stored110px texture is one corner, not the entire launch glow.
 if name=='launch_effect':im=mirror_hv(im)
 rects[name]=inset_sprite(im,(i*256+2,136,*size))
# Original HOME wireless glyphs from MiniSet/HudSignal, preserving Nintendo's
# inactive/partial/full pixel shapes instead of drawing replacement arcs.
for i,(name,pixel) in enumerate([('wifi_unset','wifi-unset'),('wifi_connecting','wifi-01'),('wifi_connected','wifi-00')]):
 rects[name]=inset_sprite(tex('MiniSet',pixel),(1282+i*36,136,32,32))
wait=tint_mask(tex('common','waiticon_00_d'));wait=wait.resize((84,84),Image.Resampling.LANCZOS)
wait_center=Image.new('RGBA',(128,128));wait_center.alpha_composite(wait,(22,22))
for frame in range(60):
 # FLTS rotation: -2 to -362 degrees, linear slope -6 degrees/frame.
 im=wait_center.rotate(-2-6*frame,resample=Image.Resampling.BICUBIC,expand=False)
 inset_sprite(im,(frame%8*132+2,364+frame//8*82,80,80))
cursor=tex('ResidentMenu','CursorEff_08_w');alpha=cursor.getchannel('R');cursor=Image.new('RGBA',cursor.size,'white');cursor.putalpha(alpha)
for frame in range(72):
 im=cursor.rotate(-90-frame*10,resample=Image.Resampling.BICUBIC,expand=False)
 inset_sprite(im,(1060+frame%12*80,364+frame//12*80,76,76))
save_changed(atlas,art/'stock-ui.png');save_changed(atlas,target/'stock-ui.tga',orientation=1)
profiles=Image.new('RGBA',(2048,4864))
for i,row in enumerate(manifest['profiles']):profiles.alpha_composite(Image.open(ROOT/row['asset']).convert('RGBA'),(i%8*256,i//8*256))
save_changed(profiles,art/'stock-profiles.png');save_changed(profiles,target/'stock-profiles.tga',orientation=1)
def uv(rect):x,y,w,h=rect;return f'{{{x}.0f/2048.0f,{y}.0f/1024.0f,{w}.0f/2048.0f,{h}.0f/1024.0f}}'
header='''// Original firmware UI pixels and measurements, selectively converted.
// Generated by tools/launcher/firmware-ui-atlas.py. Presentation only.
#pragma once
#include "pe/gfx/draw_list.hpp"
#include <algorithm>
#include <cmath>
namespace pe::ui::stock_assets {
enum class Icon { news, shop, multiplayer, joycon, settings, power, balloon_tip,
                  balloon_shadow, launch_effect, plus, check,
                  wifi_unset, wifi_connecting, wifi_connected };
inline gfx::Rect icon_uv(Icon icon) {
    constexpr gfx::Rect rects[] = {
'''
header+=''.join('        '+uv(rects[n])+', // '+n+'\n' for n in rects)
header+='''    };
    const int index=std::clamp(static_cast<int>(icon),0,13);
    return rects[index];
}
inline gfx::Rect wait_uv(float frame) {
    const int i=static_cast<int>(std::floor(frame))%60;
    const int safe=i<0 ? i+60 : i;
    return {(safe%8*132+2)/2048.0f,(364+safe/8*82)/1024.0f,80.0f/2048.0f,80.0f/1024.0f};
}
inline gfx::Rect cursor_texture_uv(float frame) {
    const int i=static_cast<int>(std::floor(frame/10.0f))%72;
    const int safe=i<0 ? i+72 : i;
    return {(1060+safe%12*80)/2048.0f,(364+safe/12*80)/1024.0f,76.0f/2048.0f,76.0f/1024.0f};
}
inline constexpr int kProfileIconCount=147;
inline constexpr int kProfileColorCount=72;
inline gfx::Rect profile_uv(int icon) {
    icon=std::clamp(icon,0,kProfileIconCount-1);
    return {icon%8*0.125f,icon/8*(1.0f/19.0f),0.125f,1.0f/19.0f};
}
inline gfx::Color profile_color(int color) {
    constexpr std::uint32_t colors[] = {
'''
for row in manifest['colors']:
 r,g,b,a=row['rgba']; header+=f'        0x{r:02x}{g:02x}{b:02x}, // {row["name"]}\n'
header+='''    };
    return gfx::Color::rgb(colors[std::clamp(color,0,kProfileColorCount-1)]);
}
// Authored dimensions in1080p; shader falloff uses the existing UI draw layer.
inline constexpr float kArtworkShadowDistance=1.5f;
inline constexpr float kArtworkShadowSize=7.5f;
inline constexpr float kFocusShadowDistance=12.0f;
inline constexpr float kFocusShadowSize=6.0f;
inline constexpr float kCursorBorder=7.5f;
inline constexpr float kCursorShadowDistance=4.5f;
inline constexpr float kCursorShadowSize=4.5f;
inline constexpr float kCursorGlowSize=6.0f;
inline constexpr float kDialogRadius=7.5f;
inline constexpr float kDialogShadowDistance=1.5f;
inline constexpr float kDialogShadowSize=6.0f;
} // namespace pe::ui::stock_assets
'''
write_changed(ROOT/'headless/prosperoeden/pe/ui/stock_assets.hpp',header.replace('\n','\r\n').encode('utf-8'))
generated={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [target/'stock-ui.tga',target/'stock-profiles.tga',art/'stock-ui.png',art/'stock-profiles.png',ROOT/'headless/prosperoeden/pe/ui/stock_assets.hpp']}
(ROOT/'assets/firmware-ui-atlas.json').write_text(json.dumps(dict(dimensions=[2048,1024],icon_rects=rects,wait_frames=60,cursor_texture_frames=72,profiles=len(manifest["profiles"]),profile_dimensions=[2048,4864],palette_colors=72,generated_sha256=generated),indent=2)+'\n')
print('Supplied HOME pictograms, original Joy-Con,60waitframes,72cursorframes,147profiles,72colors baked')
