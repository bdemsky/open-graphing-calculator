#!/usr/bin/env python3
"""Check NOTEBOOK.HTML as the calculator's CALC drive serves it (app/nbhtml.c).

    nbhtml_check.py FILE

The page must work without JavaScript and without the network: no <script>, no on...= handlers,
no http(s) URL in src/href, no external stylesheet. Every data: URL must be a PNG that decodes:
signature, chunk CRCs, IHDR, the IDAT zlib stream inflating to the image's rows. Prints a summary;
exit status 1 on the first problem. (tests/ui/114_notebook_html.txt writes FILE.)
"""
import base64
import re
import struct
import sys
import zlib


def check_png(data, w_attr, h_attr):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG signature")
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        crc, = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])
        if zlib.crc32(kind + body) & 0xFFFFFFFF != crc:
            raise ValueError(f"bad CRC in chunk {kind!r}")
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
        if kind == b"IEND":
            break
    if not ihdr:
        raise ValueError("no IHDR")
    w, h, depth, ctype = ihdr[:4]
    if (w, h) != (w_attr, h_attr):
        raise ValueError(f"PNG is {w}x{h}, the img says {w_attr}x{h_attr}")
    raw = zlib.decompress(idat)
    if depth != 8 or ctype != 3 or len(raw) != h * (w + 1):
        raise ValueError(f"unexpected pixel data ({depth} bit, type {ctype}, {len(raw)} bytes)")
    return w, h


def main():
    text = open(sys.argv[1], encoding="utf-8").read()
    problems = []
    low = text.lower()
    if "<script" in low:
        problems.append("contains <script>")
    if re.search(r"\son[a-z]+\s*=", low):
        problems.append("contains an event handler attribute")
    if re.search(r"""(src|href)\s*=\s*["']?\s*(https?:|//)""", low) or "<link" in low or "@import" in low:
        problems.append("loads something from elsewhere")
    if not low.startswith("<!doctype html>") or "</html>" not in low:
        problems.append("not a complete HTML document")
    pngs = 0
    for m in re.finditer(r'<img[^>]*>', text):
        tag = m.group(0)
        src = re.search(r'src="data:image/png;base64,([A-Za-z0-9+/=]+)"', tag)
        w = re.search(r'width="(\d+)"', tag)
        h = re.search(r'height="(\d+)"', tag)
        if not (src and w and h):
            problems.append(f"an <img> without a PNG data URL and its size: {tag[:80]}")
            continue
        try:
            check_png(base64.b64decode(src.group(1), validate=True), int(w.group(1)), int(h.group(1)))
            pngs += 1
        except Exception as e:  # noqa: BLE001 - report any decoding problem
            problems.append(f"PNG {pngs + 1}: {e}")
    cells = len(re.findall(r'<div class="c ', text))
    if problems:
        for p in problems:
            print("nbhtml_check:", p)
        return 1
    print(f"nbhtml_check: {sys.argv[1]}: {len(text)} bytes, {cells} cells, {pngs} PNG pictures, no scripts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
