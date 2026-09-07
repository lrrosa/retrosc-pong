#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Gera as imagens da placa para a documentacao:

  docs/images/pcb_top.png     montada (com os modelos 3D que existirem)
  docs/images/pcb_silk3d.png  SEM componentes -> a serigrafia toda visivel
  docs/images/pcb_iso.png     vista isometrica, sem componentes

O truque do "sem componentes": salva uma copia da placa com a lista de
modelos 3D de cada footprint esvaziada. So o Pico tem modelo (os
footprints proprios do RCA e do PAM8403 nao tem), e ele cobre justamente o
logo -- por isso a versao nua e a unica em que da para conferir o silk.

Uso (Python do KiCad):  python tools/render_placa.py [--yd]
"""

import subprocess
import sys
import tempfile
from pathlib import Path

import pcbnew

REPO = Path(__file__).resolve().parent.parent
PROJECT = "retrosc-pong-yd" if "--yd" in sys.argv else "retrosc-pong"
PCB = REPO / "kicad" / f"{PROJECT}.kicad_pcb"
IMG = REPO / "docs" / "images"
CLI = Path(sys.executable).parent / "kicad-cli.exe"

VISTAS = [
    ("pcb_top.png", False, ["--side", "top", "--width", "1400",
                            "--height", "1160"]),
    ("pcb_silk3d.png", True, ["--side", "top", "--width", "1500",
                              "--height", "1240"]),
    ("pcb_iso.png", True, ["--width", "1500", "--height", "1150",
                           "--perspective", "--rotate", "-28,0,18",
                           "--zoom", "0.82", "--floor"]),
]


def placa_nua(destino: Path) -> None:
    """Copia da placa com os modelos 3D removidos."""
    b = pcbnew.LoadBoard(str(PCB))
    for fp in b.GetFootprints():
        fp.Models().clear()
    pcbnew.SaveBoard(str(destino), b)


def main():
    sufixo = "-yd" if "--yd" in sys.argv else ""
    with tempfile.TemporaryDirectory() as tmp:
        nua = Path(tmp) / "nua.kicad_pcb"
        placa_nua(nua)
        for nome, sem_comp, args in VISTAS:
            saida = IMG / (nome.replace(".png", f"{sufixo}.png"))
            fonte = nua if sem_comp else PCB
            r = subprocess.run(
                [str(CLI), "pcb", "render", "--quality", "high",
                 "--background", "opaque", "-o", str(saida), *args,
                 str(fonte)],
                capture_output=True, text=True)
            if r.returncode:
                print(f"FALHOU {nome}: {r.stderr.strip()[:120]}")
                continue
            # PNG de render vem pesado: 256 cores corta ~3x sem perda visivel
            try:
                from PIL import Image
                Image.open(saida).convert("RGB").quantize(
                    colors=256).save(saida, optimize=True)
            except ImportError:
                pass
            print(f"{saida.relative_to(REPO)}  "
                  f"({saida.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
