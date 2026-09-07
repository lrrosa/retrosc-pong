#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RetroSC Pong contributors
"""
Simulador local do RetroSC Pong (RP2040).

Reproduz em Python a logica de jogo e o rendering do framebuffer 1-bit
de src/game.c + src/phases.c, carregando os mesmos bitmaps e a mesma fonte
direto dos arquivos C. Util para iterar em telas/UX antes de flashear hardware.

Uso:
    python tools/sim.py                 # interativo
    python tools/sim.py --shots DIR     # so gera os PNGs das telas (headless)
    python tools/sim.py --fase N        # comeca direto na fase N (0..9)

Controles:
    Mouse Y na metade esquerda  -> pot P1
    Mouse Y na metade direita   -> pot P2
    Espaco                      -> botao SELETOR
    N                           -> pula para a proxima fase (debug)
    R                           -> reseta high-scores em memoria
    Q ou ESC                    -> sair

Notas:
- Audio nao e simulado (e PWM dedicado no hardware).
- A janela e 3x o framebuffer = 768x576.
- A persistencia de high-scores em flash NAO e simulada (so RAM).
"""

import argparse
import math
import os
import random
import re
import sys
from pathlib import Path

# ============================================================
# Constantes (espelham src/config.h)
# ============================================================
FB_W, FB_H        = 256, 192
SCALE             = 3
PHASE_WIN_SCORE   = 9
PADDLE_W, PADDLE_H = 3, 24
BALL_SIZE         = 3
PADDLE_MARGIN     = 6
BALL_SPEED_INIT_Q = 0x180
BALL_SPEED_MAX_Q  = 0x500
BALL_SPEED_STEP_Q = 0x020
ATTRACT_TIMEOUT_S = 20
MENU_TIMEOUT_S    = 15
INITIALS_TIMEOUT_S = 30
PAUSE_TIMEOUT_S   = 30
PAUSE_POT_STEP    = 400
PADDLE_TAKEOVER_TOL = 6
HISCORE_COUNT     = 5
INITIALS_LEN      = 3
FONT_CELL_W       = 6
FONT_CELL_H       = 8

BRICK_W           = 4
BRICK_H           = 8
BRICK_ROWS        = FB_H // BRICK_H
TRIPLE_SEG_H      = PADDLE_H // 3
TRIPLE_GAP        = 8

BONUS_W, BONUS_H    = 16, 16      # o mascote
BONUS_VY_Q         = 0x0C0
BONUS_VX_MIN_Q     = 0x040
BONUS_VX_MAX_Q     = 0x0C0
BONUS_X_MIN        = 40
BONUS_X_MAX        = FB_W - 40 - BONUS_W
BONUS_WAIT_MIN     = 4 * 60
BONUS_WAIT_RANGE   = 8 * 60
BONUS_PASSES_MAX   = 2
BONUS_POINTS        = 3
TOTAL_FLASH_FRAMES = 90

NAVE_SCALE       = 2
NAVE_W           = 13 * NAVE_SCALE
NAVE_H           = 8 * NAVE_SCALE
NAVE_X           = FB_W // 2 - NAVE_W // 2
NAVE_SPEED       = 1
NAVE_SHOT_PERIOD = 100
NAVE_SHOT_MAX    = 4
SHOT_W, SHOT_H    = 4, 2
SHOT_SPEED        = 3
SHRINK_FRAMES     = 300

BUMPER_W, BUMPER_H = 12, 12
BUMPER_SPIN_SHIFT = 4
BALL_VX_MIN_Q     = 0x40
BARREIRA3_BLOCOS  = (5, 4, 2, 4, 5)
COL_BUMPERS       = 5
COL_GAP           = 18
COL_SPEED         = 1

VOLLEY_PADDLE_W   = 24
VOLLEY_PADDLE_H   = 4
VOLLEY_PADDLE_Y   = 8
VOLLEY_MARGIN     = 4
NET_W             = 4
NET_TOP           = 112
GRAVITY_Q         = 0x1E
BALL_VY_MAX_Q     = 0x500
VOLLEY_VY_Q       = 0x4C0
VOLLEY_VX_BASE_Q   = 0x1C0
VOLLEY_VX_SPREAD_Q = 0x140

AI_PADDLE_SPEED   = 3
AI_ERROR_PX       = 26

DEMO_PADDLE_Y_MIN = 116

# ============================================================
# Estados e modos (espelham src/game.h)
# ============================================================
(GS_ATTRACT, GS_MENU, GS_PHASE_INTRO, GS_COUNTDOWN, GS_PLAY, GS_PAUSE,
 GS_ROUND_END, GS_PHASE_END, GS_GAME_OVER, GS_ENTER_INITIALS,
 GS_HIGH_SCORES) = range(11)

MODE_ARCADE, MODE_VERSUS = 0, 1
MODE_COUNT = 2

# ============================================================
# Fases (espelham src/phases.c)
# ============================================================
(PHASE_CLASSICO, PHASE_TRIPLO, PHASE_NAVE, PHASE_BARREIRA1, PHASE_PINBALL,
 PHASE_BARREIRA2, PHASE_COLUNA, PHASE_MURALHA, PHASE_REBOUND,
 PHASE_BARREIRA3) = range(10)
PHASE_COUNT = 10

PHASE_NAMES = ["PONG CLASSICO", "TRIPLO", "NAVE", "BARREIRA I", "PINBALL",
               "BARREIRA II", "COLUNA", "MURALHA", "REBOUND", "BARREIRA III"]
PHASE_HINTS = ["O PONG DE SEMPRE", "TRES RAQUETES COM VAOS",
               "OS TIROS ENCOLHEM A RAQUETE", "DOIS MUROS NO MEIO",
               "OBSTACULOS NO MEIO", "TRES MUROS COM VAOS",
               "OBSTACULOS SOBEM E DESCEM", "OS TIJOLOS GUARDAM O GOL",
               "VOLEI: NAO DEIXE A BOLA CAIR", "QUATRO MUROS: ABRA CAMINHO"]

PF_NO_CENTER_LINE = 1 << 0
PF_GRAVITY        = 1 << 1
PF_FLOOR_SCORES   = 1 << 2
PF_SIDE_WALLS     = 1 << 3
PF_PADDLE_HORIZ   = 1 << 4
PF_TEM_BONUS       = 1 << 5


def phase_tem_bonus(idx):
    return idx in (PHASE_CLASSICO, PHASE_TRIPLO, PHASE_BARREIRA1, PHASE_MURALHA)


def pontos_em_disputa(idx):
    total = 0
    for i in range(idx + 1, PHASE_COUNT):
        total += PHASE_WIN_SCORE
        if phase_tem_bonus(i):
            total += BONUS_PASSES_MAX * BONUS_POINTS
    return total


class Phase:
    """Cenario da fase: raquetes, tijolos, solidos e bichos. Espelha phases.c."""

    def __init__(self):
        self.begin(PHASE_CLASSICO)

    # ---------- montagem ----------
    def begin(self, idx):
        self.cur = idx
        self.flags = 0
        self.col_x = []
        self.start = []
        self.solids = []
        self.rebuild_round = False
        self.frame_ctr = 0
        self.bonus_on = False
        self.bonus_x_q = self.bonus_y_q = 0
        self.bonus_vx_q = self.bonus_vy_q = 0
        self.nave_reset()
        self.shots = []
        self.shrink = [0, 0]
        self.bonus_left = BONUS_PASSES_MAX
        self.bonus_sleep()

        if phase_tem_bonus(idx):
            self.flags |= PF_TEM_BONUS

        if idx == PHASE_BARREIRA1:
            self._barreira(2, 1, None)
        elif idx == PHASE_BARREIRA2:
            self._barreira(3, 16, None)
        elif idx == PHASE_BARREIRA3:
            self._barreira(4, 1, BARREIRA3_BLOCOS)
        elif idx == PHASE_MURALHA:
            self._muralha()
        elif idx == PHASE_PINBALL:
            self._pinball()
        elif idx == PHASE_COLUNA:
            self._coluna()
        elif idx == PHASE_REBOUND:
            self._rebound()
        self.alive = list(self.start)

    def bonus_sleep(self):
        self.bonus_on = False
        self.bonus_wait = BONUS_WAIT_MIN + random.randrange(BONUS_WAIT_RANGE)

    def nave_reset(self):
        # pode ficar no meio: a contagem regressiva tem fundo preto
        self.nave_y = (FB_H - NAVE_H) // 2
        self.nave_dir = 1
        self.nave_cool = NAVE_SHOT_PERIOD

    def _barreira(self, cols, gap, blocos):
        step = BRICK_W + gap
        total = cols * BRICK_W + (cols - 1) * gap
        x0 = FB_W // 2 - total // 2
        mask = 0
        if blocos is None:
            for r in range(BRICK_ROWS):
                mask |= (1 << r)
        else:
            r = 0
            for b in blocos:
                for _ in range(b):
                    if r < BRICK_ROWS:
                        mask |= (1 << r)
                    r += 1
                r += 1                 # corredor entre os blocos
        self.col_x = [x0 + c * step for c in range(cols)]
        self.start = [mask] * cols
        self.flags |= PF_NO_CENTER_LINE

    def _muralha(self):
        self.col_x = [0, FB_W - BRICK_W]
        mask = 0
        for r in range(BRICK_ROWS):
            if (r % 6) < 2 or (r % 6) > 4:
                mask |= (1 << r)
        self.start = [mask, mask]
        self.rebuild_round = True

    def _pinball(self):
        cx, cy = FB_W // 2 - BUMPER_W // 2, FB_H // 2 - BUMPER_H // 2
        self.solids = [(cx + dx, cy + dy, BUMPER_W, BUMPER_H) for (dx, dy) in (
            (0, 0), (0, -72), (0, 72),
            (-32, -30), (32, -30), (-32, 30), (32, 30))]

    def _coluna_span(self):
        return COL_BUMPERS * BUMPER_H + (COL_BUMPERS - 1) * COL_GAP

    def _coluna_place(self):
        x = FB_W // 2 - BUMPER_W // 2
        self.solids = [(x, self.col_y + i * (BUMPER_H + COL_GAP),
                        BUMPER_W, BUMPER_H) for i in range(COL_BUMPERS)]

    def _coluna(self):
        self.col_y = (FB_H - self._coluna_span()) // 2
        self.col_dir = 1
        self._coluna_place()
        self.flags |= PF_NO_CENTER_LINE

    def _rebound(self):
        self.solids = [(FB_W // 2 - NET_W // 2, NET_TOP, NET_W, FB_H - NET_TOP)]
        self.flags |= (PF_NO_CENTER_LINE | PF_GRAVITY | PF_FLOOR_SCORES |
                       PF_SIDE_WALLS | PF_PADDLE_HORIZ)

    def round_reset(self):
        self.shrink = [0, 0]
        self.shots = []
        self.nave_reset()
        self.bonus_sleep()
        if self.rebuild_round:
            self.alive = list(self.start)

    # ---------- raquetes ----------
    def paddle_margin(self):
        return PADDLE_MARGIN + 6 if self.cur == PHASE_MURALHA else PADDLE_MARGIN

    def paddle_range(self):
        if self.flags & PF_PADDLE_HORIZ:
            return FB_W // 2 - 2 * VOLLEY_MARGIN - VOLLEY_PADDLE_W
        if self.cur == PHASE_TRIPLO:
            return FB_H - (3 * TRIPLE_SEG_H + 2 * TRIPLE_GAP)
        return FB_H - PADDLE_H

    def paddle_segments(self, player, pos):
        if self.flags & PF_PADDLE_HORIZ:
            base = VOLLEY_MARGIN if player == 0 else FB_W // 2 + VOLLEY_MARGIN
            return [(base + pos, FB_H - VOLLEY_PADDLE_Y,
                     VOLLEY_PADDLE_W, VOLLEY_PADDLE_H)]
        m = self.paddle_margin()
        x = m if player == 0 else FB_W - m - PADDLE_W
        if self.cur == PHASE_TRIPLO:
            return [(x, pos + i * (TRIPLE_SEG_H + TRIPLE_GAP),
                     PADDLE_W, TRIPLE_SEG_H) for i in range(3)]
        if self.shrink[player & 1] > 0:
            return [(x, pos + PADDLE_H // 4, PADDLE_W, PADDLE_H // 2)]
        return [(x, pos, PADDLE_W, PADDLE_H)]

    # ---------- saque ----------
    def serve_x(self, direction):
        if self.col_x and not self.rebuild_round:
            if direction < 0:
                return self.col_x[0] - 8 - BALL_SIZE
            return self.col_x[-1] + BRICK_W + 8
        if self.flags & PF_PADDLE_HORIZ:
            return FB_W // 4 if direction < 0 else 3 * FB_W // 4
        if self.cur in (PHASE_COLUNA, PHASE_PINBALL, PHASE_NAVE):
            # a nave volta ao meio a cada saque: sair do centro seria nascer
            # dentro dela e deixar o primeiro quique escolher o lado
            return FB_W // 2 - 34 if direction < 0 else FB_W // 2 + 34
        return FB_W // 2

    def serve_y(self):
        return 24 if (self.flags & PF_GRAVITY) else FB_H // 2

    # ---------- colisao ----------
    @staticmethod
    def _bounce(rx0, ry0, rx1, ry1, ball):
        # face de saida escolhida pela direcao da bola (ver phases.c)
        bx, by, vx, vy = ball
        x0, x1 = bx >> 8, (bx >> 8) + BALL_SIZE - 1
        y0, y1 = by >> 8, (by >> 8) + BALL_SIZE - 1
        LONGE = 0x7FFF
        p_x = (x1 - rx0 + 1) if vx > 0 else ((rx1 - x0 + 1) if vx < 0 else LONGE)
        p_y = (y1 - ry0 + 1) if vy > 0 else ((ry1 - y0 + 1) if vy < 0 else LONGE)
        if p_x <= p_y:
            return (((rx0 - BALL_SIZE) << 8) if vx > 0 else ((rx1 + 1) << 8),
                    by, -vx, vy), True
        return (bx, ((ry0 - BALL_SIZE) << 8) if vy > 0 else ((ry1 + 1) << 8),
                vx, -vy), False

    def ball_collide(self, prev, ball):
        """prev/ball = (x, y, vx, vy) em Q8. Devolve (hit, novo ball)."""
        bx, by, vx, vy = ball
        x0, x1 = bx >> 8, (bx >> 8) + BALL_SIZE - 1
        y0, y1 = by >> 8, (by >> 8) + BALL_SIZE - 1

        for c, cx0 in enumerate(self.col_x):
            if not self.alive[c]:
                continue
            cx1 = cx0 + BRICK_W - 1
            if x1 < cx0 or x0 > cx1:
                continue
            r0 = max(0, y0 // BRICK_H)
            r1 = min(BRICK_ROWS - 1, y1 // BRICK_H)
            for r in range(r0, r1 + 1):
                if not (self.alive[c] & (1 << r)):
                    continue
                self.alive[c] &= ~(1 << r)
                novo, _ = self._bounce(cx0, r * BRICK_H, cx1,
                                       (r + 1) * BRICK_H - 1, ball)
                return True, novo

        for (sx, sy, sw, sh) in self.solids:
            if x1 < sx or x0 > sx + sw - 1:
                continue
            if y1 < sy or y0 > sy + sh - 1:
                continue
            novo, eixo_x = self._bounce(sx, sy, sx + sw - 1, sy + sh - 1, ball)
            if self.cur in (PHASE_PINBALL, PHASE_COLUNA):
                bx2, by2, vx2, vy2 = novo
                giro = 1 if random.getrandbits(1) else -1
                saida_x, saida_y = vx2, vy2
                vx2, vy2 = (vx2 - giro * (vy2 >> BUMPER_SPIN_SHIFT),
                            vy2 + giro * (vx2 >> BUMPER_SPIN_SHIFT))
                # o eixo que acabou de rebater nao pode trocar de sinal
                if eixo_x:
                    if (vx2 < 0) != (saida_x < 0):
                        vx2 = -vx2
                elif (vy2 < 0) != (saida_y < 0):
                    vy2 = -vy2
                if -BALL_VX_MIN_Q < vx2 < BALL_VX_MIN_Q:
                    vx2 = -BALL_VX_MIN_Q if vx2 < 0 else BALL_VX_MIN_Q
                novo = (bx2, by2, vx2, vy2)
            return True, novo

        if self.cur == PHASE_NAVE:
            gx1, gy1 = NAVE_X + NAVE_W - 1, self.nave_y + NAVE_H - 1
            if not (x1 < NAVE_X or x0 > gx1 or y1 < self.nave_y or y0 > gy1):
                novo, _ = self._bounce(NAVE_X, self.nave_y, gx1, gy1, ball)
                return True, novo
        return False, ball

    # ---------- bichos ----------
    @staticmethod
    def _overlap(a, b):
        ax, ay, aw, ah = a
        bx, by, bw, bh = b
        return not (ax >= bx + bw or ax + aw <= bx or ay >= by + bh or ay + ah <= by)

    def _bonus_spawn(self):
        de_cima = random.getrandbits(1)
        self.bonus_x_q = random.randint(BONUS_X_MIN, BONUS_X_MAX) << 8
        self.bonus_y_q = (-BONUS_H << 8) if de_cima else (FB_H << 8)
        self.bonus_vy_q = BONUS_VY_Q if de_cima else -BONUS_VY_Q
        vx = random.randint(BONUS_VX_MIN_Q, BONUS_VX_MAX_Q)
        self.bonus_vx_q = -vx if random.getrandbits(1) else vx
        self.bonus_on = True

    def _update_bonus(self, ball_x, ball_y, last_hitter, bonus):
        if not self.bonus_on:
            if self.bonus_left <= 0:
                return
            self.bonus_wait -= 1
            if self.bonus_wait <= 0:
                self.bonus_left -= 1
                self._bonus_spawn()
            return
        self.bonus_x_q += self.bonus_vx_q
        self.bonus_y_q += self.bonus_vy_q
        sx, sy = self.bonus_x_q >> 8, self.bonus_y_q >> 8
        if sx < BONUS_X_MIN:
            self.bonus_x_q = BONUS_X_MIN << 8; self.bonus_vx_q = -self.bonus_vx_q
        if sx > BONUS_X_MAX:
            self.bonus_x_q = BONUS_X_MAX << 8; self.bonus_vx_q = -self.bonus_vx_q
        if sy > FB_H or sy < -BONUS_H:
            self.bonus_sleep()
            return
        sx = self.bonus_x_q >> 8
        if self._overlap((ball_x >> 8, ball_y >> 8, BALL_SIZE, BALL_SIZE),
                         (sx, sy, BONUS_W, BONUS_H)):
            if last_hitter in (0, 1):
                bonus[last_hitter] += BONUS_POINTS
            self.bonus_sleep()

    def update(self, ball_x, ball_y, paddle_pos, last_hitter):
        bonus = [0, 0]
        self.frame_ctr += 1
        for p in (0, 1):
            if self.shrink[p] > 0:
                self.shrink[p] -= 1

        if self.flags & PF_TEM_BONUS:
            self._update_bonus(ball_x, ball_y, last_hitter, bonus)

        if self.cur == PHASE_COLUNA:
            mx = FB_H - self._coluna_span()
            self.col_y += self.col_dir * COL_SPEED
            if self.col_y >= mx:
                self.col_y, self.col_dir = mx, -1
            if self.col_y <= 0:
                self.col_y, self.col_dir = 0, 1
            self._coluna_place()

        if self.cur == PHASE_NAVE:
            self.nave_y += self.nave_dir * NAVE_SPEED
            if self.nave_y > FB_H - NAVE_H:
                self.nave_y, self.nave_dir = FB_H - NAVE_H, -1
            if self.nave_y < 0:
                self.nave_y, self.nave_dir = 0, 1
            self.nave_cool -= 1
            if self.nave_cool <= 0:
                self.nave_cool = NAVE_SHOT_PERIOD
                if len(self.shots) < NAVE_SHOT_MAX:
                    esq = random.getrandbits(1)
                    self.shots.append({
                        "x": NAVE_X - SHOT_W if esq else NAVE_X + NAVE_W,
                        "y": self.nave_y + NAVE_H // 2 - SHOT_H // 2,
                        "vx": -SHOT_SPEED if esq else SHOT_SPEED,
                    })
            vivos = []
            for s in self.shots:
                s["x"] += s["vx"]
                if s["x"] < -SHOT_W or s["x"] > FB_W:
                    continue
                p = 0 if s["vx"] < 0 else 1
                pego = False
                for seg in self.paddle_segments(p, paddle_pos[p]):
                    if self._overlap((s["x"], s["y"], SHOT_W, SHOT_H), seg):
                        self.shrink[p] = SHRINK_FRAMES
                        pego = True
                        break
                if not pego:
                    vivos.append(s)
            self.shots = vivos
        return bonus

    # ---------- desenho ----------
    def draw(self, fb, mascote=None, glyphs=None):
        for c, cx in enumerate(self.col_x):
            for r in range(BRICK_ROWS):
                if self.alive[c] & (1 << r):
                    fb.fill_rect(cx, r * BRICK_H, BRICK_W, BRICK_H - 1, 1)
        for (sx, sy, sw, sh) in self.solids:
            fb.fill_rect(sx, sy, sw, sh, 1)

        if self.cur == PHASE_NAVE:
            s = NAVE_SCALE
            for (dx, dy, w, h) in ((5, 0, 3, 1), (4, 1, 5, 1), (3, 2, 7, 1),
                                   (2, 3, 9, 1), (0, 4, 13, 2), (1, 6, 2, 2),
                                   (5, 6, 3, 2), (10, 6, 2, 2)):
                fb.fill_rect(NAVE_X + dx * s, self.nave_y + dy * s,
                             w * s, h * s, 1)
            for sh in self.shots:
                fb.fill_rect(sh["x"], sh["y"], SHOT_W, SHOT_H, 1)

        if (self.flags & PF_TEM_BONUS) and self.bonus_on:
            x, y = self.bonus_x_q >> 8, self.bonus_y_q >> 8
            if mascote:
                fb.blit_1bit(mascote, 16, 16, x, y, 1)
            if glyphs and not ((self.frame_ctr >> 4) & 1):
                tw = text_width("BONUS", 1)
                tx = min(max(2, x + BONUS_W // 2 - tw // 2), FB_W - tw - 2)
                ty = (y - FONT_CELL_H - 1) if y > FB_H // 2 else (y + BONUS_H + 2)
                ty = min(max(0, ty), FB_H - FONT_CELL_H)
                fb.fill_rect(tx - 2, ty - 1, tw + 4, 9, 0)
                gfx_text(fb, glyphs, tx, ty, "BONUS", 1, 1)


# ============================================================
# Carregar assets de src/assets.c
# ============================================================
def load_assets():
    root = Path(__file__).parent.parent
    text = (root / "src" / "assets.c").read_text(encoding="utf-8", errors="replace")
    assets = {}
    for sym in ("retrosc_logo", "retrosc_mascote"):
        m = re.search(rf"{sym}_data\[\d+\]\s*=\s*\{{(.*?)\}};", text, re.DOTALL)
        if not m:
            raise RuntimeError(f"Nao achei {sym}_data em assets.c")
        hexs = re.findall(r"0x[0-9A-Fa-f]+", m.group(1))
        data = bytes(int(h, 16) for h in hexs)
        prefix = sym.upper()
        for k in ("W", "H", "STRIDE"):
            v = re.search(rf"{prefix}_{k}\s*=\s*(\d+)", text)
            if not v:
                raise RuntimeError(f"Nao achei {prefix}_{k}")
            assets[f"{sym}_{k}"] = int(v.group(1))
        assets[sym] = data
    return assets

# ============================================================
# Carregar fonte de src/font.c (formato C: ['X' - 0x20] = {bytes})
# ============================================================
def load_font():
    root = Path(__file__).parent.parent
    text = (root / "src" / "font.c").read_text(encoding="utf-8", errors="replace")
    glyphs = {}
    pattern = re.compile(r"\['([^']+|\\')'\s*-\s*0x20\]\s*=\s*\{([^}]+)\}", re.DOTALL)
    for m in pattern.finditer(text):
        ch = m.group(1)
        if ch.startswith("\\"):
            ch = {"\\'": "'", "\\\\": "\\"}.get(ch, ch[-1])
        bytes_ = [int(h, 16) for h in re.findall(r"0x[0-9A-Fa-f]+", m.group(2))]
        if len(bytes_) >= 7:
            glyphs[ch] = bytes_[:7]
    # garante espaco
    glyphs.setdefault(" ", [0]*7)
    return glyphs

# ============================================================
# Framebuffer + primitivas de desenho (mesma logica de gfx.c)
# ============================================================
class FB:
    def __init__(self):
        # numpy seria mais rapido, mas mantenho dep so pygame
        self.px = bytearray(FB_W * FB_H)

    def clear(self, color=0):
        v = 0xFF if color else 0
        for i in range(len(self.px)):
            self.px[i] = v

    def set(self, x, y, color):
        if 0 <= x < FB_W and 0 <= y < FB_H:
            self.px[y * FB_W + x] = 0xFF if color else 0

    def fill_rect(self, x, y, w, h, color):
        v = 0xFF if color else 0
        x0 = max(0, x); x1 = min(FB_W, x + w)
        y0 = max(0, y); y1 = min(FB_H, y + h)
        for yy in range(y0, y1):
            base = yy * FB_W
            for xx in range(x0, x1):
                self.px[base + xx] = v

    def hline(self, x, y, w, color):
        self.fill_rect(x, y, w, 1, color)

    def vline(self, x, y, h, color):
        self.fill_rect(x, y, 1, h, color)

    def blit_1bit(self, data, bw, bh, x, y, color=1):
        stride = (bw + 7) // 8
        v = 0xFF if color else 0
        for row in range(bh):
            dy = y + row
            if not (0 <= dy < FB_H):
                continue
            base = dy * FB_W
            for col in range(bw):
                dx = x + col
                if not (0 <= dx < FB_W):
                    continue
                b = data[row * stride + (col >> 3)]
                if b & (0x80 >> (col & 7)):
                    self.px[base + dx] = v

    def dotted_vline(self, x, y0, y1, dot_h, gap_h):
        phase = 0
        draw = 1
        for y in range(y0, y1):
            if 0 <= y < FB_H and 0 <= x < FB_W:
                self.px[y * FB_W + x] = 0xFF if draw else 0
            phase += 1
            if draw and phase >= dot_h:
                phase = 0; draw = 0
            elif (not draw) and phase >= gap_h:
                phase = 0; draw = 1

def gfx_text(fb, glyphs, x, y, s, scale, color=1):
    cx = x
    for c in s:
        cc = c.upper() if c.isalpha() else c
        g = glyphs.get(cc, glyphs[" "])
        for r in range(7):
            row = g[r]
            for col in range(5):
                if row & (0x80 >> col):
                    fb.fill_rect(cx + col*scale, y + r*scale, scale, scale, color)
        cx += 6 * scale

def text_width(s, scale):
    return len(s) * 6 * scale

def center_text(fb, glyphs, y, s, scale, color=1):
    w = text_width(s, scale)
    gfx_text(fb, glyphs, (FB_W - w)//2, y, s, scale, color)

def right_text(fb, glyphs, x_right, y, s, scale, color=1):
    gfx_text(fb, glyphs, x_right - text_width(s, scale), y, s, scale, color)

def center_text_boxed(fb, glyphs, y, s, scale):
    margem = 4
    w = text_width(s, scale)
    h = 7 * scale
    x = (FB_W - w) // 2
    fb.fill_rect(x - margem, y - margem, w + 2 * margem, h + 2 * margem, 0)
    gfx_text(fb, glyphs, x, y, s, scale, 1)

# ============================================================
# Estado do jogo
# ============================================================
class Game:
    def __init__(self, assets, glyphs):
        self.assets = assets
        self.glyphs = glyphs
        self.fb = FB()
        self.phase = Phase()
        self.state = GS_ATTRACT
        self.state_timer = 0
        self.mode = MODE_ARCADE
        self.menu_sel = MODE_ARCADE
        self.menu_idle = 0
        self.pause_sel = 0
        self.pause_pot_ref = [2048, 2048]
        self.frames_globais = 0
        self.total_flash = [0, 0]
        self.phase_idx = 0
        self.phase_score = [0, 0]
        self.total_score = [0, 0]
        self.phase_first_round = True
        self.paddle_pos = [DEMO_PADDLE_Y_MIN + 16, DEMO_PADDLE_Y_MIN + 16]
        self.paddle_travado = [False, False]
        self.last_scorer = 2
        self.last_hitter = -1
        self.last_winner = 0
        self.arcade_perdeu = False
        self.ai_bias = 0
        self.ball_x = (FB_W // 2) << 8
        self.ball_y = (FB_H - 40) << 8
        self.ball_vx = BALL_SPEED_INIT_Q
        self.ball_vy = BALL_SPEED_INIT_Q // 2
        self.ball_speed_q = BALL_SPEED_INIT_Q
        self.baseline_pot = [2048, 2048]
        self.movement_remaining = 0
        self.last_moved = 0
        # Highscore: (score, player, "ABC", mode)
        self.hiscores = [(0, 0, "   ", 0)] * HISCORE_COUNT
        # Estado de entrada de iniciais
        self.initials_buf = list("   ")
        self.initials_slot = 0
        self.initials_armed = False
        self.initials_player = 1
        # Input mockado por mouse: cada frame setamos
        self.input_pot = [2048, 2048]
        self.input_seletor = False

    # ---------- helpers ----------
    def set_state(self, s):
        self.state = s
        self.state_timer = 0

    def input_paddle_y(self, p, rng):
        return max(0, min(rng, (self.input_pot[p] * rng) // 4095))

    def poll_input(self):
        for i in (0, 1):
            d = abs(self.input_pot[i] - self.baseline_pot[i])
            if d > 40:
                self.movement_remaining = 20
                self.last_moved = i
                self.baseline_pot[i] = self.input_pot[i]
        if self.movement_remaining > 0:
            self.movement_remaining -= 1

    def reset_movement(self):
        self.movement_remaining = 0
        self.baseline_pot = list(self.input_pot)

    def roll_ai_bias(self):
        self.ai_bias = random.randint(-AI_ERROR_PX, AI_ERROR_PX)

    def vx_from_vy(self, vy_frac):
        s2 = self.ball_speed_q * self.ball_speed_q
        vx2 = max(0, s2 - vy_frac * vy_frac)
        return int(math.sqrt(vx2)) or (self.ball_speed_q // 2)

    def p2_label(self):
        return "CPU" if self.mode == MODE_ARCADE else "P2"

    # ---------- partida ----------
    def serve_ball(self, direction):
        self.ball_x = self.phase.serve_x(direction) << 8
        self.ball_y = self.phase.serve_y() << 8
        self.ball_speed_q = BALL_SPEED_INIT_Q
        self.last_hitter = -1
        self.roll_ai_bias()
        if self.phase.flags & PF_GRAVITY:
            self.ball_vx = direction * 0x60
            self.ball_vy = 0
            return
        r = random.randint(0, 255) - 128
        vy_frac = (r * self.ball_speed_q) // 256
        self.ball_vx = direction * self.vx_from_vy(vy_frac)
        self.ball_vy = vy_frac

    def reset_round(self, scorer):
        mid = self.phase.paddle_range() // 2
        self.paddle_pos = [mid, mid]
        self.paddle_travado = [False, False]
        self.phase.round_reset()
        self.serve_ball(+1 if scorer == 1 else -1)

    def begin_phase(self, idx, scorer):
        # scorer = quem venceu a fase anterior; a bola sai para o outro lado.
        self.phase_idx = idx
        self.phase_score = [0, 0]
        self.phase.begin(idx)
        self.phase_first_round = True
        self.last_scorer = scorer
        self.reset_round(self.last_scorer)

    def begin_game(self, mode):
        self.mode = mode
        self.total_score = [0, 0]
        self.total_flash = [0, 0]
        self.last_winner = 0
        self.arcade_perdeu = False
        self.begin_phase(0, random.choice((1, 2)))

    def fim_de_jogo(self):
        if self.mode == MODE_ARCADE and self.phase_score[1] >= PHASE_WIN_SCORE:
            return True
        if self.phase_idx + 1 >= PHASE_COUNT:
            return True
        if self.mode == MODE_VERSUS:
            dif = abs(self.total_score[0] - self.total_score[1])
            if dif > pontos_em_disputa(self.phase_idx):
                return True
        return False

    def hiscore_player(self):
        if self.mode == MODE_ARCADE:
            return 1
        return 1 if self.total_score[0] >= self.total_score[1] else 2

    # ---------- raquetes ----------
    def update_paddle_ai(self, player):
        rng = self.phase.paddle_range()
        seg0 = self.phase.paddle_segments(player, 0)[0]
        if self.phase.flags & PF_PADDLE_HORIZ:
            bxc = (self.ball_x >> 8) + BALL_SIZE // 2
            coming = (bxc >= FB_W // 2) if player == 1 else (bxc < FB_W // 2)
            target = bxc - seg0[2] // 2 - seg0[0] + self.ai_bias
        else:
            span = FB_H - rng
            coming = (self.ball_vx > 0) if player == 1 else (self.ball_vx < 0)
            target = (self.ball_y >> 8) + BALL_SIZE // 2 - span // 2 + self.ai_bias
        if not coming:
            target = rng // 2
        diff = target - self.paddle_pos[player]
        speed = AI_PADDLE_SPEED + self.phase_idx // 3
        if abs(diff) > speed:
            diff = speed if diff > 0 else -speed
        self.paddle_pos[player] = max(0, min(rng, self.paddle_pos[player] + diff))

    def update_paddle_humano(self, player, rng):
        lido = self.input_paddle_y(player, rng)
        if self.paddle_travado[player]:
            if abs(lido - self.paddle_pos[player]) <= PADDLE_TAKEOVER_TOL:
                self.paddle_travado[player] = False
            else:
                return
        self.paddle_pos[player] = lido

    def update_paddles(self):
        rng = self.phase.paddle_range()
        self.update_paddle_humano(0, rng)
        if self.mode == MODE_VERSUS:
            self.update_paddle_humano(1, rng)
        else:
            self.update_paddle_ai(1)

    def update_paddles_demo(self):
        for i in (0, 1):
            target = (self.ball_y >> 8) - PADDLE_H // 2
            diff = target - self.paddle_pos[i]
            speed = 2
            if abs(diff) > speed:
                diff = speed if diff > 0 else -speed
            self.paddle_pos[i] += diff
            self.paddle_pos[i] = max(DEMO_PADDLE_Y_MIN,
                                     min(FB_H - PADDLE_H, self.paddle_pos[i]))

    # ---------- fisica ----------
    def on_paddle_hit(self, player, seg):
        sx, sy, sw, sh = seg
        seg_center = sy + sh // 2
        by = (self.ball_y >> 8) + BALL_SIZE // 2
        offset = by - seg_center
        if self.ball_speed_q < BALL_SPEED_MAX_Q:
            self.ball_speed_q += BALL_SPEED_STEP_Q
        vy_frac = (offset * self.ball_speed_q) // max(1, sh)
        vx_abs = self.vx_from_vy(vy_frac)
        self.ball_vx = +vx_abs if player == 0 else -vx_abs
        self.ball_vy = vy_frac
        self.roll_ai_bias()

    def on_volley_hit(self, player, seg):
        sx, sy, sw, sh = seg
        bxc = (self.ball_x >> 8) + BALL_SIZE // 2
        offset = bxc - (sx + sw // 2)
        half = max(1, sw // 2)
        frente = 1 if player == 0 else -1
        self.ball_vy = -VOLLEY_VY_Q
        self.ball_vx = (frente * VOLLEY_VX_BASE_Q +
                        (frente * offset * VOLLEY_VX_SPREAD_Q) // half)
        self.roll_ai_bias()

    def add_point(self, player):
        self.phase_score[player] += 1
        self.total_score[player] += 1
        self.last_scorer = player + 1
        self.set_state(GS_ROUND_END)

    def physics(self):
        f = self.phase.flags
        prev = (self.ball_x, self.ball_y, self.ball_vx, self.ball_vy)
        if f & PF_GRAVITY:
            self.ball_vy = min(BALL_VY_MAX_Q, self.ball_vy + GRAVITY_Q)
        self.ball_x += self.ball_vx
        self.ball_y += self.ball_vy

        if self.ball_y < 0:
            self.ball_y = 0; self.ball_vy = -self.ball_vy
        max_y = (FB_H - BALL_SIZE) << 8
        if self.ball_y > max_y:
            if f & PF_FLOOR_SCORES:
                cx = (self.ball_x >> 8) + BALL_SIZE // 2
                self.add_point(1 if cx < FB_W // 2 else 0)
                return
            self.ball_y = max_y; self.ball_vy = -self.ball_vy

        hit, ball = self.phase.ball_collide(
            prev, (self.ball_x, self.ball_y, self.ball_vx, self.ball_vy))
        if hit:
            self.ball_x, self.ball_y, self.ball_vx, self.ball_vy = ball

        horiz = bool(f & PF_PADDLE_HORIZ)
        bx = self.ball_x >> 8
        by = self.ball_y >> 8
        prev_by = prev[1] >> 8
        for p in (0, 1):
            if horiz:
                if self.ball_vy <= 0:
                    continue
            else:
                if p == 0 and self.ball_vx >= 0:
                    continue
                if p == 1 and self.ball_vx <= 0:
                    continue
            for seg in self.phase.paddle_segments(p, self.paddle_pos[p]):
                sx, sy, sw, sh = seg
                if bx >= sx + sw or bx + BALL_SIZE <= sx:
                    continue
                if horiz:
                    if not (prev_by + BALL_SIZE <= sy and by + BALL_SIZE >= sy):
                        continue
                elif by >= sy + sh or by + BALL_SIZE <= sy:
                    continue
                if horiz:
                    self.ball_y = (sy - BALL_SIZE) << 8
                    self.on_volley_hit(p, seg)
                else:
                    self.ball_x = ((sx + sw) << 8) if p == 0 else ((sx - BALL_SIZE) << 8)
                    self.on_paddle_hit(p, seg)
                self.last_hitter = p
                break

        if f & PF_SIDE_WALLS:
            max_x = (FB_W - BALL_SIZE) << 8
            if self.ball_x < 0:
                self.ball_x = 0; self.ball_vx = -self.ball_vx
            if self.ball_x > max_x:
                self.ball_x = max_x; self.ball_vx = -self.ball_vx
        else:
            bx = self.ball_x >> 8
            if bx + BALL_SIZE < 0:
                self.add_point(1)
            elif bx > FB_W:
                self.add_point(0)

    # ---------- highscores ----------
    def grava_iniciais(self):
        pts = self.total_score[self.initials_player - 1]
        self.hi_consider(pts, self.initials_player,
                         "".join(self.initials_buf), self.mode)
        self.set_state(GS_HIGH_SCORES)

    def hi_qualifies(self, score):
        return any(score > s for s, _, _, _ in self.hiscores)

    def hi_consider(self, score, player, initials, mode):
        pos = -1
        for i, (s, _, _, _) in enumerate(self.hiscores):
            if score > s:
                pos = i; break
        if pos < 0:
            return
        ini = "".join(c if "A" <= c <= "Z" else " " for c in initials)
        new = (score, player, (ini + "   ")[:3], mode)
        self.hiscores = self.hiscores[:pos] + [new] + self.hiscores[pos:-1]

    # ============== desenho ===============
    def draw_field(self):
        if not (self.phase.flags & PF_NO_CENTER_LINE):
            self.fb.dotted_vline(FB_W // 2, 0, FB_H, 4, 4)
        self.phase.draw(self.fb, self.assets.get("retrosc_mascote"), self.glyphs)

    def draw_paddles(self):
        for p in (0, 1):
            if self.paddle_travado[p] and ((self.frames_globais >> 3) & 1):
                continue
            for (x, y, w, h) in self.phase.paddle_segments(p, self.paddle_pos[p]):
                self.fb.fill_rect(x, y, w, h, 1)

    def draw_ball(self):
        self.fb.fill_rect(self.ball_x >> 8, self.ball_y >> 8, BALL_SIZE, BALL_SIZE, 1)

    def draw_total_box(self, x, total, piscando=False):
        w_label = text_width("TOTAL", 1)
        gfx_text(self.fb, self.glyphs, x, 10, "TOTAL", 1, 1)
        if piscando and ((self.frames_globais >> 2) & 1):
            return
        s = str(total)
        gfx_text(self.fb, self.glyphs, x + (w_label - text_width(s, 1)) // 2,
                 19, s, 1, 1)

    def draw_scores(self):
        cx = FB_W // 2
        y_grande = 8
        w_label = text_width("TOTAL", 1)
        f0, f1 = str(self.phase_score[0]), str(self.phase_score[1])
        right_text(self.fb, self.glyphs, cx - 30, y_grande, f0, 3)
        self.draw_total_box(cx - 30 - text_width(f0, 3) - 8 - w_label,
                            self.total_score[0], self.total_flash[0] > 0)
        gfx_text(self.fb, self.glyphs, cx + 30, y_grande, f1, 3, 1)
        self.draw_total_box(cx + 30 + text_width(f1, 3) + 8, self.total_score[1],
                            self.total_flash[1] > 0)

    def draw_attract_background(self):
        self.fb.clear(0)
        lw = self.assets["retrosc_logo_W"]
        lh = self.assets["retrosc_logo_H"]
        self.fb.blit_1bit(self.assets["retrosc_logo"], lw, lh, (FB_W - lw)//2, 4, 1)
        center_text(self.fb, self.glyphs, 4 + lh + 8, "RETRO PONG", 3)
        self.fb.dotted_vline(FB_W // 2, FB_H - 60, FB_H - 12, 4, 4)
        self.fb.fill_rect(PADDLE_MARGIN, self.paddle_pos[0], PADDLE_W, PADDLE_H, 1)
        self.fb.fill_rect(FB_W - PADDLE_MARGIN - PADDLE_W, self.paddle_pos[1],
                          PADDLE_W, PADDLE_H, 1)
        self.draw_ball()

    def draw_attract(self):
        self.draw_attract_background()
        if ((self.state_timer >> 5) & 1) == 0:
            center_text(self.fb, self.glyphs, FB_H - 9, "APERTE O SELETOR", 1)

    def draw_menu(self):
        self.draw_attract_background()
        bx, by = 24, 108
        bw, bh = FB_W - 2 * bx, 60
        self.fb.fill_rect(bx, by, bw, bh, 0)
        self.fb.hline(bx, by, bw, 1)
        self.fb.hline(bx, by + bh - 1, bw, 1)
        self.fb.vline(bx, by, bh, 1)
        self.fb.vline(bx + bw - 1, by, bh, 1)
        for i in range(MODE_COUNT):
            label = "MODO ARCADE" if i == MODE_ARCADE else "MODO VERSUS"
            w = text_width(label, 2)
            tx = (FB_W - w) // 2
            ty = by + 7 + i * 24
            if i == self.menu_sel:
                self.fb.fill_rect(tx - 6, ty - 3, w + 12, FONT_CELL_H * 2 + 5, 1)
                gfx_text(self.fb, self.glyphs, tx, ty, label, 2, 0)
            else:
                gfx_text(self.fb, self.glyphs, tx, ty, label, 2, 1)
        center_text(self.fb, self.glyphs, by + bh + 5,
                    "1 JOGADOR CONTRA A CPU" if self.menu_sel == MODE_ARCADE
                    else "2 JOGADORES", 1)
        if ((self.state_timer >> 4) & 1) == 0:
            center_text(self.fb, self.glyphs, FB_H - 9, "SELETOR CONFIRMA", 1)

    def draw_phase_intro(self):
        self.fb.clear(0)
        center_text(self.fb, self.glyphs, 28, f"FASE {self.phase_idx + 1}", 3)
        center_text(self.fb, self.glyphs, 64, PHASE_NAMES[self.phase_idx], 2)
        center_text(self.fb, self.glyphs, 92, PHASE_HINTS[self.phase_idx], 1)
        center_text(self.fb, self.glyphs, 124,
                    f"TOTAL  P1 {self.total_score[0]}   "
                    f"{self.p2_label()} {self.total_score[1]}", 1)
        if ((self.state_timer >> 4) & 1) == 0:
            center_text(self.fb, self.glyphs, FB_H - 16,
                        "MODO ARCADE" if self.mode == MODE_ARCADE
                        else "MODO VERSUS", 1)

    def draw_countdown(self):
        self.fb.clear(0)
        self.draw_field(); self.draw_scores(); self.draw_paddles()
        if not self.phase_first_round:
            center_text_boxed(self.fb, self.glyphs, FB_H // 2 - 10, "GO", 3)
            return
        sec_left = 3 - self.state_timer // 60
        if sec_left > 0:
            center_text_boxed(self.fb, self.glyphs, FB_H // 2 - 10, str(sec_left), 3)
        else:
            center_text_boxed(self.fb, self.glyphs, FB_H // 2 - 10, "GO", 3)

    def draw_play(self):
        self.fb.clear(0)
        self.draw_field(); self.draw_scores(); self.draw_paddles(); self.draw_ball()

    def draw_round_end(self):
        self.draw_play()

    def draw_pause(self):
        self.draw_play()
        bx, by = 40, 56
        bw, bh = FB_W - 2 * bx, 84
        self.fb.fill_rect(bx, by, bw, bh, 0)
        self.fb.hline(bx, by, bw, 1)
        self.fb.hline(bx, by + bh - 1, bw, 1)
        self.fb.vline(bx, by, bh, 1)
        self.fb.vline(bx + bw - 1, by, bh, 1)
        center_text(self.fb, self.glyphs, by + 6, "PAUSA", 3)
        seg = max(0, (PAUSE_TIMEOUT_S * 60 - self.state_timer + 59) // 60)
        center_text_boxed(self.fb, self.glyphs, by + bh + 4,
                          f"{'VOLTA' if self.pause_sel == 0 else 'SAI'} EM {seg}S", 1)
        for i in range(2):
            label = "CONTINUAR" if i == 0 else "SAIR DO JOGO"
            w = text_width(label, 2)
            tx = (FB_W - w) // 2
            ty = by + 36 + i * 24
            if i == self.pause_sel:
                self.fb.fill_rect(tx - 6, ty - 3, w + 12, FONT_CELL_H * 2 + 5, 1)
                gfx_text(self.fb, self.glyphs, tx, ty, label, 2, 0)
            else:
                gfx_text(self.fb, self.glyphs, tx, ty, label, 2, 1)

    def draw_phase_end(self):
        self.fb.clear(0)
        center_text(self.fb, self.glyphs, 24,
                    f"FASE {self.phase_idx + 1} COMPLETA", 2)
        center_text(self.fb, self.glyphs, 48, PHASE_NAMES[self.phase_idx], 1)
        center_text(self.fb, self.glyphs, 80,
                    f"P1 {self.phase_score[0]}   X   "
                    f"{self.phase_score[1]} {self.p2_label()}", 2)
        center_text(self.fb, self.glyphs, 116,
                    f"TOTAL  P1 {self.total_score[0]}   "
                    f"{self.p2_label()} {self.total_score[1]}", 1)
        if ((self.state_timer >> 4) & 1) == 0:
            if self.mode == MODE_ARCADE and self.phase_score[1] >= PHASE_WIN_SCORE:
                center_text(self.fb, self.glyphs, 148, "A CPU FECHOU A FASE", 1)
            elif not self.fim_de_jogo():
                center_text(self.fb, self.glyphs, 148,
                            f"PROXIMA: {PHASE_NAMES[self.phase_idx + 1]}", 1)
            elif self.phase_idx + 1 < PHASE_COUNT:
                center_text(self.fb, self.glyphs, 148, "VANTAGEM DECISIVA", 1)
            else:
                center_text(self.fb, self.glyphs, 148, "FIM DE JOGO", 1)

    def draw_game_over(self):
        self.fb.clear(0)
        center_text(self.fb, self.glyphs, 24, "FIM DE JOGO", 2)
        if self.last_winner == 0:
            center_text(self.fb, self.glyphs, 64, "EMPATE", 3)
        elif self.mode == MODE_ARCADE:
            center_text(self.fb, self.glyphs, 64,
                        "VOCE VENCEU" if self.last_winner == 1
                        else "A CPU VENCEU", 2)
        else:
            center_text(self.fb, self.glyphs, 64,
                        f"JOGADOR {self.last_winner} VENCE", 2)
        center_text(self.fb, self.glyphs, 104,
                    f"P1 {self.total_score[0]}   X   "
                    f"{self.total_score[1]} {self.p2_label()}", 2)
        if self.mode == MODE_ARCADE:
            center_text(self.fb, self.glyphs, 132,
                        f"CHEGOU ATE A FASE {self.phase_idx + 1} DE {PHASE_COUNT}", 1)
        else:
            center_text(self.fb, self.glyphs, 132,
                        f"SOMA DE {self.phase_idx + 1} FASES", 1)

    def draw_highscores(self):
        self.fb.clear(0)
        center_text(self.fb, self.glyphs, 8, "HIGH SCORES", 2)
        y = 36
        for i, (s, p, ini, md) in enumerate(self.hiscores):
            if s > 0:
                line = f"{i+1}. {ini} {s:3d} {'ARCADE' if md == MODE_ARCADE else 'VERSUS'}"
            else:
                line = f"{i+1}. ---   -"
            gfx_text(self.fb, self.glyphs, 52, y, line, 1, 1)
            y += 14
        center_text(self.fb, self.glyphs, FB_H - 12,
                    "APERTE O SELETOR" if ((self.state_timer >> 6) & 1)
                    else "RETRO PONG", 1)

    def draw_enter_initials(self):
        self.fb.clear(0)
        center_text(self.fb, self.glyphs, 8, "NEW HIGH SCORE!", 2)
        pts = self.total_score[self.initials_player - 1]
        center_text(self.fb, self.glyphs, 32,
                    f"JOGADOR {self.initials_player} - {pts} PONTOS", 1)

        slot_w = FONT_CELL_W * 4
        gap = 8
        total = INITIALS_LEN * slot_w + (INITIALS_LEN - 1) * gap
        x0 = (FB_W - total) // 2
        y0 = 64
        for i in range(INITIALS_LEN):
            sx = x0 + i * (slot_w + gap)
            c = self.initials_buf[i]
            s = c if c != " " else ("_" if i != self.initials_slot else " ")
            show = not (i == self.initials_slot and ((self.state_timer >> 3) & 1))
            if show:
                gfx_text(self.fb, self.glyphs, sx, y0, s, 4, 1)
            if i == self.initials_slot:
                self.fb.fill_rect(sx, y0 + 8*4 + 2, slot_w - 4, 2, 1)
        center_text(self.fb, self.glyphs, FB_H - 30, "POT = LETRA", 1)
        if ((self.state_timer >> 5) & 1) == 0:
            center_text(self.fb, self.glyphs, FB_H - 18, "SELETOR PARA CONFIRMAR", 1)
        seg = max(0, (INITIALS_TIMEOUT_S * 60 - self.state_timer + 59) // 60)
        center_text(self.fb, self.glyphs, FB_H - 8,
                    f"P{self.initials_player} - GRAVA EM {seg}S", 1)

    # ============== frame por estado ===============
    def enter_attract(self):
        self.reset_movement()
        self.phase.begin(PHASE_CLASSICO)
        self.set_state(GS_ATTRACT)
        self.ball_x = (FB_W // 2) << 8
        self.ball_y = (FB_H - 40) << 8
        self.ball_vx = BALL_SPEED_INIT_Q
        self.ball_vy = BALL_SPEED_INIT_Q // 2

    def pediu_pausa(self):
        if not self.input_seletor:
            return False
        self.pause_sel = 0                      # sempre abre em CONTINUAR
        self.pause_pot_ref = list(self.input_pot)
        self.reset_movement()
        self.set_state(GS_PAUSE)
        return True

    def open_menu(self):
        self.menu_sel = MODE_ARCADE
        self.menu_idle = 0
        self.reset_movement()
        self.set_state(GS_MENU)

    def demo_ball_step(self):
        if self.ball_x < ((PADDLE_MARGIN + PADDLE_W) << 8):
            self.ball_vx = abs(self.ball_vx)
        if self.ball_x > ((FB_W - PADDLE_MARGIN - PADDLE_W - BALL_SIZE) << 8):
            self.ball_vx = -abs(self.ball_vx)
        min_y = (FB_H - 70) << 8
        max_y = (FB_H - 6 - BALL_SIZE) << 8
        if self.ball_y < min_y:
            self.ball_y = min_y; self.ball_vy = abs(self.ball_vy)
        if self.ball_y > max_y:
            self.ball_y = max_y; self.ball_vy = -abs(self.ball_vy)
        self.ball_x += self.ball_vx
        self.ball_y += self.ball_vy

    def frame(self):
        # Espelha game_frame() de game.c
        if self.state == GS_ATTRACT:
            self.update_paddles_demo()
            self.demo_ball_step()
            if self.input_seletor:
                self.open_menu()
                self.state_timer += 1
                return
            if self.state_timer >= ATTRACT_TIMEOUT_S * 60:
                self.set_state(GS_HIGH_SCORES)
                self.state_timer += 1
                return
            self.draw_attract()

        elif self.state == GS_MENU:
            self.update_paddles_demo()
            self.demo_ball_step()
            pot = self.input_pot[self.last_moved]
            if pot < 2048 - 300:
                self.menu_sel = MODE_ARCADE
            elif pot > 2048 + 300:
                self.menu_sel = MODE_VERSUS
            if self.input_seletor:
                self.begin_game(self.menu_sel)
                self.set_state(GS_PHASE_INTRO)
                self.state_timer += 1
                return
            self.menu_idle = 0 if self.movement_remaining > 0 else self.menu_idle + 1
            if self.menu_idle >= MENU_TIMEOUT_S * 60:
                self.enter_attract()
                self.state_timer += 1
                return
            self.draw_menu()

        elif self.state == GS_PHASE_INTRO:
            self.draw_phase_intro()
            if self.state_timer >= 150 or self.input_seletor:
                self.set_state(GS_COUNTDOWN)

        elif self.state == GS_COUNTDOWN:
            if self.pediu_pausa():
                self.state_timer += 1
                return
            self.update_paddles(); self.draw_countdown()
            if self.state_timer >= (3*60 + 30 if self.phase_first_round else 45):
                self.phase_first_round = False
                self.set_state(GS_PLAY)

        elif self.state == GS_PLAY:
            if self.pediu_pausa():
                self.state_timer += 1
                return
            self.update_paddles()
            self.physics()
            if self.state == GS_PLAY:
                bonus = self.phase.update(self.ball_x, self.ball_y,
                                          self.paddle_pos, self.last_hitter)
                for p in (0, 1):
                    if bonus[p]:
                        self.total_score[p] += bonus[p]   # so no total geral
                        self.total_flash[p] = TOTAL_FLASH_FRAMES
            self.draw_play()

        elif self.state == GS_PAUSE:
            d0 = self.input_pot[0] - self.pause_pot_ref[0]
            d1 = self.input_pot[1] - self.pause_pot_ref[1]
            p = 0 if abs(d0) >= abs(d1) else 1
            d = d0 if p == 0 else d1
            if d < -PAUSE_POT_STEP:
                self.pause_sel = 0
                self.pause_pot_ref[p] = self.input_pot[p]
            elif d > PAUSE_POT_STEP:
                self.pause_sel = 1
                self.pause_pot_ref[p] = self.input_pot[p]
            if self.input_seletor or self.state_timer >= PAUSE_TIMEOUT_S * 60:
                if self.pause_sel == 0:
                    self.paddle_travado = [True, self.mode == MODE_VERSUS]
                    self.phase_first_round = False
                    self.set_state(GS_COUNTDOWN)
                else:
                    self.enter_attract()
                self.state_timer += 1
                return
            self.draw_pause()

        elif self.state == GS_ROUND_END:
            self.update_paddles(); self.draw_round_end()
            if self.state_timer >= 60:
                if max(self.phase_score) >= PHASE_WIN_SCORE:
                    self.set_state(GS_PHASE_END)
                else:
                    self.reset_round(self.last_scorer)
                    self.set_state(GS_COUNTDOWN)

        elif self.state == GS_PHASE_END:
            self.draw_phase_end()
            if self.state_timer >= 3*60 or self.input_seletor:
                if not self.fim_de_jogo():
                    venceu = 1 if self.phase_score[0] > self.phase_score[1] else 2
                    self.begin_phase(self.phase_idx + 1, venceu)
                    self.set_state(GS_PHASE_INTRO)
                else:
                    self.arcade_perdeu = (self.mode == MODE_ARCADE and
                                          self.phase_score[1] >= PHASE_WIN_SCORE)
                    if self.arcade_perdeu:
                        self.last_winner = 2
                    elif self.total_score[0] == self.total_score[1]:
                        self.last_winner = 0
                    else:
                        self.last_winner = 1 if self.total_score[0] > self.total_score[1] else 2
                    self.set_state(GS_GAME_OVER)

        elif self.state == GS_GAME_OVER:
            self.draw_game_over()
            if self.state_timer >= 4*60 or self.input_seletor:
                self.initials_player = self.hiscore_player()
                pts = self.total_score[self.initials_player - 1]
                if self.hi_qualifies(pts):
                    self.initials_armed = False
                    self.set_state(GS_ENTER_INITIALS)
                else:
                    self.set_state(GS_HIGH_SCORES)

        elif self.state == GS_ENTER_INITIALS:
            if not self.initials_armed:
                self.initials_buf = [" "] * INITIALS_LEN
                self.initials_slot = 0
                self.initials_armed = True
            if self.initials_slot < INITIALS_LEN:
                pot = self.input_pot[self.initials_player - 1]
                idx = max(0, min(25, (pot * 26) // 4096))
                self.initials_buf[self.initials_slot] = chr(ord("A") + idx)
                if self.input_seletor:
                    self.initials_slot += 1
            else:
                self.grava_iniciais()
                self.state_timer += 1
                return
            if self.state_timer >= INITIALS_TIMEOUT_S * 60:
                self.grava_iniciais()
                self.state_timer += 1
                return
            self.draw_enter_initials()

        elif self.state == GS_HIGH_SCORES:
            self.update_paddles_demo()
            self.draw_highscores()
            if self.input_seletor:
                self.open_menu()
                self.state_timer += 1
                return
            if self.state_timer >= 10*60:
                self.enter_attract()

        self.state_timer += 1
        self.frames_globais += 1
        for p in (0, 1):
            if self.total_flash[p] > 0:
                self.total_flash[p] -= 1

# ============================================================
# Captura das telas (headless): usado para as imagens do README
# ============================================================
SHOTS = ["attract", "menu", "phase_intro", "countdown", "play", "pause",
         "play_triplo", "play_nave", "play_barreira1", "play_pinball",
         "play_barreira2", "play_coluna", "play_muralha", "play_rebound",
         "play_barreira3", "phase_end", "game_over", "enter_initials",
         "highscores"]

FASE_DO_SHOT = {
    "play_nave": PHASE_NAVE, "play_triplo": PHASE_TRIPLO,
    "play_barreira1": PHASE_BARREIRA1, "play_pinball": PHASE_PINBALL,
    "play_barreira2": PHASE_BARREIRA2, "play_coluna": PHASE_COLUNA,
    "play_muralha": PHASE_MURALHA, "play_rebound": PHASE_REBOUND,
    "play_barreira3": PHASE_BARREIRA3,
}

def save_shots(outdir, assets, glyphs):
    import pygame
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    pygame.init()
    palette = [(0, 0, 0)] * 256
    palette[255] = (240, 240, 240)

    for name in SHOTS:
        g = Game(assets, glyphs)
        random.seed(7)
        g.mode = MODE_ARCADE
        g.total_score = [14, 11]
        g.phase_score = [6, 4]
        g.paddle_pos = [70, 84]
        g.ball_x, g.ball_y = 150 << 8, 96 << 8

        if name in FASE_DO_SHOT:
            idx = FASE_DO_SHOT[name]
            g.phase_idx = idx
            g.phase.begin(idx)
            if idx == PHASE_BARREIRA3:
                g.ball_x = 100 << 8                     # barreira ainda intacta
            elif idx == PHASE_BARREIRA2:
                for c in range(len(g.phase.alive)):
                    for r in (12, 13):
                        g.phase.alive[c] &= ~(1 << r)
                g.ball_x = 90 << 8
            elif idx == PHASE_NAVE:
                g.phase.nave_y = 70
                g.phase.shots = [{"x": 90, "y": 78, "vx": -SHOT_SPEED},
                                 {"x": 170, "y": 78, "vx": SHOT_SPEED}]
                g.phase.shrink[0] = 60
            elif idx == PHASE_REBOUND:
                r = g.phase.paddle_range()
                g.paddle_pos = [r // 3, r // 2]
                g.ball_x, g.ball_y = 80 << 8, 60 << 8
            if g.phase.flags & PF_TEM_BONUS:      # mostra a nave-bonus na foto
                g.phase.bonus_on = True
                g.phase.bonus_x_q, g.phase.bonus_y_q = 70 << 8, 120 << 8
                g.phase.bonus_vx_q, g.phase.bonus_vy_q = BONUS_VX_MIN_Q, -BONUS_VY_Q
            g.draw_play()
        elif name == "attract":
            g.update_paddles_demo(); g.draw_attract()
        elif name == "menu":
            g.menu_sel = MODE_VERSUS
            g.paddle_pos = [130, 150]
            g.draw_menu()
        elif name == "phase_intro":
            g.phase_idx = PHASE_COLUNA
            g.phase.begin(PHASE_COLUNA)
            g.draw_phase_intro()
        elif name == "countdown":
            g.phase_idx = PHASE_BARREIRA3
            g.phase.begin(PHASE_BARREIRA3)
            g.phase_first_round = True; g.state_timer = 30; g.draw_countdown()
        elif name == "play":
            g.phase.bonus_on = True
            g.phase.bonus_x_q, g.phase.bonus_y_q = 60 << 8, 110 << 8
            g.draw_play()
        elif name == "pause":
            g.pause_sel = 0
            g.draw_pause()
        elif name == "phase_end":
            g.phase_idx = PHASE_TRIPLO; g.phase.begin(PHASE_TRIPLO)
            g.phase_score = [9, 7]
            g.total_score = [18, 15]      # coerente: fase 1 deu 8x8
            g.draw_phase_end()
        elif name == "game_over":
            g.phase_idx = PHASE_COUNT - 1
            g.total_score = [63, 55]; g.last_winner = 1; g.draw_game_over()
        elif name == "enter_initials":
            g.initials_player = 1
            g.initials_buf = ["L", "E", " "]
            g.initials_slot = 2
            g.total_score = [63, 55]
            g.draw_enter_initials()
        elif name == "highscores":
            g.hiscores = [(63, 1, "LEO", MODE_ARCADE), (58, 2, "ANA", MODE_VERSUS),
                          (44, 1, "BIA", MODE_ARCADE), (31, 1, "JOA", MODE_VERSUS),
                          (12, 2, "PED", MODE_ARCADE)]
            g.draw_highscores()

        surf = pygame.image.frombuffer(bytes(g.fb.px), (FB_W, FB_H), "P")
        surf.set_palette(palette)
        surf = pygame.transform.scale(surf, (FB_W * SCALE, FB_H * SCALE))
        path = outdir / f"sim_{name}.png"
        pygame.image.save(surf, str(path))
        print(f"gerado: {path}")
    pygame.quit()

# ============================================================
# Main loop com pygame
# ============================================================
def main():
    ap = argparse.ArgumentParser(description="Simulador do RetroSC Pong")
    ap.add_argument("--shots", metavar="DIR",
                    help="renderiza as telas em PNG e sai (headless)")
    ap.add_argument("--fase", type=int, default=None,
                    help=f"comeca direto nesta fase (0..{PHASE_COUNT - 1})")
    args = ap.parse_args()

    assets = load_assets()
    glyphs = load_font()

    if args.shots:
        os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
        save_shots(args.shots, assets, glyphs)
        return

    print(__doc__)
    import pygame
    game = Game(assets, glyphs)
    if args.fase is not None:
        game.begin_game(MODE_ARCADE)
        game.begin_phase(max(0, min(PHASE_COUNT - 1, args.fase)), 1)
        game.set_state(GS_COUNTDOWN)

    pygame.init()
    win = pygame.display.set_mode((FB_W * SCALE, FB_H * SCALE))
    pygame.display.set_caption("RetroSC Pong — Simulador")
    clock = pygame.time.Clock()
    palette = [(0, 0, 0)] * 256
    palette[0] = (0, 0, 0)
    palette[255] = (240, 240, 240)

    running = True
    while running:
        game.input_seletor = False
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                running = False
            elif ev.type == pygame.KEYDOWN:
                if ev.key in (pygame.K_ESCAPE, pygame.K_q):
                    running = False
                elif ev.key == pygame.K_SPACE:
                    game.input_seletor = True
                elif ev.key == pygame.K_n:
                    # pula fase (debug): encerra a fase atual na hora
                    if game.state in (GS_PLAY, GS_COUNTDOWN, GS_ROUND_END):
                        game.set_state(GS_PHASE_END)
                elif ev.key == pygame.K_r:
                    game.hiscores = [(0, 0, "   ", 0)] * HISCORE_COUNT
                    print("Highscores zeradas.")

        # mapeia mouse Y para pots
        mx, my = pygame.mouse.get_pos()
        ny = max(0, min(FB_H * SCALE - 1, my))
        pot = (ny * 4095) // (FB_H * SCALE - 1)
        if mx < FB_W * SCALE // 2:
            game.input_pot[0] = pot
        else:
            game.input_pot[1] = pot

        game.poll_input()
        game.frame()

        # copia framebuffer (bytearray) para surface 1-byte-per-pixel e escala
        surf_pix = pygame.image.frombuffer(bytes(game.fb.px), (FB_W, FB_H), "P")
        surf_pix.set_palette(palette)
        scaled = pygame.transform.scale(surf_pix, (FB_W * SCALE, FB_H * SCALE))
        win.blit(scaled, (0, 0))
        pygame.display.flip()
        clock.tick(60)

    pygame.quit()

if __name__ == "__main__":
    main()
