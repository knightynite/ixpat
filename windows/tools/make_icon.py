"""Renders the Windows app icon: a graphite Surface Mouse, top-down, on a pale grey plate.

The Mac icon is a white shell on a frosted plate, which disappears on a light taskbar at 16 px. Here the shell
is dark so the silhouette carries at every size, and the plate stays light so the icon also stands on a dark
taskbar. Frames of 32 px and under drop the action pill and widen the wheel slot; at those sizes a hint of
detail reads better than a faithful one.

Run:  python windows/tools/make_icon.py windows/res/ixpat.ico [--preview out.png]
Needs Pillow.
"""
import sys

from PIL import Image, ImageDraw

SIZES = (256, 128, 64, 48, 32, 24, 16)
PLATE_TOP, PLATE_BOTTOM, PLATE_RIM = (246, 247, 249), (224, 228, 234), (190, 196, 205, 150)
SHELL_TOP, SHELL_BOTTOM = (74, 77, 84), (30, 32, 36)
SLOT, SLOT_TICK, PILL = (196, 200, 207), (70, 73, 79), (186, 190, 198)


def cubic(p0, p1, p2, p3, n=48):
    for i in range(n):
        t = i / n
        u = 1 - t
        yield (u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
               u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1])


def shell_polygon(cx, cy, mw, mh):
    """The Surface shell outline (the same curves as the Mac icon); x, y in 0..1 with y = 0 at the front."""
    def pt(x, y):
        return (cx - mw / 2 + mw * x, cy - mh / 2 + mh * y)
    segments = [
        (pt(0.5, 0), pt(0.80, 0), pt(0.97, 0.06), pt(0.97, 0.22)),
        (pt(0.97, 0.22), pt(0.99, 0.40), pt(1.0, 0.55), pt(1.0, 0.70)),
        (pt(1.0, 0.70), pt(1.0, 0.92), pt(0.78, 1.0), pt(0.5, 1.0)),
        (pt(0.5, 1.0), pt(0.22, 1.0), pt(0.0, 0.92), pt(0.0, 0.70)),
        (pt(0.0, 0.70), pt(0.0, 0.55), pt(0.01, 0.40), pt(0.03, 0.22)),
        (pt(0.03, 0.22), pt(0.03, 0.06), pt(0.20, 0), pt(0.5, 0)),
    ]
    points = []
    for seg in segments:
        points.extend(cubic(*seg))
    return points


def vertical_gradient(size, top, bottom):
    ramp = Image.linear_gradient('L').resize(size)
    return Image.composite(Image.new('RGBA', size, bottom + (255,)), Image.new('RGBA', size, top + (255,)), ramp)


def radial_alpha(size, strength):
    """A soft white spot, opaque at the centre fading to nothing at the edge."""
    ramp = Image.radial_gradient('L').resize(size).point(lambda v: int(max(0, 255 - v) * strength))
    spot = Image.new('RGBA', size, (255, 255, 255, 0))
    spot.putalpha(ramp)
    return spot


def render(size):
    simple = size <= 32
    ss = 8 if size <= 64 else 4
    S = size * ss
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))

    # Plate: pale, a hair darker toward the bottom, with a hairline rim so it separates from a light taskbar.
    content = 0.94 if simple else 0.84
    pad = (1 - content) / 2 * S
    radius = content * S * 0.2237
    plate_mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(plate_mask).rounded_rectangle((pad, pad, S - pad, S - pad), radius, fill=255)
    img.paste(vertical_gradient((S, S), PLATE_TOP, PLATE_BOTTOM), (0, 0), plate_mask)
    rim = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle((pad, pad, S - pad, S - pad), radius, outline=PLATE_RIM, width=max(1, int((1.0 if simple else 1.5) * ss)))
    img.alpha_composite(rim)

    # Shell: graphite, lighter at the front, with a broad highlight on the dome and a soft contact shadow.
    cx, cy = S / 2, S / 2
    mw = S * (0.50 if simple else 0.36)
    mh = mw * 540 / 330
    body = shell_polygon(cx, cy, mw, mh)
    shadow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(x, y + mh * 0.03) for x, y in body], fill=(20, 22, 26, 70))
    from PIL import ImageFilter
    shadow = shadow.filter(ImageFilter.GaussianBlur(S * 0.02))
    img.alpha_composite(shadow)
    body_mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(body_mask).polygon(body, fill=255)
    shell = vertical_gradient((S, S), SHELL_TOP, SHELL_BOTTOM)
    spot = radial_alpha((int(mw * 1.4), int(mw * 1.4)), 0.30)
    shell.alpha_composite(spot, (int(cx - mw * 0.09 - mw * 0.7), int(cy - mh / 2 + mh * 0.42 - mw * 0.7)))
    img.paste(shell, (0, 0), body_mask)

    # Details: the wheel slot (and, at larger sizes, the action pill in front of it), in light grey.
    draw = ImageDraw.Draw(img)
    top = cy - mh / 2
    if simple:
        w, y0, y1 = mw * 0.17, top + mh * 0.20, top + mh * 0.40
        draw.rounded_rectangle((cx - w / 2, y0, cx + w / 2, y1), w / 2, fill=SLOT)
    else:
        w, y0, y1 = mw * 0.07, top + mh * 0.10, top + mh * 0.175
        draw.rounded_rectangle((cx - w / 2, y0, cx + w / 2, y1), w / 2, fill=PILL)
        w, y0, y1 = mw * 0.115, top + mh * 0.225, top + mh * 0.37
        draw.rounded_rectangle((cx - w / 2, y0, cx + w / 2, y1), w * 0.3, fill=SLOT)
        for k in range(4):
            y = y0 + (y1 - y0) * (0.22 + 0.19 * k)
            draw.rectangle((cx - w * 0.28, y, cx + w * 0.28, y + max(1, S * 0.002)), fill=SLOT_TICK)

    return img.resize((size, size), Image.LANCZOS)


def preview(frames, path):
    """Each size on a light and a dark taskbar grey, enlarged 3x with no smoothing, plus the 256 frame."""
    sizes = [s for s in SIZES if s <= 64]
    scale, gap = 3, 12
    width = sum(s * scale + gap for s in sizes) + gap
    sheet = Image.new('RGBA', (width * 2 + 256 + 3 * gap, 64 * scale + 2 * gap), (255, 255, 255, 255))
    for col, bg in enumerate(((243, 243, 243, 255), (32, 32, 32, 255))):
        x = col * width
        ImageDraw.Draw(sheet).rectangle((x, 0, x + width, sheet.height), fill=bg)
        x += gap
        for s in sizes:
            big = frames[s].resize((s * scale, s * scale), Image.NEAREST)
            sheet.alpha_composite(big, (x, (sheet.height - s * scale) // 2))
            x += s * scale + gap
    sheet.alpha_composite(frames[256], (width * 2 + gap, (sheet.height - 256) // 2))
    sheet.save(path)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    frames = {s: render(s) for s in SIZES}
    frames[256].save(argv[1], format='ICO', sizes=[(s, s) for s in SIZES], append_images=[frames[s] for s in SIZES[1:]])
    if '--preview' in argv:
        preview(frames, argv[argv.index('--preview') + 1])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
