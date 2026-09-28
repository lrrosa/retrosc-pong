// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the production C, including its private state, with hardware stubs.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.c"
#include "../src/phases.c"
#include "../src/input.c"
#include "../src/highscores.c"

uint32_t fb[FB_WORDS];
uint8_t test_flash[FLASH_SECTOR_SIZE];
static uint16_t adc_values[2] = {2048, 2048};
static unsigned adc_channel;
static bool pressed;
static uint32_t rng = 12345;
uint32_t get_rand_32(void) { rng = rng * 1664525u + 1013904223u; return rng; }
void gpio_init(unsigned p) { (void)p; }
void gpio_set_dir(unsigned p, bool v) { (void)p; (void)v; }
void gpio_put(unsigned p, bool v) { (void)p; (void)v; }
void gpio_pull_up(unsigned p) { (void)p; }
bool gpio_get(unsigned p) { (void)p; return !pressed; }
void adc_init(void) {}
void adc_gpio_init(unsigned p) { (void)p; }
void adc_select_input(unsigned c) { adc_channel = c; }
uint16_t adc_read(void) { return adc_values[adc_channel]; }
void flash_range_erase(uint32_t offset, size_t n) {
    assert(offset + n <= sizeof(test_flash)); memset(test_flash + offset, 255, n);
}
void flash_range_program(uint32_t offset, const uint8_t *p, size_t n) {
    assert(offset + n <= sizeof(test_flash)); memcpy(test_flash + offset, p, n);
}
void audio_paddle_hit(void) {}
void audio_wall_hit(void) {}
void audio_score(void) {}
void audio_brick_hit(void) {}
void audio_confirm(void) {}
void audio_attract_tick(void) {}

static void setup(int phase) {
    pressed = false;
    adc_values[0] = adc_values[1] = 2048;
    input_init(); hi_init(); game_init();
    mode = MODE_VERSUS;
    begin_phase(phase, 1);
    state = GS_PLAY;
    paddle_pos[0] = paddle_pos[1] = 80;
}

static void test_turbo(void) {
    // All Q8 alignments at both paddles, including the former 9.25 -> 3.75 miss.
    for (int p = 0; p < 2; p++) for (int frac = 0; frac < 256; frac++) {
        setup(PHASE_CLASSICO);
        ball_x = ((p == 0) ? 9 : 244) * 256 + frac;
        ball_y = 90 * 256;
        ball_vx = p ? TURBO_MAX_Q : -TURBO_MAX_Q;
        ball_vy = 0; ball_speed_q = BALL_SPEED_MAX_Q; turbo_frames = 60;
        physics();
        assert(last_hitter == p);
        assert(p ? ball_vx < 0 : ball_vx > 0);
        assert(phase_score[0] == 0 && phase_score[1] == 0);
    }
    // A real miss must still score.
    setup(PHASE_CLASSICO);
    ball_x = 9 * 256 + 64; ball_y = 30 * 256;
    ball_vx = -TURBO_MAX_Q; ball_vy = 0; turbo_frames = 60;
    for (int i = 0; i < 10 && state == GS_PLAY; i++) physics();
    assert(phase_score[1] == 1 && last_hitter == -1);
}

static void test_spin(void) {
    setup(PHASE_PINBALL);
    int32_t vx = 1280, vy = 0;
    // Feed repeated real obstacle contacts; no paddle resets the speed.
    for (int i = 0; i < 1000; i++) {
        int32_t x = 121 * 256, y = 94 * 256;
        vx = abs(vx);
        assert(phase_ball_collide(118 * 256, y, &x, &y, &vx, &vy));
        int64_t norm2 = (int64_t)vx * vx + (int64_t)vy * vy;
        assert(norm2 >= 1279LL * 1279 && norm2 <= 1281LL * 1281);
        assert(vx < 0);
    }
}

static void test_input(void) {
    setup(PHASE_CLASSICO);
    adc_values[0] = 4095; adc_values[1] = 0;
    for (int i = 0; i < 100; i++) input_poll();
    assert(input_paddle_y(0, 168) == 168);
    assert(input_paddle_y(1, 168) == 0);
    adc_values[0] = 0; adc_values[1] = 4095;
    for (int i = 0; i < 100; i++) input_poll();
    assert(input_paddle_y(0, 168) == 0);
    assert(input_paddle_y(1, 168) == 168);
}

static void test_button(void) {
    setup(PHASE_CLASSICO);
    // A single sampled glitch is not a press; release bounce cannot re-arm.
    const bool sequence[] = {true, false, true, true, true, false, true, true,
                             false, false, false, true, true, true};
    int edges = 0;
    for (unsigned i = 0; i < sizeof(sequence) / sizeof(*sequence); i++) {
        pressed = sequence[i]; input_poll();
        if (input_seletor_pressed()) edges++;
    }
    assert(edges == 2);
    pressed = true; input_init(); input_poll();
    assert(!input_seletor_pressed());
}

static void test_menu(void) {
    setup(PHASE_REBOUND);
    ball_x = 40 * 256; ball_y = FB_HEIGHT * 256; ball_vx = 0; ball_vy = 1280;
    state = GS_HIGH_SCORES;
    open_menu();
    assert(state == GS_MENU);
    assert(phase_current() == PHASE_CLASSICO);
    assert(ball_x == (FB_WIDTH / 2) * 256 && ball_vx != 0);
    assert(ball_y < FB_HEIGHT * 256);
}

static void test_scores(void) {
    setup(PHASE_CLASSICO);
    memset(test_flash, 255, sizeof(test_flash)); hi_load();
    assert(!hi_qualifies(0));
    for (int i = 1; i <= 12; i++) assert(hi_consider(i, 1, MODE_ARCADE, "ABC"));
    hi_save(); hi_init(); hi_load();
    assert(hi_get()->entries[0].score == 12);
    assert(hi_get()->entries[HISCORE_COUNT - 1].score == 4);
    test_flash[20] ^= 1; hi_load();
    assert(hi_get()->entries[0].score == 0);
    total_score[0] = pontos_em_disputa(phase_idx);
    total_score[1] = 0;
    assert(!fim_de_jogo());
    total_score[0]++; assert(fim_de_jogo());
    mode = MODE_ARCADE; phase_score[1] = PHASE_WIN_SCORE;
    assert(fim_de_jogo());
}

static void test_phases(void) {
    for (int phase = 0; phase < PHASE_COUNT; phase++) for (int scorer = 1; scorer <= 2; scorer++) {
        setup(phase); reset_round(scorer);
        assert(ball_x >= 0 && ball_x < FB_WIDTH * 256);
        assert(ball_y >= 0 && ball_y < FB_HEIGHT * 256);
        for (int i = 0; i < 5; i++) physics();
        assert(state == GS_PLAY);
        assert(scorer == 1 ? ball_vx > 0 : ball_vx < 0);
        int range = phase_paddle_range();
        rect_t seg[PADDLE_SEG_MAX];
        for (int pos = 0; pos <= range; pos++) for (int p = 0; p < 2; p++) {
            int n = phase_paddle_segments(p, pos, seg);
            assert(n > 0 && n <= PADDLE_SEG_MAX);
            for (int k = 0; k < n; k++) {
                assert(seg[k].x >= 0 && seg[k].x + seg[k].w <= FB_WIDTH);
                assert(seg[k].y >= 0 && seg[k].y + seg[k].h <= FB_HEIGHT);
            }
        }
    }
}

static void test_pause(void) {
    setup(PHASE_CLASSICO); mode = MODE_ARCADE;
    paddle_pos[0] = 80; pause_sel = 0; confirma_pausa();
    assert(state == GS_COUNTDOWN && paddle_travado[0] && !paddle_travado[1]);
    adc_values[0] = 4095;
    for (int i = 0; i < 100; i++) input_poll();
    update_paddles(); assert(paddle_pos[0] == 80 && paddle_travado[0]);
    adc_values[0] = 80 * 4095 / 168;
    for (int i = 0; i < 100; i++) input_poll();
    update_paddles(); assert(!paddle_travado[0]);
}

static void test_motion(void) {
    // Substeps must preserve fractional displacement and tick timers only once.
    for (int v = -1408; v <= 1408; v += 13) {
        setup(PHASE_CLASSICO);
        ball_x = 100 * 256; ball_y = 80 * 256;
        ball_vx = v; ball_vy = -v / 3;
        int32_t expected_y = ball_y + ball_vy;
        physics();
        assert(ball_x == 100 * 256 + v && ball_y == expected_y);
    }
    setup(PHASE_REBOUND);
    ball_x = 40 * 256; ball_y = 50 * 256;
    ball_vx = 10; ball_vy = 100;
    physics(); assert(ball_vy == 100 + GRAVITY_Q);
    setup(PHASE_CLASSICO);
    turbo_frames = 20; physics(); assert(turbo_frames == 19);
}

static void test_geometry(void) {
    setup(PHASE_CLASSICO);
    efeito[0][BONUS_ESCUDO] = BONUS_EFEITO_FRAMES; escudo_arma(0);
    uint32_t shield = escudo_alive[0];
    ball_x = 9 * 256 + 64; ball_y = 90 * 256;
    ball_vx = -TURBO_MAX_Q; ball_vy = 0; turbo_frames = 60;
    physics(); assert(last_hitter == 0 && escudo_alive[0] == shield);
    setup(PHASE_TRIPLO);
    // Middle of a gap must not be treated as one solid paddle.
    ball_x = 9 * 256; ball_y = 90 * 256;
    ball_vx = -TURBO_MAX_Q; ball_vy = 0; turbo_frames = 60;
    physics(); assert(last_hitter == -1);
    for (int p = 0; p < 2; p++) {
        setup(PHASE_REBOUND);
        paddle_pos[p] = 40;
        rect_t seg[PADDLE_SEG_MAX]; phase_paddle_segments(p, 40, seg);
        ball_x = (seg[0].x + 8) * 256;
        ball_y = (seg[0].y - BALL_SIZE - 1) * 256;
        ball_vx = 0; ball_vy = BALL_VY_MAX_Q;
        physics(); assert(last_hitter == p && ball_vy < 0);
        assert(phase_score[0] == 0 && phase_score[1] == 0);
    }
    // Obstacle pushes above y=0 are valid negative Q8, not signed left shifts.
    int32_t x = 10 * 256, y = 0, vx = 0, vy = 100;
    bounce_off(x, -3 * 256, 0, 0, 20, 7, &x, &y, &vx, &vy);
    assert(y == -3 * 256 && vy < 0);
}

static void test_timeouts(void) {
    setup(PHASE_CLASSICO);
    state = GS_PAUSE; pause_sel = 1; state_timer = PAUSE_TIMEOUT_S * 60;
    pause_pot_ref[0] = pause_pot_ref[1] = 2048;
    frame_pause(); assert(state == GS_ATTRACT);
    initials_player = 1; total_score[0] = 10; initials_armed = false;
    state = GS_ENTER_INITIALS; state_timer = INITIALS_TIMEOUT_S * 60;
    frame_enter_initials();
    assert(state == GS_HIGH_SCORES && hi_get()->entries[0].score == 10);
    setup(PHASE_CLASSICO);
    efeito[0][BONUS_RAQUETE] = 100;
    phase_round_reset(); assert(efeito[0][BONUS_RAQUETE] == 100);
    bonus_out_t out; phase_update(ball_x, ball_y, paddle_pos, -1, &out);
    assert(efeito[0][BONUS_RAQUETE] == 99);
    phase_begin(PHASE_CLASSICO); assert(!efeito[0][BONUS_RAQUETE]);
}

static void test_soak(void) {
    for (int phase = 0; phase < PHASE_COUNT; phase++) {
        setup(phase);
        int rounds = 0;
        for (int i = 0; i < 30000; i++) {
            update_paddle_ai(0); update_paddle_ai(1);
            physics();
            if (state != GS_PLAY) {
                rounds++; reset_round(last_scorer); state = GS_PLAY;
            } else {
                bonus_out_t out;
                phase_update(ball_x, ball_y, paddle_pos, last_hitter, &out);
            }
            assert(abs_i(ball_vx) < 8 * 256 && abs_i(ball_vy) < 8 * 256);
            assert(ball_x >= -8 * 256 && ball_x <= (FB_WIDTH + 8) * 256);
            assert(ball_y >= -BALL_SIZE * 256 && ball_y <= FB_HEIGHT * 256);
        }
        assert(rounds > 0);
    }
}

static void dump_trace(void) {
    for (int phase = 0; phase < PHASE_COUNT; phase++) for (int sample = 0; sample < 200; sample++) {
        setup(phase);
        paddle_pos[0] = paddle_pos[1] = phase_paddle_range() / 2;
        ball_x = ((sample * 37) % (FB_WIDTH - BALL_SIZE)) * 256 + sample;
        ball_y = ((sample * 23) % (FB_HEIGHT - BALL_SIZE)) * 256 + sample;
        ball_vx = sample & 1 ? TURBO_MAX_Q : -TURBO_MAX_Q;
        ball_vy = (sample % 9 - 4) * 128;
        ball_speed_q = BALL_SPEED_MAX_Q; turbo_frames = 60; rng = 42;
        physics();
        printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", phase, sample,
               (int)ball_x, (int)ball_y, (int)ball_vx, (int)ball_vy, last_hitter,
               state, phase_score[0], phase_score[1]);
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "turbo")) test_turbo();
    else if (!strcmp(argv[1], "spin")) test_spin();
    else if (!strcmp(argv[1], "input")) test_input();
    else if (!strcmp(argv[1], "button")) test_button();
    else if (!strcmp(argv[1], "menu")) test_menu();
    else if (!strcmp(argv[1], "scores")) test_scores();
    else if (!strcmp(argv[1], "phases")) test_phases();
    else if (!strcmp(argv[1], "pause")) test_pause();
    else if (!strcmp(argv[1], "motion")) test_motion();
    else if (!strcmp(argv[1], "geometry")) test_geometry();
    else if (!strcmp(argv[1], "timeouts")) test_timeouts();
    else if (!strcmp(argv[1], "soak")) test_soak();
    else if (!strcmp(argv[1], "trace")) { dump_trace(); return 0; }
    else return 2;
    printf("PASS %s\n", argv[1]);
    return 0;
}
