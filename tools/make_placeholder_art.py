# Writes placeholder PNGs into src/game/art/placeholder/. Standard library only.
# Run from the repo root: python tools/make_placeholder_art.py

import os
import struct
import zlib

OUT_FOLDER = os.path.join("src", "game", "art", "placeholder")

CLEAR = (0, 0, 0, 0)
OUTLINE = (20, 20, 30, 255)
FACE = (240, 200, 60, 255)
CHEEK = (230, 90, 90, 255)

# 16x16 test face: outline, eyes, cheeks, mouth. Each character is one pixel.
TEST_SPRITE = [
    "................",
    ".....######.....",
    "...##oooooo##...",
    "..#oooooooooo#..",
    ".#oooooooooooo#.",
    ".#ooo##oo##ooo#.",
    "#oooo##oo##oooo#",
    "#oooooooooooooo#",
    "#occoooooooocco#",
    "#oooooooooooooo#",
    "#ooo#oooooo#ooo#",
    ".#ooo######ooo#.",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "...##oooooo##...",
    ".....######.....",
]
TEST_SPRITE_COLORS = {".": CLEAR, "#": OUTLINE, "o": FACE, "c": CHEEK}


def write_png(path, width, height, pixels):
    rows = b""
    for y in range(height):
        rows += b"\x00" + b"".join(bytes(pixels[y * width + x]) for x in range(width))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def grid_pixels(rows, colors):
    return [colors[c] for row in rows for c in row]


def main():
    os.makedirs(OUT_FOLDER, exist_ok=True)
    pixels = grid_pixels(TEST_SPRITE, TEST_SPRITE_COLORS)
    write_png(os.path.join(OUT_FOLDER, "test_sprite.png"), 16, 16, pixels)


if __name__ == "__main__":
    main()
