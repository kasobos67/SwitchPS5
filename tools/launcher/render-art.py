#!/usr/bin/env python3
# ProsperoEden - Launcher art from the source images (run by tools/launcher/assets.sh).
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes headless/prosperoeden/ui/art:

  backdrop.tga       one flat gray pixel; the menu background is drawn by the UI
  backdrop-blur.tga  the same compatibility asset; panels are flat, without artwork blur
  brand.tga          the app icon, for the header and for games without cover art
  controller.tga     a controller in white on transparent, tinted by the home screen: 144x100,
                     twice the size it is drawn at

render-art.py [name...] renders only the named pictures (backdrop, brand, controller).
"""

import sys

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "headless/prosperoeden/ui/art"


def save_tga(image, name):
    image.convert("RGBA").save(ART / name, orientation=1)
    print(f"{name}: {image.size[0]}x{image.size[1]}")


def controller():
    """The controller as a mask: the shell is opaque, its touchpad, sticks and buttons are holes.

    Drawn in a 72x50 box (the size on screen) at 16 times that, then reduced to 144x100.
    """
    k = 16
    mask = Image.new("L", (72 * k, 50 * k), 0)
    draw = ImageDraw.Draw(mask)

    def box(x0, y0, x1, y1, radius, fill):
        draw.rounded_rectangle((x0 * k, y0 * k, x1 * k, y1 * k), radius * k, fill=fill)

    def disc(x, y, radius, fill):
        draw.ellipse(((x - radius) * k, (y - radius) * k, (x + radius) * k, (y + radius) * k), fill=fill)

    def bar(x0, y0, x1, y1, width, fill):
        draw.line((x0 * k, y0 * k, x1 * k, y1 * k), fill=fill, width=round(width * k))
        disc(x0, y0, width / 2, fill)
        disc(x1, y1, width / 2, fill)

    # The shell: a wide body and two grips that lean outwards.
    box(3.5, 5, 68.5, 34.5, 13, 255)
    bar(15.5, 25, 10, 40.5, 13.5, 255)
    bar(56.5, 25, 62, 40.5, 13.5, 255)
    # Touchpad, with the two small buttons beside it.
    box(25, 8.5, 47, 20.5, 3.2, 0)
    bar(21.6, 9.6, 21.2, 12.6, 1.5, 0)
    bar(50.4, 9.6, 50.8, 12.6, 1.5, 0)
    # Direction pad and the four buttons.
    box(9.2, 15.4, 18.2, 18.2, 1.2, 0)
    box(12.3, 12.3, 15.1, 21.3, 1.2, 0)
    for dx, dy in ((0, -3.7), (3.7, 0), (0, 3.7), (-3.7, 0)):
        disc(58.3 + dx, 16.8 + dy, 1.55, 0)
    # Sticks (a ring around a cap) and the round button between them.
    for x in (26.2, 45.8):
        disc(x, 27.6, 4.7, 0)
        disc(x, 27.6, 2.9, 255)
    disc(36, 27.4, 1.5, 0)
    mask = mask.resize((144, 100), Image.Resampling.LANCZOS)
    icon = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    icon.putalpha(mask)
    return icon


def main():
    ART.mkdir(parents=True, exist_ok=True)
    wanted = set(sys.argv[1:]) or {"backdrop", "brand", "controller", "stock-icons"}
    if "backdrop" in wanted:
        art = Image.new("RGBA", (1, 1), (45, 45, 45, 255))
        save_tga(art, "backdrop.tga")
        # Retain the existing asset name for the texture-loading interface.
        small = art.copy()
        save_tga(small, "backdrop-blur.tga")
    if "brand" in wanted:
        icon = Image.open(ROOT / "sce_sys/icon-source.png").convert("RGBA")
        save_tga(icon.resize((512, 512), Image.Resampling.LANCZOS), "brand.tga")
    if "controller" in wanted:
        save_tga(Image.open(ROOT / "assets/art/pro-controller.png"), "controller.tga")
    if "stock-icons" in wanted:
        save_tga(Image.open(ROOT / "assets/art/stock-icons.png"), "stock-icons.tga")


if __name__ == "__main__":
    main()
