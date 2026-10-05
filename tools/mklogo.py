#!/usr/bin/env python3
"""Converts a logo PNG to the raw format shown by the `logo` shell command.

Output: 768 bytes of palette (256 x RGB, 6-bit VGA DAC values) followed by SIZE x SIZE
pixel indexes. Needs Pillow:  uv run --with pillow tools/mklogo.py drakelogo.png assets/drakelogo.bin
"""
import sys
from PIL import Image

SIZE = 200


def main(src, dst):
    img = Image.open(src).convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)
    pal_img = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    palette = (pal_img.getpalette() or [])[: 256 * 3]
    palette += [0] * (768 - len(palette))
    with open(dst, "wb") as out:
        out.write(bytes(v >> 2 for v in palette))
        out.write(pal_img.tobytes())


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: mklogo.py INPUT.png OUTPUT.bin")
    main(sys.argv[1], sys.argv[2])
