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

W = 162.56   # 32 HP
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
add(text("two tetrachords, four tracks", 45.0, 9.4, 2.1, FONT_MED, ACCENT, anchor="start"))
add(text("scale = lower + upper", W - 8.0, 9.4, 2.1, FONT_MED, SOFT, anchor="end"))
rect(6.0, 12.0, 150.56, 19.0, SCREEN, 1.6)

# --- tetrachord rows -------------------------------------------------------
ROW_Y = {"UPPER": 44.0, "LOWER": 59.0}
for name, y in ROW_Y.items():
    ly = y - 6.4
    label("GENUS", 14, ly)
    label(name, 46.5, ly, 2.1, ACCENT)
    for k in range(4):
        hint(str(k + 1), 30 + 11 * k - 3.6, y - 3.0, 1.3)
    label("SPREAD", 79, ly, 1.9)
    label("LEAP", 93, ly, 1.9)
    label("REV", 106, ly, 1.9)
hint("octaves", 79, 64.6, 1.3)
hint("how many", 93, 64.6, 1.3)
vline(115.5, 35.0, 64.0)
label("ROOT", 127, 37.6)
label("CV", 147, 37.6)
hint("V/oct", 147, 49.6, 1.3)
label("JOIN", 124, 52.6, 1.9)
hint("up: conj", 124, 65.2, 1.3)
label("RESEED", 143, 52.6, 1.9)
hint("button", 137, 65.2, 1.3)
hint("trig", 149, 65.2, 1.3)

# --- patterns --------------------------------------------------------------
hline(66.0, 6.0, W - 6.0)
PAT_Y = 74.0
label("PATTERNS", 48.75, 69.4, 1.9, ACCENT)
label("CHAIN", 98, 69.0, 1.8)
label("PAT CV", 111, 69.0, 1.8)
label("NEXT", 123, 69.0, 1.8)
label("RESET", 135, 69.0, 1.8)
rect(143.0, 66.3, 12.0, 13.0, OUTBOX)
add(text("EOC", 149, 69.3, 1.8, FONT_BOLD, OUTINK, spacing=0.06))

# --- tracks ----------------------------------------------------------------
TRACK_X = [22.0, 61.5, 101.0, 140.5]
Y1, Y2, Y3 = 89.5, 102.5, 116.5
for x in (41.75, 81.25, 120.75):
    vline(x, 79.0, 123.5)
for t, x in enumerate(TRACK_X):
    add(text(f"TRACK {t + 1}", x, 82.0, 2.0, FONT_BOLD, TRACK_COL[t], spacing=0.12))
    for dx, n in ((-11, "PATH"), (0, "SOURCE"), (11, "OCT")):
        label(n, x + dx, Y1 - 5.0, 1.6)
    for dx, n in ((-11, "LENGTH"), (0, "CHANCE"), (11, "CHORD")):
        label(n, x + dx, Y2 - 5.0, 1.6)
    rect(x - 5.6, Y3 - 7.4, 22.2, 13.4, OUTBOX)
    label("CLOCK", x - 11, Y3 - 5.0, 1.6)
    add(text("V/OCT", x, Y3 - 5.0, 1.6, FONT_BOLD, OUTINK, spacing=0.06))
    add(text("GATE", x + 11, Y3 - 5.0, 1.6, FONT_BOLD, OUTINK, spacing=0.06))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
with open(os.path.join(HERE, "Tetrapods.svg"), "w") as fh:
    fh.write(svg)
print("wrote Tetrapods.svg")
