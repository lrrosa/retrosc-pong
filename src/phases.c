// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
//
// Fases do RetroSC Pong.
//
// Cada fase e so um "cenario": muda o formato/orientacao da raquete, poe
// obstaculos na quadra ou solta um bicho no meio do jogo. As regras
// (PHASE_WIN_SCORE pontos por fase, cada ponto somando no total geral) sao
// iguais em todas e ficam em game.c, valendo tanto para o modo arcade quanto
// para o versus.
//
// O que uma fase pode ter:
//   - tijolos: colunas verticais de BRICK_W x BRICK_H que SOMEM quando a bola
//     bate. Cada coluna guarda suas BRICK_ROWS (24) linhas num bitmask de 32
//     bits -- barato de testar e de copiar quando a fase rearma os tijolos.
//   - solidos: retangulos que so rebatem. Ficam parados (bumpers do pinball,
//     rede do Rebound) ou andam (a coluna da fase COLUNA).
//   - bichos: a nave da fase PHASE_NAVE (obstaculo que atira) e o mascote,
//     que nao e fase nenhuma: ele cruza a quadra de tempos em tempos nas fases
//     marcadas com PF_TEM_BONUS e vale pontos para quem acerta-lo.
//
// Para acrescentar uma fase: um item no enum phase_id_t, o nome/dica nas
// tabelas abaixo e o que ela tem de especial. O resto do jogo -- pontuacao,
// menu, telas -- nao muda.
//
// Ideias que ainda nao viraram fase (ver "ideias de fases.txt", uma pasta
// acima do repo): variacoes de outros pongs, tipo o Telejogo 10 e a colecao
// "Pongs" do Pippin Barr.

#include "phases.h"

#include "gfx.h"
#include "font.h"
#include "assets.h"

#include "pico/rand.h"

// =============================================================
// Estado
// =============================================================
#define BRICK_COLS_MAX 4
#define SOLID_MAX      7

static int      cur_phase;
static uint32_t cur_flags;
static uint32_t frame_ctr;                      // so para piscar textos

// tijolos
static int      brick_cols;                     // colunas em uso (0 = sem tijolos)
static int      brick_col_x[BRICK_COLS_MAX];    // x da esquerda de cada coluna
static uint32_t brick_alive[BRICK_COLS_MAX];    // linhas ainda de pe
static uint32_t brick_start[BRICK_COLS_MAX];    // padrao original da fase

// solidos (parados ou moveis)
static rect_t   solids[SOLID_MAX];
static int      solid_count;
static int      col_y, col_dir;                 // deslocamento da coluna movel

// mascote-bonus (qualquer fase com PF_TEM_BONUS)
static bool     bonus_on;
static int32_t  bonus_x_q, bonus_y_q, bonus_vx_q, bonus_vy_q;
static int      bonus_wait;
static int      bonus_left;                      // passagens que ainda restam

// nave + tiros (PHASE_NAVE)
static int      nave_y, nave_dir, nave_cool;
static int      nave_revide;    // revide pendente: -1 esquerda, +1 direita, 0 nenhum
static struct { int x, y, vx; bool on; } shots[NAVE_SHOT_MAX];
static int      shrink[2];                      // frames de raquete encolhida

// Efeitos ganhos no mascote: frames que faltam de cada tipo, por jogador
// ATINGIDO (o BONUS_ENCOLHE cai no adversario de quem pegou). Sobrevivem ao
// fim do ponto: os 10 s sao de tempo de jogo.
static int      efeito[2][BONUS_TIPOS];
static uint32_t escudo_alive[2];                // tijolos do escudo que restam

// =============================================================
// Tabela das fases
// =============================================================
static const char *const names[PHASE_COUNT] = {
    [PHASE_CLASSICO]  = "PONG CLASSICO",
    [PHASE_NAVE]      = "NAVE",
    [PHASE_TRIPLO]    = "TRIPLO",
    [PHASE_BARREIRA1] = "BARREIRA I",
    [PHASE_PINBALL]   = "PINBALL",
    [PHASE_BARREIRA2] = "BARREIRA II",
    [PHASE_COLUNA]    = "COLUNA",
    [PHASE_MURALHA]   = "MURALHA",
    [PHASE_REBOUND]   = "REBOUND",
    [PHASE_BARREIRA3] = "BARREIRA III",
};

static const char *const hints[PHASE_COUNT] = {
    [PHASE_CLASSICO]  = "O PONG DE SEMPRE",
    [PHASE_NAVE]  = "OS TIROS ENCOLHEM A RAQUETE",
    [PHASE_TRIPLO]    = "TRES RAQUETES COM VAOS",
    [PHASE_BARREIRA1] = "DOIS MUROS NO MEIO",
    [PHASE_PINBALL]   = "OBSTACULOS NO MEIO",
    [PHASE_BARREIRA2] = "TRES MUROS COM VAOS",
    [PHASE_COLUNA]    = "OBSTACULOS SOBEM E DESCEM",
    [PHASE_MURALHA]   = "OS TIJOLOS GUARDAM O GOL",
    [PHASE_REBOUND]   = "VOLEI: NAO DEIXE A BOLA CAIR",
    [PHASE_BARREIRA3] = "QUATRO MUROS: ABRA CAMINHO",
};

const char *phase_name(int idx) {
    if ((unsigned)idx >= PHASE_COUNT) return "";
    return names[idx];
}

const char *phase_hint(int idx) {
    if ((unsigned)idx >= PHASE_COUNT) return "";
    return hints[idx];
}

// Fases de quadra limpa, onde o bonus tem espaco para cruzar sem se confundir
// com o cenario. E a unica lista: phase_begin() liga a flag a partir dela.
bool phase_tem_bonus(int idx) {
    switch (idx) {
        case PHASE_CLASSICO:
        case PHASE_TRIPLO:
        case PHASE_BARREIRA1:
        case PHASE_MURALHA:
            return true;
        default:
            return false;
    }
}

int      phase_current(void) { return cur_phase; }
uint32_t phase_flags(void)   { return cur_flags; }

// =============================================================
// Montagem dos cenarios
// =============================================================
// BARREIRA III: blocos de tijolo separados por um corredor de uma linha,
// grossos nas pontas e fino no meio. Sao 24 linhas ao todo (FB_HEIGHT/BRICK_H):
// 5+4+2+4+5 = 20 de tijolo mais os 4 corredores fecham exatamente 24. O padrao
// 5-4-3-4-5 pedido daria 21+4 = 25 linhas, uma a mais do que cabe na tela.
static const uint8_t barreira3_blocos[] = { 5, 4, 2, 4, 5 };

// Muros no meio da quadra: 'cols' colunas com 'gap' px entre elas. Com
// 'blocos' != NULL, a coluna nasce dividida nesses blocos, separados por uma
// linha vazia -- corredores prontos que tiram o pior da barreira mais grossa.
// O estrago fica ate o fim da fase -- quando abre um vao de ponta a ponta, a
// fase volta a ser um pong normal, que e a graca dela. Por isso a progressao
// de 2 -> 3 -> 4 muros ao longo do jogo.
static void build_barreira(int cols, int gap,
                           const uint8_t *blocos, int n_blocos) {
    brick_cols = cols;
    int step  = BRICK_W + gap;
    int total = cols * BRICK_W + (cols - 1) * gap;
    int x0    = FB_WIDTH / 2 - total / 2;

    uint32_t mask = 0;
    if (blocos == NULL) {
        for (int r = 0; r < BRICK_ROWS; r++) mask |= (1u << r);
    } else {
        int r = 0;
        for (int b = 0; b < n_blocos && r < BRICK_ROWS; b++) {
            for (int i = 0; i < blocos[b] && r < BRICK_ROWS; i++, r++)
                mask |= (1u << r);
            r++;                       // corredor entre um bloco e o proximo
        }
    }
    for (int c = 0; c < cols; c++) {
        brick_col_x[c] = x0 + c * step;
        brick_start[c] = mask;
    }
    cur_flags |= PF_NO_CENTER_LINE;
}

// MURALHA: uma parede atras de cada raquete, com poucos vaos abertos de
// saida. So marca ponto quem enfiar a bola num vao, e cada bola que passa da
// raquete quebra mais um tijolo -- o estrago fica ate o fim da fase, como nas
// barreiras, entao a parede vai se abrindo e a fase acelerando. Um tijolo a
// menos ja e uma passagem de 8 px, e a bola tem 3.
static void build_muralha(void) {
    brick_cols = 2;
    brick_col_x[0] = 0;
    brick_col_x[1] = FB_WIDTH - BRICK_W;
    uint32_t mask = 0;
    for (int r = 0; r < BRICK_ROWS; r++)
        if ((r % (MURALHA_CHEIOS + MURALHA_VAZIOS)) < MURALHA_CHEIOS)
            mask |= (1u << r);
    brick_start[0] = brick_start[1] = mask;
}

// PINBALL: obstaculos fixos espalhados pelo meio da quadra em losango. As
// raquetes continuam nos lugares de sempre; o que muda e o caminho da bola.
// Nada acima de y=40: ali em cima estao o placar da fase e o total.
static void build_pinball(void) {
    #define BX(dx) (FB_WIDTH  / 2 + (dx) - BUMPER_W / 2)
    #define BY(dy) (FB_HEIGHT / 2 + (dy) - BUMPER_H / 2)
    // Sem postes no eixo central entre o centro e as pontas (fechavam fileiras
    // de tres e tampavam a passagem pelo meio) e sem os laterais, que ficavam
    // na cara das raquetes.
    static const int pos[7][2] = {
        { BX(  0), BY(  0) },                       // centro
        { BX(  0), BY(-72) }, { BX(  0), BY(+72) }, // pontas do eixo vertical
        { BX(-32), BY(-30) }, { BX(+32), BY(-30) }, // diagonais
        { BX(-32), BY(+30) }, { BX(+32), BY(+30) },
    };
    #undef BX
    #undef BY
    solid_count = 7;
    for (int i = 0; i < solid_count; i++) {
        solids[i].x = pos[i][0];
        solids[i].y = pos[i][1];
        solids[i].w = BUMPER_W;
        solids[i].h = BUMPER_H;
    }
}

// Altura ocupada pela coluna movel inteira.
static int coluna_span(void) {
    return COL_BUMPERS * BUMPER_H + (COL_BUMPERS - 1) * COL_GAP;
}

static void coluna_place(void) {
    for (int i = 0; i < COL_BUMPERS; i++)
        solids[i].y = col_y + i * (BUMPER_H + COL_GAP);
}

// COLUNA: os mesmos obstaculos do pinball, empilhados no meio e subindo e
// descendo juntos. Quem manda na jogada e o instante em que a bola chega.
static void build_coluna(void) {
    solid_count = COL_BUMPERS;
    int x = FB_WIDTH / 2 - BUMPER_W / 2;
    for (int i = 0; i < COL_BUMPERS; i++) {
        solids[i].x = x;
        solids[i].w = BUMPER_W;
        solids[i].h = BUMPER_H;
    }
    col_y   = (FB_HEIGHT - coluna_span()) / 2;
    col_dir = +1;
    coluna_place();
    cur_flags |= PF_NO_CENTER_LINE;
}

// REBOUND: volei. As raquetes deitam no chao e andam na horizontal dentro da
// propria meia-quadra; a bola tem gravidade e o ponto sai quando ela toca o
// chao. A rede no meio e um solido que vai do chao ate NET_TOP.
static void build_rebound(void) {
    solid_count = 1;
    solids[0].x = FB_WIDTH / 2 - NET_W / 2;
    solids[0].y = NET_TOP;
    solids[0].w = NET_W;
    solids[0].h = FB_HEIGHT - NET_TOP;
    cur_flags |= PF_NO_CENTER_LINE | PF_GRAVITY | PF_FLOOR_SCORES |
                 PF_SIDE_WALLS | PF_PADDLE_HORIZ;
}

static void bonus_sleep(void) {
    bonus_on   = false;
    bonus_wait = BONUS_WAIT_MIN + (int)(get_rand_32() % BONUS_WAIT_RANGE);
}

static void nave_reset(void) {
    // Pode nascer no meio da quadra: a contagem regressiva e desenhada com um
    // fundo preto e nao some mais atras dela.
    nave_y      = (FB_HEIGHT - NAVE_H) / 2;
    nave_dir    = +1;
    nave_cool   = NAVE_SHOT_PERIOD;
    nave_revide = 0;
}

static void efeitos_reset(void) {
    for (int p = 0; p < 2; p++) {
        for (int t = 0; t < BONUS_TIPOS; t++) efeito[p][t] = 0;
        escudo_alive[p] = 0;
    }
}

void phase_begin(int idx) {
    if ((unsigned)idx >= PHASE_COUNT) idx = 0;
    cur_phase   = idx;
    cur_flags   = 0;
    frame_ctr   = 0;
    brick_cols  = 0;
    solid_count = 0;
    nave_reset();
    shrink[0] = shrink[1] = 0;
    efeitos_reset();
    for (int i = 0; i < NAVE_SHOT_MAX; i++) shots[i].on = false;
    bonus_left = BONUS_PASSES_MAX;
    bonus_sleep();

    if (phase_tem_bonus(idx)) cur_flags |= PF_TEM_BONUS;

    switch (idx) {
        case PHASE_BARREIRA1: build_barreira(2, 1, NULL, 0); break;
        case PHASE_BARREIRA2: build_barreira(3, 16, NULL, 0); break;
        // Sem os corredores, a barreira cheia de 4 muros vira uma partida do
        // jogador contra a propria parede.
        case PHASE_BARREIRA3:
            build_barreira(4, 1, barreira3_blocos,
                           (int)(sizeof(barreira3_blocos))); break;
        case PHASE_MURALHA:   build_muralha();             break;
        case PHASE_PINBALL:   build_pinball();             break;
        case PHASE_COLUNA:    build_coluna();              break;
        case PHASE_REBOUND:   build_rebound();             break;
        default: break;
    }
    for (int c = 0; c < brick_cols; c++) brick_alive[c] = brick_start[c];
}

void phase_round_reset(void) {
    shrink[0] = shrink[1] = 0;
    for (int i = 0; i < NAVE_SHOT_MAX; i++) shots[i].on = false;
    nave_reset();
    bonus_sleep();
}

// =============================================================
// Raquetes
// =============================================================
// Na MURALHA a raquete anda um pouco mais para dentro, senao fica colada nos
// tijolos e o olho nao separa as duas coisas.
static int paddle_margin(void) {
    return (cur_phase == PHASE_MURALHA) ? (PADDLE_MARGIN + 6) : PADDLE_MARGIN;
}

// O escudo nasce entre a raquete e o gol, 2 px atras dela -- colado a raquete
// o olho junta as duas coisas, que e a mesma razao de a MURALHA afastar a
// raquete do muro dela. Nas fases normais nao ha esses 2 px de folga ate a
// borda, e o clamp encosta o escudo no gol; na MURALHA, onde a raquete anda
// mais para dentro, ele fica no meio do caminho sem tocar no muro da fase.
static int escudo_x(int player) {
    int m = paddle_margin();
    int x = (player == 0) ? (m - BRICK_W - 2) : (FB_WIDTH - m + 2);
    if (x < 0) x = 0;
    if (x > FB_WIDTH - BRICK_W) x = FB_WIDTH - BRICK_W;
    return x;
}

static void escudo_arma(int player) {
    uint32_t m = 0;
    for (int r = 0; r < BRICK_ROWS; r++)
        if ((r % ESCUDO_PERIODO) < ESCUDO_CHEIO) m |= (1u << r);
    escudo_alive[player & 1] = m;
}

bool phase_turbo(int jogador) {
    return efeito[jogador & 1][BONUS_TURBO] > 0;
}

const char *bonus_nome(int tipo) {
    switch (tipo) {
        case BONUS_PONTOS:  return "BONUS +3";
        case BONUS_RAQUETE: return "RAQUETE MAIOR";
        case BONUS_ENCOLHE: return "ENCOLHEU O RIVAL";
        case BONUS_ESCUDO:  return "ESCUDO";
        case BONUS_TURBO:   return "TURBO";
        default:            return "";
    }
}

int phase_paddle_range(void) {
    if (cur_flags & PF_PADDLE_HORIZ)
        return FB_WIDTH / 2 - 2 * VOLLEY_MARGIN - VOLLEY_PADDLE_W;
    if (cur_phase == PHASE_TRIPLO)
        return FB_HEIGHT - (3 * TRIPLE_SEG_H + 2 * TRIPLE_GAP);
    return FB_HEIGHT - PADDLE_H;
}

int phase_paddle_segments(int player, int pos, rect_t *out) {
    if (cur_flags & PF_PADDLE_HORIZ) {
        int base = (player == 0) ? VOLLEY_MARGIN
                                 : (FB_WIDTH / 2 + VOLLEY_MARGIN);
        out[0].x = base + pos;
        out[0].y = FB_HEIGHT - VOLLEY_PADDLE_Y;
        out[0].w = VOLLEY_PADDLE_W;
        out[0].h = VOLLEY_PADDLE_H;
        return 1;
    }

    int margin = paddle_margin();
    int x = (player == 0) ? margin : (FB_WIDTH - margin - PADDLE_W);
    int p = player & 1;

    // A raquete muda de tamanho, nunca de curso: phase_paddle_range() continua
    // sendo o da raquete normal, e o que cresce ou encolhe fica centrado na
    // mesma posicao lida do pot -- senao a raquete pularia debaixo da mao do
    // jogador. Os dois bonus podem cair um em cada ponta da mesma fase; nesse
    // caso um anula o outro.
    bool maior = efeito[p][BONUS_RAQUETE] > 0;
    bool menor = shrink[p] > 0 || efeito[p][BONUS_ENCOLHE] > 0;
    if (maior && menor) maior = menor = false;

    if (cur_phase == PHASE_TRIPLO) {
        int h = TRIPLE_SEG_H;
        if (maior) h = TRIPLE_SEG_H_BIG;
        if (menor) h = TRIPLE_SEG_H / 2;
        int span  = 3 * h + 2 * TRIPLE_GAP;
        int span0 = 3 * TRIPLE_SEG_H + 2 * TRIPLE_GAP;
        int y0 = pos + (span0 - span) / 2;
        if (y0 < 0) y0 = 0;
        if (y0 > FB_HEIGHT - span) y0 = FB_HEIGHT - span;
        for (int i = 0; i < 3; i++) {
            out[i].x = x;
            out[i].y = y0 + i * (h + TRIPLE_GAP);
            out[i].w = PADDLE_W;
            out[i].h = h;
        }
        return 3;
    }

    out[0].x = x;
    out[0].y = pos;
    out[0].w = PADDLE_W;
    out[0].h = PADDLE_H;
    if (maior) {
        out[0].y = pos - (PADDLE_H_BIG - PADDLE_H) / 2;
        out[0].h = PADDLE_H_BIG;
        if (out[0].y < 0) out[0].y = 0;
        if (out[0].y > FB_HEIGHT - PADDLE_H_BIG)
            out[0].y = FB_HEIGHT - PADDLE_H_BIG;
    } else if (menor) {
        out[0].y = pos + PADDLE_H / 4;
        out[0].h = PADDLE_H / 2;
    }
    return 1;
}

// =============================================================
// Saque
// =============================================================
int phase_serve_x(int dir) {
    // So vale para as barreiras do MEIO da quadra. Os muros da MURALHA ficam
    // nas bordas, atras das raquetes: la o saque do centro esta certo, e usar
    // esta conta colocaria a bola em x negativo, fora da tela, dando ponto no
    // ato.
    if (brick_cols > 0 && brick_col_x[0] > FB_WIDTH / 4) {
        // Sair do centro colocaria a bola dentro da barreira: saca do lado de
        // quem vai RECEBER (dir aponta para ele), colado na barreira, para a
        // bola ter a quadra inteira dele pela frente antes de virar gol.
        int left  = brick_col_x[0] - 8 - BALL_SIZE;
        int right = brick_col_x[brick_cols - 1] + BRICK_W + 8;
        return (dir < 0) ? left : right;
    }
    if (cur_flags & PF_PADDLE_HORIZ) {
        // Volei: a bola cai na meia-quadra de quem vai sacar.
        return (dir < 0) ? (FB_WIDTH / 4) : (3 * FB_WIDTH / 4);
    }
    if (cur_phase == PHASE_COLUNA || cur_phase == PHASE_PINBALL ||
        cur_phase == PHASE_NAVE) {
        // Sair do centro seria sair de dentro de um obstaculo: o poste do
        // pinball, a coluna movel ou a nave, que volta ao meio da quadra a
        // cada saque. Nascendo dentro dela, quem escolhia o lado era o
        // primeiro quique -- em metade dos saques a bola saia na direcao de
        // quem tinha feito o ponto (medido no simulador).
        return (dir < 0) ? (FB_WIDTH / 2 - 34) : (FB_WIDTH / 2 + 34);
    }
    return FB_WIDTH / 2;
}

int phase_serve_y(void) {
    if (cur_flags & PF_GRAVITY) return 24;      // solta a bola do alto
    return FB_HEIGHT / 2;
}

// =============================================================
// Colisao da bola com o cenario
// =============================================================
// Rebate a bola num retangulo em que ela ja entrou. A face sai da TRAVESSIA --
// onde a bola estava no frame anterior contra o retangulo de agora -- e nao da
// velocidade dela: quem ja cruzava a faixa horizontal do obstaculo so pode ter
// entrado por cima ou por baixo, diga o que disser a velocidade. Isso e o que
// conserta os obstaculos que ANDAM: a coluna movel alcanca por tras uma bola
// que ja estava indo embora e, pela velocidade, ela era "devolvida" para quem
// acabara de rebater (medido: um quarto dos rebotes da coluna). Escolher pela
// menor penetracao, que foi a primeira versao disto, tinha o defeito oposto:
// a bola entrada pelo canto saia de lado sem ninguem inverter e atravessava o
// obstaculo. Quando ela ja estava dentro nos dois eixos (o obstaculo veio por
// cima dela) ou em nenhum (entrou bem pelo canto), vale a saida mais curta. A
// velocidade so e invertida se ainda apontar para dentro: assim o obstaculo
// que alcanca a bola a empurra, em vez de rebate-la.
// Vale para tijolo, escudo, bumper, coluna movel, rede e nave.
// Devolve true se quem mudou foi o eixo X.
static bool bounce_off(int32_t prev_x, int32_t prev_y,
                       int rx0, int ry0, int rx1, int ry1,
                       int32_t *bx, int32_t *by,
                       int32_t *vx, int32_t *vy) {
    int x0 = *bx >> 8, x1 = x0 + BALL_SIZE - 1;
    int y0 = *by >> 8, y1 = y0 + BALL_SIZE - 1;
    int ax0 = prev_x >> 8, ax1 = ax0 + BALL_SIZE - 1;
    int ay0 = prev_y >> 8, ay1 = ay0 + BALL_SIZE - 1;

    bool antes_x = !(ax1 < rx0 || ax0 > rx1);   // ja cruzava a faixa vertical
    bool antes_y = !(ay1 < ry0 || ay0 > ry1);   // ja cruzava a faixa horizontal

    int p_esq = x1 - rx0 + 1, p_dir = rx1 - x0 + 1;   // saidas possiveis
    int p_cim = y1 - ry0 + 1, p_bai = ry1 - y0 + 1;

    bool eixo_x;
    if (antes_x != antes_y) {
        eixo_x = antes_y;                       // entrou por uma face vertical
    } else {
        int m_x = (p_esq < p_dir) ? p_esq : p_dir;
        int m_y = (p_cim < p_bai) ? p_cim : p_bai;
        eixo_x = (m_x <= m_y);
    }

    if (eixo_x) {
        bool esquerda = (ax1 < rx0) ? true
                      : (ax0 > rx1) ? false
                                    : (p_esq <= p_dir);
        *bx = esquerda ? ((int32_t)(rx0 - BALL_SIZE) << 8)
                       : ((int32_t)(rx1 + 1) << 8);
        if (esquerda) { if (*vx > 0) *vx = -*vx; }
        else          { if (*vx < 0) *vx = -*vx; }
        return true;
    }
    bool cima = (ay1 < ry0) ? true
              : (ay0 > ry1) ? false
                            : (p_cim <= p_bai);
    *by = cima ? ((int32_t)(ry0 - BALL_SIZE) << 8)
               : ((int32_t)(ry1 + 1) << 8);
    if (cima) { if (*vy > 0) *vy = -*vy; }
    else      { if (*vy < 0) *vy = -*vy; }
    return false;
}

bool phase_ball_collide(int32_t prev_x, int32_t prev_y,
                        int32_t *bx, int32_t *by,
                        int32_t *vx, int32_t *vy) {
    int x0 = *bx >> 8, x1 = x0 + BALL_SIZE - 1;
    int y0 = *by >> 8, y1 = y0 + BALL_SIZE - 1;

    // --- tijolos (somem ao serem atingidos) ---
    for (int c = 0; c < brick_cols; c++) {
        if (brick_alive[c] == 0) continue;
        int cx0 = brick_col_x[c];
        int cx1 = cx0 + BRICK_W - 1;
        if (x1 < cx0 || x0 > cx1) continue;

        int r0 = y0 / BRICK_H;
        int r1 = y1 / BRICK_H;
        if (r0 < 0) r0 = 0;
        if (r1 > BRICK_ROWS - 1) r1 = BRICK_ROWS - 1;

        for (int r = r0; r <= r1; r++) {
            if (!(brick_alive[c] & (1u << r))) continue;
            brick_alive[c] &= ~(1u << r);
            bounce_off(prev_x, prev_y,
                       cx0, r * BRICK_H, cx1, (r + 1) * BRICK_H - 1,
                       bx, by, vx, vy);
            return true;
        }
    }

    // --- escudo do bonus: os mesmos tijolos, na frente do gol ---
    for (int p = 0; p < 2; p++) {
        if (efeito[p][BONUS_ESCUDO] <= 0 || escudo_alive[p] == 0) continue;
        int cx0 = escudo_x(p);
        int cx1 = cx0 + BRICK_W - 1;
        if (x1 < cx0 || x0 > cx1) continue;

        int r0 = y0 / BRICK_H;
        int r1 = y1 / BRICK_H;
        if (r0 < 0) r0 = 0;
        if (r1 > BRICK_ROWS - 1) r1 = BRICK_ROWS - 1;

        for (int r = r0; r <= r1; r++) {
            if (!(escudo_alive[p] & (1u << r))) continue;
            escudo_alive[p] &= ~(1u << r);
            bounce_off(prev_x, prev_y,
                       cx0, r * BRICK_H, cx1, (r + 1) * BRICK_H - 1,
                       bx, by, vx, vy);
            return true;
        }
    }

    // --- solidos (bumpers, coluna movel, rede) ---
    for (int i = 0; i < solid_count; i++) {
        const rect_t *s = &solids[i];
        if (x1 < s->x || x0 > s->x + s->w - 1) continue;
        if (y1 < s->y || y0 > s->y + s->h - 1) continue;
        bool eixo_x = bounce_off(prev_x, prev_y,
                                 s->x, s->y, s->x + s->w - 1, s->y + s->h - 1,
                                 bx, by, vx, vy);
        if (cur_phase == PHASE_PINBALL || cur_phase == PHASE_COLUNA ||
            cur_phase == PHASE_REBOUND) {
            // Gira o vetor alguns graus para um lado ou para o outro: duas
            // faces paralelas devolvendo a bola sempre no mesmo angulo a
            // deixavam presa entre dois postes. A rede do REBOUND fecha o
            // mesmo tipo de orbita contra a raquete de quem esta parado --
            // medido no simulador: 641 idas e vindas entre a raquete e a rede
            // sem ninguem pontuar, e a fase nunca acabava.
            int32_t giro = (get_rand_32() & 1) ? +1 : -1;
            int32_t saida_x = *vx, saida_y = *vy;
            int32_t nvx = *vx - giro * (*vy >> BUMPER_SPIN_SHIFT);
            int32_t nvy = *vy + giro * (*vx >> BUMPER_SPIN_SHIFT);
            *vx = nvx;
            *vy = nvy;
            // Com a bola quase vertical o giro chega a virar o sinal do eixo
            // que acabou de rebater -- e ai ela volta para dentro do poste de
            // onde saiu. O eixo da saida mantem o sentido, custe o que custar.
            if (eixo_x) { if ((*vx ^ saida_x) < 0) *vx = -*vx; }
            else        { if ((*vy ^ saida_y) < 0) *vy = -*vy; }
            if (*vx > -BALL_VX_MIN_Q && *vx < BALL_VX_MIN_Q)
                *vx = (*vx < 0) ? -BALL_VX_MIN_Q : BALL_VX_MIN_Q;
        }
        return true;
    }

    // --- a propria nave rebate a bola, e revida ---
    if (cur_phase == PHASE_NAVE) {
        int gx1 = NAVE_X + NAVE_W - 1;
        int gy1 = nave_y + NAVE_H - 1;
        if (!(x1 < NAVE_X || x0 > gx1 || y1 < nave_y || y0 > gy1)) {
            // Quem dispara e update_nave(), que e quem recebe o ultimo a
            // rebater. Aqui fica so o lado de onde a bola veio, usado como
            // reserva enquanto ninguem tocou nela na jogada.
            nave_revide = (*vx > 0) ? -1 : +1;
            bounce_off(prev_x, prev_y, NAVE_X, nave_y, gx1, gy1,
                       bx, by, vx, vy);
            return true;
        }
    }
    return false;
}

// =============================================================
// Partes moveis
// =============================================================
static bool overlap(int ax, int ay, int aw, int ah,
                    int bx, int by, int bw, int bh) {
    return !(ax >= bx + bw || ax + aw <= bx || ay >= by + bh || ay + ah <= by);
}

// O mascote entra por cima ou por baixo e atravessa na diagonal. Fica presa a
// faixa BONUS_X_MIN..BONUS_X_MAX para nao passear em cima das raquetes.
static void bonus_spawn(void) {
    uint32_t r = get_rand_32();
    bool de_cima = (r & 1) != 0;
    int  faixa   = BONUS_X_MAX - BONUS_X_MIN + 1;
    int32_t vx   = BONUS_VX_MIN_Q +
                   (int32_t)((r >> 8) % (BONUS_VX_MAX_Q - BONUS_VX_MIN_Q + 1));

    bonus_x_q  = (int32_t)(BONUS_X_MIN + (int)((r >> 1) % faixa)) << 8;
    bonus_y_q  = de_cima ? -((int32_t)BONUS_H << 8)
                        :  ((int32_t)FB_HEIGHT << 8);
    bonus_vy_q = de_cima ? +BONUS_VY_Q : -BONUS_VY_Q;
    bonus_vx_q = ((r >> 20) & 1) ? -vx : +vx;
    bonus_on   = true;
}

// Sorteia o que o mascote pagou. O de pontos e o unico que o game.c precisa
// somar; os outros ficam aqui, cronometrados junto com as raquetes e o escudo.
static void aplica_bonus(int p, int tipo, bonus_out_t *out) {
    if (tipo == BONUS_PONTOS) {
        out->pontos[p] += BONUS_POINTS;
    } else if (tipo == BONUS_ENCOLHE) {
        efeito[1 - p][BONUS_ENCOLHE] = BONUS_EFEITO_FRAMES;   // vai no rival
    } else {
        if (tipo == BONUS_ESCUDO) escudo_arma(p);
        efeito[p][tipo] = BONUS_EFEITO_FRAMES;
    }
    out->tipo    = tipo;
    out->jogador = p;                 // quem pegou, mesmo quando o efeito e no rival
}

static void update_bonus(int32_t ball_x, int32_t ball_y,
                        int last_hitter, bonus_out_t *out) {
    if (!bonus_on) {
        if (bonus_left <= 0) return;             // ja passou o limite da fase
        if (--bonus_wait <= 0) { bonus_left--; bonus_spawn(); }
        return;
    }
    bonus_x_q += bonus_vx_q;
    bonus_y_q += bonus_vy_q;

    int sx = bonus_x_q >> 8;
    int sy = bonus_y_q >> 8;
    if (sx < BONUS_X_MIN) { bonus_x_q = (int32_t)BONUS_X_MIN << 8; bonus_vx_q = -bonus_vx_q; }
    if (sx > BONUS_X_MAX) { bonus_x_q = (int32_t)BONUS_X_MAX << 8; bonus_vx_q = -bonus_vx_q; }
    if (sy > FB_HEIGHT || sy < -BONUS_H) { bonus_sleep(); return; }

    sx = bonus_x_q >> 8;
    if (overlap(ball_x >> 8, ball_y >> 8, BALL_SIZE, BALL_SIZE,
                sx, sy, BONUS_W, BONUS_H)) {
        // A bola atravessa o mascote (nao desvia a jogada); quem rebateu por
        // ultimo leva o bonus, sorteado entre os cinco tipos.
        if (last_hitter == 0 || last_hitter == 1)
            aplica_bonus(last_hitter, (int)(get_rand_32() % BONUS_TIPOS), out);
        bonus_sleep();
    }
}

static void update_coluna(void) {
    int max = FB_HEIGHT - coluna_span();
    col_y += col_dir * COL_SPEED;
    if (col_y >= max) { col_y = max; col_dir = -1; }
    if (col_y <= 0)   { col_y = 0;   col_dir = +1; }
    coluna_place();
}

// Solta um tiro pelo lado 'lado' (-1 esquerda, +1 direita) se houver vaga.
// Reinicia o relogio do tiro periodico: o revide conta como o tiro da vez,
// senao uma bola teimosa na nave viraria uma saraivada.
static void nave_dispara(int lado) {
    for (int i = 0; i < NAVE_SHOT_MAX; i++) {
        if (shots[i].on) continue;
        shots[i].on = true;
        shots[i].y  = nave_y + NAVE_H / 2 - SHOT_H / 2;
        shots[i].x  = (lado < 0) ? (NAVE_X - SHOT_W) : (NAVE_X + NAVE_W);
        shots[i].vx = (lado < 0) ? -SHOT_SPEED : +SHOT_SPEED;
        nave_cool   = NAVE_SHOT_PERIOD;
        return;
    }
}

static void update_nave(const int paddle_pos[2], int last_hitter) {
    nave_y += nave_dir * NAVE_SPEED;
    if (nave_y > FB_HEIGHT - NAVE_H) {
        nave_y = FB_HEIGHT - NAVE_H; nave_dir = -1;
    }
    if (nave_y < 0) { nave_y = 0; nave_dir = +1; }

    // Levou bolada: devolve um tiro pelo lado de quem rebateu a bola.
    if (nave_revide != 0) {
        int lado = nave_revide;
        if (last_hitter == 0)      lado = -1;
        else if (last_hitter == 1) lado = +1;
        nave_revide = 0;
        nave_dispara(lado);
    }
    // Fora isso ela atira sozinha, para um lado sorteado.
    if (--nave_cool <= 0) {
        nave_cool = NAVE_SHOT_PERIOD;
        nave_dispara((get_rand_32() & 1) ? -1 : +1);
    }

    rect_t seg[PADDLE_SEG_MAX];
    for (int i = 0; i < NAVE_SHOT_MAX; i++) {
        if (!shots[i].on) continue;
        shots[i].x += shots[i].vx;
        if (shots[i].x < -SHOT_W || shots[i].x > FB_WIDTH) {
            shots[i].on = false;
            continue;
        }
        int p = (shots[i].vx < 0) ? 0 : 1;
        int n = phase_paddle_segments(p, paddle_pos[p], seg);
        for (int k = 0; k < n; k++) {
            if (overlap(shots[i].x, shots[i].y, SHOT_W, SHOT_H,
                        seg[k].x, seg[k].y, seg[k].w, seg[k].h)) {
                shots[i].on = false;
                shrink[p] = SHRINK_FRAMES;
                break;
            }
        }
    }
}

void phase_update(int32_t ball_x, int32_t ball_y, const int paddle_pos[2],
                  int last_hitter, bonus_out_t *out) {
    out->pontos[0] = out->pontos[1] = 0;
    out->tipo = out->jogador = -1;
    frame_ctr++;
    for (int p = 0; p < 2; p++) {
        if (shrink[p] > 0) shrink[p]--;
        for (int t = 0; t < BONUS_TIPOS; t++)
            if (efeito[p][t] > 0) efeito[p][t]--;
    }

    if (cur_flags & PF_TEM_BONUS) update_bonus(ball_x, ball_y, last_hitter, out);
    if (cur_phase == PHASE_COLUNA)   update_coluna();
    if (cur_phase == PHASE_NAVE) update_nave(paddle_pos, last_hitter);
}

// =============================================================
// Desenho
// =============================================================
// Sprite da nave (13x8 na escala 1), desenhado por retangulos.
static void draw_sprite_nave(int x, int y, int s) {
    gfx_fill_rect(x +  5 * s, y + 0 * s,  3 * s, 1 * s, 1);
    gfx_fill_rect(x +  4 * s, y + 1 * s,  5 * s, 1 * s, 1);
    gfx_fill_rect(x +  3 * s, y + 2 * s,  7 * s, 1 * s, 1);
    gfx_fill_rect(x +  2 * s, y + 3 * s,  9 * s, 1 * s, 1);
    gfx_fill_rect(x +  0 * s, y + 4 * s, 13 * s, 2 * s, 1);
    gfx_fill_rect(x +  1 * s, y + 6 * s,  2 * s, 2 * s, 1);
    gfx_fill_rect(x +  5 * s, y + 6 * s,  3 * s, 2 * s, 1);
    gfx_fill_rect(x + 10 * s, y + 6 * s,  2 * s, 2 * s, 1);
}

static void draw_bonus(void) {
    if (!bonus_on) return;
    int x = bonus_x_q >> 8, y = bonus_y_q >> 8;
    gfx_blit(retrosc_mascote_data, RETROSC_MASCOTE_W, RETROSC_MASCOTE_H,
             x, y, 1);

    // "BONUS" piscando junto do mascote (acima ou abaixo, o que couber).
    if ((frame_ctr >> 4) & 1) return;
    const char *msg = "BONUS";
    int tw = gfx_text_width(msg, 1);
    int tx = x + BONUS_W / 2 - tw / 2;
    if (tx < 2) tx = 2;
    if (tx > FB_WIDTH - tw - 2) tx = FB_WIDTH - tw - 2;
    int ty = (y > FB_HEIGHT / 2) ? (y - FONT_CELL_H - 1) : (y + BONUS_H + 2);
    if (ty < 0) ty = 0;
    if (ty > FB_HEIGHT - FONT_CELL_H) ty = FB_HEIGHT - FONT_CELL_H;
    gfx_fill_rect(tx - 2, ty - 1, tw + 4, FONT_H + 2, 0);   // fundo preto
    gfx_text(tx, ty, msg, 1, 1);
}

void phase_draw(void) {
    for (int c = 0; c < brick_cols; c++) {
        for (int r = 0; r < BRICK_ROWS; r++) {
            if (!(brick_alive[c] & (1u << r))) continue;
            // 1 px de folga embaixo so para o olho separar os tijolos; a
            // colisao continua usando a linha inteira (BRICK_H).
            gfx_fill_rect(brick_col_x[c], r * BRICK_H, BRICK_W, BRICK_H - 1, 1);
        }
    }
    for (int i = 0; i < solid_count; i++)
        gfx_fill_rect(solids[i].x, solids[i].y, solids[i].w, solids[i].h, 1);

    // Escudo: pisca no ultimo segundo para o dono ver que ele vai embora.
    for (int p = 0; p < 2; p++) {
        if (efeito[p][BONUS_ESCUDO] <= 0) continue;
        if (efeito[p][BONUS_ESCUDO] < 60 && ((frame_ctr >> 3) & 1)) continue;
        int x = escudo_x(p);
        for (int r = 0; r < BRICK_ROWS; r++)
            if (escudo_alive[p] & (1u << r))
                gfx_fill_rect(x, r * BRICK_H, BRICK_W, BRICK_H - 1, 1);
    }

    if (cur_phase == PHASE_NAVE) {
        draw_sprite_nave(NAVE_X, nave_y, NAVE_SCALE);
        for (int i = 0; i < NAVE_SHOT_MAX; i++)
            if (shots[i].on)
                gfx_fill_rect(shots[i].x, shots[i].y, SHOT_W, SHOT_H, 1);
    }

    if (cur_flags & PF_TEM_BONUS) draw_bonus();
}
