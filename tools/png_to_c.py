#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Converte um PNG em um array C de 1 bit por pixel para uso no framebuffer
do Pong RP2040.

Uso:
    python png_to_c.py <entrada.png> <nome_simbolo> [largura_max] [altura_max] [modo]

modo: "dither" (padrao, Floyd-Steinberg) ou "threshold" (corte fixo em 128).

Saida no stdout: trecho em C com largura, altura e bytes do bitmap.
Cada linha e empacotada em bytes MSB-first; linhas sao alinhadas em bytes.
"""

import sys
from PIL import Image


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    src = sys.argv[1]
    name = sys.argv[2]
    max_w = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    max_h = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    mode = sys.argv[5] if len(sys.argv) > 5 else "dither"

    img = Image.open(src).convert("RGBA")
    # Fundo preto: imagens com transparencia ficam corretas
    bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
    img = Image.alpha_composite(bg, img).convert("L")

    if max_w or max_h:
        w, h = img.size
        sx = (max_w / w) if max_w else 1.0
        sy = (max_h / h) if max_h else 1.0
        s = min(sx if sx > 0 else 1.0, sy if sy > 0 else 1.0)
        if s < 1.0:
            img = img.resize((int(w * s), int(h * s)), Image.LANCZOS)

    if mode == "threshold":
        img = img.point(lambda v: 255 if v >= 128 else 0, mode="1")
    else:
        img = img.convert("1", dither=Image.FLOYDSTEINBERG)
    w, h = img.size
    bytes_per_row = (w + 7) // 8

    pixels = img.load()
    data = bytearray()
    for y in range(h):
        for bx in range(bytes_per_row):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < w:
                    # Pillow '1': 0 = preto, 255 = branco
                    if pixels[x, y]:
                        byte |= 1 << (7 - bit)
            data.append(byte)

    print(f"// Gerado de {src}  ({w}x{h}, 1-bit, {bytes_per_row} bytes/linha)")
    print(f"#define {name.upper()}_W {w}")
    print(f"#define {name.upper()}_H {h}")
    print(f"#define {name.upper()}_STRIDE {bytes_per_row}")
    print(f"static const uint8_t {name}_data[{len(data)}] = {{")
    for i, b in enumerate(data):
        if i % 12 == 0:
            print("    ", end="")
        print(f"0x{b:02X},", end="")
        if (i + 1) % 12 == 0 or i == len(data) - 1:
            print()
        else:
            print(" ", end="")
    print("};")


if __name__ == "__main__":
    main()
