#!/usr/bin/env python3
"""Convert a raw RGB565 little-endian framebuffer dump to PNG (no dependencies).

Usage: fb2png.py IN.bin OUT.png [--width 320] [--height 240] [--stride BYTES]

Used by openocd/readfb.sh, which dumps the framebuffer at 0xD0000000 over SWD.
This works even when the firmware is hung (no console needed), unlike
calcctl.py --shot.
"""
import argparse
import struct
import sys
import zlib


def rgb565_to_rgb888(data, w, h, stride):
    """PNG scanline data (filter byte 0 + RGB888 per pixel) from a raw RGB565
    little-endian dump with `stride` bytes per row. 5/6-bit channels are
    widened by bit replication so full scale maps to 255."""
    rows = []
    for y in range(h):
        row = bytearray(b"\x00")  # PNG filter type 0
        base = y * stride
        for x in range(w):
            v = data[base + 2 * x] | (data[base + 2 * x + 1] << 8)
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        rows.append(bytes(row))
    return b"".join(rows)


def write_png(path, w, h, raw):
    """Write 8-bit RGB scanlines `raw` as a PNG (IHDR, one zlib IDAT, IEND)."""
    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--stride", type=int, default=0, help="bytes per row (default width*2)")
    a = ap.parse_args()
    stride = a.stride or a.width * 2
    data = open(a.inp, "rb").read()
    need = stride * (a.height - 1) + a.width * 2  # last row needs no padding
    if len(data) < need:
        sys.exit(f"{a.inp}: {len(data)} bytes, need {need}")
    write_png(a.out, a.width, a.height, rgb565_to_rgb888(data, a.width, a.height, stride))
    print(f"wrote {a.out} ({a.width}x{a.height})")


if __name__ == "__main__":
    main()
