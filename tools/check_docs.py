#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Confere a documentacao contra o projeto de verdade (esquematico e arquivos).

Os diagramas de docs/images/*.svg sao desenhados A MAO, enquanto o
esquematico KiCad e GERADO por script: os dois podem divergir sem ninguem
notar. Este script pega essa classe de erro.

Verifica:
  1. links, ancoras e imagens dos .md resolvem;
  2. todo componente do esquematico aparece na tabela do BOM;
  3. toda referencia citada nos docs/SVGs existe no projeto (pega typos).

Uso:  python tools/check_docs.py        (sai != 0 se achar problema)
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOCS = ["README.md", "docs/bom.md", "docs/pecas-do-gabinete.md",
        "docs/pinout.md", "docs/schematic.md", "kicad/README.md"]
PREFIXOS = ("RV", "SW", "R", "C", "J", "U", "H")
# modelos de peca citados em prosa que "parecem" designador
IGNORAR = {"RV16", "CR22E", "FCP22E", "SW1x"}


def slug_github(h: str) -> str:
    """github-slugger: minusculas, sem pontuacao, CADA espaco vira hifen."""
    s = re.sub(r"[^\w\s-]", "", h.lower().strip(), flags=re.U)
    return s.replace(" ", "-")


def refs_do_projeto() -> set[str]:
    """Referencias validas: esquematico (inclui pecas de painel) + furos."""
    sch = (REPO / "kicad" / "retrosc-pong.kicad_sch").read_text(encoding="utf-8")
    refs = set(re.findall(r'\(property "Reference" "([A-Z]+\d+)"', sch))
    pcb = (REPO / "kicad" / "retrosc-pong.kicad_pcb").read_text(encoding="utf-8")
    refs |= set(re.findall(r'"reference" "(H\d+)"', pcb))
    refs |= set(re.findall(r'\(property "Reference" "(H\d+)"', pcb))
    return refs


def checa_links() -> list[str]:
    erros = []
    for d in DOCS:
        base = (REPO / d).parent
        txt = (REPO / d).read_text(encoding="utf-8")
        for m in re.finditer(r"!?\[[^\]]*\]\(([^)\s]+)\)", txt):
            alvo = m.group(1)
            if alvo.startswith(("http", "#", "mailto:")):
                continue
            arq, _, anc = alvo.partition("#")
            if not arq:
                continue
            cam = (base / arq).resolve()
            ln = txt[:m.start()].count("\n") + 1
            if not cam.exists():
                erros.append(f"{d}:{ln} arquivo inexistente: {alvo}")
            elif anc and cam.suffix == ".md":
                heads = re.findall(r"^#+\s+(.+)$",
                                   cam.read_text(encoding="utf-8"), re.M)
                if anc not in [slug_github(h) for h in heads]:
                    erros.append(f"{d}:{ln} ancora invalida: {alvo}")
    return erros


def checa_bom(validas: set[str]) -> list[str]:
    """Todo componente com footprint (= vai na placa) deve estar no BOM."""
    net = (REPO / "kicad" / "retrosc-pong.net").read_text(encoding="utf-8")
    bloco = net[net.find("(components"):net.find("(libparts")]
    na_placa = set(re.findall(r'\(ref "([A-Z]+\d+)"\)', bloco))

    bom = (REPO / "docs" / "bom.md").read_text(encoding="utf-8")
    ini = bom.index("## 1. Componentes da placa")
    tabela = bom[ini:bom.index("## 2.", ini)]
    citadas = set()
    for linha in tabela.splitlines():
        if linha.startswith("|"):               # so a 1a coluna (Ref)
            citadas |= set(re.findall(r"[A-Z]+\d+", linha.split("|")[1]))

    erros = [f"bom.md: componente do esquematico ausente: {r}"
             for r in sorted(na_placa - citadas)]
    erros += [f"bom.md: referencia que nao existe no projeto: {r}"
              for r in sorted(citadas - validas)]
    return erros


def checa_refs(validas: set[str]) -> list[str]:
    """Referencias citadas em .md e .svg tem que existir no projeto."""
    erros = []
    alvos = [REPO / d for d in DOCS] + sorted((REPO / "docs" / "images").glob("*.svg"))
    padrao = re.compile(r"\b(" + "|".join(PREFIXOS) + r")(\d+)\b")
    for f in alvos:
        txt = f.read_text(encoding="utf-8")
        if f.suffix == ".svg":                      # so o texto visivel
            txt = " ".join(re.findall(r">([^<]+)<", txt))
        for m in padrao.finditer(txt):
            ref = m.group(0)
            if ref not in validas and ref not in IGNORAR:
                ln = txt[:m.start()].count("\n") + 1
                erros.append(f"{f.relative_to(REPO)}:{ln} referencia "
                             f"desconhecida: {ref}")
    return erros


if __name__ == "__main__":
    validas = refs_do_projeto()
    print(f"referencias validas no projeto: {len(validas)}")
    problemas = checa_links() + checa_bom(validas) + checa_refs(validas)
    if problemas:
        print("\n".join("  " + p for p in problemas))
        sys.exit(f"\n{len(problemas)} problema(s) encontrado(s)")
    print("OK: links, BOM e referencias conferem")
