#!/usr/bin/env python3
"""Draw the player profile network art into the Zulu texture atlas
assets/Art/Textures/ZuluNetBadges.tga (512x128): the badges (a steel medal
with a gold emblem, 4x supersampled then reduced) as 64x64 cells in BADGES
order on the top row, then the tier chips (CHIP_W x CHIP_H) and the per game
dots (DOT x DOT) in TIERS order below. The MappedImages in ZuluNetBadges.ini
point at those cells.
Usage: make_net_badges.py [ATLAS.tga] [--png DIR]  (needs Pillow)"""
import math
import sys
from PIL import Image, ImageDraw, ImageFilter

SS = 4
SIZE = 64
N = SIZE * SS
GOLD, GOLD_DK, GOLD_LT = (222, 178, 74), (128, 92, 28), (255, 230, 150)


def medal(rim):
    """Round steel medal with a coloured rim; returns image + draw."""
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = N / 2
    # Outer rim: a vertical light to dark sweep, like the stock honors.
    for i in range(N // 2):
        r = c - i
        if r < N * 0.40:
            break
        t = i / (N * 0.14)
        shade = tuple(int(v * (1.15 - 0.35 * t)) for v in rim)
        d.ellipse((c - r, c - r, c + r, c + r), fill=tuple(min(255, s) for s in shade) + (255,))
    # Notched bezel ring.
    for k in range(36):
        a = 2 * math.pi * k / 36
        x, y = c + math.cos(a) * N * 0.455, c + math.sin(a) * N * 0.455
        d.ellipse((x - N * 0.018, y - N * 0.018, x + N * 0.018, y + N * 0.018), fill=(30, 30, 34, 160))
    # Dark field with a soft top highlight.
    fr = N * 0.40
    d.ellipse((c - fr, c - fr, c + fr, c + fr), fill=(22, 30, 46, 255))
    hl = Image.new("L", (N, N), 0)
    ImageDraw.Draw(hl).ellipse((c - fr * 0.9, c - fr * 0.95, c + fr * 0.9, c + fr * 0.1), fill=60)
    hl = hl.filter(ImageFilter.GaussianBlur(N * 0.04))
    im.paste((120, 150, 200, 255), (0, 0), hl.point(lambda v: v // 3))
    d.ellipse((c - fr, c - fr, c + fr, c + fr), outline=(0, 0, 0, 200), width=int(N * 0.012))
    return im, ImageDraw.Draw(im), c


def emboss(im, draw_fn):
    """Draw an emblem in gold with a dark drop shadow and a light top edge."""
    mask = Image.new("L", (N, N), 0)
    draw_fn(ImageDraw.Draw(mask))
    shadow = mask.filter(ImageFilter.GaussianBlur(N * 0.012))
    im.paste((0, 0, 0, 255), (int(N * 0.012), int(N * 0.018)), shadow.point(lambda v: v * 3 // 4))
    grad = Image.new("RGBA", (N, N))
    gd = ImageDraw.Draw(grad)
    for y in range(N):
        t = y / N
        col = tuple(int(GOLD_LT[i] * (1 - t) + GOLD_DK[i] * t) for i in range(3))
        gd.line((0, y, N, y), fill=col + (255,))
    im.paste(grad, (0, 0), mask)
    edge = mask.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.GaussianBlur(1))
    im.paste((60, 40, 10, 255), (0, 0), edge.point(lambda v: min(255, v * 2)))


def star(dr, cx, cy, r, fill=255):
    pts = []
    for k in range(10):
        a = -math.pi / 2 + k * math.pi / 5
        rr = r if k % 2 == 0 else r * 0.42
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    dr.polygon(pts, fill=fill)


def link(dr):
    # Two interlocked chain links under a small star.
    c = N / 2
    w, h, t = N * 0.19, N * 0.115, int(N * 0.06)
    for dx, ang in ((-0.10, 0), (0.10, 0)):
        x = c + dx * N
        dr.rounded_rectangle((x - w, c - h + N * 0.04, x + w, c + h + N * 0.04), radius=h, outline=255, width=t)
    star(dr, c, c - N * 0.19, N * 0.08)


def nolag(dr):
    # A lightning bolt across a stopwatch ring.
    c = N / 2
    r = N * 0.23
    dr.ellipse((c - r, c - r + N * 0.03, c + r, c + r + N * 0.03), outline=255, width=int(N * 0.035))
    dr.rectangle((c - N * 0.03, c - r - N * 0.04, c + N * 0.03, c - r + N * 0.01), fill=255)
    bolt = [(c + N * 0.06, c - N * 0.27), (c - N * 0.10, c + N * 0.04), (c - N * 0.005, c + N * 0.04),
            (c - N * 0.07, c + N * 0.29), (c + N * 0.11, c - N * 0.03), (c + N * 0.015, c - N * 0.03)]
    dr.polygon(bolt, fill=255)


def comms(dr):
    # A field radio mast sending waves.
    c = N / 2
    base = c + N * 0.24
    dr.polygon([(c, c - N * 0.09), (c - N * 0.10, base), (c - N * 0.05, base), (c, c + N * 0.02),
                (c + N * 0.05, base), (c + N * 0.10, base)], fill=255)
    dr.ellipse((c - N * 0.04, c - N * 0.14, c + N * 0.04, c - N * 0.06), fill=255)
    for k, r in enumerate((0.09, 0.15, 0.21)):
        rr = N * r
        box = (c - rr, c - N * 0.10 - rr, c + rr, c - N * 0.10 + rr)
        dr.arc(box, 200, 340, fill=255, width=int(N * 0.028))


def machine(dr):
    # A cog with a star in its hub.
    c = N / 2
    ro, ri = N * 0.29, N * 0.22
    pts = []
    teeth = 10
    for k in range(teeth * 4):
        a = 2 * math.pi * k / (teeth * 4)
        r = ro if (k % 4) in (1, 2) else ri
        pts.append((c + math.cos(a) * r, c + math.sin(a) * r))
    dr.polygon(pts, fill=255)
    dr.ellipse((c - N * 0.14, c - N * 0.14, c + N * 0.14, c + N * 0.14), fill=0)
    star(dr, c, c + N * 0.005, N * 0.12)


def improved(dr):
    # Three rising chevrons, like a sergeant's stripes.
    c = N / 2
    for k in range(3):
        y = c + N * 0.16 - k * N * 0.13
        w, t = N * 0.24, N * 0.07
        dr.polygon([(c - w, y + N * 0.06), (c, y - N * 0.08), (c + w, y + N * 0.06),
                    (c + w, y + N * 0.06 + t), (c, y - N * 0.08 + t), (c - w, y + N * 0.06 + t)], fill=255)


BADGES = [("link", (150, 160, 175), link), ("nolag", (150, 160, 175), nolag), ("comms", (150, 160, 175), comms),
          ("machine", (150, 160, 175), machine), ("improved", (176, 120, 70), improved)]

# Top tier, Solid, Rough patch, not enough games: (fill, border) like the mockup.
TIERS = [("Top", (18, 48, 94), (78, 161, 255)), ("Solid", (26, 36, 56), (74, 90, 120)),
         ("Rough", (58, 42, 12), (245, 165, 36)), ("None", (24, 28, 36), (70, 78, 92))]
DOT_FILL = [(78, 161, 255), (131, 145, 171), (245, 165, 36), (58, 67, 86)]
CHIP_W, CHIP_H, CHIP_Y, DOT, DOT_Y = 120, 26, 66, 16, 100


def chip(fill, border):
    im = Image.new("RGBA", (CHIP_W * SS, CHIP_H * SS), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle((0, 0, CHIP_W * SS - 1, CHIP_H * SS - 1), radius=4 * SS,
                                         fill=fill + (255,), outline=border + (255,), width=2 * SS)
    return im.resize((CHIP_W, CHIP_H), Image.LANCZOS)


def dot(fill):
    im = Image.new("RGBA", (DOT * SS, DOT * SS), (0, 0, 0, 0))
    ImageDraw.Draw(im).ellipse((SS, SS, DOT * SS - SS, DOT * SS - SS), fill=fill + (255,))
    return im.resize((DOT, DOT), Image.LANCZOS)


def write_tga(im, path):
    """Uncompressed 32 bit BGRA, top-left origin (descriptor 0x28), like the other Zulu textures."""
    w, h = im.size
    head = bytes([0, 0, 2]) + bytes(9) + w.to_bytes(2, "little") + h.to_bytes(2, "little") + bytes([32, 0x28])
    r, g, b, a = im.split()
    with open(path, "wb") as f:
        f.write(head + Image.merge("RGBA", (b, g, r, a)).tobytes())


if __name__ == "__main__":
    args = sys.argv[1:]
    png = args[args.index("--png") + 1] if "--png" in args else None
    out = next((x for x in args if x.endswith(".tga")), "assets/Art/Textures/ZuluNetBadges.tga")
    atlas = Image.new("RGBA", (512, 128), (0, 0, 0, 0))
    for i, (key, rim, fn) in enumerate(BADGES):
        im, _, _ = medal(rim)
        emboss(im, fn)
        small = im.resize((SIZE, SIZE), Image.LANCZOS)
        atlas.paste(small, (i * SIZE, 0))
        if png:
            im.resize((256, 256), Image.LANCZOS).save("%s/badge_%s.png" % (png, key))
    for i, (name, fill, border) in enumerate(TIERS):
        atlas.paste(chip(fill, border), (i * (CHIP_W + 4), CHIP_Y))
        atlas.paste(dot(DOT_FILL[i]), (i * (DOT + 4), DOT_Y))
    write_tga(atlas, out)
    if png:
        atlas.save("%s/atlas.png" % png)
