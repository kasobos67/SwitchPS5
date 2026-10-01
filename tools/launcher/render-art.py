#!/usr/bin/env python3
# ProsperoEden - Launcher art from the source images (run by tools/launcher/assets.sh).
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes headless/prosperoeden/ui/art:

  backdrop.tga       the dusk artwork behind every screen, darkened towards the left where the
                     text sits, 2048x1152 (larger than the 1920x1080 screen: it drifts slowly)
  backdrop-blur.tga  the same picture heavily blurred and small: what frosted panels show
  brand.tga          the app icon, for the header and for games without cover art
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "headless/prosperoeden/ui/art"


def save_tga(image, name):
    image.convert("RGBA").save(ART / name, orientation=1)
    print(f"{name}: {image.size[0]}x{image.size[1]}")


def backdrop(source):
    size = (2048, 1152)
    art = source.resize(size, Image.Resampling.LANCZOS)
    art = ImageEnhance.Color(art).enhance(0.82)
    art = ImageEnhance.Brightness(art).enhance(0.72)
    art = art.filter(ImageFilter.GaussianBlur(6)).convert("RGBA")
    # Dark on the left, where titles and lists sit; the picture opens to the right.
    overlay = Image.new("RGBA", size)
    draw = ImageDraw.Draw(overlay)
    stops = ((0, .97), (.26, .94), (.48, .70), (.72, .28), (1, .05))
    for x in range(size[0]):
        fraction = x / (size[0] - 1)
        for (a, first), (b, second) in zip(stops, stops[1:]):
            if fraction <= b:
                opacity = first + (second - first) * (fraction - a) / (b - a)
                break
        draw.line((x, 0, x, size[1]), fill=(6, 9, 10, round(opacity * 255)))
    art.alpha_composite(overlay)
    # And towards the bottom, under the hint line.
    fade = Image.new("RGBA", size)
    draw = ImageDraw.Draw(fade)
    for y in range(size[1]):
        opacity = max(0, (y / size[1] - .68) / .32) * .65
        draw.line((0, y, size[0], y), fill=(3, 7, 7, round(opacity * 255)))
    art.alpha_composite(fade)
    return art


def grain(image, strength):
    """Fine noise, so the dark gradients do not band on a TV."""
    noise = Image.effect_noise(image.size, 12).point(lambda value: 255 if value > 128 else 0)
    layer = Image.new("RGBA", image.size, (220, 230, 216, 0))
    layer.putalpha(noise.point(lambda value: round(value * strength)))
    result = image.copy()
    result.alpha_composite(layer)
    return result


def main():
    ART.mkdir(parents=True, exist_ok=True)
    source = Image.open(ROOT / "assets/background-source.png").convert("RGB")
    art = backdrop(source)
    save_tga(grain(art, .025), "backdrop.tga")
    # A quarter of the size, blurred until only light and colour remain.
    small = art.resize((512, 288), Image.Resampling.LANCZOS).filter(ImageFilter.GaussianBlur(9))
    save_tga(small, "backdrop-blur.tga")
    icon = Image.open(ROOT / "assets/prosperoeden-icon-source.png").convert("RGBA")
    save_tga(icon.resize((384, 384), Image.Resampling.LANCZOS), "brand.tga")


if __name__ == "__main__":
    main()
