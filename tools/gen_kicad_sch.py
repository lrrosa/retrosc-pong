#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""
Gera o esquematico KiCad do RetroSC Pong.

Le os simbolos direto das bibliotecas instaladas do KiCad, embute-os no
.kicad_sch (lib_symbols) e posiciona os componentes ligando tudo por labels.

Uso:
    python tools/gen_kicad_sch.py [caminho_do_kicad]

Gera em kicad/: retrosc-pong.kicad_sch e retrosc-pong.kicad_pro
Valide com:  kicad-cli sch export svg -o docs/images/kicad kicad/retrosc-pong.kicad_sch
"""

import math
import re
import sys
import uuid as _uuid
from pathlib import Path

# ---------------------------------------------------------------- libs
# --yd: variante para o modulo RP2040 "roxo" de 1 botao (USB-C, 16 MB;
# mesmas dimensoes e furacao, mas outra funcao em cada posicao de pino).
VARIANT_YD = "--yd" in sys.argv
_args = [a for a in sys.argv[1:] if a != "--yd"]
KICAD_ROOT = Path(_args[0] if _args else r"C:\Program Files\KiCad\10.0")
SYMDIR = KICAD_ROOT / "share" / "kicad" / "symbols"
FPDIR = KICAD_ROOT / "share" / "kicad" / "footprints"

OUT = Path(__file__).resolve().parent.parent / "kicad"
PROJECT = "retrosc-pong-yd" if VARIANT_YD else "retrosc-pong"
VERSION = "20250610"          # formato do KiCad 10

_sym_cache: dict[str, str] = {}


def _uid() -> str:
    return str(_uuid.uuid4())


def _balanced(text: str, start: int) -> str:
    """Devolve o s-expr balanceado que comeca em `start` (respeita strings)."""
    depth, i, in_str, esc = 0, start, False, False
    while i < len(text):
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    raise ValueError("s-expr nao balanceado")


def lib_symbol(lib_id: str) -> str:
    """Extrai '(symbol "Lib:Nome" ...)' pronto para o bloco lib_symbols."""
    if lib_id in _sym_cache:
        return _sym_cache[lib_id]
    lib, name = lib_id.split(":", 1)
    path = SYMDIR / f"{lib}.kicad_sym"
    if not path.exists():
        raise SystemExit(f"ERRO: biblioteca ausente: {path}")
    text = path.read_text(encoding="utf-8")
    m = re.search(r'^\t\(symbol "%s"' % re.escape(name), text, re.M)
    if not m:
        raise SystemExit(f"ERRO: simbolo '{name}' nao existe em {lib}")
    block = _balanced(text, m.start())
    # renomeia o simbolo raiz para "Lib:Nome"
    block = block.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)
    _sym_cache[lib_id] = block
    return block


def sym_pins(lib_id: str, unit: int = 1) -> dict[str, tuple[float, float]]:
    """{numero_do_pino: (x, y)} da unidade pedida, em coords da biblioteca.

    Simbolos multi-unidade (ex.: SW_DPDT_x2) tem sub-simbolos "<nome>_<unit>_<style>";
    a unidade 0 e comum a todas. Sem esse filtro, os pinos das duas unidades
    (que ficam nas MESMAS coordenadas) se misturam.
    """
    block = lib_symbol(lib_id)
    name = lib_id.split(":", 1)[1]
    pins: dict[str, tuple[float, float]] = {}
    for m in re.finditer(r'\(symbol "([^"]+)"', block):
        sub = m.group(1)
        mm = re.fullmatch(re.escape(name) + r"_(\d+)_(\d+)", sub)
        if not mm or int(mm.group(1)) not in (0, unit):
            continue
        sblock = _balanced(block, m.start())
        for pm in re.finditer(r"\(pin\s+\w+\s+\w+\s*\n", sblock):
            chunk = _balanced(sblock, pm.start())
            at = re.search(r"\(at\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)", chunk)
            num = re.search(r'\(number\s+"([^"]+)"', chunk)
            if at and num:
                pins[num.group(1)] = (float(at.group(1)), float(at.group(2)),
                                      float(at.group(3)))
    if not pins:
        raise SystemExit(f"ERRO: nenhum pino lido de {lib_id} (unit {unit})")
    return pins


def sym_pin_names(lib_id: str, unit: int = 1) -> dict[str, str]:
    """{numero_do_pino: nome} - usado para achar os GND do Pico."""
    block = lib_symbol(lib_id)
    name = lib_id.split(":", 1)[1]
    out: dict[str, str] = {}
    for m in re.finditer(r'\(symbol "([^"]+)"', block):
        mm = re.fullmatch(re.escape(name) + r"_(\d+)_(\d+)", m.group(1))
        if not mm or int(mm.group(1)) not in (0, unit):
            continue
        sblock = _balanced(block, m.start())
        for pm in re.finditer(r"\(pin\s+\w+\s+\w+\s*\n", sblock):
            chunk = _balanced(sblock, pm.start())
            num = re.search(r'\(number\s+"([^"]+)"', chunk)
            nm = re.search(r'\(name\s+"([^"]+)"', chunk)
            if num and nm:
                out[num.group(1)] = nm.group(1)
    return out


def check_footprint(fp: str) -> str:
    """Aborta se o footprint nao existir (evita esquematico que nao vira PCB).

    Procura nas libs do KiCad e tambem na lib propria do projeto
    (kicad/retrosc-pong.pretty), gerada por tools/gen_kicad_fp.py.
    """
    if not fp:
        return fp
    lib, name = fp.split(":", 1)
    local = Path(__file__).resolve().parent.parent / "kicad"
    for root in (FPDIR, local):
        if (root / f"{lib}.pretty" / f"{name}.kicad_mod").exists():
            return fp
    raise SystemExit(f"ERRO: footprint inexistente: {fp}")


# ------------------------------------------------------- geometria
class Pin(tuple):
    """(x, y) na folha + (dx, dy) = direcao do stub, saindo PARA FORA do corpo."""
    def __new__(cls, x, y, dx, dy):
        o = super().__new__(cls, (round(x, 2), round(y, 2)))
        o.dx, o.dy = dx, dy
        return o


def pin_place(px, py, place_angle, lx, ly, pin_angle):
    """Pino da lib -> folha. O Y da folha cresce para BAIXO (dai o -ry).

    pin_angle e a direcao em que o pino aponta PARA DENTRO do corpo; o stub
    tem que sair no sentido oposto (+180), senao dois pinos vizinhos de topo
    (ex.: VBUS e 3V3 do Pico) acabam com os stubs se tocando -> curto.
    """
    a = math.radians(place_angle)
    rx = lx * math.cos(a) - ly * math.sin(a)
    ry = lx * math.sin(a) + ly * math.cos(a)
    d = math.radians(pin_angle + 180 + place_angle)
    return Pin(px + rx, py - ry, math.cos(d), -math.sin(d))


def grid(v: float) -> float:
    """Alinha na grade de 1.27 mm (senao o KiCad nao conecta)."""
    return round(round(v / 1.27) * 1.27, 2)


# ------------------------------------------------------- emissao
class Sheet:
    def __init__(self, root_uuid: str):
        self.root = root_uuid
        self.items: list[str] = []
        self.used: list[str] = []       # lib_ids na ordem de uso

    def symbol(self, lib_id, ref, value, at, angle=0, footprint="",
               fields_hidden=(), dnp=False, on_board=True, mirror=None,
               unit=1):
        if lib_id not in self.used:
            self.used.append(lib_id)
        check_footprint(footprint)
        px, py = at
        pins = sym_pins(lib_id, unit)
        pin_uuids = "\n".join(
            f'\t\t(pin "{n}" (uuid "{_uid()}"))' for n in sorted(pins))
        mir = f"\n\t\t(mirror {mirror})" if mirror else ""
        props = [
            ("Reference", ref, px + 5.08, py - 2.54, False),
            ("Value", value, px + 5.08, py + 2.54, False),
            ("Footprint", footprint, px, py, True),
            ("Datasheet", "", px, py, True),
            ("Description", "", px, py, True),
        ]
        pblock = []
        for name, val, x, y, hide in props:
            h = "\n\t\t\t(hide yes)" if hide or name in fields_hidden else ""
            pblock.append(
                f'\t\t(property "{name}" "{val}"\n'
                f"\t\t\t(at {x} {y} 0){h}\n"
                f"\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n"
                f"\t\t\t\t)\n\t\t\t\t(justify left)\n\t\t\t)\n\t\t)")
        self.items.append(
            f"\t(symbol\n"
            f'\t\t(lib_id "{lib_id}")\n'
            f"\t\t(at {px} {py} {angle}){mir}\n"
            f"\t\t(unit {unit})\n"
            f"\t\t(exclude_from_sim no)\n"
            f"\t\t(in_bom yes)\n"
            f"\t\t(on_board {'yes' if on_board else 'no'})\n"
            f"\t\t(dnp {'yes' if dnp else 'no'})\n"
            f'\t\t(uuid "{_uid()}")\n'
            + "\n".join(pblock) + "\n"
            + pin_uuids + "\n"
            f"\t\t(instances\n"
            f'\t\t\t(project "{PROJECT}"\n'
            f'\t\t\t\t(path "/{self.root}"\n'
            f'\t\t\t\t\t(reference "{ref}")\n'
            f"\t\t\t\t\t(unit {unit})\n"
            f"\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)")
        return {n: pin_place(px, py, angle, *p) for n, p in pins.items()}

    def no_connect(self, at):
        self.items.append(
            f"\t(no_connect\n\t\t(at {at[0]} {at[1]})\n"
            f'\t\t(uuid "{_uid()}")\n\t)')

    def wire(self, a, b):
        self.items.append(
            f"\t(wire\n\t\t(pts\n\t\t\t(xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})\n\t\t)\n"
            f"\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n"
            f'\t\t(uuid "{_uid()}")\n\t)')

    def label(self, name, at, angle=0, justify="left"):
        self.items.append(
            f'\t(label "{name}"\n\t\t(at {at[0]} {at[1]} {angle})\n'
            f"\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n"
            f"\t\t\t(justify {justify} bottom)\n\t\t)\n"
            f'\t\t(uuid "{_uid()}")\n\t)')

    def text(self, s, at, size=2.0):
        self.items.append(
            f'\t(text "{s}"\n\t\t(exclude_from_sim no)\n\t\t(at {at[0]} {at[1]} 0)\n'
            f"\t\t(effects\n\t\t\t(font\n\t\t\t\t(size {size} {size})\n"
            f"\t\t\t\t(bold yes)\n\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n"
            f'\t\t(uuid "{_uid()}")\n\t)')

    def render(self, title, rev, date):
        libs = "\n".join(
            "\n".join("\t" + ln for ln in lib_symbol(l).splitlines())
            for l in self.used)
        return (
            f"(kicad_sch\n"
            f"\t(version {VERSION})\n"
            f'\t(generator "gen_kicad_sch.py")\n'
            f'\t(generator_version "10.0")\n'
            f'\t(uuid "{self.root}")\n'
            f'\t(paper "A3")\n'
            f"\t(title_block\n"
            f'\t\t(title "{title}")\n'
            f'\t\t(date "{date}")\n'
            f'\t\t(rev "{rev}")\n'
            f'\t\t(company "RetroSC")\n'
            f'\t\t(comment 1 "Hardware licenciado sob CERN-OHL-S-2.0 '
            f'(SPDX: CERN-OHL-S-2.0) - ver LICENSE-HARDWARE.txt")\n'
            f"\t)\n"
            f"\t(lib_symbols\n{libs}\n\t)\n"
            + "\n".join(self.items) + "\n"
            f"\t(sheet_instances\n"
            f'\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n'
            f"\t(embedded_fonts no)\n"
            f")\n")


# ------------------------------------------------------- footprints
FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical"
FP_C = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
FP_CP = "Capacitor_THT:CP_Radial_D5.0mm_P2.50mm"
FP_PICO = "Module:RaspberryPi_Pico_Common_THT"
FP_RCA = "retrosc-pong:RCA_Jack_THT_Panel"      # lib propria (gen_kicad_fp.py)
FP_AMP = "retrosc-pong:PAM8403_HW-012"


def fp_hdr(n: int) -> str:
    return f"Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical"


def U(n: float) -> float:
    """Coordenada em passos de 2.54 mm (mantem os pinos na grade de 1.27)."""
    return round(n * 2.54, 2)


def stub(sh, pins, num, _center_x=None, name="", length=U(2)):
    """Puxa um stub na direcao do pino e prende um label na ponta.

    A rotacao do label acompanha a direcao para o texto sair sempre para fora:
    0 = direita, 90 = cima, 180/'right' = esquerda, 270 = baixo.
    """
    p = pins[num]
    end = (round(p[0] + p.dx * length, 2), round(p[1] + p.dy * length, 2))
    sh.wire(p, end)
    if abs(p.dx) > abs(p.dy):                     # horizontal
        rot, just = 0, ("left" if p.dx > 0 else "right")
    else:                                          # vertical
        rot, just = (90 if p.dy < 0 else 270), "left"
    sh.label(name, end, rot, just)
    return end


def vstub(sh, pins, num, name, up=None, length=U(3)):
    """Compat: a direcao agora vem do proprio pino, 'up' e ignorado."""
    return stub(sh, pins, num, None, name, length)


def build():
    root = _uid()
    sh = Sheet(root)

    # ---------------------------------------------------------- Pico
    # O footprint e a furacao sao os MESMOS nas duas variantes; o que muda
    # e a funcao em cada posicao fisica (numeracao do Pico: 1-20 desce a
    # esquerda, 21-40 sobe a direita). Funcoes usadas e onde cada modulo
    # as expoe:
    #             funcao       Pico oficial   roxa 1 botao
    #   SYNC      GP16         21             19
    #   VIDEO     GP17         22             20
    #   AUDIO_PWM GP18         24             21
    #   START     GP22         29             25
    #   POT1      GP26/A0      31             30
    #   POT2      GP27/A1      32             31
    #   AGND                   33             29
    #   +3V3                   36             36
    #   +5V       VBUS         40             40
    #   GND (estrela video)    23             38
    px, py = U(22), U(46)
    if VARIANT_YD:
        # ATENCAO: o simbolo do Pico une pinos de mesmo nome (os 8 GND) --
        # ligar um GND arrastaria os outros 7, que no YD sao GPIOs! Por
        # isso a variante usa um conector generico 2x20 com a MESMA
        # numeracao fisica (1-20 desce a esq., 21-40 sobe a dir.) e o mesmo
        # footprint; cada pino e ligado individualmente.
        sh.text("RP2040 roxo 1 botao (clone do Pico - pinagem propria)",
                (U(8), U(18)), 2.5)
        pico = sh.symbol("Connector_Generic:Conn_02x20_Counter_Clockwise",
                         "U1", "RP2040 roxo 1 botao", (px, py), footprint=FP_PICO,
                         fields_hidden=("Value",))
        used = {"19": "SYNC", "20": "VIDEO", "21": "AUDIO_PWM",
                "25": "START", "30": "POT1", "31": "POT2", "29": "AGND",
                "36": "+3V3", "40": "+5V", "38": "GND"}
        gnd_extra = {"6", "15", "35"}       # demais GNDs reais da roxa
        names = sym_pin_names(
            "Connector_Generic:Conn_02x20_Counter_Clockwise")
    else:
        sh.text("Raspberry Pi Pico", (U(8), U(18)), 2.5)
        pico = sh.symbol("MCU_Module:RaspberryPi_Pico", "U1",
                         "RaspberryPi_Pico", (px, py), footprint=FP_PICO,
                         fields_hidden=("Value",))
        used = {"21": "SYNC", "22": "VIDEO", "24": "AUDIO_PWM",
                "29": "START", "31": "POT1", "32": "POT2", "33": "AGND",
                "36": "+3V3", "40": "+5V", "23": "GND"}
        gnd_extra = None                    # usa os nomes GND do simbolo
        names = sym_pin_names("MCU_Module:RaspberryPi_Pico")
    for num, net in used.items():
        stub(sh, pico, num, px, net)
    # Os demais GND do modulo tambem vao para a estrela; o resto leva
    # flag No-Connect (senao o ERC acusa 'pin not connected' em cada GPIO livre).
    for num, nm in names.items():
        if num in used:
            continue
        is_gnd = (num in gnd_extra) if gnd_extra is not None \
            else (nm.strip().upper() == "GND")
        if is_gnd:
            stub(sh, pico, num, px, "GND")
        else:
            sh.no_connect(pico[num])
    if VARIANT_YD:
        sh.text("U1 = modulo RP2040 roxo 1 botao (posicoes na numeracao do "
                "Pico: 1-20 desce a esq., 21-40 sobe a dir.)",
                (U(8), U(76)), 1.8)

    # ------------------------------------------------------- video DAC
    sh.text("Video composto - DAC de 2 resistores", (U(48), U(8)), 2.5)
    r1 = sh.symbol("Device:R", "R1", "470", (U(52), U(16)), footprint=FP_R)
    vstub(sh, r1, "1", "SYNC", up=True)
    vstub(sh, r1, "2", "COMPOSITE", up=False)

    r2 = sh.symbol("Device:R", "R2", "270", (U(62), U(16)), footprint=FP_R)
    vstub(sh, r2, "1", "VIDEO", up=True)
    vstub(sh, r2, "2", "COMPOSITE", up=False)

    j1 = sh.symbol("Connector_Generic:Conn_01x02", "J1", "RCA video (na placa)",
                   (U(76), U(16)), footprint=FP_RCA)
    stub(sh, j1, "1", U(76), "COMPOSITE")
    stub(sh, j1, "2", U(76), "GND")

    # ---------------------------------------------------------- audio
    sh.text("Audio - filtro RC, acoplamento e divisor de linha",
            (U(48), U(30)), 2.5)
    r3 = sh.symbol("Device:R", "R3", "1k", (U(52), U(36)), 90, footprint=FP_R)
    stub(sh, r3, "1", U(52), "AUDIO_PWM")
    stub(sh, r3, "2", U(52), "AUD_F")

    c1 = sh.symbol("Device:C", "C1", "100n", (U(60), U(41)), footprint=FP_C)
    vstub(sh, c1, "1", "AUD_F", up=True)
    vstub(sh, c1, "2", "GND", up=False)

    c2 = sh.symbol("Device:C_Polarized", "C2", "1u", (U(68), U(36)), 90,
                   footprint=FP_CP)
    stub(sh, c2, "1", U(68), "AUD_F")
    stub(sh, c2, "2", U(68), "AUD_C")

    # divisor de linha (posicao A da chave)
    r4 = sh.symbol("Device:R", "R4", "10k", (U(80), U(36)), 90, footprint=FP_R)
    stub(sh, r4, "1", U(80), "AUD_C")
    stub(sh, r4, "2", U(80), "LINHA")

    r5 = sh.symbol("Device:R", "R5", "1k", (U(88), U(41)), footprint=FP_R)
    vstub(sh, r5, "1", "LINHA", up=True)
    vstub(sh, r5, "2", "GND", up=False)

    # entradas do amp (mesma faixa do resto do audio, à direita)
    r6 = sh.symbol("Device:R", "R6", "1k", (U(102), U(36)), 90, footprint=FP_R)
    stub(sh, r6, "1", None, "AUD_C")
    stub(sh, r6, "2", None, "AMP_L")

    r7 = sh.symbol("Device:R", "R7", "1k", (U(102), U(42)), 90, footprint=FP_R)
    stub(sh, r7, "1", None, "AUD_C")
    stub(sh, r7, "2", None, "AMP_R")

    # Modulo PAM8403 (HW-012) soldado NA PLACA (em pe, como no prototipo).
    # Pinagem FISICA da fileira de 9 pads (esq -> dir no silk do modulo):
    #   1=ROUT-  2=ROUT+  3=LOUT+  4=LOUT-  5=PWR-  6=PWR+  7=IN_L  8=IN_G  9=IN_R
    amp = sh.symbol("Connector_Generic:Conn_01x09", "U2", "PAM8403 HW-012",
                    (U(122), U(40)), footprint=FP_AMP)
    for n, net in (("3", "LOUT_P"), ("4", "LOUT_N"), ("5", "GND"),
                   ("6", "+5V"), ("7", "AMP_L"), ("8", "GND"),
                   ("9", "AMP_R")):
        stub(sh, amp, n, None, net)
    sh.no_connect(amp["1"])   # ROUT-: canal R nao usado (audio mono)
    sh.no_connect(amp["2"])   # ROUT+

    # ----------------------------------------------------- potenciometros
    # Titulo bem acima: os labels +3V3/POT sobem dos pots e alcancam ~U(65).
    sh.text("Potenciometros (painel) e botao START", (U(48), U(58)), 2.5)
    for i, (ref, net, cref, cap) in enumerate(
            [("RV1", "POT1", "C3", "100n"), ("RV2", "POT2", "C4", "100n")]):
        # cada canal ocupa uma faixa larga: pot | cap de filtro | header
        x = U(52 + i * 34)
        rv = sh.symbol("Device:R_Potentiometer", ref, "10k", (x, U(70)),
                       on_board=False)
        vstub(sh, rv, "1", "+3V3", up=True)
        vstub(sh, rv, "3", "AGND", up=False)
        wx, wy = rv["2"]                       # wiper
        sh.wire((wx, wy), (wx + U(3), wy))
        sh.label(net, (wx + U(3), wy), 0, "left")

        c = sh.symbol("Device:C", cref, cap, (x + U(13), U(70)),
                      footprint=FP_C)
        vstub(sh, c, "1", net, up=True)
        vstub(sh, c, "2", "AGND", up=False)

        j = sh.symbol("Connector_Generic:Conn_01x03", f"J{5+i}",
                      f"pot P{i+1} (painel)", (x + U(24), U(70)),
                      footprint=fp_hdr(3))
        for n, nt in (("1", "+3V3"), ("2", net), ("3", "AGND")):
            stub(sh, j, n, x + U(24), nt)

    sw1 = sh.symbol("Switch:SW_Push", "SW1", "START", (U(124), U(70)),
                    on_board=False)
    stub(sh, sw1, "1", U(124), "START")
    stub(sh, sw1, "2", U(124), "GND")
    j7 = sh.symbol("Connector_Generic:Conn_01x02", "J7", "START (painel)",
                   (U(140), U(70)), footprint=fp_hdr(2))
    stub(sh, j7, "1", U(140), "START")
    stub(sh, j7, "2", U(140), "GND")

    # ------------------------------- chave A/B (painel) + RCAs (na placa)
    sh.text("Chave A/B (painel, via J4) e RCAs de audio (na placa)",
            (U(48), U(80)), 2.5)
    # A chave fica no painel: 6 fios ate a placa pelo header J4.
    j4 = sh.symbol("Connector_Generic:Conn_01x06", "J4",
                   "chave A/B (painel)", (U(30), U(91)), footprint=fp_hdr(6))
    for n, net in (("1", "RCA_C"), ("2", "RCA_S"), ("3", "LINHA"),
                   ("4", "GND"), ("5", "LOUT_P"), ("6", "LOUT_N")):
        stub(sh, j4, n, U(30), net)
    # SW_DPDT_x2 e multi-unidade: um polo por instancia (mesma referencia).
    # Pinagem do simbolo: 2/5 = comum (esquerda), 1/4 = posicao A, 3/6 = B.
    swa = sh.symbol("Switch:SW_DPDT_x2", "SW2", "A/B", (U(56), U(87)),
                    on_board=False, unit=1)
    stub(sh, swa, "2", U(56), "RCA_C")     # comum -> centros dos RCAs
    stub(sh, swa, "1", U(56), "LINHA")     # A: linha p/ TV
    stub(sh, swa, "3", U(56), "LOUT_P")    # B: Lout+

    swb = sh.symbol("Switch:SW_DPDT_x2", "SW2", "A/B", (U(56), U(96)),
                    on_board=False, unit=2)
    stub(sh, swb, "5", U(56), "RCA_S")     # comum -> shields dos RCAs
    stub(sh, swb, "4", U(56), "GND")       # A: GND (estrela)
    stub(sh, swb, "6", U(56), "LOUT_N")    # B: Lout-

    # Os dois RCAs de audio ficam NA PLACA, em paralelo (audio e mono).
    for i in range(2):
        j = sh.symbol("Connector_Generic:Conn_01x02", f"J{8+i}",
                      f"RCA audio {'L' if i == 0 else 'R'} (na placa)",
                      (U(84), U(87 + i * 9)), footprint=FP_RCA)
        stub(sh, j, "1", U(84), "RCA_C")
        stub(sh, j, "2", U(84), "RCA_S")

    # (LOUT+/- vem direto do modulo U2 na placa e sobem para J4 -> chave.)

    # ---------------------------------------------------------- notas
    sh.text("Notas: GND em estrela no Pico (RCA video -> pino 23, amp -> 38,",
            (U(8), U(96)), 1.8)
    sh.text("pots -> AGND pino 33). Saidas do PAM8403 sao BTL: NUNCA aterrar",
            (U(8), U(99)), 1.8)
    sh.text("Lout-/Rout- nem liga-las na entrada de audio da TV.",
            (U(8), U(102)), 1.8)
    sh.text("Simbolos sem footprint = pecas de painel (excluidas da placa).",
            (U(8), U(105)), 1.8)

    OUT.mkdir(exist_ok=True)
    sch = OUT / f"{PROJECT}.kicad_sch"
    title = ("RetroSC Pong - RP2040"
             + (" (RP2040 roxo 1 botao)" if VARIANT_YD else ""))
    sch.write_text(sh.render(title, "1.0", "2026-06-08"),
                   encoding="utf-8")

    pro = OUT / f"{PROJECT}.kicad_pro"
    if not pro.exists():
        pro.write_text('{\n  "board": {},\n  "libraries": {"pinned_footprint_libs": [], '
                       '"pinned_symbol_libs": []},\n  "meta": {"filename": '
                       f'"{PROJECT}.kicad_pro", "version": 3}},\n  "sheets": '
                       f'[["{sh.root}", "Root"]],\n  "text_variables": {{}}\n}}\n',
                       encoding="utf-8")
    print(f"OK: {sch}")


if __name__ == "__main__":
    build()
