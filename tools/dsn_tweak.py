# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
# (origem: skill kicad-freerouting)
"""Ajusta um DSN exportado pelo KiCad antes do Freerouting.

  python dsn_tweak.py route.dsn [width_sinal_um] [width_power_um] [clearance_um]

Padrao: sinais 300 um, power (+5V/GND) 600 um, clearance 140 um.
Move +5V e GND da classe default para uma classe "power" mais larga.
So e necessario se as netclasses do .kicad_pro nao estiverem configuradas
(se estiverem, o proprio exportador do KiCad ja emite as classes certas).
"""
import re
import sys

path = sys.argv[1]
w_sig = sys.argv[2] if len(sys.argv) > 2 else '300'
w_pow = sys.argv[3] if len(sys.argv) > 3 else '600'
clr = sys.argv[4] if len(sys.argv) > 4 else '140'

text = open(path, encoding='utf-8').read()

# larguras/clearances globais (o export padrao do KiCad usa 200/200)
text = re.sub(r'\(width \d+\)', '(width %s)' % w_sig, text)
text = re.sub(r'\(clearance (\d+)\)', '(clearance %s)' % clr, text, count=0)
# preserva a sub-regra smd_smd se existir (voltou a 'clr'; inofensivo em THT)

# bloco da classe default
i = text.find('(class kicad_default')
if i < 0:
    print('classe kicad_default nao encontrada - nada a fazer')
    sys.exit(0)
depth = 0
j = i
while True:
    c = text[j]
    if c == '(':
        depth += 1
    elif c == ')':
        depth -= 1
        if depth == 0:
            break
    j += 1
block = text[i:j + 1]

header_end = block.find('(', 1)
header = block[:header_end]
power_nets = [n for n in (' +5V', ' GND') if n in header]
for n in power_nets:
    header = header.replace(n, '', 1)
new_block = header + block[header_end:]

power = re.sub(r'\(class\s+kicad_default[^(]*',
               '(class power%s\n      ' % ''.join(power_nets), block, count=1)
power = power.replace('(width %s)' % w_sig, '(width %s)' % w_pow)

text = text[:i] + new_block + '\n    ' + power + text[j + 1:]
open(path, 'w', encoding='utf-8').write(text)
print('dsn ajustado: sinais %s um, power %s um (%s), clearance %s um'
      % (w_sig, w_pow, ''.join(power_nets).strip() or 'nenhuma', clr))
