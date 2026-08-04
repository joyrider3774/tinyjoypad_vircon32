#!/usr/bin/env python3
"""Generate the 256-tile "OLED column" texture atlas used by portVircon32.c.

TinyJoypad games (all lineages) stream their 128x64 monochrome display to
hardware one SSD1306 "page" byte at a time - each byte packs 8 vertical
pixels of a single 1px-wide column (bit 0 = top pixel, bit 7 = bottom
pixel, the standard SSD1306 vertical byte order). There are only 256
possible byte values, so instead of drawing pixels on Vircon32 at runtime
(there is no CPU-writable framebuffer - see VIRCON32_C_DIALECT.md), this
script pre-renders all 256 possible column patterns once, at build time,
already scaled up to the final on-screen pixel size. portVircon32.c then
slices this image into 256 texture regions (ids 0-255, matching the byte
value each depicts) via define_region_matrix(), and turns each
SendPixels(byte) call into a single select_region(byte) + draw_region_at()
GPU blit.

Off bits are rendered as opaque black (not transparent) rather than
alpha-blended: every column cell is drawn at most once per frame, over a
screen already clear_screen()-ed to black, so opaque black-for-off is
visually identical to transparent-for-off and avoids needing an alpha
channel (or a PNG-encoding dependency) entirely - a plain 24-bit PPM is
enough, converted to PNG with ImageMagick.

Output: tools/atlas.ppm (regenerate assets/columns.png from it with
`magick tools/atlas.ppm assets/columns.png`).

Re-run this only if SCALE, GRID_COLS, or the bit order below ever change -
the output is fully deterministic, so the checked-in assets/columns.png
does not need Python or ImageMagick to build the cartridge day-to-day.
"""

import struct

SCALE = 5          # 1 OLED pixel -> SCALE x SCALE physical pixels
GRID_COLS = 16      # tiles per row in the atlas (16x16 = 256 tiles)
GRID_ROWS = 16
TILE_W = SCALE
TILE_H = 8 * SCALE

IMG_W = GRID_COLS * TILE_W
IMG_H = GRID_ROWS * TILE_H

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


def main():
    pixels = bytearray(IMG_W * IMG_H * 3)

    for value in range(256):
        grid_x = value % GRID_COLS
        grid_y = value // GRID_COLS
        tile_origin_x = grid_x * TILE_W
        tile_origin_y = grid_y * TILE_H

        for bit in range(8):
            lit = (value >> bit) & 1
            color = WHITE if lit else BLACK
            row_origin_y = tile_origin_y + bit * SCALE

            for dy in range(SCALE):
                y = row_origin_y + dy
                row_start = y * IMG_W * 3
                for dx in range(SCALE):
                    x = tile_origin_x + dx
                    offset = row_start + x * 3
                    pixels[offset:offset + 3] = bytes(color)

    with open("tools/atlas.ppm", "wb") as f:
        header = f"P6\n{IMG_W} {IMG_H}\n255\n".encode("ascii")
        f.write(header)
        f.write(pixels)

    print(f"Wrote tools/atlas.ppm ({IMG_W}x{IMG_H}, {GRID_COLS}x{GRID_ROWS} "
          f"grid of {TILE_W}x{TILE_H} tiles)")


if __name__ == "__main__":
    main()
