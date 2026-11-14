#!/usr/bin/env python3
"""Build assets/Window/Menus/PopupPlayerInfo.wnd from the retail layout:
stock persona view (+ honors list LENGTH 8 with a scrollbar for the network
badges), a NETWORK FORM button, and the hidden NetFormParent panel.
Usage: make_form_wnd.py RETAIL.wnd OUT.wnd
  RETAIL.wnd = Window\\Menus\\PopupPlayerInfo.wnd from the retail WindowZH.big;
  OUT.wnd = assets/Window/Menus/PopupPlayerInfo.wnd (rerun after any edit here)."""
import re
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from wndblk import block  # noqa: E402

P = "PopupPlayerInfo.wnd:"
src, out = sys.argv[1], sys.argv[2]
ls = open(src, "rb").read().decode("latin-1").split("\r\n")


def get(name):
    s, e = block(ls, P + name + '"') if name else (None, None)
    return ls[s:e + 1]


def header(blk):
    """A block's own lines (up to its first child), closed with END."""
    out_, depth0 = [], len(blk[1]) - len(blk[1].lstrip())
    for l in blk[:-1]:
        if l.strip() in ("CHILD", "ENDALLCHILDREN") and l is not blk[0]:
            break
        out_.append(l)
    return out_ + [blk[-1]]


def reindent(blk, ind):
    cur = len(blk[0]) - len(blk[0].lstrip())
    return [(" " * ind + l[cur:]) if l.startswith(" " * cur) else l for l in blk]


def clone(blk, name, rect, text=None, font=None, centered=None, image=None, status=None):
    t = "\r\n".join(blk)
    t = re.sub(r'NAME = "[^"]*";', 'NAME = "%s%s";' % (P, name), t, count=1)
    t = re.sub(r"UPPERLEFT: \d+ \d+,", "UPPERLEFT: %d %d," % rect[:2], t, count=1)
    t = re.sub(r"BOTTOMRIGHT: \d+ \d+,", "BOTTOMRIGHT: %d %d," % rect[2:], t, count=1)
    if text == "":  # the parser rejects TEXT = ""; leave it out
        t = re.sub(r'\r\n\s*TEXT = "[^"]*";', "", t, count=1)
    elif text is not None:
        t = re.sub(r'TEXT = "[^"]*";', 'TEXT = "%s";' % text, t, count=1)
    if font:
        t = re.sub(r'FONT = NAME: "[^"]*", SIZE: \d+, BOLD: \d;', 'FONT = NAME: "Arial", SIZE: %d, BOLD: %d;' % font, t, count=1)
    if centered is not None:
        t = re.sub(r"CENTERED: \d;", "CENTERED: %d;" % centered, t, count=1)
    if image:
        t = re.sub(r"ENABLEDDRAWDATA = IMAGE: [^,]*,", "ENABLEDDRAWDATA = IMAGE: %s," % image, t, count=1)
    if status:
        t = re.sub(r"STATUS = [^;]*;", "STATUS = %s;" % status, t, count=1)
    return t.split("\r\n")


TEXT = reindent(get("StaticTextGamesPlayedValue"), 6)
IMG = reindent(header(get("WinRank")), 6)
assert "CENTERED:" in "".join(TEXT)

# Persona view: badges fit in the honors list; name its label so the code can hide it.
s, e = block(ls, P + 'ListboxInfo"')
t = "\r\n".join(ls[s:e + 1]).replace("LENGTH: 5,", "LENGTH: 8,").replace("SCROLLBAR: 0,", "SCROLLBAR: 1,")
ls[s:e + 1] = t.split("\r\n")
s, e = block(ls, "UPPERLEFT: 57 329")
ls[s:e + 1] = [l.replace('NAME = "%s";' % P, 'NAME = "%sStaticTextBattleHonors";' % P) for l in ls[s:e + 1]]

# NETWORK FORM button where the (hidden in Zulu GO) Set Locale button sits.
s, e = block(ls, P + 'ButtonSetLocale"')
ls[e + 1:e + 1] = clone(ls[s:e + 1], "ButtonNetForm", (541, 414, 729, 446), text="GUI:NetFormButton")

# The form panel: a hidden see-through container with the rows as children.
kids = []
kids += clone(TEXT, "NFTitle", (57, 84, 752, 104), text="GUI:NetFormTitle", font=(10, 0))
rows = [("net", 0), ("net", 1), ("net", 2), ("pc", 3), ("pc", 4), ("pc", 5), ("pc", 6)]
for col, x0, head in ((0, 57, "GUI:NetFormNetwork"), (1, 412, "GUI:NetFormPC")):
    kids += clone(TEXT, "NFHead%d" % col, (x0, 108, x0 + 330, 128), text=head, font=(14, 0))
for i, (sec, m) in enumerate(rows):
    x0 = 57 if sec == "net" else 412
    y0 = 132 + 44 * (i if sec == "net" else i - 3)
    kids += clone(TEXT, "NFRow%dLabel" % m, (x0, y0, x0 + 150, y0 + 20), text="", font=(12, 1))
    kids += clone(TEXT, "NFRow%dTrend" % m, (x0, y0 + 20, x0 + 150, y0 + 38), text="", font=(9, 0))
    kids += clone(TEXT, "NFRow%dValue" % m, (x0 + 152, y0, x0 + 222, y0 + 20), text="", font=(12, 0))
    for d in range(5):
        kids += clone(IMG, "NFRow%dDot%d" % (m, d), (x0 + 154 + 12 * d, y0 + 24, x0 + 163 + 12 * d, y0 + 33), image="ZuluDotNone")
    kids += clone(IMG, "NFRow%dChip" % m, (x0 + 228, y0 + 6, x0 + 330, y0 + 30), image="ZuluChipNone")
    kids += clone(TEXT, "NFRow%dChipText" % m, (x0 + 228, y0 + 6, x0 + 330, y0 + 30), text="", font=(10, 1), centered=1)
kids += clone(TEXT, "NFHeadBadges", (57, 312, 752, 332), text="GUI:NetFormBadges", font=(14, 0))
for b in range(8):  # NETFORM_SLOTS: network badges, then honors
    x0 = 57 + 87 * b
    kids += clone(IMG, "NFBadge%d" % b, (x0 + 21, 334, x0 + 65, 378), image="ZuluBadgeLink")
    kids += clone(TEXT, "NFBadgeName%d" % b, (x0, 379, x0 + 86, 409), text="", font=(9, 1), centered=1)  # two lines
kids += clone(TEXT, "NFNoBadges", (57, 350, 752, 370), text="GUI:NetFormNoBadges", font=(10, 0))
# Outside the panel: the panel must end above the buttons or it eats their clicks.
foot = clone(TEXT, "NFFootnote", (57, 412, 530, 456), text="TOOLTIP:NetForm", font=(9, 0), status="ENABLED+HIDDEN")

s, e = block(ls, P + 'PlayerInfoParent"')
par = header(ls[s:e + 1])
par = clone(par, "NetFormParent", (52, 81, 757, 410), status="ENABLED+HIDDEN+SEE_THRU")
child_lines = []
for l in kids:
    child_lines.append(l)
end_ind = len(par[-1]) - len(par[-1].lstrip())
ls[e + 1:e + 1] = par[:-1] + child_lines + [" " * (end_ind + 2) + "ENDALLCHILDREN", par[-1]] + reindent(foot, end_ind)
open(out, "wb").write("\r\n".join(ls).encode("latin-1"))
print("controls:", "".join(kids).count('NAME = "'))
