#!/usr/bin/env python3
"""Draw the player profile honor badges (single holder awards from radarvan
stats) into assets/Art/Textures/ZuluHonors.tga: 64x64 cells, 8 per row, in
HONORS order. Same medal as make_net_badges.py with a gold rim and a crimson
field (purple for the tournament champions).
Usage: make_honor_badges.py [ATLAS.tga] [--png DIR]  (needs Pillow)"""
import math
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from make_net_badges import N, SIZE, emboss, medal, star, write_tga  # noqa: E402

GOLD_RIM = (214, 172, 80)
CRIMSON = (70, 18, 26)
PURPLE = (48, 22, 74)
c = N / 2


def poly(dr, pts):
    dr.polygon([(c + x * N, c + y * N) for x, y in pts], fill=255)


def circle(dr, x, y, r, fill=255, width=0):
    box = (c + (x - r) * N, c + (y - r) * N, c + (x + r) * N, c + (y + r) * N)
    if width:
        dr.ellipse(box, outline=fill, width=int(width * N))
    else:
        dr.ellipse(box, fill=fill)


def rect(dr, x0, y0, x1, y1, fill=255):
    dr.rectangle((c + x0 * N, c + y0 * N, c + x1 * N, c + y1 * N), fill=fill)


def streak(dr):  # a flame with two tongues
    poly(dr, [(0.02, -0.28), (0.1, -0.15), (0.17, -0.04), (0.18, 0.08), (0.13, 0.19), (0.04, 0.25), (-0.06, 0.25),
              (-0.15, 0.19), (-0.19, 0.08), (-0.17, -0.04), (-0.1, -0.12), (-0.08, -0.02), (-0.03, -0.12)])
    poly(dr, [(0.0, -0.02), (0.06, 0.07), (0.08, 0.15), (0.03, 0.21), (-0.04, 0.21), (-0.08, 0.15), (-0.06, 0.06)][::-1])
    dr.polygon([(c + x * N, c + y * N) for x, y in [(0.0, 0.0), (0.06, 0.08), (0.07, 0.15), (0.03, 0.2), (-0.04, 0.2),
                                                      (-0.07, 0.15), (-0.05, 0.07)]], fill=0)
    poly(dr, [(0.0, 0.06), (0.04, 0.12), (0.03, 0.18), (-0.03, 0.18), (-0.04, 0.12)])


def general(dr):  # a general's star over two bars
    star(dr, c, c - N * 0.06, N * 0.17)
    rect(dr, -0.17, 0.13, 0.17, 0.17)
    rect(dr, -0.17, 0.2, 0.17, 0.24)


def mapmaster(dr):  # a map pin
    circle(dr, 0, -0.08, 0.15)
    poly(dr, [(-0.13, -0.01), (0.13, -0.01), (0, 0.26)])
    circle(dr, 0, -0.08, 0.06, fill=0)


def duelist(dr):  # two crossed swords
    for s in (1, -1):
        poly(dr, [(-0.2 * s, -0.22), (-0.16 * s, -0.24), (0.2 * s, 0.13), (0.16 * s, 0.16)])
        poly(dr, [(0.09 * s, 0.07), (0.15 * s, 0.01), (0.2 * s, 0.06), (0.14 * s, 0.12)])
        circle(dr, 0.2 * s, 0.2, 0.035)


def ironwill(dr):  # a shield with a star
    poly(dr, [(-0.19, -0.21), (0.19, -0.21), (0.19, 0.02), (0.1, 0.16), (0.0, 0.26), (-0.1, 0.16), (-0.19, 0.02)])
    star(dr, c, c - 0.01 * N, N * 0.12, fill=0)


def closer(dr):  # an hourglass
    rect(dr, -0.16, -0.24, 0.16, -0.2)
    rect(dr, -0.16, 0.2, 0.16, 0.24)
    poly(dr, [(-0.13, -0.2), (0.13, -0.2), (0.025, 0.0), (0.13, 0.2), (-0.13, 0.2), (-0.025, 0.0)])
    poly(dr, [(-0.08, -0.17), (0.08, -0.17), (0.0, -0.04)])
    poly(dr, [(-0.08, 0.17), (0.08, 0.17), (0.0, 0.1)])


def terrorist(dr):  # a bundle of dynamite with a lit fuse
    for x in (-0.12, 0.0, 0.12):
        dr.rounded_rectangle((c + (x - 0.055) * N, c - 0.1 * N, c + (x + 0.055) * N, c + 0.24 * N), radius=0.02 * N, fill=255)
    rect(dr, -0.18, 0.02, 0.18, 0.06, fill=0)
    dr.line([(c, c - 0.1 * N), (c + 0.05 * N, c - 0.17 * N), (c + 0.12 * N, c - 0.2 * N)], fill=255, width=int(0.025 * N))
    star(dr, c + 0.15 * N, c - 0.22 * N, N * 0.06)


def hero(dr):  # a burst behind a star
    pts = []
    for k in range(24):
        a = 2 * math.pi * k / 24
        r = 0.27 if k % 2 == 0 else 0.18
        pts.append((math.cos(a) * r, math.sin(a) * r))
    poly(dr, pts)
    circle(dr, 0, 0, 0.15, fill=0)
    star(dr, c, c + N * 0.01, N * 0.13)


def collector(dr):  # a cut gem
    poly(dr, [(-0.2, -0.08), (-0.1, -0.2), (0.1, -0.2), (0.2, -0.08), (0.0, 0.24)])
    dr.line([(c - 0.2 * N, c - 0.08 * N), (c + 0.2 * N, c - 0.08 * N)], fill=0, width=int(0.02 * N))
    dr.line([(c - 0.06 * N, c - 0.2 * N), (c - 0.09 * N, c - 0.08 * N), (c, c + 0.24 * N), (c + 0.09 * N, c - 0.08 * N),
             (c + 0.06 * N, c - 0.2 * N)], fill=0, width=int(0.015 * N))


def takeover(dr):  # a flag planted on a building
    rect(dr, -0.2, 0.04, 0.12, 0.24)
    poly(dr, [(-0.22, 0.05), (-0.04, -0.06), (0.14, 0.05)])
    for x in (-0.14, -0.06, 0.02):
        rect(dr, x, 0.1, x + 0.05, 0.16, fill=0)
    rect(dr, 0.15, -0.26, 0.18, 0.24)
    poly(dr, [(0.18, -0.26), (0.06, -0.2), (0.18, -0.13)][::-1])
    poly(dr, [(0.18, -0.26), (0.18, -0.13), (0.06, -0.2)])


def oilbaron(dr):  # an oil drop
    poly(dr, [(0, -0.26), (0.09, -0.1), (0.16, 0.02), (0.16, 0.1), (0.1, 0.2), (0, 0.24), (-0.1, 0.2), (-0.16, 0.1),
              (-0.16, 0.02), (-0.09, -0.1)])
    circle(dr, -0.06, 0.08, 0.035, fill=0)


def firststrike(dr):  # a crosshair
    circle(dr, 0, 0, 0.2, width=0.035)
    circle(dr, 0, 0, 0.035)
    for x0, y0, x1, y1 in ((-0.02, -0.27, 0.02, -0.08), (-0.02, 0.08, 0.02, 0.27), (-0.27, -0.02, -0.08, 0.02), (0.08, -0.02, 0.27, 0.02)):
        rect(dr, x0, y0, x1, y1)


def bigred(dr):  # a radiation trefoil
    for k in range(3):
        a0 = -90 + k * 120
        box = (c - 0.25 * N, c - 0.25 * N, c + 0.25 * N, c + 0.25 * N)
        dr.pieslice(box, a0 - 30, a0 + 30, fill=255)
    circle(dr, 0, 0, 0.08, fill=0)
    circle(dr, 0, 0, 0.05)


def cup(dr, label_stars):  # a trophy cup with stars
    poly(dr, [(-0.15, -0.2), (0.15, -0.2), (0.12, 0.0), (0.04, 0.07), (-0.04, 0.07), (-0.12, 0.0)])
    circle(dr, -0.16, -0.1, 0.06, width=0.025)
    circle(dr, 0.16, -0.1, 0.06, width=0.025)
    rect(dr, -0.03, 0.06, 0.03, 0.15)
    rect(dr, -0.11, 0.15, 0.11, 0.21)
    for k in range(label_stars):
        x = (k - (label_stars - 1) / 2) * 0.11
        star(dr, c + x * N, c - 0.1 * N, N * 0.05, fill=0)


def champ1(dr):
    cup(dr, 1)


def champ2(dr):
    cup(dr, 2)


def paladin(dr):  # a tank
    dr.rounded_rectangle((c - 0.22 * N, c + 0.06 * N, c + 0.22 * N, c + 0.18 * N), radius=0.06 * N, fill=255)
    poly(dr, [(-0.16, 0.06), (-0.1, -0.06), (0.1, -0.06), (0.15, 0.06)])
    rect(dr, 0.06, -0.04, 0.27, 0.0)
    for x in (-0.14, -0.05, 0.04, 0.13):
        circle(dr, x, 0.12, 0.03, fill=0)


def scud(dr):  # a missile
    poly(dr, [(0.0, -0.27), (0.06, -0.17), (0.06, 0.14), (-0.06, 0.14), (-0.06, -0.17)])
    poly(dr, [(-0.06, 0.04), (-0.14, 0.2), (-0.06, 0.14)])
    poly(dr, [(0.06, 0.04), (0.06, 0.14), (0.14, 0.2)])
    poly(dr, [(-0.04, 0.14), (0.04, 0.14), (0.0, 0.26)])


def nukecannon(dr):  # a mushroom cloud
    circle(dr, -0.1, -0.1, 0.1)
    circle(dr, 0.1, -0.1, 0.1)
    circle(dr, 0, -0.16, 0.11)
    rect(dr, -0.16, -0.1, 0.16, -0.03)
    poly(dr, [(-0.05, -0.03), (0.05, -0.03), (0.07, 0.16), (-0.07, 0.16)])
    dr.ellipse((c - 0.2 * N, c + 0.14 * N, c + 0.2 * N, c + 0.24 * N), fill=255)


def plague(dr):  # a skull
    circle(dr, 0, -0.05, 0.18)
    rect(dr, -0.1, 0.06, 0.1, 0.2)
    circle(dr, -0.07, -0.05, 0.05, fill=0)
    circle(dr, 0.07, -0.05, 0.05, fill=0)
    poly(dr, [(0, 0.02), (0.03, 0.07), (-0.03, 0.07)][::-1])
    poly(dr, [(0, 0.02), (-0.03, 0.07), (0.03, 0.07)])
    for x in (-0.05, 0.0, 0.05):
        rect(dr, x - 0.012, 0.13, x + 0.012, 0.2, fill=0)


def holdline(dr):  # a castle tower
    rect(dr, -0.15, -0.1, 0.15, 0.24)
    for x in (-0.15, -0.03, 0.09):
        rect(dr, x, -0.2, x + 0.06, -0.1)
    rect(dr, -0.05, 0.08, 0.05, 0.24, fill=0)
    circle(dr, 0, 0.08, 0.05, fill=0)
    rect(dr, -0.025, -0.05, 0.025, 0.02, fill=0)


# key (as the service sends it), image name suffix, field, emblem
HONORS = [
    ("champ1v1", "Champ1v1", PURPLE, champ1), ("champ2v2", "Champ2v2", PURPLE, champ2),
    ("streak", "Streak", CRIMSON, streak), ("general", "General", CRIMSON, general),
    ("map", "Map", CRIMSON, mapmaster), ("duelist", "Duelist", CRIMSON, duelist),
    ("ironwill", "IronWill", CRIMSON, ironwill), ("closer", "Closer", CRIMSON, closer),
    ("terrorist", "Terrorist", CRIMSON, terrorist), ("hero", "Hero", CRIMSON, hero),
    ("collector", "Collector", CRIMSON, collector), ("takeover", "Takeover", CRIMSON, takeover),
    ("oil", "Oil", CRIMSON, oilbaron), ("firststrike", "FirstStrike", CRIMSON, firststrike),
    ("bigred", "BigRed", CRIMSON, bigred), ("paladin", "Paladin", CRIMSON, paladin),
    ("scud", "Scud", CRIMSON, scud), ("nukecannon", "NukeCannon", CRIMSON, nukecannon),
    ("plague", "Plague", CRIMSON, plague), ("holdline", "HoldLine", CRIMSON, holdline),
]

if __name__ == "__main__":
    args = sys.argv[1:]
    png = args[args.index("--png") + 1] if "--png" in args else None
    out = next((x for x in args if x.endswith(".tga")), "assets/Art/Textures/ZuluHonors.tga")
    atlas = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    for i, (key, _, field, fn) in enumerate(HONORS):
        im, _, _ = medal(GOLD_RIM, field)
        emboss(im, fn)
        atlas.paste(im.resize((SIZE, SIZE), Image.LANCZOS), ((i % 8) * SIZE, (i // 8) * SIZE))
        if png:
            im.resize((256, 256), Image.LANCZOS).save("%s/honor_%s.png" % (png, key))
    write_tga(atlas, out)
