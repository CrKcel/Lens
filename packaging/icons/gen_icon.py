#!/usr/bin/env python3
"""Generate Lens icon assets (PNG at multiple sizes + Windows .ico) in pure
Python, so no image toolchain is required. Design mirrors icons/lens.svg:
a metallic ring around a blue glass lens with a specular highlight.

Usage: python3 packaging/icons/gen_icon.py
Writes: lens.png (512), lens-<size>.png, lens.ico  (next to this script)
"""

import math
import struct
import sys
import zlib
from pathlib import Path

SIZES = [512, 256, 128, 64, 48, 32, 16]

RING_OUTER = (0xF1, 0xF5, 0xF9)
RING_INNER = (0x94, 0xA3, 0xB8)
GLASS_CENTER = (0x3B, 0x82, 0xF6)
GLASS_MID = (0x1D, 0x4E, 0xD8)
GLASS_EDGE = (0x0F, 0x17, 0x2A)


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    return tuple(int(lerp(c1[i], c2[i], t)) for i in range(3))


def render(size):
    """Return RGBA rows for the icon at the given size."""
    cx = cy = size / 2.0
    r_out = size * 60 / 128      # metallic ring outer radius
    r_glass = size * 50 / 128    # glass radius
    # specular highlight: ellipse centered upper-left, rotated -32 deg
    hx, hy = size * 46 / 128, size * 42 / 128
    hrx, hry = size * 16 / 128, size * 10 / 128
    ang = -32 * math.pi / 180
    ca, sa = math.cos(ang), math.sin(ang)

    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = (dx * dx + dy * dy) ** 0.5
            if d >= r_out:
                row += b"\x00\x00\x00\x00"
                continue
            # 1px anti-aliased edge on the outer ring
            a_out = max(0.0, min(1.0, (r_out - d) / 1.0))
            if d > r_glass:
                t = d / max(r_out, 1e-6)
                rgb = mix(RING_INNER, RING_OUTER, t)
                alpha = a_out
            else:
                t = d / max(r_glass, 1e-6)
                if t < 0.55:
                    rgb = mix(GLASS_CENTER, GLASS_MID, t / 0.55)
                else:
                    rgb = mix(GLASS_MID, GLASS_EDGE, (t - 0.55) / 0.45)
                # highlight
                px, py = x + 0.5 - hx, y + 0.5 - hy
                ux, uy = px * ca - py * sa, px * sa + py * ca
                e = (ux / hrx) ** 2 + (uy / hry) ** 2
                if e < 1.0:
                    h = (1.0 - e) ** 0.5 * 0.55
                    rgb = mix(rgb, (255, 255, 255), h)
                alpha = a_out * max(0.0, min(1.0, (r_glass - d) / 1.0))
            row += bytes(rgb) + bytes([int(alpha * 255)])
        rows.append(bytes(row))
    return size, size, rows


def write_png(path, size, rows):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    raw = b"".join(b"\x00" + r for r in rows)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    Path(path).write_bytes(png)
    return png


def write_ico(path, images):
    """images: list of (w, h, png_bytes). ICO entries may embed PNG data."""
    out = bytearray(struct.pack("<HHH", 0, 0, len(images)))
    offset = 6 + 16 * len(images)
    for w, h, data in images:
        out += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32,
                           len(data), offset)
        offset += len(data)
    for _, _, data in images:
        out += data
    Path(path).write_bytes(bytes(out))


def main():
    here = Path(__file__).resolve().parent
    images = []
    for size in SIZES:
        w, h, rows = render(size)
        png = write_png(here / f"lens-{size}.png", w, rows)
        images.append((w, h, png))
        print(f"lens-{size}.png")
    (here / "lens-512.png").rename(here / "lens.png")
    print("lens.png")
    write_ico(here / "lens.ico", images)
    print("lens.ico")


if __name__ == "__main__":
    sys.exit(main())
