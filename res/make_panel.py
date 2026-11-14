#!/usr/bin/env python3
# Draws res/Tetrapods.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# the layout namespace in src/Tetrapods.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W = 203.2   # 40 HP
H = 128.5
PAPER = "#ECEEE8"
INK = "#1C2224"
SOFT = "#66706C"
ACCENT = "#1F7A70"
OUTBOX = "#262B2D"
OUTINK = "#ECEEE8"
SCREEN = "#121516"
TRACK_COL = ["#D9503E", "#C99A1E", "#2F9DB8", "#8F67C9"]   # match the display

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = x - width * scale / 2 if anchor == "middle" else (x - width * scale if anchor == "end" else x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = [f'<rect width="{W}" height="{H}" fill="{PAPER}"/>']
add = parts.append


def label(s, x, y, size=2.1, fill=INK):
    add(text(s, x, y, size, FONT_BOLD, fill, spacing=0.06))


def hint(s, x, y, size=1.45, fill=SOFT, anchor="middle"):
    add(text(s, x, y, size, FONT_MED, fill, anchor=anchor))


def rect(x, y, w, h, fill, rx=1.2):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"/>')


def vline(x, y1, y2):
    add(f'<line x1="{x}" y1="{y1}" x2="{x}" y2="{y2}" stroke="#C9CDC4" stroke-width="0.35" stroke-dasharray="0.9 0.9"/>')


def hline(y, x1, x2):
    add(f'<line x1="{x1}" y1="{y}" x2="{x2}" y2="{y}" stroke="#C9CDC4" stroke-width="0.35" stroke-dasharray="0.9 0.9"/>')


def border(y):
    """Groups of four dots: tetrachords."""
    x = 4.0
    while x < W - 6.0:
        for k in range(4):
            add(f'<circle cx="{x + k * 1.6:.2f}" cy="{y}" r="0.42" fill="{ACCENT}"/>')
        x += 9.0


border(2.4)
border(H - 2.4)
add(text("TETRAPODS", 8.0, 9.6, 4.4, FONT_BOLD, INK, anchor="start", spacing=0.14))
add(text("two tetrachords, four tracks", 47.0, 9.5, 2.8, FONT_MED, ACCENT, anchor="start"))
add(text("scale = lower + upper", W - 8.0, 9.5, 2.8, FONT_MED, SOFT, anchor="end"))
rect(6.0, 12.0, W - 12.0, 19.0, SCREEN, 1.6)

# --- tetrachord rows -------------------------------------------------------
GENUS_X, NOTE_X0, NOTE_DX, SPREAD_X, LEAP_X, REV_X = 15.0, 34.0, 12.5, 91.0, 109.0, 125.0
ROW_Y = {"UPPER": 44.0, "LOWER": 59.0}
for name, y in ROW_Y.items():
    ly = y - 6.2
    label("GENUS", GENUS_X, ly, 3.2)
    label(name, NOTE_X0 + 1.5 * NOTE_DX, ly, 3.2, ACCENT)
    for k in range(4):
        hint(str(k + 1), NOTE_X0 + NOTE_DX * k - 5.2, y - 2.6, 2.2)
    label("SPREAD", SPREAD_X, ly, 3.2)
    label("LEAP", LEAP_X, ly, 3.2)
    label("REV", REV_X, ly, 3.2)
hint("octaves", SPREAD_X, 65.3, 2.2)
hint("how many", LEAP_X, 65.3, 2.2)
vline(136.0, 35.0, 64.0)
label("ROOT", 152, 37.8, 3.2)
label("CV", 176, 37.8, 2.9)
hint("V/oct", 183.5, 45.0, 2.2, anchor="start")
label("JOIN", 150, 52.8, 2.9)
hint("up: conj", 150, 65.3, 2.2)
label("RESEED", 177, 52.8, 2.9)
hint("button", 168, 65.3, 2.2)
hint("trig", 186, 65.3, 2.2)

# --- patterns --------------------------------------------------------------
hline(66.6, 6.0, W - 6.0)
PAT_Y = 74.0
label("PATTERNS", 12.0 + 3.5 * 13.0, 69.4, 2.9, ACCENT)
label("CHAIN", 120, 69.4, 2.9)
label("PAT CV", 136, 69.4, 2.9)
label("NEXT", 152, 69.4, 2.9)
label("RESET", 168, 69.4, 2.9)
rect(179.0, 66.9, 14.0, 12.6, OUTBOX)
add(text("EOC", 186, 69.4, 2.9, FONT_BOLD, OUTINK, spacing=0.06))

# --- tracks ----------------------------------------------------------------
TRACK_X = [27.5, 77.0, 126.5, 176.0]
DX = 16.0
Y1, Y2, Y3 = 91.0, 104.0, 117.6
for x in (52.25, 101.75, 151.25):
    vline(x, 80.0, 123.5)
for t, x in enumerate(TRACK_X):
    add(text(f"TRACK {t + 1}", x, 82.6, 2.9, FONT_BOLD, TRACK_COL[t], spacing=0.10))
    for dx, n in ((-DX, "PATH"), (0, "SOURCE"), (DX, "OCT")):
        label(n, x + dx, Y1 - 5.2, 2.9)
    for dx, n in ((-DX, "LENGTH"), (0, "CHANCE"), (DX, "CHORD")):
        label(n, x + dx, Y2 - 5.2, 2.9)
    rect(x - 7.6, Y3 - 7.6, 31.6, 13.2, OUTBOX)
    label("CLOCK", x - DX, Y3 - 5.2, 2.9)
    add(text("V/OCT", x, Y3 - 5.2, 2.9, FONT_BOLD, OUTINK, spacing=0.04))
    add(text("GATE", x + DX, Y3 - 5.2, 2.9, FONT_BOLD, OUTINK, spacing=0.04))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
with open(os.path.join(HERE, "Tetrapods.svg"), "w") as fh:
    fh.write(svg)
print("wrote Tetrapods.svg")

# --- Haer maker logo (res/haer_logo.py), placed clear of the jacks and rail artwork
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import haer_logo
haer_logo.add_to_file(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "Tetrapods.svg"), x=101.64, y=126.0, cap=3.0, bg='#ECEEE8', knock=(125.55, 126.65))
