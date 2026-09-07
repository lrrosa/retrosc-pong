#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Cria a placa-ISCA para o Freerouting a partir da placa real e exporta o DSN.

Por que a isca (ver skill kicad-freerouting):
  - remove as zonas (pours): no DSN elas viram "planes" que o Freerouting
    trata como obstaculo -> ele vira quase mono-camada e abandona os nets
    longos. Sem zonas, ele roteia GND como trilha nas 2 camadas livremente.
  - recua o contorno 0.35 mm para dentro: o roteador chega ate a borda da
    isca; com a borda falsa recuada o cobre fica a >=0.5 mm da borda real.

As zonas voltam na placa REAL, ao importar o SES.

Uso (Python do KiCad):
  python tools/make_decoy.py [--yd]
--yd opera na variante YD-RP2040 (retrosc-pong-yd.*).
Gera: kicad/<projeto>-decoy.kicad_pcb (+ .kicad_pro) e .dsn
"""

import shutil
import sys
from pathlib import Path

import pcbnew

KICAD = Path(__file__).resolve().parent.parent / "kicad"
PROJECT = "retrosc-pong-yd" if "--yd" in sys.argv else "retrosc-pong"
REAL = KICAD / f"{PROJECT}.kicad_pcb"
DECOY = KICAD / f"{PROJECT}-decoy.kicad_pcb"
DSN = KICAD / f"{PROJECT}-decoy.dsn"
RECESS = 0.35
mm = pcbnew.FromMM

# .kicad_pro com o MESMO basename da isca (o exportador le as netclasses dele)
src_pro = REAL.with_suffix(".kicad_pro")
if src_pro.exists():
    shutil.copy(src_pro, DECOY.with_suffix(".kicad_pro"))

board = pcbnew.LoadBoard(str(REAL))

# 1. remove todas as zonas
# so os PREENCHIMENTOS (planos de GND) saem; as areas de exclusao (rule
# areas) FICAM, senao o roteador passaria trilha por baixo dos parafusos.
zones = [z for z in (board.GetArea(i) for i in range(board.GetAreaCount()))
         if not z.GetIsRuleArea()]
for z in zones:
    board.Remove(z)
print(f"zonas removidas: {len(zones)} (areas de exclusao preservadas)")

# 2. recua o contorno: acha o bbox das segmentos de Edge.Cuts e redesenha
edges = [d for d in board.GetDrawings()
         if d.GetClass() == "PCB_SHAPE" and d.GetLayer() == pcbnew.Edge_Cuts]
xs, ys = [], []
for e in edges:
    for p in (e.GetStart(), e.GetEnd()):
        xs.append(p.x)
        ys.append(p.y)
    board.Remove(e)
x0, x1 = min(xs) + mm(RECESS), max(xs) - mm(RECESS)
y0, y1 = min(ys) + mm(RECESS), max(ys) - mm(RECESS)
corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
for i in range(4):
    seg = pcbnew.PCB_SHAPE(board)
    seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
    seg.SetStart(pcbnew.VECTOR2I(*corners[i]))
    seg.SetEnd(pcbnew.VECTOR2I(*corners[(i + 1) % 4]))
    seg.SetLayer(pcbnew.Edge_Cuts)
    seg.SetWidth(mm(0.1))
    board.Add(seg)
print(f"contorno recuado {RECESS} mm ({len(edges)} segmentos)")

pcbnew.SaveBoard(str(DECOY), board)
ok = pcbnew.ExportSpecctraDSN(board, str(DSN))
print(f"isca: {DECOY.name}")
print(f"DSN:  {DSN.name} -> {'OK' if ok else 'FALHOU'}")
