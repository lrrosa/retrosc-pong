#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Desenha a placa DENTRO da caixa Patola PB-085/3, em escala, para conferir
encaixe: folgas, coincidencia dos furos com os bossos da tampa e por onde
cada conector atravessa a parede.

Le a geometria da PRÓPRIA placa (kicad/retrosc-pong.kicad_pcb), entao o
desenho acompanha qualquer mudanca de layout.

Uso (Python do KiCad):  python tools/fit_caixa.py
Saida: docs/images/caixa-encaixe.svg
"""

from pathlib import Path

import pcbnew

REPO = Path(__file__).resolve().parent.parent
PCB = REPO / "kicad" / "retrosc-pong.kicad_pcb"
OUT = REPO / "docs" / "images" / "caixa-encaixe.svg"

# ---- Patola PB-085/3, dos desenhos do fabricante (mm) ----
CX_EXT_W, CX_EXT_H = 85.0, 73.0     # corpo externo (montada: 85 x 73 x 32)
CX_INT_W, CX_INT_H = 82.0, 70.0     # interno util
BOSS_SPAN = 58.0                    # entre centros dos 2 bossos da tampa
BOSS_PILOT = 2.5                    # furo-guia do bosso

S = 6.0                             # px por mm
MARG = 22.0                         # margem do desenho, em mm


def mm(v):
    return v * S


def _protrusoes(partes, P):
    """Quanto cada peca passa da face EXTERNA (ou quanto falta para chegar)."""
    par = (CX_EXT_W - CX_INT_W) / 2          # espessura da parede (~1,5 mm)
    fora_w, fora_n = CX_INT_W + par, -par    # faces externas direita / cima
    linhas = []
    for ref, x0, y0, x1, y1 in partes:
        ax0, ay0 = P(x0, y0)
        ax1, ay1 = P(x1, y1)
        if ax1 > CX_INT_W:
            linhas.append(f"{ref}: passa {ax1 - fora_w:.1f} mm da face externa "
                          f"(direita) — furo de parede")
        if ay0 < 0:
            linhas.append(f"{ref}: passa {fora_n - ay0:.1f} mm da face externa "
                          f"(cima) — furo de parede")
        if ref == "U1":                       # USB nao protrai: fica recuado
            linhas.append(f"U1 (USB): a face fica {ay0:.1f} mm PARA DENTRO da "
                          f"parede — o recorte tem que deixar o plugue entrar "
                          f"({ay0 + par:.1f} mm da face externa)")
    return linhas


def main():
    b = pcbnew.LoadBoard(str(PCB))
    bb = b.GetBoardEdgesBoundingBox()
    ox, oy = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY())
    bw = round(pcbnew.ToMM(bb.GetWidth()), 1)
    bh = round(pcbnew.ToMM(bb.GetHeight()), 1)

    # placa centrada no interno da caixa
    px = (CX_INT_W - bw) / 2
    py = (CX_INT_H - bh) / 2
    folga_x, folga_y = px, py

    def P(x, y):                     # placa -> coords do desenho (mm)
        return (px + x, py + y)

    def caixa_do(fp, layer):
        xs, ys = [], []
        for it in fp.GraphicalItems():
            if it.GetLayer() != layer or not hasattr(it, "GetStart"):
                continue                        # PCB_TEXT nao tem geometria
            for q in (it.GetStart(), it.GetEnd()):
                xs.append(pcbnew.ToMM(q.x) - ox)
                ys.append(pcbnew.ToMM(q.y) - oy)
        return (min(xs), min(ys), max(xs), max(ys)) if xs else None

    partes, furos = [], []
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        # o courtyard e a extensao FISICA (inclui barril do RCA e eixo do pot);
        # se vier degenerado (o do Pico vem), cai para a serigrafia.
        cx_ = caixa_do(fp, pcbnew.F_CrtYd)
        if cx_ is None or (cx_[2] - cx_[0]) < 1.0 or (cx_[3] - cx_[1]) < 1.0:
            cx_ = caixa_do(fp, pcbnew.F_SilkS)
        if cx_ is None:
            continue
        xs = [cx_[0], cx_[2]]
        ys = [cx_[1], cx_[3]]
        if ref.startswith("H"):
            p = fp.GetPosition()
            furos.append((ref, pcbnew.ToMM(p.x) - ox, pcbnew.ToMM(p.y) - oy))
        else:
            partes.append((ref, min(xs), min(ys), max(xs), max(ys)))

    W = CX_EXT_W + 2 * MARG
    H = CX_EXT_H + 2 * MARG
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {mm(W):.0f} {mm(H):.0f}"'
         f' font-family="ui-monospace, Menlo, Consolas, monospace" font-size="9">',
         f'<rect width="{mm(W):.0f}" height="{mm(H):.0f}" fill="#ffffff"/>',
         f'<g transform="translate({mm(MARG):.1f},{mm(MARG):.1f}) scale({S})"'
         f' stroke-width="0.25">']
    # deslocamento do corpo externo em relacao ao interno
    dx = (CX_INT_W - CX_EXT_W) / 2
    dy = (CX_INT_H - CX_EXT_H) / 2
    o.append(f'<rect x="{dx}" y="{dy}" width="{CX_EXT_W}" height="{CX_EXT_H}" rx="2"'
             f' fill="#f6f8fa" stroke="#8b949e" stroke-dasharray="1.5 1"/>')
    o.append(f'<rect x="0" y="0" width="{CX_INT_W}" height="{CX_INT_H}" rx="1.5"'
             f' fill="#ffffff" stroke="#24292f" stroke-width="0.4"/>')
    # placa
    o.append(f'<rect x="{px}" y="{py}" width="{bw}" height="{bh}"'
             f' fill="#dff0d8" stroke="#2f6f2f" stroke-width="0.4"/>')
    # componentes
    for ref, x0, y0, x1, y1 in partes:
        ax, ay = P(x0, y0)
        o.append(f'<rect x="{ax:.2f}" y="{ay:.2f}" width="{x1-x0:.2f}"'
                 f' height="{y1-y0:.2f}" fill="#ffffff" fill-opacity="0.65"'
                 f' stroke="#2563eb" stroke-width="0.2"/>')
        if (x1 - x0) > 6 and (y1 - y0) > 3:
            o.append(f'<text x="{ax + (x1-x0)/2:.2f}" y="{ay + (y1-y0)/2 + 0.6:.2f}"'
                     f' text-anchor="middle" font-size="2" fill="#2563eb">{ref}</text>')
    # bossos da tampa x furos da placa
    for i, sy in enumerate((CX_INT_H / 2 - BOSS_SPAN / 2,
                            CX_INT_H / 2 + BOSS_SPAN / 2)):
        o.append(f'<circle cx="{CX_INT_W/2}" cy="{sy}" r="2.5" fill="none"'
                 f' stroke="#d1242f" stroke-width="0.3"/>')
        o.append(f'<circle cx="{CX_INT_W/2}" cy="{sy}" r="{BOSS_PILOT/2}"'
                 f' fill="#d1242f"/>')
    for ref, hx, hy in furos:
        ax, ay = P(hx, hy)
        o.append(f'<circle cx="{ax:.2f}" cy="{ay:.2f}" r="1.6" fill="none"'
                 f' stroke="#2f6f2f" stroke-width="0.35"/>')
    # onde cada peca atravessa a parede -> recorte necessario
    global RECORTES
    RECORTES = []
    for ref, x0, y0, x1, y1 in partes:
        ax0, ay0 = P(x0, y0)
        ax1, ay1 = P(x1, y1)
        for cond, seg, lado in (
                (ax1 > CX_INT_W, (CX_INT_W, ay0, CX_INT_W, ay1), "direita"),
                (ax0 < 0, (0, ay0, 0, ay1), "esquerda"),
                (ay0 < 0, (max(ax0, 0), 0, min(ax1, CX_INT_W), 0), "cima"),
                (ay1 > CX_INT_H, (max(ax0, 0), CX_INT_H,
                                  min(ax1, CX_INT_W), CX_INT_H), "baixo")):
            if cond:
                o.append(f'<line x1="{seg[0]:.2f}" y1="{seg[1]:.2f}"'
                         f' x2="{seg[2]:.2f}" y2="{seg[3]:.2f}"'
                         f' stroke="#d1242f" stroke-width="1.2"'
                         f' stroke-linecap="round" opacity="0.75"/>')
                RECORTES.append((ref, lado))

    # cotas das folgas
    o.append(f'<text x="{px/2:.1f}" y="{CX_INT_H/2:.1f}" text-anchor="middle"'
             f' font-size="2.2" fill="#d1242f">{folga_x:.1f}</text>')
    o.append(f'<text x="{CX_INT_W - px/2:.1f}" y="{CX_INT_H/2:.1f}"'
             f' text-anchor="middle" font-size="2.2" fill="#d1242f">{folga_x:.1f}</text>')
    o.append(f'<text x="{CX_INT_W/2:.1f}" y="{py/2 + 0.8:.1f}" text-anchor="middle"'
             f' font-size="2.2" fill="#d1242f">{folga_y:.1f}</text>')
    o.append(f'<text x="{CX_INT_W/2:.1f}" y="{CX_INT_H - py/2 + 0.8:.1f}"'
             f' text-anchor="middle" font-size="2.2" fill="#d1242f">{folga_y:.1f}</text>')
    o.append("</g>")
    # legenda
    y = mm(MARG) + mm(CX_EXT_H) + mm(dy) + 26
    linhas = [
        ("#24292f", f"Caixa Patola PB-085/3 — interno {CX_INT_W:.0f} x {CX_INT_H:.0f} mm "
                    f"(externo {CX_EXT_W:.0f} x {CX_EXT_H:.0f} x 32)"),
        ("#2f6f2f", f"Placa {bw:.0f} x {bh:.0f} mm — folga {folga_x:.1f} mm nas laterais, "
                    f"{folga_y:.1f} mm em cima/embaixo"),
        ("#d1242f", f"Bossos da tampa (vermelho) a {BOSS_SPAN:.0f} mm: coincidem com os "
                    f"furos da placa (verde)"),
        ("#8b949e", "Tracejado = corpo externo (parede ~1,5 mm). "
                    "Vermelho grosso = recorte necessario na parede."),
    ] + [("#d1242f", l) for l in _protrusoes(partes, P)]
    for cor, txt in linhas:
        o.append(f'<text x="{mm(MARG):.0f}" y="{y:.0f}" fill="{cor}">{txt}</text>')
        y += 13
    o.append("</svg>")
    OUT.write_text("\n".join(o), encoding="utf-8")
    print(f"placa {bw} x {bh} | folgas {folga_x:.1f} / {folga_y:.1f} mm")
    for ref, hx, hy in sorted(furos):
        alvo = CX_INT_H / 2 + (BOSS_SPAN / 2 if hy > bh / 2 else -BOSS_SPAN / 2)
        print(f"  {ref}: furo em ({px+hx:.1f}, {py+hy:.1f}) | bosso em "
              f"({CX_INT_W/2:.1f}, {alvo:.1f})  ->  "
              f"{'COINCIDE' if abs(px+hx - CX_INT_W/2) < 0.05 and abs(py+hy-alvo) < 0.05 else 'DESALINHADO'}")
    if RECORTES:
        print("recortes necessarios na caixa:")
        for ref, lado in RECORTES:
            print(f"  {ref}: parede da {lado}")
    print("OK:", OUT)


if __name__ == "__main__":
    main()
