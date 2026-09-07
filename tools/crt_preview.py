#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Aplica efeito de TV CRT a imagens do framebuffer 1-bit.

Uso:
    python tools/crt_preview.py <entrada.png> [saida.png]
    python tools/crt_preview.py --all
        processa todos os docs/images/sim_*.png gerando docs/images/crt_*.png

Efeitos:
- Upscale 4x nearest-neighbor (pixels nitidos)
- Phosphor: tint levemente esverdeado-branco (CRT P4)
- Bloom: brancos brilham nos vizinhos
- Scanlines: linhas alternadas mais escuras
- Vignette: bordas sutilmente escurecidas
- Curvatura (barrel): leve distorcao radial simulando o tubo

Requisitos: pip install pillow numpy
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter, ImageOps


def crt_effect(img: Image.Image,
               scale: int = 4,
               phosphor: tuple = (245, 252, 240),
               scanline_strength: float = 0.35,
               bloom_radius: float = 2.0,
               bloom_strength: float = 0.85,
               brightness_boost: float = 1.35,
               vignette_strength: float = 0.40,
               barrel_k: float = 0.06,
               gap_color: tuple = (8, 10, 8)) -> Image.Image:
    # ---- 1. upscale com pixels nitidos ----
    img = img.convert("L").resize(
        (img.width * scale, img.height * scale), Image.NEAREST
    )
    w, h = img.size

    # ---- 2. fosforo: 0 -> preto, 255 -> phosphor ----
    base = ImageOps.colorize(img, black=(0, 0, 0), white=phosphor)
    base_a = np.asarray(base, dtype=np.float32)

    # ---- 3. bloom ADITIVO (mantem o pixel original brilhante) ----
    bloom = base.filter(ImageFilter.GaussianBlur(radius=scale * bloom_radius))
    bloom_a = np.asarray(bloom, dtype=np.float32) * bloom_strength
    a = base_a + bloom_a

    # ---- 4. scanlines (linhas a cada scale px escurecidas) ----
    sl = np.ones((h,), dtype=np.float32)
    sl[::scale] = 1.0 - scanline_strength
    a *= sl[:, None, None]

    # ---- 5. aperture grille suave (gap vertical fino) ----
    g = np.ones((w,), dtype=np.float32)
    g[::scale] = 0.90
    a *= g[None, :, None]

    # ---- 6. boost geral pra compensar perdas das texturas ----
    a *= brightness_boost

    # ---- 7. vignette ----
    yy, xx = np.meshgrid(np.arange(h), np.arange(w), indexing="ij")
    nx = (xx - w/2) / (w/2)
    ny = (yy - h/2) / (h/2)
    r2 = nx*nx + ny*ny
    vig = np.clip(1.0 - vignette_strength * (r2 ** 1.4), 0.25, 1.0)
    a *= vig[..., None]

    # ---- 8. floor (preto puro fica muito chapado) ----
    floor = np.array(gap_color, dtype=np.float32)
    a = np.maximum(a, floor[None, None, :] * vig[..., None])

    a = np.clip(a, 0, 255).astype(np.uint8)
    out = Image.fromarray(a)

    # ---- 9. barrel distortion (curvatura do tubo) ----
    if barrel_k > 0:
        out = barrel(out, k=barrel_k)

    return out


def barrel(img: Image.Image, k: float = 0.08) -> Image.Image:
    """Distorcao radial (barrel) via PIL.MESH com grid 16x16.

    Para cada celula do output, calcula de onde amostrar no input usando
    o mapeamento r' = r * (1 - k*r**2) (compressao nas bordas).
    """
    w, h = img.size
    cx, cy = w / 2, h / 2
    cells = 24
    mesh = []
    for gy in range(cells):
        for gx in range(cells):
            x0 = gx * w // cells
            y0 = gy * h // cells
            x1 = (gx + 1) * w // cells
            y1 = (gy + 1) * h // cells
            # source coords para cada um dos 4 cantos do retangulo de destino
            corners = []
            for px, py in [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]:
                nx = (px - cx) / cx
                ny = (py - cy) / cy
                r2 = nx*nx + ny*ny
                factor = 1.0 + k * r2
                sx = nx * factor * cx + cx
                sy = ny * factor * cy + cy
                corners.append((sx, sy))
            mesh.append((
                (x0, y0, x1, y1),
                (corners[0][0], corners[0][1],
                 corners[1][0], corners[1][1],
                 corners[2][0], corners[2][1],
                 corners[3][0], corners[3][1]),
            ))
    return img.transform(img.size, Image.MESH, mesh, Image.BILINEAR, fillcolor=(0, 0, 0))


def process_file(src: Path, dst: Path) -> None:
    img = Image.open(src)
    # Se a entrada ja for grande (PNG do simulador a 3x), volta para tamanho FB
    # base via downscale com NEAREST para nao borrar.
    if img.width >= 600:
        # detecta scale=3 e volta a 256x192
        base_w, base_h = 256, 192
        if img.width % base_w == 0 and img.height % base_h == 0:
            img = img.resize((base_w, base_h), Image.NEAREST)
    out = crt_effect(img)
    dst.parent.mkdir(parents=True, exist_ok=True)
    out.save(dst)
    print(f"  {src.name}  ->  {dst}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", nargs="?", help="PNG de entrada")
    parser.add_argument("output", nargs="?", help="PNG de saida (opcional)")
    parser.add_argument("--all", action="store_true",
                        help="processa todos os docs/images/sim_*.png em docs/images/crt_*.png")
    parser.add_argument("--scale", type=int, default=4)
    args = parser.parse_args()

    if args.all:
        root = Path(__file__).parent.parent
        srcdir = root / "docs" / "images"
        srcs = sorted(srcdir.glob("sim_*.png"))
        if not srcs:
            print("Nenhum sim_*.png encontrado em docs/images/", file=sys.stderr)
            sys.exit(1)
        for s in srcs:
            d = srcdir / s.name.replace("sim_", "crt_")
            process_file(s, d)
        return

    if not args.input:
        parser.print_help()
        sys.exit(1)
    src = Path(args.input)
    dst = Path(args.output) if args.output else src.with_name("crt_" + src.name)
    process_file(src, dst)


if __name__ == "__main__":
    main()
