// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#include "game.h"

#include <stdio.h>

#include "config.h"
#include "gfx.h"
#include "font.h"
#include "input.h"
#include "audio.h"
#include "highscores.h"
#include "assets.h"
#include "phases.h"

#include "pico/stdlib.h"
#include "pico/rand.h"

// =============================================================
// Estado do jogo
// =============================================================
static game_state_t state;
static int          state_timer;  // contador de frames dentro do estado

static game_mode_t  mode;         // arcade (1 jogador) ou versus (2 jogadores)
static int          menu_sel;     // item destacado no menu do attract
static int          menu_idle;    // frames sem tocar em nada dentro do menu
static int          pause_sel;      // 0 = continuar, 1 = sair do jogo
static int          pause_pot_ref[2];  // pots no instante em que a pausa abriu
static uint32_t     frames_globais;  // contador continuo, para textos piscando

static int phase_idx;             // fase atual (0..PHASE_COUNT-1)
static int phase_score[2];        // pontos na fase corrente (vai ate PHASE_WIN_SCORE)
static int total_score[2];        // soma dos pontos de todas as fases
static bool phase_first_round;    // primeiro ponto da fase (contagem longa)

// Posicao lida do pot, 0..phase_paddle_range(). Em quase toda fase e o Y do
// topo da raquete; no REBOUND e o X dela dentro da meia-quadra.
static int paddle_pos[2];
static bool paddle_travado[2];    // esperando o pot voltar (saida da pausa)
static int last_scorer;           // 1 ou 2: quem fez o ultimo ponto
static int last_hitter;           // 0, 1 ou -1: quem rebateu a bola por ultimo
static int last_winner;           // 0 = empate, 1 ou 2 ao terminar a partida
static bool arcade_perdeu;        // no arcade, a CPU fechou uma fase: acabou
static int ai_bias;               // erro de mira da CPU, sorteado a cada rebatida
static int total_flash[2];        // frames restantes do total piscando (bonus)
static int turbo_frames;          // pique de bola rapida que ainda falta (BONUS_TURBO)
static int aviso_tipo, aviso_jogador, aviso_frames;   // aviso do bonus na tela

// Estado da entrada de iniciais
static char initials_buf[INITIALS_LEN + 1];
static int  initials_letra;       // letra em foco (0..25), com freio
static int  initials_cool;        // frames que faltam para andar mais uma
static int  initials_slot;
static bool initials_armed;       // se ja iniciou o processo
static int  initials_player;      // quem digita (1 ou 2)

// Bola em ponto fixo Q8 (1 unidade = 1/256 pixel)
static int32_t ball_x, ball_y;
static int32_t ball_vx, ball_vy;
static int32_t ball_speed_q;    // |v| atual (fases sem gravidade)

// =============================================================
// Helpers
// =============================================================
static int abs_i(int v) { return v < 0 ? -v : v; }

static void set_state(game_state_t s) {
    state = s;
    state_timer = 0;
}

// Interpola em linha reta entre o valor da primeira fase e o da ultima. E o
// unico lugar que decide o quanto a CPU melhora ao longo do jogo.
static int ai_curva(int primeira, int ultima) {
    int n = PHASE_COUNT - 1;
    int i = phase_idx;
    if (i < 0) i = 0;
    if (i > n) i = n;
    return (n > 0) ? (primeira + (ultima - primeira) * i / n) : ultima;
}

static int ai_speed(void) { return ai_curva(AI_SPEED_MIN, AI_SPEED_MAX); }
static int ai_error(void) { return ai_curva(AI_ERROR_MAX_PX, AI_ERROR_MIN_PX); }

static void roll_ai_bias(void) {
    int e = ai_error();
    ai_bias = (int)(get_rand_32() % (2 * e + 1)) - e;
}

// Raiz quadrada inteira por busca crescente: so roda com valores pequenos
// (velocidade da bola em Q8) e evita puxar a libm para dentro do binario.
static int32_t vx_from_vy(int32_t vy_frac, int32_t speed_q) {
    int32_t s2  = speed_q * speed_q;
    int32_t vx2 = s2 - vy_frac * vy_frac;
    if (vx2 < 0) vx2 = 0;
    for (int32_t g = (speed_q >> 2); g <= speed_q; g++) {
        if (g * g >= vx2) return g;
    }
    return speed_q >> 1;
}

// Velocidade com que a bola sai de uma rebatida: a normal, ou a turbinada
// enquanto durar o pique do BONUS_TURBO. O teto TURBO_MAX_Q nao e estetico --
// acima dele a bola atravessa a raquete sem tocar nela (ver config.h).
static int32_t velocidade_saida(void) {
    if (turbo_frames <= 0) return ball_speed_q;
    int32_t v = ball_speed_q + TURBO_EXTRA_Q;
    return (v > TURBO_MAX_Q) ? TURBO_MAX_Q : v;
}

static void serve_ball(int direction /* -1 ou +1 */) {
    ball_x = phase_serve_x(direction) << 8;
    ball_y = phase_serve_y() << 8;
    ball_speed_q = BALL_SPEED_INIT_Q;
    turbo_frames = 0;
    last_hitter = -1;
    roll_ai_bias();

    if (phase_flags() & PF_GRAVITY) {
        // Volei: a bola so e solta; a gravidade faz o resto.
        ball_vx = direction * 0x60;
        ball_vy = 0;
        return;
    }
    // angulo aleatorio: vy entre -0.5 e +0.5 da velocidade
    int32_t r = (int32_t)(get_rand_32() & 0xFF) - 128;     // -128..127
    int32_t vy_frac = (r * ball_speed_q) / 256;
    ball_vx = direction * vx_from_vy(vy_frac, ball_speed_q);
    ball_vy = vy_frac;
}

static void reset_round(int scorer) {
    int mid = phase_paddle_range() / 2;
    paddle_pos[0] = mid;
    paddle_pos[1] = mid;
    paddle_travado[0] = paddle_travado[1] = false;
    aviso_frames = 0;
    phase_round_reset();
    // bola sai em direcao a quem perdeu o ponto
    int dir = (scorer == 1) ? +1 : -1;
    serve_ball(dir);
}

// 'scorer' = quem ganhou a fase anterior (1 ou 2). Como reset_round() saca em
// direcao a quem perdeu o ponto, a fase nova comeca com a bola indo para o
// lado de quem perdeu a fase passada. Na primeira fase nao ha anterior: sorteia.
static void begin_phase(int idx, int scorer) {
    phase_idx = idx;
    phase_score[0] = phase_score[1] = 0;
    phase_begin(idx);
    phase_first_round = true;
    last_scorer = scorer;
    reset_round(last_scorer);
}

static void begin_game(game_mode_t m) {
    mode = m;
    total_score[0] = total_score[1] = 0;
    total_flash[0] = total_flash[1] = 0;
    last_winner = 0;
    arcade_perdeu = false;
    begin_phase(0, (get_rand_32() & 1) ? 1 : 2);
}

// Maior numero de pontos que ainda esta em disputa nas fases apos 'idx': os
// PHASE_WIN_SCORE do placar de cada uma, mais as passagens do bonus so nas
// fases que o tem.
static int pontos_em_disputa(int idx) {
    int total = 0;
    for (int i = idx + 1; i < PHASE_COUNT; i++) {
        total += PHASE_WIN_SCORE;
        if (phase_tem_bonus(i)) total += BONUS_PASSES_MAX * BONUS_POINTS;
    }
    return total;
}

// No arcade a partida acaba na primeira fase que o jogador perder -- e ate
// onde ele chegou que vira o recorde. No versus jogam-se as PHASE_COUNT fases
// e ganha quem somar mais, mas nao faz sentido arrastar o resto quando um dos
// dois ja nao alcanca o outro nem ganhando tudo o que falta.
static bool fim_de_jogo(void) {
    if (mode == MODE_ARCADE && phase_score[1] >= PHASE_WIN_SCORE) return true;
    if (phase_idx + 1 >= PHASE_COUNT) return true;

    if (mode == MODE_VERSUS) {
        int dif = abs_i(total_score[0] - total_score[1]);
        // ">" e nao ">=": com a diferenca igual ao que resta o outro ainda
        // pode empatar, e ai a partida vale a pena.
        if (dif > pontos_em_disputa(phase_idx)) return true;
    }
    return false;
}

// Qual lado a tabela de recordes considera: no arcade so o humano (P1) entra
// no ranking; no versus, quem venceu.
static int hiscore_player(void) {
    if (mode == MODE_ARCADE) return 1;
    return (total_score[0] >= total_score[1]) ? 1 : 2;
}

// =============================================================
// Raquetes
// =============================================================
static void update_paddle_ai(int player) {
    int range = phase_paddle_range();
    rect_t seg[PADDLE_SEG_MAX];
    phase_paddle_segments(player, 0, seg);     // pos 0 => canto de partida

    int target;
    bool coming;
    if (phase_flags() & PF_PADDLE_HORIZ) {
        // Volei: persegue o X da bola enquanto ela estiver na meia-quadra dela.
        int bxc = (ball_x >> 8) + BALL_SIZE / 2;
        coming = (player == 1) ? (bxc >= FB_WIDTH / 2) : (bxc < FB_WIDTH / 2);
        target = bxc - seg[0].w / 2 - seg[0].x + ai_bias;
    } else {
        int span = FB_HEIGHT - range;          // altura ocupada pela raquete
        coming = (player == 1) ? (ball_vx > 0) : (ball_vx < 0);
        target = (ball_y >> 8) + BALL_SIZE / 2 - span / 2 + ai_bias;
    }
    if (!coming) target = range / 2;           // sem bola vindo, volta ao centro

    int diff  = target - paddle_pos[player];
    int speed = ai_speed();
    if (abs_i(diff) > speed) diff = (diff > 0) ? speed : -speed;
    paddle_pos[player] += diff;
    if (paddle_pos[player] < 0)     paddle_pos[player] = 0;
    if (paddle_pos[player] > range) paddle_pos[player] = range;
}

// Le o pot de um jogador respeitando a trava de retomada: enquanto o pot
// estiver longe de onde a raquete parou, a raquete nao se mexe.
static void update_paddle_humano(int player, int range) {
    int lido = input_paddle_y(player, range);
    // No volei o pot manda no eixo X, e X cresce para a direita enquanto Y
    // cresce para BAIXO. Alimentar os dois com o mesmo sinal faz o mesmo giro
    // do botao parecer certo numa fase e invertido na outra -- um dos eixos tem
    // que ser espelhado, e o espelhado e este, porque as outras nove fases sao
    // a referencia do jogador.
    if (phase_flags() & PF_PADDLE_HORIZ) lido = range - lido;
    if (paddle_travado[player]) {
        if (abs_i(lido - paddle_pos[player]) <= PADDLE_TAKEOVER_TOL)
            paddle_travado[player] = false;
        else
            return;                    // fica onde estava
    }
    paddle_pos[player] = lido;
}

static void update_paddles(void) {
    int range = phase_paddle_range();
    update_paddle_humano(0, range);
    if (mode == MODE_VERSUS) update_paddle_humano(1, range);
    else                     update_paddle_ai(1);
}

// Limite superior da zona de demo (abaixo do texto "RETRO PONG")
#define DEMO_PADDLE_Y_MIN 116

static void update_paddles_demo(void) {
    // AI simples: cada raquete persegue a bola lentamente.
    // Raquetes ficam restritas a metade inferior pra nao colidir com texto.
    for (int i = 0; i < 2; i++) {
        int target = (ball_y >> 8) - PADDLE_H / 2;
        int diff = target - paddle_pos[i];
        int speed = 2;
        if (abs_i(diff) > speed) diff = (diff > 0) ? speed : -speed;
        paddle_pos[i] += diff;
        if (paddle_pos[i] < DEMO_PADDLE_Y_MIN) paddle_pos[i] = DEMO_PADDLE_Y_MIN;
        if (paddle_pos[i] > FB_HEIGHT - PADDLE_H)
            paddle_pos[i] = FB_HEIGHT - PADDLE_H;
    }
}

// =============================================================
// Fisica e colisoes (chamado em GS_PLAY)
// =============================================================
static void on_paddle_hit(int player, const rect_t *seg) {
    // Inverte vx e ajusta vy conforme onde bateu NO PEDACO atingido: no
    // TRIPLO cada pedaco tem o mesmo leque de angulos da raquete inteira.
    int seg_center = seg->y + seg->h / 2;
    int by = (ball_y >> 8) + BALL_SIZE / 2;
    int offset = by - seg_center;

    if (ball_speed_q < BALL_SPEED_MAX_Q) ball_speed_q += BALL_SPEED_STEP_Q;
    // Turbo: cada toque de quem pegou o bonus relanca o pique de bola rapida.
    // Passado o pique ela desacelera sozinha e so acelera no proximo toque
    // dele -- por isso o cronometro do pique fica aqui e o dos 10 s na fase.
    if (phase_turbo(player)) turbo_frames = TURBO_HOLD_FRAMES;

    int32_t vel     = velocidade_saida();
    int32_t vy_frac = (offset * vel) / (seg->h ? seg->h : 1);
    int32_t vx_abs  = vx_from_vy(vy_frac, vel);
    ball_vx = (player == 0) ? +vx_abs : -vx_abs;
    ball_vy = vy_frac;
    roll_ai_bias();
    audio_paddle_hit();
}

// Toque de volei: a bola sobe sempre e vai para o campo do adversario; a borda
// em que ela bateu decide se o toque e curto ou longo.
static void on_volley_hit(int player, const rect_t *seg) {
    int bxc = (ball_x >> 8) + BALL_SIZE / 2;
    int offset = bxc - (seg->x + seg->w / 2);      // -w/2..+w/2
    int half = (seg->w / 2) ? (seg->w / 2) : 1;
    int frente = (player == 0) ? +1 : -1;          // para onde fica o adversario

    ball_vy = -VOLLEY_VY_Q;
    ball_vx = frente * VOLLEY_VX_BASE_Q +
              (frente * offset * VOLLEY_VX_SPREAD_Q) / half;
    roll_ai_bias();
    audio_paddle_hit();
}

static void add_point(int player) {
    phase_score[player]++;
    total_score[player]++;
    last_scorer = player + 1;
    audio_score();
    set_state(GS_ROUND_END);
}

static void physics(void) {
    uint32_t f = phase_flags();
    int32_t prev_x = ball_x, prev_y = ball_y;

    // Fim do pique do turbo: a bola volta a velocidade normal mantendo a
    // direcao. Sem reescalar aqui ela so desaceleraria na proxima rebatida.
    if (turbo_frames > 0) {
        int32_t vel = velocidade_saida();
        if (--turbo_frames == 0 && vel > 0) {
            int32_t vy = (ball_vy * ball_speed_q) / vel;
            int32_t vx = vx_from_vy(vy, ball_speed_q);
            ball_vy = vy;
            ball_vx = (ball_vx < 0) ? -vx : +vx;
        }
    }

    if (f & PF_GRAVITY) {
        ball_vy += GRAVITY_Q;
        if (ball_vy > BALL_VY_MAX_Q) ball_vy = BALL_VY_MAX_Q;
    }
    ball_x += ball_vx;
    ball_y += ball_vy;

    // Topo
    if (ball_y < 0) { ball_y = 0; ball_vy = -ball_vy; audio_wall_hit(); }

    // Fundo: no volei o chao vale ponto para o lado oposto ao da queda
    int32_t max_y = (int32_t)(FB_HEIGHT - BALL_SIZE) << 8;
    if (ball_y > max_y) {
        if (f & PF_FLOOR_SCORES) {
            int cx = (ball_x >> 8) + BALL_SIZE / 2;
            // Tira a bola da tela antes de pontuar: o GS_ROUND_END congela o
            // quadro, e parada em cima do chao ela ficava com uma tira de
            // pixels aparecendo na borda de baixo.
            ball_y = (int32_t)FB_HEIGHT << 8;
            add_point((cx < FB_WIDTH / 2) ? 1 : 0);
            return;
        }
        ball_y = max_y; ball_vy = -ball_vy; audio_wall_hit();
    }

    // Cenario da fase: tijolos, bumpers, rede
    if (phase_ball_collide(prev_x, prev_y, &ball_x, &ball_y, &ball_vx, &ball_vy)) {
        audio_brick_hit();
    }

    // Raquetes (uma ou tres por lado; deitadas no volei)
    bool horiz = (f & PF_PADDLE_HORIZ) != 0;
    int bx = ball_x >> 8;
    int by = ball_y >> 8;
    int prev_by = prev_y >> 8;
    rect_t seg[PADDLE_SEG_MAX];
    for (int p = 0; p < 2; p++) {
        if (horiz) {
            if (ball_vy <= 0) continue;             // so pega a bola caindo
        } else {
            if (p == 0 && ball_vx >= 0) continue;
            if (p == 1 && ball_vx <= 0) continue;
        }
        int n = phase_paddle_segments(p, paddle_pos[p], seg);
        for (int i = 0; i < n; i++) {
            const rect_t *s = &seg[i];
            if (bx >= s->x + s->w || bx + BALL_SIZE <= s->x) continue;
            if (horiz) {
                // Raquete deitada e fina (4 px): em queda rapida a bola pularia
                // por cima dela num frame so. Testa a TRAVESSIA do topo entre o
                // frame anterior e este, em vez da sobreposicao no instante.
                if (!(prev_by + BALL_SIZE <= s->y && by + BALL_SIZE >= s->y))
                    continue;
            } else if (by >= s->y + s->h || by + BALL_SIZE <= s->y) {
                continue;
            }
            if (horiz) {
                ball_y = (int32_t)(s->y - BALL_SIZE) << 8;
                on_volley_hit(p, s);
            } else {
                ball_x = (p == 0) ? ((int32_t)(s->x + s->w) << 8)
                                  : ((int32_t)(s->x - BALL_SIZE) << 8);
                on_paddle_hit(p, s);
            }
            last_hitter = p;
            break;
        }
    }

    // Laterais: gol, ou parede quando a fase e fechada (volei)
    if (f & PF_SIDE_WALLS) {
        int32_t max_x = (int32_t)(FB_WIDTH - BALL_SIZE) << 8;
        if (ball_x < 0)      { ball_x = 0;     ball_vx = -ball_vx; audio_wall_hit(); }
        if (ball_x > max_x)  { ball_x = max_x; ball_vx = -ball_vx; audio_wall_hit(); }
    } else {
        bx = ball_x >> 8;
        if (bx + BALL_SIZE < 0)   add_point(1);
        else if (bx > FB_WIDTH)   add_point(0);
    }
}

// =============================================================
// Desenho
// =============================================================
static void center_text(int y, const char *s, int scale) {
    int w = gfx_text_width(s, scale);
    gfx_text((FB_WIDTH - w) / 2, y, s, scale, 1);
}

static void right_text(int x_right, int y, const char *s, int scale) {
    gfx_text(x_right - gfx_text_width(s, scale), y, s, scale, 1);
}

// Texto centralizado com um retangulo preto por tras: sobre os tijolos das
// barreiras ou os obstaculos do pinball, texto branco sozinho some.
// Texto com fundo preto centrado em 'cx', aparado nas bordas da tela.
static void text_boxed_at(int cx, int y, const char *s, int scale) {
    const int margem = 4;
    int w = gfx_text_width(s, scale);
    int h = FONT_H * scale;
    int x = cx - w / 2;
    if (x < margem) x = margem;
    if (x > FB_WIDTH - w - margem) x = FB_WIDTH - w - margem;
    gfx_fill_rect(x - margem, y - margem, w + 2 * margem, h + 2 * margem, 0);
    gfx_text(x, y, s, scale, 1);
}

static void center_text_boxed(int y, const char *s, int scale) {
    text_boxed_at(FB_WIDTH / 2, y, s, scale);
}

static const char *p2_label(void) {
    return (mode == MODE_ARCADE) ? "CPU" : "P2";
}

static void draw_field(void) {
    if (!(phase_flags() & PF_NO_CENTER_LINE))
        gfx_dotted_vline(FB_WIDTH / 2, 0, FB_HEIGHT, 4, 4);
    phase_draw();
}

static void draw_paddles(void) {
    rect_t seg[PADDLE_SEG_MAX];
    for (int p = 0; p < 2; p++) {
        // Raquete travada pisca: e o aviso de "gire o pot de volta".
        if (paddle_travado[p] && ((frames_globais >> 3) & 1)) continue;
        int n = phase_paddle_segments(p, paddle_pos[p], seg);
        for (int i = 0; i < n; i++)
            gfx_fill_rect(seg[i].x, seg[i].y, seg[i].w, seg[i].h, 1);
    }
}

static void draw_ball(void) {
    gfx_fill_rect(ball_x >> 8, ball_y >> 8, BALL_SIZE, BALL_SIZE, 1);
}

// Bloco "TOTAL" com o numero centralizado embaixo. 'x' e o canto esquerdo.
#define TOTAL_LABEL "TOTAL"

static void draw_total_box(int x, int total, bool piscando) {
    char buf[8];
    int w_label = gfx_text_width(TOTAL_LABEL, 1);
    gfx_text(x, 10, TOTAL_LABEL, 1, 1);
    // Depois de um bonus da nave o numero pisca um pouco, para o jogador ver
    // que ganhou pontos sem que nada tenha mudado no placar da fase.
    if (piscando && ((frames_globais >> 2) & 1)) return;
    snprintf(buf, sizeof(buf), "%d", total);
    gfx_text(x + (w_label - gfx_text_width(buf, 1)) / 2, 19, buf, 1, 1);
}

// Placar da fase em corpo grande e, do lado de fora dele, o total acumulado
// das fases -- na mesma faixa de altura, para nao ocupar mais uma linha da
// quadra. Tudo fica fora da faixa central, que pode estar tomada por tijolos
// ou obstaculos.
static void draw_scores(void) {
    char fase[8];
    int cx = FB_WIDTH / 2;
    const int y_grande = 8;
    int w_label = gfx_text_width(TOTAL_LABEL, 1);

    snprintf(fase, sizeof(fase), "%d", phase_score[0]);
    right_text(cx - 30, y_grande, fase, 3);
    draw_total_box(cx - 30 - gfx_text_width(fase, 3) - 8 - w_label,
                   total_score[0], total_flash[0] > 0);

    snprintf(fase, sizeof(fase), "%d", phase_score[1]);
    gfx_text(cx + 30, y_grande, fase, 3, 1);
    draw_total_box(cx + 30 + gfx_text_width(fase, 3) + 8,
                   total_score[1], total_flash[1] > 0);
}

// =============================================================
// Telas
// =============================================================
static void draw_attract_background(void) {
    gfx_clear(0);

    // Logo RetroSC (alto-res mono, 220x69) centralizado no topo
    int lx = (FB_WIDTH - RETROSC_LOGO_W) / 2;
    gfx_blit(retrosc_logo_data, RETROSC_LOGO_W, RETROSC_LOGO_H, lx, 4, 1);

    // Titulo "RETRO PONG" abaixo do logo
    center_text(4 + RETROSC_LOGO_H + 8, "RETRO PONG", 3);

    // Demo do jogo no fundo
    gfx_dotted_vline(FB_WIDTH / 2, FB_HEIGHT - 60, FB_HEIGHT - 12, 4, 4);
    gfx_fill_rect(PADDLE_MARGIN, paddle_pos[0], PADDLE_W, PADDLE_H, 1);
    gfx_fill_rect(FB_WIDTH - PADDLE_MARGIN - PADDLE_W, paddle_pos[1], PADDLE_W, PADDLE_H, 1);
    draw_ball();
}

static void draw_attract(void) {
    draw_attract_background();

    // Chamada piscante a cada ~32 frames
    if (((state_timer >> 5) & 1) == 0) {
        center_text(FB_HEIGHT - 9, "APERTE O SELETOR", 1);
    }
}

// O menu so existe depois que o jogador aperta o SELETOR no attract.
static void draw_menu(void) {
    draw_attract_background();

    const int bx = 24, by = 108;
    const int bw = FB_WIDTH - 2 * bx, bh = 60;

    gfx_fill_rect(bx, by, bw, bh, 0);            // tampa a demo atras do menu
    gfx_hline(bx, by, bw, 1);
    gfx_hline(bx, by + bh - 1, bw, 1);
    gfx_vline(bx, by, bh, 1);
    gfx_vline(bx + bw - 1, by, bh, 1);

    for (int i = 0; i < MODE_COUNT; i++) {
        const char *label = (i == MODE_ARCADE) ? "MODO ARCADE" : "MODO VERSUS";
        int w  = gfx_text_width(label, 2);
        int tx = (FB_WIDTH - w) / 2;
        int ty = by + 7 + i * 24;
        if (i == menu_sel) {
            // item selecionado em video reverso (jeito arcade de destacar)
            gfx_fill_rect(tx - 6, ty - 3, w + 12, FONT_CELL_H * 2 + 5, 1);
            gfx_text(tx, ty, label, 2, 0);
        } else {
            gfx_text(tx, ty, label, 2, 1);
        }
    }

    // Fundo preto: a linha central pontilhada da quadra do demo desce ate
    // FB_HEIGHT-12 e cruza esta legenda. Como as duas sao centradas, o traco
    // caia sempre na letra do meio da frase -- em "2 JOGADORES" era o A, que
    // aparecia com o vao interno preenchido e um pixel solto logo acima.
    center_text_boxed(by + bh + 5,
                      (menu_sel == MODE_ARCADE) ? "1 JOGADOR CONTRA A CPU"
                                                : "2 JOGADORES",
                      1);
    if (((state_timer >> 4) & 1) == 0) {
        center_text(FB_HEIGHT - 9, "SELETOR CONFIRMA", 1);
    }
}

static void draw_phase_intro(void) {
    char buf[32];
    gfx_clear(0);

    snprintf(buf, sizeof(buf), "FASE %d", phase_idx + 1);
    center_text(28, buf, 3);
    center_text(64, phase_name(phase_idx), 2);
    center_text(92, phase_hint(phase_idx), 1);

    snprintf(buf, sizeof(buf), "TOTAL  P1 %d   %s %d",
             total_score[0], p2_label(), total_score[1]);
    center_text(124, buf, 1);

    if (((state_timer >> 4) & 1) == 0) {
        center_text(FB_HEIGHT - 16,
                    (mode == MODE_ARCADE) ? "MODO ARCADE" : "MODO VERSUS", 1);
    }
}

static void draw_countdown(void) {
    gfx_clear(0);
    draw_field();
    draw_scores();
    draw_paddles();

    if (!phase_first_round) {
        center_text_boxed(FB_HEIGHT / 2 - 10, "GO", 3);
        return;
    }
    int sec_left = 3 - state_timer / 60;
    if (sec_left > 0) {
        char buf[2] = { (char)('0' + sec_left), 0 };
        center_text_boxed(FB_HEIGHT / 2 - 10, buf, 3);
    } else {
        center_text_boxed(FB_HEIGHT / 2 - 10, "GO", 3);
    }
}

static void draw_play(void) {
    gfx_clear(0);
    draw_field();
    draw_scores();
    draw_paddles();
    draw_ball();
    // Qual bonus saiu, do lado de quem pegou: com cinco tipos o total piscando
    // ja nao diz o que aconteceu. Fica abaixo do placar e fora do meio da
    // quadra, onde as fases de barreira tem tijolo.
    if (aviso_frames > 0)
        text_boxed_at((aviso_jogador == 1) ? (3 * FB_WIDTH / 4) : (FB_WIDTH / 4),
                      40, bonus_nome(aviso_tipo), 1);
}

static void draw_round_end(void) {
    draw_play();   // congelado
}

// Pausa: o jogo fica congelado atras de uma caixa com as duas saidas.
static void draw_pause(void) {
    draw_play();

    const int bx = 40, by = 56;
    const int bw = FB_WIDTH - 2 * bx, bh = 84;

    gfx_fill_rect(bx, by, bw, bh, 0);
    gfx_hline(bx, by, bw, 1);
    gfx_hline(bx, by + bh - 1, bw, 1);
    gfx_vline(bx, by, bh, 1);
    gfx_vline(bx + bw - 1, by, bh, 1);

    center_text(by + 6, "PAUSA", 3);

    // A tela nao pode ficar parada para sempre: mostra quanto falta para a
    // opcao destacada valer sozinha.
    char buf[24];
    int seg = (PAUSE_TIMEOUT_S * 60 - state_timer + 59) / 60;
    if (seg < 0) seg = 0;
    snprintf(buf, sizeof(buf), "%s EM %dS",
             (pause_sel == 0) ? "VOLTA" : "SAI", seg);
    center_text_boxed(by + bh + 4, buf, 1);   // fica fora da caixa, sobre a quadra

    for (int i = 0; i < 2; i++) {
        const char *label = (i == 0) ? "CONTINUAR" : "SAIR DO JOGO";
        int w  = gfx_text_width(label, 2);
        int tx = (FB_WIDTH - w) / 2;
        int ty = by + 36 + i * 24;
        if (i == pause_sel) {
            gfx_fill_rect(tx - 6, ty - 3, w + 12, FONT_CELL_H * 2 + 5, 1);
            gfx_text(tx, ty, label, 2, 0);
        } else {
            gfx_text(tx, ty, label, 2, 1);
        }
    }
}

static void draw_phase_end(void) {
    char buf[32];
    gfx_clear(0);

    snprintf(buf, sizeof(buf), "FASE %d COMPLETA", phase_idx + 1);
    center_text(24, buf, 2);
    center_text(48, phase_name(phase_idx), 1);

    snprintf(buf, sizeof(buf), "P1 %d   X   %d %s",
             phase_score[0], phase_score[1], p2_label());
    center_text(80, buf, 2);

    snprintf(buf, sizeof(buf), "TOTAL  P1 %d   %s %d",
             total_score[0], p2_label(), total_score[1]);
    center_text(116, buf, 1);

    if (((state_timer >> 4) & 1) == 0) {
        if (mode == MODE_ARCADE && phase_score[1] >= PHASE_WIN_SCORE) {
            center_text(148, "A CPU FECHOU A FASE", 1);
        } else if (!fim_de_jogo()) {
            snprintf(buf, sizeof(buf), "PROXIMA: %s", phase_name(phase_idx + 1));
            center_text(148, buf, 1);
        } else if (phase_idx + 1 < PHASE_COUNT) {
            center_text(148, "VANTAGEM DECISIVA", 1);
        } else {
            center_text(148, "FIM DE JOGO", 1);
        }
    }
}

static void draw_game_over(void) {
    char buf[32];
    gfx_clear(0);

    center_text(24, "FIM DE JOGO", 2);

    if (last_winner == 0) {
        center_text(64, "EMPATE", 3);
    } else if (mode == MODE_ARCADE) {
        center_text(64, (last_winner == 1) ? "VOCE VENCEU" : "A CPU VENCEU", 2);
    } else {
        snprintf(buf, sizeof(buf), "JOGADOR %d VENCE", last_winner);
        center_text(64, buf, 2);
    }

    snprintf(buf, sizeof(buf), "P1 %d   X   %d %s",
             total_score[0], total_score[1], p2_label());
    center_text(104, buf, 2);
    if (mode == MODE_ARCADE) {
        snprintf(buf, sizeof(buf), "CHEGOU ATE A FASE %d DE %d",
                 phase_idx + 1, PHASE_COUNT);
    } else {
        // No versus a partida pode ter fechado antes da ultima fase.
        snprintf(buf, sizeof(buf), "SOMA DE %d FASES", phase_idx + 1);
    }
    center_text(132, buf, 1);
}

static void draw_highscores(void) {
    gfx_clear(0);
    center_text(8, "HIGH SCORES", 2);
    const hi_table_t *t = hi_get();
    int y = 36;
    for (int i = 0; i < HISCORE_COUNT; i++) {
        char buf[24];
        if (t->entries[i].score > 0) {
            snprintf(buf, sizeof(buf), "%d. %c%c%c %3d %s",
                     i + 1,
                     t->entries[i].initials[0] ? t->entries[i].initials[0] : ' ',
                     t->entries[i].initials[1] ? t->entries[i].initials[1] : ' ',
                     t->entries[i].initials[2] ? t->entries[i].initials[2] : ' ',
                     t->entries[i].score,
                     (t->entries[i].mode == MODE_ARCADE) ? "ARCADE" : "VERSUS");
        } else {
            snprintf(buf, sizeof(buf), "%d. ---   -", i + 1);
        }
        gfx_text(52, y, buf, 1, 1);
        y += 14;
    }
    // Rodape alternando: a tabela faz parte do attract, entao vale lembrar
    // que dali tambem se comeca um jogo.
    center_text(FB_HEIGHT - 12,
                ((state_timer >> 6) & 1) ? "APERTE O SELETOR" : "RETRO PONG", 1);
}

// =============================================================
// Tela de entrada de iniciais (3 letras, controlado pelo vencedor)
// =============================================================
// Letra que o pot esta apontando, com a banda da letra ATUAL alargada para os
// dois lados: so troca quem sair dela de verdade.
static int letra_do_pot(int pot_val, int atual) {
    const int larg = 4096 / 26;                  // 157 contagens por letra
    if (atual >= 0 && atual <= 25) {
        int lo = atual * larg - INITIALS_HIST;
        int hi = (atual + 1) * larg + INITIALS_HIST;
        if (pot_val >= lo && pot_val < hi) return atual;
    }
    int idx = (pot_val * 26) / 4096;
    if (idx < 0) idx = 0;
    if (idx > 25) idx = 25;
    return idx;
}

static void draw_enter_initials(void) {
    gfx_clear(0);
    center_text(8, "NEW HIGH SCORE!", 2);

    // info de quem esta digitando
    char info[24];
    snprintf(info, sizeof(info), "%s - %d PONTOS",
             (initials_player == 1) ? "JOGADOR 1" : "JOGADOR 2",
             total_score[initials_player - 1]);
    center_text(32, info, 1);

    // 3 slots de iniciais centralizados, escala 4
    int slot_w = FONT_CELL_W * 4;
    int gap    = 8;
    int total_w = 3 * slot_w + 2 * gap;
    int x0 = (FB_WIDTH - total_w) / 2;
    int y0 = 64;

    for (int i = 0; i < INITIALS_LEN; i++) {
        int sx = x0 + i * (slot_w + gap);
        char c = initials_buf[i];
        char s[2] = { c ? c : ' ', 0 };
        // se ainda nao confirmou e nao e a vez, mostra '_'
        if (c == 0 && i != initials_slot) { s[0] = '_'; }
        // slot atual pisca enquanto o jogador rola pelo pot
        bool show = true;
        if (i == initials_slot && ((state_timer >> 3) & 1)) show = false;
        if (show) gfx_text(sx, y0, s, 4, 1);
        // underline embaixo do slot atual
        if (i == initials_slot) {
            gfx_fill_rect(sx, y0 + 8 * 4 + 2, slot_w - 4, 2, 1);
        }
    }

    center_text(FB_HEIGHT - 30, "POT = LETRA", 1);
    if (((state_timer >> 5) & 1) == 0) {
        center_text(FB_HEIGHT - 18, "SELETOR PARA CONFIRMAR", 1);
    }
    char hint[32];
    int seg = (INITIALS_TIMEOUT_S * 60 - state_timer + 59) / 60;
    if (seg < 0) seg = 0;
    snprintf(hint, sizeof(hint), "P%d - GRAVA EM %dS", initials_player, seg);
    center_text(FB_HEIGHT - 8, hint, 1);
}

// =============================================================
// Frame de cada estado
// =============================================================
static void enter_attract(void) {
    input_reset_movement();
    phase_begin(PHASE_CLASSICO);
    set_state(GS_ATTRACT);
    // posicao inicial da bola no espaco do demo
    ball_x = (FB_WIDTH / 2) << 8;
    ball_y = ((FB_HEIGHT - 40)) << 8;
    ball_vx = BALL_SPEED_INIT_Q;
    ball_vy = BALL_SPEED_INIT_Q / 2;
}

static void open_menu(void) {
    menu_sel  = MODE_ARCADE;
    menu_idle = 0;
    input_reset_movement();
    audio_attract_tick();
    set_state(GS_MENU);
}

// SELETOR no meio da partida abre a pausa (continuar / sair do jogo).
static bool pediu_pausa(void) {
    if (!input_seletor_pressed()) return false;
    pause_sel = 0;                       // sempre abre em CONTINUAR
    pause_pot_ref[0] = input_pot_raw(0);
    pause_pot_ref[1] = input_pot_raw(1);
    input_reset_movement();
    audio_attract_tick();
    set_state(GS_PAUSE);
    return true;
}

// Anima a bola do demo dentro da faixa de baixo do attract.
static void demo_ball_step(void) {
    if (ball_x < ((PADDLE_MARGIN + PADDLE_W) << 8)) {
        ball_vx = abs_i(ball_vx);
    }
    if (ball_x > ((FB_WIDTH - PADDLE_MARGIN - PADDLE_W - BALL_SIZE) << 8)) {
        ball_vx = -abs_i(ball_vx);
    }
    int32_t min_y = (int32_t)(FB_HEIGHT - 70) << 8;
    int32_t max_y = (int32_t)(FB_HEIGHT - 6 - BALL_SIZE) << 8;
    if (ball_y < min_y) { ball_y = min_y; ball_vy = abs_i(ball_vy); }
    if (ball_y > max_y) { ball_y = max_y; ball_vy = -abs_i(ball_vy); }
    ball_x += ball_vx;
    ball_y += ball_vy;
}

static void frame_attract(void) {
    update_paddles_demo();
    demo_ball_step();

    // O menu so aparece depois do SELETOR -- mexer no pot nao abre nada.
    if (input_seletor_pressed()) {
        open_menu();
        return;
    }
    // Sem ninguem por perto, o attract reveza com a tabela de recordes.
    if (state_timer >= ATTRACT_TIMEOUT_S * 60) {
        set_state(GS_HIGH_SCORES);
        return;
    }
    draw_attract();
}

static void frame_menu(void) {
    update_paddles_demo();
    demo_ball_step();

    // Selecao pelo pot de qualquer um dos dois jogadores (o ultimo que mexeu).
    // A zona morta no meio evita o item ficar piscando entre um e outro.
    int pot  = input_pot_raw(input_last_moved());
    int prev = menu_sel;
    if (pot < 2048 - 300)      menu_sel = MODE_ARCADE;
    else if (pot > 2048 + 300) menu_sel = MODE_VERSUS;
    if (menu_sel != prev) audio_attract_tick();

    if (input_seletor_pressed()) {
        audio_confirm();
        begin_game((game_mode_t)menu_sel);
        set_state(GS_PHASE_INTRO);
        return;
    }
    // O menu so desiste depois de MENU_TIMEOUT_S sem ninguem encostar nos pots.
    menu_idle = input_movement_detected() ? 0 : (menu_idle + 1);
    if (menu_idle >= MENU_TIMEOUT_S * 60) {
        enter_attract();
        return;
    }
    draw_menu();
}

static void frame_phase_intro(void) {
    draw_phase_intro();
    if (state_timer >= 150 || input_seletor_pressed()) {
        set_state(GS_COUNTDOWN);
    }
}

static void frame_countdown(void) {
    if (pediu_pausa()) return;
    update_paddles();
    draw_countdown();
    int len = phase_first_round ? (3 * 60 + 30) : 45;
    if (state_timer >= len) {
        phase_first_round = false;
        set_state(GS_PLAY);
    }
}

// Aplica o item destacado da pausa (pelo SELETOR ou pelo fim da contagem).
static void confirma_pausa(void) {
    if (pause_sel == 0) {
        // Volta com um "GO" curto, para ninguem ser pego de surpresa. As
        // raquetes ficam travadas ate cada pot voltar ao lugar em que
        // estava, senao a pausa viraria uma forma de salvar a bola.
        // So a raquete de quem joga com o pot: no arcade a da direita e da
        // CPU e nada limparia a trava -- ela ficaria piscando a toa.
        paddle_travado[0] = true;
        paddle_travado[1] = (mode == MODE_VERSUS);
        phase_first_round = false;
        set_state(GS_COUNTDOWN);
    } else {
        enter_attract();
    }
}

static void frame_pause(void) {
    // Troca de item por movimento relativo: girar o pot para cima marca
    // CONTINUAR, para baixo marca SAIR. Vale qualquer um dos dois pots.
    int d0 = input_pot_raw(0) - pause_pot_ref[0];
    int d1 = input_pot_raw(1) - pause_pot_ref[1];
    int p  = (abs_i(d0) >= abs_i(d1)) ? 0 : 1;
    int d  = (p == 0) ? d0 : d1;
    int prev = pause_sel;
    if (d < -PAUSE_POT_STEP) {
        pause_sel = 0;
        pause_pot_ref[p] = input_pot_raw(p);
    } else if (d > PAUSE_POT_STEP) {
        pause_sel = 1;
        pause_pot_ref[p] = input_pot_raw(p);
    }
    if (pause_sel != prev) audio_attract_tick();

    if (input_seletor_pressed()) {
        audio_confirm();
        confirma_pausa();
        return;
    }
    if (state_timer >= PAUSE_TIMEOUT_S * 60) {
        confirma_pausa();
        return;
    }
    draw_pause();
}

static void frame_play(void) {
    if (pediu_pausa()) return;
    update_paddles();
    physics();

    // Bichos da fase: o mascote paga um bonus sorteado a quem acertou. Quando
    // ele e de pontos, vai so para o total geral, sem mexer no placar da fase;
    // os outros quatro sao efeitos cronometrados dentro da propria fase.
    if (state == GS_PLAY) {
        bonus_out_t b;
        phase_update(ball_x, ball_y, paddle_pos, last_hitter, &b);
        for (int p = 0; p < 2; p++) {
            if (b.pontos[p] <= 0) continue;
            total_score[p] += b.pontos[p];
            total_flash[p]  = TOTAL_FLASH_FRAMES;
        }
        if (b.tipo >= 0) {
            aviso_tipo    = b.tipo;
            aviso_jogador = b.jogador;
            aviso_frames  = BONUS_AVISO_FRAMES;
            audio_confirm();
        }
    }
    if (aviso_frames > 0) aviso_frames--;
    draw_play();
}

static void frame_round_end(void) {
    update_paddles();
    draw_round_end();
    if (state_timer >= 60) {
        if (phase_score[0] >= PHASE_WIN_SCORE || phase_score[1] >= PHASE_WIN_SCORE) {
            set_state(GS_PHASE_END);
        } else {
            reset_round(last_scorer);
            set_state(GS_COUNTDOWN);
        }
    }
}

static void frame_phase_end(void) {
    draw_phase_end();
    if (state_timer >= 3 * 60 || input_seletor_pressed()) {
        if (!fim_de_jogo()) {
            int venceu_a_fase = (phase_score[0] > phase_score[1]) ? 1 : 2;
            begin_phase(phase_idx + 1, venceu_a_fase);
            set_state(GS_PHASE_INTRO);
            return;
        }
        arcade_perdeu = (mode == MODE_ARCADE && phase_score[1] >= PHASE_WIN_SCORE);
        if (arcade_perdeu)                        last_winner = 2;
        else if (total_score[0] == total_score[1]) last_winner = 0;
        else last_winner = (total_score[0] > total_score[1]) ? 1 : 2;
        set_state(GS_GAME_OVER);
    }
}

static void frame_game_over(void) {
    draw_game_over();
    if (state_timer >= 4 * 60 || input_seletor_pressed()) {
        initials_player = hiscore_player();
        uint16_t pts = (uint16_t)total_score[initials_player - 1];
        if (hi_qualifies(pts)) {
            initials_armed = false;
            set_state(GS_ENTER_INITIALS);
        } else {
            set_state(GS_HIGH_SCORES);
        }
    }
}

static void grava_iniciais(void) {
    uint16_t pts = (uint16_t)total_score[initials_player - 1];
    hi_consider(pts, (uint8_t)initials_player, (uint8_t)mode, initials_buf);
    hi_save();
    set_state(GS_HIGH_SCORES);
}

static void frame_enter_initials(void) {
    if (!initials_armed) {
        initials_buf[0] = 0;
        initials_buf[1] = 0;
        initials_buf[2] = 0;
        initials_buf[3] = 0;
        initials_slot = 0;
        initials_letra = letra_do_pot(input_pot_raw(initials_player - 1), -1);
        initials_cool = 0;
        initials_armed = true;
    }

    if (initials_slot < INITIALS_LEN) {
        int pot = input_pot_raw(initials_player - 1);
        int alvo = letra_do_pot(pot, initials_letra);
        // Anda no maximo uma letra por vez, e so a cada INITIALS_STEP_FRAMES:
        // girar depressa vira rolagem, nao salto.
        if (initials_cool > 0) {
            initials_cool--;
        } else if (alvo != initials_letra) {
            initials_letra += (alvo > initials_letra) ? +1 : -1;
            initials_cool = INITIALS_STEP_FRAMES;
        }
        initials_buf[initials_slot] = (char)('A' + initials_letra);

        if (input_seletor_pressed()) {
            audio_attract_tick();
            initials_slot++;
            initials_cool = 0;
        }
    } else {
        grava_iniciais();
        return;
    }
    // A tela tem hora para acabar: no fim da contagem vale o que ja foi
    // digitado (as letras nao escolhidas viram espaco em hi_consider).
    if (state_timer >= INITIALS_TIMEOUT_S * 60) {
        grava_iniciais();
        return;
    }
    draw_enter_initials();
}

static void frame_high_scores(void) {
    update_paddles_demo();      // mantem a demo viva atras das transicoes
    draw_highscores();
    if (input_seletor_pressed()) {
        open_menu();
        return;
    }
    if (state_timer >= 10 * 60) {
        enter_attract();
    }
}

// =============================================================
// API
// =============================================================
void game_init(void) {
    mode = MODE_ARCADE;
    menu_sel = MODE_ARCADE;
    phase_score[0] = phase_score[1] = 0;
    total_score[0] = total_score[1] = 0;
    last_winner = 0;
    last_scorer = 2;
    last_hitter = -1;
    arcade_perdeu = false;
    paddle_pos[0] = paddle_pos[1] = DEMO_PADDLE_Y_MIN + 16;
    paddle_travado[0] = paddle_travado[1] = false;
    ball_speed_q = BALL_SPEED_INIT_Q;
    roll_ai_bias();
    enter_attract();
}

void game_frame(void) {
    switch (state) {
        case GS_ATTRACT:        frame_attract();        break;
        case GS_MENU:           frame_menu();           break;
        case GS_PHASE_INTRO:    frame_phase_intro();    break;
        case GS_COUNTDOWN:      frame_countdown();      break;
        case GS_PLAY:           frame_play();           break;
        case GS_PAUSE:          frame_pause();          break;
        case GS_ROUND_END:      frame_round_end();      break;
        case GS_PHASE_END:      frame_phase_end();      break;
        case GS_GAME_OVER:      frame_game_over();      break;
        case GS_ENTER_INITIALS: frame_enter_initials(); break;
        case GS_HIGH_SCORES:    frame_high_scores();    break;
    }
    state_timer++;
    frames_globais++;
    for (int p = 0; p < 2; p++) if (total_flash[p] > 0) total_flash[p]--;
}

game_state_t game_get_state(void) { return state; }
game_mode_t  game_get_mode(void)  { return mode; }
