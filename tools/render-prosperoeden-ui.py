"""Regenerate the native menu artwork from the checked-in source image."""

from pathlib import Path
from xml.sax.saxutils import escape
from math import cos, pi, sin

from PIL import Image, ImageColor, ImageDraw, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "headless/prosperoeden/ui"


def rounded(name, size, radius, fill, stroke, stroke_width=1, gradient=None):
    width, height = size
    scale = 2
    image = Image.new("RGBA", (width * scale, height * scale))
    draw = ImageDraw.Draw(image)
    bounds = (2 * scale, 2 * scale, (width - 3) * scale, (height - 3) * scale)
    if gradient:
        mask = Image.new("L", image.size)
        ImageDraw.Draw(mask).rounded_rectangle(bounds, radius * scale, fill=255)
        start, end = (ImageColor.getcolor(color, "RGBA") for color in gradient)
        colors = Image.new("RGBA", image.size)
        painter = ImageDraw.Draw(colors)
        for x in range(width * scale):
            t = x / (width * scale - 1)
            painter.line((x, 0, x, height * scale),
                         fill=tuple(round(a + (b - a) * t) for a, b in zip(start, end)))
        image.alpha_composite(Image.composite(colors, Image.new("RGBA", image.size), mask))
        draw.rounded_rectangle(bounds, radius * scale, outline=stroke,
                               width=stroke_width * scale)
    else:
        draw.rounded_rectangle(bounds, radius * scale, fill=fill,
                               outline=stroke, width=stroke_width * scale)
    image.resize(size, Image.Resampling.LANCZOS).save(
        UI / "chrome" / f"{name}.tga", orientation=1)
    defs = (f'<defs><linearGradient id="focus"><stop stop-color="{gradient[0]}"/>'
            f'<stop offset="1" stop-color="{gradient[1]}"/></linearGradient></defs>') if gradient else ""
    svg_fill = "url(#focus)" if gradient else escape(fill)
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
           f'height="{height}" viewBox="0 0 {width} {height}">'
           f'{defs}<rect x="2" y="2" width="{width - 5}" height="{height - 5}" '
           f'rx="{radius}" fill="{svg_fill}" stroke="{escape(stroke)}" '
           f'stroke-width="{stroke_width}"/></svg>\n')
    (UI / "chrome" / f"{name}.svg").write_text(svg)


def render_icons():
    scale = 4
    white, lime = "#f2f4e9", "#dfe8a6"
    for name in ("continue-playing", "load-rom", "settings", "help"):
        image = Image.new("RGBA", (52 * scale, 52 * scale))
        draw = ImageDraw.Draw(image)
        def points(*xy):
            return [(x * scale, y * scale) for x, y in xy]
        if name == "continue-playing":
            draw.arc((8*scale, 8*scale, 44*scale, 44*scale), 45, 355,
                     fill=white, width=3*scale)
            draw.line(points((38, 7), (43, 14), (34, 15)), fill=white,
                      width=3*scale, joint="curve")
            draw.polygon(points((22, 18), (35, 26), (22, 34)), fill=lime)
        elif name == "load-rom":
            draw.line(points((7, 37), (7, 17), (20, 17), (25, 22),
                             (44, 22), (44, 39), (9, 39)), fill=white,
                      width=3*scale, joint="curve")
            draw.polygon(points((24, 26), (34, 32), (24, 38)), fill=lime)
        elif name == "settings":
            gear = [((26 + (20 if i % 3 == 0 else 16) * cos((i*15-90)*pi/180))*scale,
                     (26 + (20 if i % 3 == 0 else 16) * sin((i*15-90)*pi/180))*scale)
                    for i in range(24)]
            draw.line(gear + gear[:1], fill=white, width=3*scale, joint="curve")
            draw.ellipse((20*scale, 20*scale, 32*scale, 32*scale),
                         outline=white, width=3*scale)
        else:
            draw.ellipse((7*scale, 7*scale, 45*scale, 45*scale),
                         outline=white, width=3*scale)
            draw.arc((20*scale, 15*scale, 33*scale, 30*scale), 195, 440,
                     fill=white, width=3*scale)
            draw.line(points((26, 29), (26, 32)), fill=white, width=3*scale)
            draw.ellipse((24*scale, 37*scale, 28*scale, 41*scale), fill=lime)
        image.resize((52, 52), Image.Resampling.LANCZOS).save(
            UI / "icons" / f"{name}.tga", orientation=1)


def render_menu_art(source):
    size = (2048, 1152)
    art = source.resize(size, Image.Resampling.LANCZOS)
    art = ImageEnhance.Color(art).enhance(0.82)
    art = ImageEnhance.Brightness(art).enhance(0.72)
    art = art.filter(ImageFilter.GaussianBlur(6)).convert("RGBA")
    overlay = Image.new("RGBA", size)
    draw = ImageDraw.Draw(overlay)
    for x in range(size[0]):
        fraction = x / (size[0] - 1)
        stops = ((0, .97), (.26, .94), (.48, .70), (.72, .28), (1, .05))
        for (a, first), (b, second) in zip(stops, stops[1:]):
            if fraction <= b:
                opacity = first + (second - first) * (fraction - a) / (b - a)
                break
        draw.line((x, 0, x, size[1]), fill=(6, 9, 10, round(opacity * 255)))
    art.alpha_composite(overlay)
    fade = Image.new("RGBA", size)
    draw = ImageDraw.Draw(fade)
    for y in range(size[1]):
        opacity = max(0, (y / size[1] - .68) / .32) * .65
        draw.line((0, y, size[0], y), fill=(3, 7, 7, round(opacity * 255)))
    art.alpha_composite(fade)
    noise = Image.effect_noise(size, 12).point(lambda value: 255 if value > 128 else 0)
    grain = Image.new("RGBA", size, (220, 230, 216, 0))
    grain.putalpha(noise.point(lambda value: round(value * .025)))
    art.alpha_composite(grain)
    art.save(UI / "background-menu.tga", orientation=1)


def render_menu_chrome():
    rounded("hero-panel", (1680, 480), 12, "#16221da8", "#ffffff20")
    rounded("hero-button", (272, 72), 12, "#e6ede419", "#ffffff30")
    rounded("hero-button-focused", (272, 72), 12, "#a9db6340", "#a9db63a0", 1,
            ("#a9db6380", "#245d4a70"))
    rounded("recent-tile", (400, 144), 12, "#eef7ed0f", "#ffffff20")
    rounded("recent-tile-focused", (400, 144), 12, "#a9db6328", "#a9db63a0", 1,
            ("#a9db6358", "#245d4a60"))
    rounded("nav-focused", (136, 64), 12, "#a9db6328", "#a9db6380", 1,
            ("#a9db6350", "#245d4a50"))
    rounded("view-all-focused", (304, 40), 12, "#a9db6328", "#a9db6380", 1,
            ("#a9db6350", "#245d4a50"))
    with Image.open(UI / "icons" / "prosperoeden.tga") as brand:
        brand.resize((72, 72), Image.Resampling.LANCZOS).save(
            UI / "icons" / "brand-72.tga", orientation=1)
    scale = 4
    white = (240, 245, 242, 185)
    for name in ("cross-mono", "triangle-mono", "dpad-mono"):
        icon = Image.new("RGBA", (26 * scale, 26 * scale))
        draw = ImageDraw.Draw(icon)
        if name == "cross-mono":
            draw.line((6*scale, 6*scale, 20*scale, 20*scale), fill=white, width=2*scale)
            draw.line((20*scale, 6*scale, 6*scale, 20*scale), fill=white, width=2*scale)
        elif name == "triangle-mono":
            draw.line((13*scale, 5*scale, 22*scale, 21*scale, 4*scale, 21*scale,
                       13*scale, 5*scale), fill=white, width=2*scale, joint="curve")
        else:
            draw.line((13*scale, 4*scale, 13*scale, 22*scale), fill=white, width=2*scale)
            draw.line((4*scale, 13*scale, 22*scale, 13*scale), fill=white, width=2*scale)
            draw.ellipse((10*scale, 10*scale, 16*scale, 16*scale), fill=white)
        icon.resize((26, 26), Image.Resampling.LANCZOS).save(
            UI / "icons" / f"{name}.tga", orientation=1)


def main():
    source = Image.open(ROOT / "assets/background-source.png").convert("RGB")
    render_menu_art(source)
    background = source.resize((1920, 1080), Image.Resampling.LANCZOS).convert("RGBA")
    shade = Image.new("RGBA", background.size)
    draw = ImageDraw.Draw(shade)
    for x in range(1920):
        alpha = round(226 * max(0, 1 - x / 1370) ** 1.8)
        draw.line((x, 0, x, 1079), fill=(3, 12, 9, alpha))
    for y in range(860, 1080):
        alpha = round(105 * (y - 860) / 220)
        draw.line((0, y, 1919, y), fill=(1, 8, 7, alpha))
    background.alpha_composite(shade)
    background.save(UI / "background.tga", orientation=1)

    rounded("action-normal", (690, 108), 19, "#111f1bdc", "#75907878")
    rounded("action-focused", (690, 108), 19, "#21392be9", "#a9db63a0", 1,
            ("#a9db6360", "#245d4a70"))
    rounded("last-played-card", (710, 220), 20, "#0b1713e8", "#829a7ba8")
    rounded("dialog-panel", (820, 720), 26, "#0b1713f5", "#768e75a8")
    rounded("modal-panel", (820, 720), 26, "#0b1713ff", "#768e75a8")
    rounded("dialog-row-normal", (736, 94), 15, "#17241ee6", "#6a826777")
    rounded("dialog-row-focused", (736, 94), 15, "#294433f2", "#a9db63a0", 1,
            ("#a9db6370", "#245d4a80"))
    rounded("library-row-normal", (760, 78), 13, "#15231de8", "#68826777")
    rounded("library-row-focused", (760, 78), 13, "#2a4434f2", "#a9db63a0", 1,
            ("#a9db6370", "#245d4a80"))
    rounded("dropdown-panel", (736, 140), 15, "#102019fa", "#a6bf9b")
    rounded("scrollbar-track", (10, 606), 5, "#49624a88", "#49624a88")
    rounded("scrollbar-thumb", (10, 110), 5, "#d8e8aa", "#d8e8aa")
    render_icons()
    render_menu_chrome()


if __name__ == "__main__":
    main()
