#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Gera kicad/retrosc-pong.kicad_pcb a partir da netlist do esquematico.

Requer o Python DO KICAD (modulo pcbnew):
  1. kicad-cli sch export netlist -o kicad/retrosc-pong.net kicad/retrosc-pong.kicad_sch
  2. "C:/Program Files/KiCad/10.0/bin/python.exe" tools/gen_kicad_pcb.py

Placement (placa 80 x 66, para a caixa Patola PB-085/3; topo = y0):
  - Pico na coluna esquerda, USB colado na borda de cima;
  - PAM8403 ao lado do Pico, corpo na placa, pot saindo pela borda de cima;
  - RCAs na borda direita (barril para fora);
  - cadeia de video/audio na faixa central entre o Pico e os RCAs;
  - headers de painel (chave A/B, pots, START) empilhados abaixo do Pico;
  - zonas de GND nas duas faces (o roteamento fica para o freerouting).
"""

import re
import sys
from pathlib import Path

import pcbnew

REPO = Path(__file__).resolve().parent.parent
# --yd: variante para o modulo YD-RP2040 (mesma furacao, pinagem propria)
VARIANT_YD = "--yd" in sys.argv
PROJECT = "retrosc-pong-yd" if VARIANT_YD else "retrosc-pong"
NETLIST = REPO / "kicad" / f"{PROJECT}.net"
OUT = REPO / "kicad" / f"{PROJECT}.kicad_pcb"
FP_LOCAL = REPO / "kicad" / "retrosc-pong.pretty"
FP_SYS = Path(sys.executable).parent.parent / "share" / "kicad" / "footprints"

BOARD_W, BOARD_H = 80.0, 66.0             # cabe na caixa Patola PB-085/3
KEEPOUT_R = 3.6                           # raio sem trilha/via nos furos
BX, BY = 20.0, 20.0                       # canto da placa na folha

mm = pcbnew.FromMM


def V(x, y):
    """Coordenada de placa (mm, relativa ao canto) -> VECTOR2I."""
    return pcbnew.VECTOR2I(mm(BX + x), mm(BY + y))


# ---------------------------------------------------------------- netlist
def parse_sexpr(text):
    tokens = re.findall(r'"(?:[^"\\]|\\.)*"|\(|\)|[^\s()]+', text)
    it = iter(tokens)

    def build():
        out = []
        for t in it:
            if t == "(":
                out.append(build())
            elif t == ")":
                return out
            else:
                out.append(t[1:-1] if t.startswith('"') else t)
        return out

    assert next(it) == "("
    return build()


def find_all(tree, key):
    for node in tree:
        if isinstance(node, list) and node and node[0] == key:
            yield node


def load_netlist():
    tree = parse_sexpr(NETLIST.read_text(encoding="utf-8"))
    comps = {}
    for section in find_all(tree, "components"):
        for c in find_all(section, "comp"):
            ref = fp = val = ""
            for f in c:
                if isinstance(f, list):
                    if f[0] == "ref":
                        ref = f[1]
                    elif f[0] == "footprint" and len(f) > 1:
                        fp = f[1]
                    elif f[0] == "value" and len(f) > 1:
                        val = f[1]
            comps[ref] = (fp, val)
    nets = {}
    for section in find_all(tree, "nets"):
        for n in find_all(section, "net"):
            name = ""
            nodes = []
            for f in n:
                if isinstance(f, list):
                    if f[0] == "name":
                        name = f[1]
                    elif f[0] == "node":
                        d = {g[0]: g[1] for g in f if isinstance(g, list)}
                        nodes.append((d.get("ref"), d.get("pin")))
            name = name.lstrip("/")         # netlist prefixa a folha raiz
            if name and not name.startswith("unconnected-"):
                nets[name] = nodes
    return comps, nets


# ---------------------------------------------------------------- board
def load_fp(fpid):
    lib, name = fpid.split(":", 1)
    for root in (FP_SYS, FP_LOCAL.parent):
        d = root / f"{lib}.pretty"
        if d.exists():
            fp = pcbnew.FootprintLoad(str(d), name)
            if fp:
                return fp
    raise SystemExit(f"ERRO: footprint nao encontrado: {fpid}")


def main():
    comps, nets = load_netlist()
    board = pcbnew.CreateEmptyBoard() if hasattr(pcbnew, "CreateEmptyBoard") \
        else pcbnew.BOARD()

    # ----- placement: ref -> (pad_ancora, x, y, rot) -----
    # pad_ancora = pad que cai exatamente em (x, y). rot em graus.
    #
    # Floorplan (placa 80 x 66 = interno da Patola PB-085/3; topo = y0):
    #   Pico (rot 0)   -> coluna ESQUERDA, USB colado na borda de cima;
    #                     pino 1 = (3.1, 2.0); coluna direita (21..40 =
    #                     GP16..VBUS) em x20.9 e a favor da faixa central.
    #   PAM8403        -> AO LADO do Pico na borda de cima: corpo na placa
    #                     (rot 0, pads y22), pot protrai pela borda superior.
    #   RCAs (A/V)     -> borda DIREITA, rot 270; sinal a 7.5 mm da borda
    #                     para o barril protrair ~8.5 mm para FORA da placa.
    #   coluna central -> x35: 1 coluna (DAC video, filtros, audio).
    #   headers        -> empilhados abaixo do Pico (rot 90: pinos +X).
    # Pico: corpo x1.5..22.5 / y0.6..51.7, keepout de cobre na base
    # (x~5..19 / y~45..52). Resistores VERTICAIS (~2.5 mm).
    PLACE = {
        # ---- borda de cima: USB do Pico (esq.) e pot do amp (dir.) ----
        "U1": ("1", 3.1, 2.0, 0),           # x1.5..22.5 / y0.6..51.7
        "U2": ("1", 47.0, 22.0, 0),         # corpo x44.9..74.5 / y0.2..20.2
        # ---- RCAs na borda direita, abaixo do amp (rot 270: barril +X) ----
        "J1": ("1", 72.5, 33.0, 270),       # video
        "J8": ("1", 72.5, 46.0, 270),       # audio L
        "J9": ("1", 72.5, 59.0, 270),       # audio R
        # ---- passivos no miolo, em 2 colunas ----
        "C3": ("1", 26.0, 10.0, 0),         # filtro ADC P1
        "C4": ("1", 26.0, 16.0, 0),         # filtro ADC P2
        "R1": ("1", 26.0, 22.0, 0),         # SYNC -> COMPOSITE
        "R2": ("1", 26.0, 28.0, 0),         # VIDEO -> COMPOSITE
        "R3": ("1", 26.0, 34.0, 0),         # AUDIO_PWM -> AUD_F
        "C1": ("1", 26.0, 40.0, 0),         # AUD_F -> GND (shunt)
        "C2": ("1", 36.0, 11.0, 0),         # AUD_F -> AUD_C (eletrolitico)
        "R4": ("1", 36.0, 18.0, 0),         # AUD_C -> LINHA
        "R5": ("1", 36.0, 24.0, 0),         # LINHA -> GND (shunt)
        "R6": ("1", 36.0, 30.0, 0),         # AUD_C -> AMP_L
        "R7": ("1", 36.0, 36.0, 0),         # AUD_C -> AMP_R
        # ---- headers de painel na borda de baixo, em UMA fileira ----
        # (2 fileiras nao cabem: cada uma precisa de ~9 mm com os rotulos)
        # J5+J6 sao um bloco (10,16 mm entre eles = 1 vaga -> femea 1x7 unica)
        # e nao cabiam a esquerda junto do J4 sem ficarem colados: o START,
        # que e independente, foi para a esquerda e o par para a direita.
        "J4": ("1", 2.0, 60.0, 90),         # chave de audio A/B (6 vias)
        "J7": ("1", 26.0, 60.0, 90),        # START
        "J5": ("1", 47.0, 60.0, 90),        # pot P1  (direita do furo H2)
        "J6": ("1", 57.16, 60.0, 90),       # pot P2
    }
    # rotulos gerais (conectores de 2 pinos e o amp): 1 texto perto de cada
    # RCA: centro=sinal, corpo=GND (convencao universal) -> so o nome da funcao
    # (o corpo do U2 ja leva "PAM8403" desenhado no proprio footprint)
    # VERTICAIS (rot 90), encostados na lateral de cada jack: o texto
    # acompanha a altura do conector e nao invade a faixa dos headers.
    SILK = {
        "J1": ("VIDEO (J1)", 67.5, 33.0, 90),
        "J8": ("AUDIO L (J8)", 67.5, 46.0, 90),
        "J9": ("AUDIO R (J9)", 67.5, 59.0, 90),
    }
    # NOME de cada header (o que o conector faz), abaixo dos pads
    # (o texto e CENTRADO na coordenada -> x = meio do header)
    HDR_NAME = {
        "J4": ("CHAVE AUDIO A/B (J4)", 8.4, 64.5),
        "J7": ("START (J7)", 27.3, 64.5),
        "J5": ("POT P1 (J5)", 49.5, 64.5),
        "J6": ("POT P2 (J6)", 59.7, 64.5),
    }
    # rotulo de FUNCAO por pino nos headers de painel (vertical, acima do pad)
    PIN_SILK = {
        "J4": ["RCA_C", "RCA_S", "LINHA", "GND", "LOUT+", "LOUT-"],  # chave A/B
        "J5": ["3V3", "P1", "AGND"],                                 # pot P1
        "J6": ["3V3", "P2", "AGND"],                                 # pot P2
        "J7": ["START", "GND"],                                      # botao
    }

    footprints = {}
    for ref, (fpid, val) in sorted(comps.items()):
        if not fpid:
            continue                        # pecas de painel (RV/SW)
        if ref not in PLACE:
            raise SystemExit(f"ERRO: sem posicao para {ref} ({fpid})")
        fp = load_fp(fpid)
        fp.SetReference(ref)
        fp.SetValue(val)
        board.Add(fp)
        pad_anchor, x, y, rot = PLACE[ref]
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(V(0, 0))
        pad = None
        for p in fp.Pads():
            if p.GetNumber() == pad_anchor:
                pad = p
                break
        if pad is None:
            raise SystemExit(f"ERRO: {ref} nao tem pad '{pad_anchor}'")
        fp.Move(V(x, y) - pad.GetPosition())
        footprints[ref] = fp

    # ----- debug de orientacao (para calibrar as rotacoes no fonte) -----
    def show(ref, nums):
        fp = footprints[ref]
        for num in nums:
            for p in fp.Pads():
                if p.GetNumber() == num:
                    pos = p.GetPosition()
                    print(f"{ref} pad {num}: ({pcbnew.ToMM(pos.x)-BX:.2f}, "
                          f"{pcbnew.ToMM(pos.y)-BY:.2f})", flush=True)
                    break

    show("U1", ("1", "40", "20", "21", "22", "24", "31", "32", "36", "38"))
    show("U2", ("1", "9"))
    show("J1", ("1", "2"))

    # ----- rotulos gerais de funcao na serigrafia -----
    for ref, (txt, tx, ty, rot) in SILK.items():
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(V(tx, ty))
        t.SetLayer(pcbnew.F_SilkS)
        if rot:
            t.SetTextAngle(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.9), mm(0.9)))
        t.SetTextThickness(mm(0.15))
        board.Add(t)

    # ----- NOME de cada header (funcao do conector) -----
    for ref, (txt, tx, ty) in HDR_NAME.items():
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(V(tx, ty))
        t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.9), mm(0.9)))
        t.SetTextThickness(mm(0.15))
        board.Add(t)

    # ----- marcacao GP17/GP18 no silk (difere entre Pico e YD-RP2040) -----
    # E o jeito rapido de notar qual variante de placa se tem em maos: no
    # Pico oficial GP17/GP18 ficam na coluna DIREITA (posicoes 22/24); no
    # YD-RP2040 ficam na BASE (GP17 = canto esq, pos 20; GP18 = canto dir,
    # pos 21). Quem montar confere o rotulo contra o silk do proprio modulo.
    # O rotulo sozinho, ao lado de uma fileira de 20 pinos iguais, nao diz
    # QUAL pino e: cada marca leva uma linha de chamada ate o pad. A linha
    # comeca FORA do contorno do modulo (senao cruza a silk dele).
    if VARIANT_YD:
        # na roxa os dois caem na fileira de baixo -> chamada para BAIXO
        GP_MARKS = [("GP17", "20", 0, +1), ("GP18", "21", 0, +1)]
    else:
        GP_MARKS = [("GP17", "22", +1, 0), ("GP18", "24", +1, 0)]
    # horizontal: sai por fora do contorno do modulo (x22.6);
    # vertical: curta, para nao invadir os rotulos dos headers (y~54.6)
    DIST = {(1, 0): (2.2, 3.4), (0, 1): (1.6, 2.4)}
    for txt, padnum, dx, dy in GP_MARKS:
        pad = next((p for p in footprints["U1"].Pads()
                    if p.GetNumber() == padnum), None)
        if pad is None:
            continue
        px = pcbnew.ToMM(pad.GetPosition().x) - BX
        py = pcbnew.ToMM(pad.GetPosition().y) - BY
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        D0, D1 = DIST[(abs(dx), abs(dy))]
        seg.SetStart(V(px + dx * D0, py + dy * D0))
        seg.SetEnd(V(px + dx * D1, py + dy * D1))
        seg.SetLayer(pcbnew.F_SilkS)
        seg.SetWidth(mm(0.12))
        board.Add(seg)
        meia = len(txt) * 0.32          # meia largura do texto (centrado)
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(V(px + dx * (D1 + 0.3 + meia),
                        py + dy * (D1 + 0.8)))
        t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        t.SetTextThickness(mm(0.15))
        board.Add(t)

    # ----- rotulo de funcao POR PINO nos headers de painel -----
    # texto vertical (90 graus) logo acima de cada pad, lido a partir da
    # posicao real do pad -> robusto a rotacao/direcao do footprint.
    for ref, names in PIN_SILK.items():
        fp = footprints[ref]
        for i, name in enumerate(names):
            pad = next((p for p in fp.Pads()
                        if p.GetNumber() == str(i + 1)), None)
            if pad is None:
                continue
            px = pcbnew.ToMM(pad.GetPosition().x) - BX
            py = pcbnew.ToMM(pad.GetPosition().y) - BY
            t = pcbnew.PCB_TEXT(board)
            t.SetText(name)
            t.SetPosition(V(px, py - 3.4))     # acima do pad (lado interno)
            t.SetLayer(pcbnew.F_SilkS)
            t.SetTextAngle(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T))
            t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
            t.SetTextThickness(mm(0.15))
            board.Add(t)

    # As referencias destes ja aparecem nos rotulos descritivos (ou sao
    # desnecessarias, no caso dos furos) e caiam sobre pads -> ocultar.
    for ref in ("J1", "J4", "J5", "J6", "J7", "J8", "J9"):
        fp = footprints.get(ref)
        if fp is not None:
            fp.Reference().SetVisible(False)
    # a ref do U2 cai em cima do pad 1: joga para a faixa livre abaixo
    if "U2" in footprints:
        footprints["U2"].Reference().SetPosition(V(52.0, 25.0))

    # ----- nets -----
    netinfo = {}
    for name in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni
    for name, nodes in nets.items():
        for ref, pin in nodes:
            fp = footprints.get(ref)
            if not fp:
                continue                    # peca de painel
            for p in fp.Pads():
                if p.GetNumber() == pin:
                    p.SetNet(netinfo[name])

    # ----- contorno -----
    pts = [(0, 0), (BOARD_W, 0), (BOARD_W, BOARD_H), (0, BOARD_H)]
    for i in range(4):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(V(*pts[i]))
        seg.SetEnd(V(*pts[(i + 1) % 4]))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(mm(0.1))
        board.Add(seg)

    # ----- furos de montagem: bossos da caixa Patola PB-085/3 -----
    # A tampa tem 2 bossos (furo-guia o2,5) a 58 mm entre centros, na linha
    # de centro do lado de 85 mm. A placa se prende neles: 2 furos, no meio
    # da largura, 58 mm entre si -> deixa livre o miolo das bordas de cima e
    # de baixo (x 36,5..43,5), onde nao pode haver componente.
    print("stage: furos", flush=True)
    for i, (hx, hy) in enumerate([(BOARD_W / 2, (BOARD_H - 58.0) / 2),
                                  (BOARD_W / 2, (BOARD_H + 58.0) / 2)]):
        # area de exclusao: nada de trilha/via sob a cabeca do parafuso
        # (o plano de GND pode entrar: o parafuso encosta em GND, sem risco)
        ka = pcbnew.ZONE(board)
        ka.SetIsRuleArea(True)
        ka.SetDoNotAllowTracks(True)
        ka.SetDoNotAllowVias(True)
        ka.SetDoNotAllowPads(False)   # o furo (NPTH) fica dentro dela
        ka.SetDoNotAllowZoneFills(False)
        lset = pcbnew.LSET()            # este build so aceita AddLayer
        lset.AddLayer(pcbnew.F_Cu)
        lset.AddLayer(pcbnew.B_Cu)
        ka.SetLayerSet(lset)
        out = ka.Outline()
        out.NewOutline()
        import math as _m
        for k in range(16):                 # circulo de raio KEEPOUT_R
            a = 2 * _m.pi * k / 16
            out.Append(mm(BX + hx + KEEPOUT_R * _m.cos(a)),
                       mm(BY + hy + KEEPOUT_R * _m.sin(a)))
        board.Add(ka)

        fp = load_fp("MountingHole:MountingHole_3.2mm_M3")
        fp.SetReference(f"H{i+1}")
        fp.Reference().SetVisible(False)    # so atrapalha (fica na borda)
        board.Add(fp)
        fp.SetPosition(V(hx, hy))

    # ----- regras (JLCPCB 2 camadas) -----
    bds = board.GetDesignSettings()
    try:
        bds.m_TrackMinWidth = mm(0.2)
        bds.m_ViasMinSize = mm(0.6)
        bds.m_MinClearance = mm(0.15)
        # 1 raio termico ja conecta o pad de GND (baixa corrente): evita
        # 'starved_thermal' onde trilhas vizinhas bloqueiam parte dos raios.
        bds.m_MinResolvedSpokes = 1
    except Exception as e:                  # API varia entre versoes
        print("aviso: regras minimas nao aplicadas:", e)
    try:
        ns = bds.GetNetSettings() if hasattr(bds, "GetNetSettings") \
            else bds.m_NetSettings
        dc = ns.GetDefaultNetclass()
        dc.SetClearance(mm(0.15))
        dc.SetTrackWidth(mm(0.3))
        dc.SetViaDiameter(mm(0.7))
        dc.SetViaDrill(mm(0.35))
        print("netclass Default: 0.3mm/0.15mm, via 0.7/0.35")
    except Exception as e:
        print("aviso: netclass nao aplicada:", e)

    # ----- zonas de GND nas duas faces -----
    print("stage: zonas", flush=True)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(netinfo["GND"])
        outline = z.Outline()
        outline.NewOutline()
        # 0,5 mm para dentro da borda (isolamento cobre-borda)
        for (zx, zy) in [(0.5, 0.5), (BOARD_W - 0.5, 0.5),
                         (BOARD_W - 0.5, BOARD_H - 0.5), (0.5, BOARD_H - 0.5)]:
            outline.Append(mm(BX + zx), mm(BY + zy))
        z.SetMinThickness(mm(0.25))
        # raios termicos finos e proximos: o plano preenche mais junto das
        # trilhas (menos ilhas isoladas) e ainda cabem raios em espacos apertados
        for m, v in (("SetThermalReliefGap", mm(0.3)),
                     ("SetThermalReliefSpokeWidth", mm(0.4)),
                     ("SetMinIslandArea", mm(0.2) * mm(0.2))):
            try:
                getattr(z, m)(v)
            except Exception as e:
                print(f"aviso: zona.{m}: {e}", flush=True)
        for setter, arg in (("SetLocalClearance", mm(0.2)),
                            ("SetPadConnection",
                             getattr(pcbnew, "ZONE_CONNECTION_THERMAL", None))):
            try:
                if arg is not None:
                    getattr(z, setter)(arg)
            except Exception as e:          # API varia entre versoes
                print(f"aviso: zona.{setter}: {e}", flush=True)
        board.Add(z)

    # ----- textos de silk -----
    print("stage: texto", flush=True)
    try:
        # assinatura na area vazia do meio da placa (x~32..56, y~40..54)
        titulo = "RetroSC Pong v1.0" + (" (roxo)" if VARIANT_YD else "")
        for txt, ty, size in ((titulo, 45.0, 1.6), ("LRRosa 2026", 49.5, 1.1)):
            t = pcbnew.PCB_TEXT(board)
            t.SetText(txt)
            t.SetPosition(V(44, ty))
            t.SetLayer(pcbnew.F_SilkS)
            t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
            t.SetTextThickness(mm(size / 6))
            board.Add(t)
    except Exception as e:
        print("aviso: texto de silk:", e, flush=True)

    # ----- logo RetroSC no silk da frente, DENTRO do espaco do Pico -----
    # O logo (docs/images/logo_retrosc_1bit.png, 220x69, branco sobre preto)
    # vira tinta de silk onde os pixels sao BRANCOS (preto/branco invertidos:
    # o traco e que e impresso). Ocupa a janela entre as duas fileiras de
    # pinos do Pico (x4.0..20.0 / y0.6..51.7, sem nenhum pad), rotacionado
    # 90 graus. A 0.22 mm/px (traco de 1 px = 0.22 >= 0.15 minimo de
    # fabrica) fica com 48.4 x 15.2 mm, centrado sob o modulo.
    print("stage: logo", flush=True)
    try:
        from PIL import Image
        img = Image.open(str(REPO / "docs" / "images" /
                             "logo_retrosc_1bit.png")).convert("1")
        LW, LH = img.size
        lpx = img.load()
        # 0.19 mm/px: o logo tem que caber ENTRE as duas linhas verticais
        # do footprint do Pico (x=4.5 e x=19.5), senao cruza as duas e gera
        # dezenas de avisos de silk sobreposto. Traco de 1 px = 0.19 mm,
        # ainda acima do minimo de fabrica (0.15).
        S = 0.19                            # mm por pixel do logo
        BLEED = 0.01                        # funde runs vizinhos na tinta
        LX, LY = 5.45, 7.0                  # centrado na faixa limpa do Pico
        # um PCB_SHAPE poly do arquivo so guarda 1 contorno -> um RECT
        # preenchido por run de pixels
        nruns = 0
        for j in range(LH):                 # linha da imagem -> X da placa
            i = 0
            while i < LW:                   # coluna da imagem -> Y da placa
                if lpx[i, j]:
                    i0 = i
                    while i < LW and lpx[i, j]:
                        i += 1
                    # frente (sem espelho): linhas j crescem para -X
                    xa = LX + (LH - 1 - j) * S - BLEED
                    xb = xa + S + 2 * BLEED
                    ya = LY + i0 * S - BLEED
                    yb = LY + i * S + BLEED
                    sh = pcbnew.PCB_SHAPE(board)
                    sh.SetShape(pcbnew.SHAPE_T_RECT)
                    sh.SetStart(V(xa, ya))
                    sh.SetEnd(V(xb, yb))
                    sh.SetFilled(True)
                    sh.SetWidth(0)
                    sh.SetLayer(pcbnew.F_SilkS)
                    board.Add(sh)
                    nruns += 1
                else:
                    i += 1
        print(f"logo: {nruns} runs em F.SilkS "
              f"({LH*S:.1f} x {LW*S:.1f} mm)", flush=True)
    except ImportError:
        print("aviso: PIL ausente no Python do KiCad - logo nao desenhado")
    except Exception as e:
        print("aviso: logo nao desenhado:", e, flush=True)

    # NAO chamar ZONE_FILLER aqui: fora do processo do pcbnew ele crasha
    # (access violation). As zonas ficam sem preencher; o kicad-cli pcb drc
    # e o proprio KiCad ("B") refazem o fill automaticamente.

    print("stage: save", flush=True)
    board.SetFileName(str(OUT))
    pcbnew.SaveBoard(str(OUT), board)
    print(f"OK: {OUT}", flush=True)

    # ----- DSN para o freerouting -----
    dsn = OUT.with_suffix(".dsn")
    ok = False
    try:
        ok = pcbnew.ExportSpecctraDSN(board, str(dsn))
    except TypeError:
        try:
            ok = pcbnew.ExportSpecctraDSN(str(dsn))
        except Exception as e:
            print("aviso: export DSN falhou:", e)
    print(f"DSN: {dsn} -> {'OK' if ok else 'FALHOU'}")


if __name__ == "__main__":
    main()
