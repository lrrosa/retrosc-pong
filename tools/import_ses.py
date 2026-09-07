#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Finaliza o roteamento: importa a sessao do Freerouting (.ses da ISCA) na
placa REAL (com zonas) e remove as trilhas de GND.

Por que remover as trilhas de GND: a isca nao tem zonas, entao o Freerouting
roteia GND como trilha. Nesta placa GND e um PLANO (zona nas 2 faces), logo
essas trilhas ficam redundantes e geram 'starved_thermal'. Removendo-as, o
plano cuida de todo o GND via thermal relief.

Nao chama ZONE_FILLER (crasha fora do processo do pcbnew). O preenchimento e
feito depois por: kicad-cli pcb drc --refill-zones --save-board ...

Uso (Python do KiCad):
  python tools/import_ses.py [--yd]
--yd opera na variante YD-RP2040 (retrosc-pong-yd.*).
"""

import sys
from pathlib import Path

import pcbnew

KICAD = Path(__file__).resolve().parent.parent / "kicad"
PROJECT = "retrosc-pong-yd" if "--yd" in sys.argv else "retrosc-pong"
PCB = KICAD / f"{PROJECT}.kicad_pcb"
SES = KICAD / f"{PROJECT}-decoy.ses"

if not SES.exists():
    raise SystemExit(f"ERRO: {SES.name} ausente (rode make_decoy + freerouting)")

board = pcbnew.LoadBoard(str(PCB))
try:
    ok = pcbnew.ImportSpecctraSES(board, str(SES))
except TypeError:
    ok = pcbnew.ImportSpecctraSES(str(SES))
if not ok:
    raise SystemExit("ERRO: ImportSpecctraSES falhou - placa intocada")

has_gnd_zone = any(board.GetArea(i).GetNetname().lstrip("/") == "GND"
                   for i in range(board.GetAreaCount()))

# ATENCAO: neste build (KiCad 10 SWIG) ImportSpecctraSES invalida o proxy do
# container de trilhas: GetTracks() funciona UMA vez logo apos o import e quebra
# na 2a chamada. Entao materializamos a lista de uma vez e operamos sobre ela.
tracks = list(board.GetTracks())
n = len(tracks)
removed = 0
if has_gnd_zone:
    for t in tracks:
        if t.GetNetname().lstrip("/") == "GND":
            board.Remove(t)
            removed += 1
    n -= removed

pcbnew.SaveBoard(str(PCB), board)
print(f"SES importado; {removed} trilhas de GND removidas (plano cuida do GND)")
print(f"placa salva com {n} segmentos de sinal. Agora rode o DRC com "
      f"--refill-zones para preencher o plano.")
