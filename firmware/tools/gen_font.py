#!/usr/bin/env python3
"""Convert the public-domain X11 misc-fixed BDF fonts into C tables.

Usage: gen_font.py OUT.c  (fonts are read from third_party/fonts/)

Only the glyphs the calculator needs are emitted (ASCII + a list of math
symbols). Each glyph is a full character cell (w x h), one row per
`bytes_per_row` bytes, MSB = leftmost pixel. Codepoints are sorted so the
renderer can binary-search them.

Output (core/gen/fonts_gen.c, consumed by core/font.c and core/gfx.c): per
font a sorted codepoint array `<name>_cp`, the glyph bitmaps `<name>_bits`
(glyph i at offset i * height * bytes_per_row) and a font_t
{width, height, ascent, bytes_per_row, count, cp, bits}. The fonts are
monospaced, so every glyph is a whole cell and text layout is width * n.

A font may name fallback BDF files for glyphs it lacks; those are rendered in
their own cell, centred horizontally in the target cell and aligned on the
same baseline. Codepoints no font has are reported on stderr (they draw as
'?'). The 2x font is made with Scale2x so diagonals stay smooth instead of
becoming staircases, and goes to flash bank 2 (core/sections.h) to keep
bank 1 for code.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FONTDIR = os.path.join(HERE, "../third_party/fonts")

FONTS = [  # (C name, bdf file, scale, fallbacks [(bdf, scale)] for glyphs the first one lacks)
    ("font_large", "10x20.bdf", 1, None),
    ("font_medium", "7x14.bdf", 1, [("6x13.bdf", 1)]),
    ("font_small", "6x13.bdf", 1, None),
    # large-font accessibility mode (app/theme.c): 7x14 doubled with Scale2x
    # smoothing (14x28), missing glyphs from 6x13 (doubled) or 10x20; in flash bank 2
    ("font_xlarge", "7x14.bdf", 2, [("6x13.bdf", 2), ("10x20.bdf", 1)]),
]

# Non-ASCII glyphs the UI and the help manual may use (tools/gen_help.py
# reads this line to validate help sources, so keep it a one-line literal).
EXTRA = "θπ√²³⁻¹→←↑↓▶◀▲▼≤≥≠∫∂∇∞ΣΔ×÷−·°±≈ⁿ█▒ᴇʳⁱˣ∠½∛∜∓Πʸ⌫⇧"
CHARSET = sorted(set(range(32, 127)) | {ord(c) for c in EXTRA})

# Math notation of the help manual's refresher pages and formulas (nabla, subscript and
# superscript digits, Greek letters, logic and set symbols); also read by tools/gen_help.py.
EXTRA_MATH = "∇₀₁₂₃₄₅₆₇₈₉⁰⁴⁵⁶⁷⁸⁹⁺⁽⁾αβγδελμνσφχωρτΦΩ∮∈∀∃¬∧∨⊕⇒⇔≡∝‖⟨⟩∪∩ℝℂℤℕ∘⊥⌊⌋⌈⌉…"
CHARSET = sorted(set(CHARSET) | {ord(c) for c in EXTRA_MATH})


def parse_bdf(path):
    """Minimal BDF 2.1 reader.

    Returns (FONTBOUNDINGBOX (w, h, xoff, yoff), FONT_ASCENT, glyphs) with
    glyphs = {encoding: ((w, h, xoff, yoff) from BBX, [bitmap row ints])}.
    Each bitmap row is the hex value from the file: the glyph's w pixels,
    MSB first, padded to whole bytes.
    """
    glyphs = {}
    fbb = None
    ascent = None
    with open(path, encoding="latin-1") as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("FONTBOUNDINGBOX"):
            fbb = tuple(int(v) for v in ln.split()[1:])
        elif ln.startswith("FONT_ASCENT"):
            ascent = int(ln.split()[1])
        elif ln.startswith("STARTCHAR"):
            enc = None
            bbx = None
            bitmap = []
            i += 1
            while not lines[i].startswith("ENDCHAR"):
                l2 = lines[i]
                if l2.startswith("ENCODING"):
                    enc = int(l2.split()[1])
                elif l2.startswith("BBX"):
                    bbx = tuple(int(v) for v in l2.split()[1:])
                elif l2.startswith("BITMAP"):
                    i += 1
                    while not lines[i].startswith("ENDCHAR"):
                        bitmap.append(int(lines[i], 16) if lines[i].strip() else 0)
                        i += 1
                    break
                i += 1
            glyphs[enc] = (bbx, bitmap)
        i += 1
    return fbb, ascent, glyphs


def render(fbb, ascent, glyph):
    """Return list of rows (ints, MSB-first, cell width bits) for the glyph.

    Places the glyph's own bounding box (BBX: size and offset from the origin,
    y up) into the font's cell: the baseline is `ascent` rows below the cell
    top and x is measured from the font bounding box's x offset. Pixels
    outside the cell are clipped.
    """
    cw, ch, cx, cy = fbb
    (w, h, xo, yo), bitmap = glyph
    nbytes = (w + 7) // 8
    rows = [0] * ch
    # baseline is at row `ascent` from the top of the cell
    top = ascent - (yo + h)
    for r, val in enumerate(bitmap):
        y = top + r
        if not 0 <= y < ch:
            continue
        for bx in range(w):
            if val & (1 << (nbytes * 8 - 1 - bx)):
                x = bx + xo - cx
                if 0 <= x < cw:
                    rows[y] |= 1 << (cw - 1 - x)
    return rows


def scale2x(rows, w, h):
    """Scale2x (EPX) on a 1-bit glyph: rows of w-bit ints -> 2h rows of 2w bits.

    Each pixel P becomes a 2x2 block E0 E1 / E2 E3. With neighbours A (above),
    B (right), C (left), D (below), a sub-pixel takes the colour of its two
    adjacent neighbours when they agree with each other and differ from the
    other two (e.g. E0 = A if C == A, C != D, A != B), else P. This rounds
    diagonal steps instead of doubling them. Pixels outside the glyph count
    as background.
    """
    def px(x, y):
        return 1 if 0 <= x < w and 0 <= y < h and rows[y] & (1 << (w - 1 - x)) else 0
    out = [0] * (2 * h)
    for y in range(h):
        for x in range(w):
            p, a, b, c, d = px(x, y), px(x, y - 1), px(x + 1, y), px(x - 1, y), px(x, y + 1)
            e0 = a if (c == a and c != d and a != b) else p
            e1 = b if (a == b and a != c and b != d) else p
            e2 = c if (d == c and d != b and c != a) else p
            e3 = d if (b == d and b != a and d != c) else p
            for (dx, dy, v) in ((0, 0, e0), (1, 0, e1), (0, 1, e2), (1, 1, e3)):
                if v:
                    out[2 * y + dy] |= 1 << (2 * w - 1 - (2 * x + dx))
    return out


SMALL_CAP_E = 0x1D07  # ᴇ (the exponent mark, ×10ⁿ): missing in 6x13 and 7x14


def synth_small_cap_e(fbb, ascent, glyphs):
    """A small capital E for fonts that lack U+1D07: the stroke columns of the
    font's own 'E' (left bar, arm length) at the height of its 'x' (the rows
    the x-height glyph covers), with arms at the top, middle and bottom.
    Returns the cell rows like render(), or None if the font lacks E or x."""
    if ord("E") not in glyphs or ord("x") not in glyphs:
        return None
    e = render(fbb, ascent, glyphs[ord("E")])
    x = render(fbb, ascent, glyphs[ord("x")])
    used = [r for r, v in enumerate(x) if v]
    top, bot = used[0], used[-1]
    bar = next(v for v in e if v)       # E's top arm (its first row with ink)
    left = 1 << (bar.bit_length() - 1)  # its leftmost column: the vertical bar
    rows = [0] * fbb[1]
    mid = (top + bot) // 2
    for r in range(top, bot + 1):
        rows[r] = left
    rows[top] = rows[bot] = bar
    rows[mid] = bar & ~(bar & -bar) | left  # the middle arm one pixel shorter
    return rows


# Glyphs redrawn here because the BDF original looks wrong at this size: (bdf file, codepoint)
# -> BITMAP rows for the font's full cell (same BBX as the BDF cell). 7x14's '/' rose above the
# digits, dropped into the descender and stepped unevenly (2-2-3-2-2-2 rows), so it looked kinked
# next to digits ("1/6"); this one spans the digit height in even 2-row steps.
OVERRIDES = {
    ("7x14.bdf", ord("/")): [0x00, 0x00, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x40, 0x40, 0x00, 0x00],
}


def main():
    """Write all FONTS as C tables to the file named by argv[1]."""
    out = sys.argv[1]
    o = []
    o.append("/* Generated by tools/gen_font.py from the public-domain X11 misc-fixed fonts")
    o.append(" * (Markus Kuhn's ucs-fonts, see third_party/README.md). Do not edit. */")
    o.append('#include "font.h"')
    o.append('#include "sections.h"\n')
    cp_tables = {}  # codepoint tuple -> name of the table already emitted
    for cname, fname, scale, fallback in FONTS:
        fbb, ascent, glyphs = parse_bdf(os.path.join(FONTDIR, fname))
        for (ofile, ocp), rows in OVERRIDES.items():
            if ofile == fname:
                glyphs[ocp] = ((fbb[0], fbb[1], fbb[2], fbb[3]), rows)
        fbs = [(parse_bdf(os.path.join(FONTDIR, f)), s) for f, s in (fallback or [])]
        cw, ch = fbb[0] * scale, fbb[1] * scale  # cell size in pixels
        bpr = (cw + 7) // 8                       # bytes per glyph row
        synth = {}  # glyphs drawn here because neither the font nor a fallback has them
        if SMALL_CAP_E not in glyphs and scale == 1:
            r = synth_small_cap_e(fbb, ascent, glyphs)
            if r:
                synth[SMALL_CAP_E] = r
        cps = [c for c in CHARSET if c in glyphs or c in synth or any(c in f[2] for f, _ in fbs)]
        missing = [chr(c) for c in CHARSET if c not in cps]
        if missing:
            print(f"{fname}: missing {''.join(missing)} (rendered as '?')", file=sys.stderr)
        place = "FLASH2_RODATA " if scale > 1 else ""
        assert max(cps) <= 0xFFFF, "codepoint tables are uint16_t"
        # fonts with the same glyph set share one codepoint table
        cpname = cp_tables.get(tuple(cps))
        if cpname is None:
            cpname = cp_tables[tuple(cps)] = f"{cname}_cp"
            o.append(f"{place}static const uint16_t {cname}_cp[{len(cps)}] = {{")
            for i in range(0, len(cps), 12):
                o.append("    " + ", ".join(f"0x{c:04X}" for c in cps[i:i + 12]) + ",")
            o.append("};")
        o.append(f"{place}static const uint8_t {cname}_bits[{len(cps) * ch * bpr}] = {{")
        for c in cps:
            if c in synth:
                rows = synth[c]
            elif c in glyphs:
                rows = render(fbb, ascent, glyphs[c])
                if scale > 1:
                    rows = scale2x(rows, fbb[0], fbb[1])
            else:  # from the first fallback font that has it: scaled, centred, same baseline
                (f2bb, f2asc, f2g), s2 = next(f for f in fbs if c in f[0][2])
                r2 = render(f2bb, f2asc, f2g[c])
                if s2 > 1:
                    r2 = scale2x(r2, f2bb[0], f2bb[1])
                w2 = f2bb[0] * s2
                dx = (cw - w2) // 2               # centre horizontally
                dy = scale * ascent - s2 * f2asc  # align the baselines
                sh = cw - w2 - dx                 # < 0: wider than the cell, cropped on both sides
                if sh < 0:
                    print(f"{fname}: fallback glyph U+{c:04X} is {w2} px wide, cell {cw} px (cropped)",
                          file=sys.stderr)
                rows = [0] * ch
                for y, v in enumerate(r2):
                    if 0 <= y + dy < ch:
                        rows[y + dy] = (v << sh if sh >= 0 else v >> -sh) & ((1 << cw) - 1)
            # left-align the cell in bpr*8 bits
            bs = []
            for r in rows:
                v = r << (bpr * 8 - cw)
                for b in range(bpr):
                    bs.append((v >> (8 * (bpr - 1 - b))) & 0xFF)
            label = chr(c) if c != 0x5C else "backslash"
            o.append("    " + ",".join(f"0x{b:02X}" for b in bs) + f",  /* U+{c:04X} {label} */")
        o.append("};")
        o.append(f"const font_t {cname} = {{ {cw}, {ch}, {ascent * scale}, {bpr}, {len(cps)}, "
                 f"{cpname}, {cname}_bits }};\n")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(o))


if __name__ == "__main__":
    main()
