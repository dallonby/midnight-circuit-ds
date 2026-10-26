"""Draw the original 32x32, 16-color Nintendo DS banner icon."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
PIXELS = [[0 for _ in range(32)] for _ in range(32)]
PALETTE = [
    (3, 8, 17), (8, 20, 35), (12, 39, 57), (12, 178, 199),
    (44, 230, 234), (44, 82, 113), (185, 32, 171), (245, 84, 198),
    (241, 186, 69), (255, 230, 132), (5, 10, 19), (13, 71, 91),
    (17, 132, 155), (75, 107, 135), (122, 211, 224), (239, 247, 249),
]


def rect(x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(32, y1)):
        for x in range(max(0, x0), min(32, x1)):
            PIXELS[y][x] = color


def line(x0, y0, x1, y1, color):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        if 0 <= x0 < 32 and 0 <= y0 < 32:
            PIXELS[y0][x0] = color
        if x0 == x1 and y0 == y1:
            break
        e = 2 * err
        if e >= dy:
            err += dy
            x0 += sx
        if e <= dx:
            err += dx
            y0 += sy


rect(0, 0, 32, 32, 1)
for y in range(5, 14):
    for x in range(12, 22):
        if (x - 17) ** 2 + (y - 9) ** 2 <= 22:
            PIXELS[y][x] = 6 if y < 9 else 7
rect(1, 7, 10, 22, 2)
rect(2, 6, 9, 8, 5)
rect(23, 4, 31, 22, 2)
rect(24, 3, 30, 5, 5)
for y in (10, 14, 18):
    rect(3, y, 5, y + 1, 3)
    rect(7, y, 9, y + 1, 3)
    rect(25, y - 2, 27, y - 1, 7)
    rect(29, y - 2, 31, y - 1, 7)
for y in range(15, 32):
    left = max(0, 12 - (y - 15) // 2)
    right = min(32, 22 + (y - 15) // 2)
    rect(left, y, right, y + 1, 10)
line(16, 15, 14, 31, 8)
line(18, 15, 19, 31, 8)
rect(10, 20, 23, 29, 3)
rect(12, 18, 21, 22, 4)
rect(13, 19, 20, 22, 5)
rect(11, 22, 22, 26, 12)
rect(12, 24, 21, 28, 3)
rect(9, 22, 11, 27, 0)
rect(22, 22, 24, 27, 0)
rect(11, 27, 14, 29, 9)
rect(19, 27, 22, 29, 9)
rect(13, 29, 20, 30, 11)

width = height = 32
pixel_bytes = bytes(PIXELS[y][x] for y in range(31, -1, -1) for x in range(32))
palette_bytes = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in PALETTE)
offset = 14 + 40 + len(palette_bytes)
size = offset + len(pixel_bytes)
header = struct.pack("<2sIHHI", b"BM", size, 0, 0, offset)
dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 8, 0, len(pixel_bytes), 2835, 2835, 16, 16)
(ROOT / "icon.bmp").write_bytes(header + dib + palette_bytes + pixel_bytes)
print("Wrote icon.bmp")
