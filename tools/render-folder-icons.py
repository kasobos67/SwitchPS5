"""Draw the Game files browser's icons: folders (ui/icons/folder.tga, folder-up.tga) and the
controller hints added to the launcher's mono set (circle-, square-, l1-, r1-, updown-, leftright-mono.tga)
and the Library's console mode icons (mode-docked.tga, mode-handheld.tga).

Uncompressed 32-bit top-left TGAs like the other launcher icons, drawn at 4x and reduced for
smooth edges. The colours are the launcher's accent (#dfe8a6) and leaf green."""

from pathlib import Path

from PIL import Image, ImageDraw

UI = Path(__file__).resolve().parents[1] / "headless/prosperoeden/ui/icons"
WIDTH, HEIGHT, SCALE = 40, 32, 4


def write_tga(image, path):
    width, height = image.size
    header = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, width & 255, width >> 8, height & 255, height >> 8, 32, 0x28])
    pixels = bytearray()
    for r, g, b, a in image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata():
        pixels += bytes((b, g, r, a))
    path.write_bytes(header + bytes(pixels))


def folder(up):
    s = SCALE
    image = Image.new("RGBA", (WIDTH * s, HEIGHT * s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    back, front = (143, 176, 102, 255), (223, 232, 166, 255)
    # Tab and back sheet, then the front sheet slightly lower.
    draw.rounded_rectangle((2 * s, 3 * s, 17 * s, 10 * s), radius=2 * s, fill=back)
    draw.rounded_rectangle((2 * s, 6 * s, 38 * s, 29 * s), radius=3 * s, fill=back)
    draw.rounded_rectangle((2 * s, 10 * s, 38 * s, 29 * s), radius=3 * s, fill=front)
    if up:
        ink = (30, 52, 38, 255)
        draw.polygon([(20 * s, 12 * s), (27 * s, 19 * s), (22.5 * s, 19 * s), (22.5 * s, 26 * s),
                      (17.5 * s, 26 * s), (17.5 * s, 19 * s), (13 * s, 19 * s)], fill=ink)
    return image.resize((WIDTH, HEIGHT), Image.LANCZOS)


MONO = (239, 245, 241, 255)


def mono(draw_shape):
    """A 26x26 hint icon in the launcher's mono style: light strokes about 2 px wide."""
    s = SCALE
    image = Image.new("RGBA", (26 * s, 26 * s), (0, 0, 0, 0))
    draw_shape(ImageDraw.Draw(image), s)
    return image.resize((26, 26), Image.LANCZOS)


def circle(draw, s):
    draw.ellipse((3 * s, 3 * s, 23 * s, 23 * s), outline=MONO, width=int(2.2 * s))


def square(draw, s):
    draw.rectangle((4 * s, 4 * s, 22 * s, 22 * s), outline=MONO, width=int(2.2 * s))


def shoulder(letter):
    def shape(draw, s):
        w = int(1.8 * s)
        draw.rounded_rectangle((1 * s, 5 * s, 25 * s, 21 * s), radius=5 * s, outline=MONO, width=w)
        if letter == "L":
            draw.line((6.5 * s, 8.5 * s, 6.5 * s, 17.5 * s), fill=MONO, width=w)
            draw.line((6.5 * s, 17.5 * s, 12 * s, 17.5 * s), fill=MONO, width=w)
        else:
            draw.line((6.5 * s, 8.5 * s, 6.5 * s, 17.5 * s), fill=MONO, width=w)
            draw.arc((3.5 * s, 8.5 * s, 12 * s, 13.5 * s), -90, 90, fill=MONO, width=w)
            draw.line((6.5 * s, 8.5 * s, 8 * s, 8.5 * s), fill=MONO, width=w)
            draw.line((6.5 * s, 13.5 * s, 8 * s, 13.5 * s), fill=MONO, width=w)
            draw.line((8.5 * s, 13.5 * s, 12 * s, 17.5 * s), fill=MONO, width=w)
        draw.line((17.5 * s, 8.5 * s, 17.5 * s, 17.5 * s), fill=MONO, width=w)
        draw.line((15 * s, 10.5 * s, 17.5 * s, 8.5 * s), fill=MONO, width=w)
    return shape


def arrows(vertical):
    def shape(draw, s):
        w = int(2.2 * s)
        a, b = (13 * s, 3 * s), (13 * s, 23 * s)
        if not vertical:
            a, b = (3 * s, 13 * s), (23 * s, 13 * s)
        draw.line((*a, *b), fill=MONO, width=w)
        for tip, sign in ((a, 1), (b, -1)):
            x, y = tip
            if vertical:
                draw.line((x - 6 * s, y + sign * 6 * s, x, y, x + 6 * s, y + sign * 6 * s), fill=MONO, width=w, joint="curve")
            else:
                draw.line((x + sign * 6 * s, y - 6 * s, x, y, x + sign * 6 * s, y + 6 * s), fill=MONO, width=w, joint="curve")
    return shape


def mode_icon(handheld):
    """48x32 console mode icon in the mono colour: a TV on its stand, or the handheld."""
    s = SCALE
    image = Image.new("RGBA", (48 * s, 32 * s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    w = int(2.4 * s)
    if handheld:
        draw.rounded_rectangle((2 * s, 6 * s, 46 * s, 27 * s), radius=6 * s, outline=MONO, width=w)
        draw.rectangle((13 * s, 9 * s, 35 * s, 24 * s), outline=MONO, width=int(1.8 * s))
        draw.ellipse((6 * s, 11 * s, 10 * s, 15 * s), fill=MONO)
        draw.ellipse((38 * s, 17 * s, 42 * s, 21 * s), fill=MONO)
    else:
        draw.rounded_rectangle((5 * s, 2 * s, 43 * s, 24 * s), radius=2 * s, outline=MONO, width=w)
        draw.line((24 * s, 24 * s, 24 * s, 28 * s), fill=MONO, width=w)
        draw.line((15 * s, 29 * s, 33 * s, 29 * s), fill=MONO, width=w)
    return image.resize((48, 32), Image.LANCZOS)


if __name__ == "__main__":
    write_tga(mono(arrows(True)), UI / "updown-mono.tga")
    write_tga(mono(arrows(False)), UI / "leftright-mono.tga")
    write_tga(mode_icon(False), UI / "mode-docked.tga")
    write_tga(mode_icon(True), UI / "mode-handheld.tga")
    write_tga(mono(circle), UI / "circle-mono.tga")
    write_tga(mono(square), UI / "square-mono.tga")
    write_tga(mono(shoulder("L")), UI / "l1-mono.tga")
    write_tga(mono(shoulder("R")), UI / "r1-mono.tga")
    write_tga(folder(False), UI / "folder.tga")
    write_tga(folder(True), UI / "folder-up.tga")
    print("wrote", UI / "folder.tga", UI / "folder-up.tga")
