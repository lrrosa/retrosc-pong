# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A 1- or 2-player **Pong for the Raspberry Pi Pico (RP2040)**, built for an
arcade cabinet at the RetroSC event. The firmware generates **NTSC composite
video** (1-bit, 256×192) entirely in PIO + DMA, reads two 10 kΩ potentiometers
via ADC, and plays PWM audio. C / Pico SDK. No RTOS — a single `while` loop in
`main.c` synced to vsync.

Two modes (**arcade** = 1 player vs CPU, **versus** = 2 players) picked from a
menu on the attract screen, and **10 phases** ("fases") played in sequence.
User-facing text is Portuguese, uppercase, **no accents** (the 5×7 font only
covers ASCII 0x20–0x5F).

## Build, flash, run

Toolchain is the **Pico SDK 1.5.1** Windows installer (GCC 10.3.1, CMake, Ninja).
Staying on 1.5.1 is intentional (RP2040-only); the code uses only stock SDK APIs
so it is forward-compatible with 2.x. `clk_sys` is forced to **125 MHz** in
`main.c` — the PIO `clkdiv` values depend on this; do not change one without the other.

Build (PowerShell, this machine's absolute paths):

```powershell
& "C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\pico-env.ps1"
& "C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\ninja\ninja.exe" -C build
```

First-time configure: `cmake -G Ninja -B build` (after dot-sourcing `pico-env.ps1`
so `PICO_SDK_PATH` and the toolchain are set). Output is `build/pong-rp2040.uf2`.

Flash: hold BOOTSEL, plug USB, copy `build/pong-rp2040.uf2` to the `RPI-RP2` drive.

There are **no unit tests**. Validation = the build must succeed (the PIO assembler
catches timing/instruction errors) plus running a simulator. For gameplay changes,
the fastest check is driving `tools/sim.py` headless from a scratch script (import
it as a module, stub the `draw_*` methods, feed `input_pot`/`input_seletor` and
step `frame()`); that is how the phase pacing numbers below were measured.

## Simulators

- **Python** (`python tools/sim.py`, needs `pygame`): re-implements the game logic
  of `game.c` **and** `phases.c` and **parses `src/assets.c` and `src/font.c` at
  runtime via regex**, so bitmap/font changes show up without porting. Mouse-Y
  halves = the two pots, Space = SELETOR, N = skip phase. It must be kept in sync
  by hand when game logic changes. `--shots DIR` renders every screen to PNG
  headless (used to regenerate `docs/images/sim_*.png`, then `crt_preview.py --all`).
- **Wokwi** (`diagram.json` + `wokwi.toml`, "Wokwi for VS Code" extension): runs the
  real compiled `.elf` on an emulated RP2040 and shows the composite output on a
  `wokwi-tv` part. Open the `pong-rp2040` folder as the workspace root. `*.vcd`
  (logic-analyzer dumps) are gitignored.
- **CRT preview** (`python tools/crt_preview.py`, needs `pillow numpy`): applies a
  TV-tube effect to framebuffer PNGs.

## Architecture

**Video (`ntsc.{pio,c,h}`) is the subtle part.** Two PIO state machines on one PIO:
- `ntsc_sync` (`.side_set 1` on the SYNC pin) consumes a **per-scanline descriptor**
  word streamed by chained DMA. Descriptor bit0 = vsync line, bit1 = active line.
  On active lines it raises IRQ 4. **One line = 54 PIO cycles at clkdiv 147.118**
  (= 63.55 µs). The first 4 cycles (descriptor fetch, sync held low) are the
  **H-sync = 4.71 µs** — this exact width matters (see gotchas).
- `ntsc_data` waits on IRQ 4 and clocks 256 framebuffer bits onto the VIDEO pin.
- The two pins form a **2-resistor DAC** (470 Ω sync + 270 Ω video) → 3 analog
  levels (sync/black/white). On Wokwi the same two pins are read digitally by `wokwi-tv`.
- `line_descriptors[262]` is built in `ntsc_init()` from `LINES_VSYNC` / `LINES_TOP_BLANK`
  / `LINES_ACTIVE` / `LINES_BOT_BLANK` (`config.h`). DMA chains (sync descriptors and
  pixel data) re-trigger each frame and bump `ntsc_frame_count`.

**Framebuffer** `fb` is 256×192, 1-bit, MSB-first: `word = y*8 + (x>>5)`,
`bit = 31 - (x & 31)`. All drawing is in `gfx.{c,h}`; text via the 5×7 font in
`font.{c,h}` (glyphs indexed `['X' - 0x20]`). In 1-bit there is no contrast to fall
back on: white text over bricks or bumpers is unreadable, so anything drawn on top of
the court (the countdown digits, the bonus mascot's "BONUS", the prize name) clears a
black rectangle first — `text_boxed_at()` in `game.c`, of which `center_text_boxed()`
is the centred case. The prize name is drawn at a quarter of the width, on the side of
whoever took it: it says *who* as well as *what*, and it stays out of the central band
where the barrier phases keep their bricks.

**Game** (`game.{c,h}`) is a state machine: `GS_ATTRACT → GS_MENU → GS_PHASE_INTRO →
GS_COUNTDOWN → GS_PLAY → GS_ROUND_END → (GS_PHASE_END → next phase | GS_GAME_OVER) →
GS_ENTER_INITIALS → GS_HIGH_SCORES`. Ball physics is fixed-point Q8. `game_frame()`
is called once per vsync.
- `GS_PAUSE` hangs off `GS_PLAY`/`GS_COUNTDOWN` (`pediu_pausa()`): CONTINUAR returns
  through a short countdown — never straight into play, and never from `GS_ROUND_END`,
  which would resume with the ball already off court and score a phantom point. Two
  details there are not cosmetic:
  - The item is picked by pot **movement** (`PAUSE_POT_STEP` from a reference taken
    when the pause opened), not by absolute position. With absolute mapping, a pot
    resting in the lower half opened the pause with SAIR DO JOGO highlighted.
  - Both screens that wait for a person time out — `PAUSE_TIMEOUT_S` (30 s) applies
    the highlighted item, `INITIALS_TIMEOUT_S` (60 s) saves whatever was typed. An
    arcade cabinet cannot be left parked on a menu, but the initials screen needs the
    longer clock: three letters dialled in on a pot is slow, and 30 s cut people off
    mid-name on the real machine.
  - Coming back, both paddles are **locked** (`paddle_travado`) until each pot returns
    within `PADDLE_TAKEOVER_TOL` of where the paddle stopped, and a locked paddle
    blinks. Pots are absolute, so without this the paddle teleports to wherever the pot
    ended up — pausing became a way to reposition and save a lost ball. A player who
    did not touch the pot notices nothing: the first read already matches. Only lock a
    paddle a *pot* drives: locking the CPU's in arcade left it blinking until the next
    point, since `update_paddle_ai()` never clears the flag.
- `GS_MENU` is reachable **only** by pressing SELETOR in attract (deliberate: the
  menu must not be visible in the attract loop). Pots pick the item with a ±300-count
  dead zone around mid-scale; SELETOR confirms; 15 s idle returns to attract.
- Scoring: `phase_score[]` runs to `PHASE_WIN_SCORE` (9) per phase, and every point
  also adds to `total_score[]`. `fim_de_jogo()` decides when the match ends: in
  **arcade** the first phase the player *loses* ends it (the run's total is the score,
  and the game-over screen says how far they got); in **versus** the match also stops
  early once one side cannot catch up even by winning everything left —
  `pontos_em_disputa()` sums `PHASE_WIN_SCORE` per remaining phase plus the bonus on
  the phases that have it (`phase_tem_bonus()`, the single source of truth for that
  list). The test is `>`, not `>=`: a difference exactly equal to what is left still
  allows a draw, so the match goes on. That total goes to the high-score table (in
  arcade always the human's). Initials: pot cycles A–Z, SELETOR confirms.
- The attract loop alternates with the high-score table every `ATTRACT_TIMEOUT_S`
  (20 s); SELETOR opens the menu from either screen.
- The CPU paddle (`update_paddle_ai`) only chases the ball while it is incoming,
  drifts to center otherwise, and re-rolls `ai_bias` (aim error) on every hit. Both
  its speed and its aim improve phase by phase — `ai_curva()` interpolates linearly
  on `phase_idx` between the two ends of `AI_SPEED_*` / `AI_ERROR_*`.

**Phases** (`phases.{c,h}`): each phase is just a *scenario*; the rules live in
`game.c` and are identical for both modes. A phase controls five things:
- **paddles** — `phase_paddle_segments()` (up to `PADDLE_SEG_MAX` rectangles) and
  `phase_paddle_range()`, which is the pot's travel (vertical normally, *horizontal*
  in REBOUND). `game.c` never assumes a paddle shape or orientation.
- **obstacles** — *bricks* (columns of `BRICK_W`×`BRICK_H` that disappear when hit;
  each column's live rows are a **32-bit mask**, so arming a wall is one mask copy)
  and *solids* (rectangles that only bounce: pinball bumpers, the moving COLUNA stack,
  the volley net). Both go through `phase_ball_collide()` → `bounce_off()`.
- **serve** — `phase_serve_x()` / `phase_serve_y()`. `begin_phase(idx, scorer)` takes
  the previous phase's winner, so each phase opens with the ball heading to whoever
  lost the last one (random for phase 1). **Whoever scores, the ball always leaves
  toward the other side** — no exceptions per phase, and that has to hold a few frames
  in, not only at the instant of the serve (see the serve gotcha).
- **live things** — `phase_update()` runs once per play frame and owns the bonus
  mascot, the ship, its shots, the moving column and the shrink timers; it returns
  a `bonus_out_t` (the mascot pays the last hitter). The ship also
  **shoots back**: `phase_ball_collide()` cannot aim the reply because it never sees
  `last_hitter`, so it leaves the side in `nave_revide` and `update_nave()` fires it
  the same frame — `physics()` always runs before `phase_update()` in `frame_play()`,
  and that order is what makes the reply land on the frame of the hit. A reply
  restarts `nave_cool`, so it counts as the shot of the turn instead of stacking on
  top of the periodic one (measured: it moved the phase from 8.9 to 9.1 s per point,
  and the human paddle spends 8.1 % of the frames halved instead of 7.4 %). With the
  four shot slots full the reply is simply dropped — 2.5 % of the hits in a rally
  measured in the sim. `frame_play()` adds those to
  `total_score[]` **only** — a bonus never touches the phase score, so it cannot close
  a phase; it just flashes the total (`total_flash[]`).
- **flags** — `phase_flags()` returns `PF_GRAVITY` / `PF_FLOOR_SCORES` /
  `PF_SIDE_WALLS` / `PF_PADDLE_HORIZ` / `PF_NO_CENTER_LINE` / `PF_TEM_BONUS`;
  `physics()` branches on these instead of special-casing phase ids.

Phase order **is** the difficulty curve (see `phase_id_t`) and ends on
`PHASE_BARREIRA3`. **No phase ever rebuilds its bricks** — damage lasts to the end of
the phase, everywhere. In the three barriers the 2→3→4 column progression exists
because a full barrier from the start means the player spends most of the time hitting
their own wall; BARREIRA III is also pre-cut into blocks (`barreira3_blocos` =
5-4-2-4-5 rows plus the four corridors, which is exactly the 24 rows a column has).
`PHASE_MURALHA` is the opposite bet: its two walls sit right at the goals and start
**mostly closed** (`MURALHA_CHEIOS`/`MURALHA_VAZIOS` = 3 rows of brick per 2 of gap,
62 % wall), so the phase is an excavation — one brick is enough to open an 8 px hole
for a 3 px ball, and the pace climbs as they accumulate. It used to rebuild both walls
at every point, and the flag that did it (`brick_rebuild_round`) is gone rather than
left permanently false — it had also been serving as the test for something else, see
the serve gotcha.
**How much the wall actually costs is worth knowing before tuning it**: a fully solid
wall measured 336 s for the phase, 8 px gaps 328 s, 16 px gaps 258 s and half-open
24 px gaps 250 s. That whole range is 25 %, because the wall is the *second* filter —
the ball has to beat a paddle before it ever reaches a hole. The phase is long for a
different reason, and so is BARREIRA III (266 s, and no wall at the goal): sharpening
the CPU stretched every phase, that one from ~68 s. Reach for `PHASE_WIN_SCORE` before
reaching for the brick pattern. The **bonus mascot is not a phase**: any phase with
`PF_TEM_BONUS` gets it on a random timer (at most `BONUS_PASSES_MAX` passes per phase),
crossing on a diagonal from the top or the bottom with "BONUS" blinking beside it.
Hitting it draws one of five prizes (`bonus_tipo_t`), the CPU included: points, a
bigger paddle, the rival's paddle halved, a breakable shield in front of your own
goal, or the turbo. Four rules keep them from breaking things:
  - **the paddle changes size, never travel** — `phase_paddle_range()` is always the
    normal paddle's, and what grows or shrinks stays centred on the position read
    from the pot. Change the range mid-phase and the paddle jumps under the player's
    hand, which is the same bug class as the pause takeover.
  - **the turbo has a hard ceiling** (`TURBO_MAX_Q`). Ball/paddle collision is
    instantaneous overlap, no sweep: with both 3 px wide, a ball moving 6 px/frame
    can be in front of the paddle on one frame and behind it on the next without ever
    overlapping. The 4 px brick columns break at 7. Everything above 5.5 px/frame is
    a goal through a solid paddle.
  - **the two paddle bonuses can meet** — one player can hold RAQUETE while the other
    lands ENCOLHE on them. They cancel, they do not stack.
  - **effects outlive the round** (only `phase_begin()` clears them), unlike the
    nave's shrink, which `phase_round_reset()` wipes. The 10 s are 10 s of play:
    `phase_update()` only runs in `GS_PLAY`, so they freeze between points.
  The split of ownership is deliberate: `phases.c` counts the 10 s windows because the
  paddles and the bricks live there, and `game.c` counts the 1.5 s turbo burst because
  it owns `ball_speed_q`. `phase_turbo()` is the only wire between them.
The ship (`PHASE_NAVE`) is the opposite: it stays in the middle of the court, shoots,
and the ball bounces off it. The two swapped roles late — the mascot is the RetroSC
emblem, so it belongs to the reward, not to an obstacle — which is why the sprite of
one is a blit (`retrosc_mascote_data`) and the other is drawn from rectangles at
`NAVE_SCALE`.

**Input** (`input.{c,h}`): ADC poll with an IIR filter. `input_paddle_y(player, range)`
maps ADC→paddle position over a phase-dependent range; `input_pot_raw(player)` is used
for initials and menu; `input_last_moved()` tells the menu which pot to read;
`input_seletor_pressed()` is the SELETOR edge. Also forces the Pico SMPS into PWM mode
(GPIO23 high) for a cleaner ADC supply.

**High scores** (`highscores.{c,h}`): persisted in the **last flash sector**
(magic + version + checksum; **v3** stores the mode, so upgrading wipes the table). Flash-touching functions are `__not_in_flash_func`
and wrap `save_and_disable_interrupts()`.

**Assets** (`assets.{c,h}`): const 1-bit bitmaps (the RetroSC logo 220×69 and the
mascot at 16×16, used as the bonus that crosses the court), generated from PNG by
`tools/png_to_c.py` and pasted in. The mascot PNG in `docs/images/` is already
cropped and **inverted** — the source art is dark-on-light — and the emblem's ring,
which is baked into that art, is masked off when generating the sprite: at 16×16 the
ring merges with the creature into a blob. Pin/dimension constants all
live in `src/config.h`.

## Project-specific gotchas

- **NTSC sync timing is non-obvious.** A standard 4.7 µs H-sync (the 54-cycle / clkdiv
  147 scheme) is required for `wokwi-tv` to lock and is also better for real CRTs. The
  delay counts in `ntsc.pio` are hand-balanced so **every code path through a line totals
  exactly 54 cycles**; with `.side_set 1` the max delay is `[15]`, so long waits are split.
- **Image centering is two separate knobs:** horizontal = the back-porch `nop [..]`
  before `irq nowait 4` in `ntsc.pio` (~7 px/cycle); vertical = `LINES_TOP_BLANK` in
  `config.h` (1 line/unit). `LINES_TOP_BLANK = 35` centers on a **real TV** (the
  project target); Wokwi's visible window is shifted up and would want ~55.
- **Vsync must be serrated.** Each of the 3 vsync lines carries 2 half-line broad
  pulses (23 cycles LOW + 4 HIGH). A whole-line LOW vsync loses the TV's H-lock:
  CRTs drop sync periodically and LED TVs never lock (real-hardware validated).
- **XIP suspends during flash writes**, so anything the running PIO/DMA dereferences must
  live in RAM, not flash/XIP. `fb_base_ptr` and `desc_base_ptr` in `ntsc.c` are
  deliberately non-`const` for this reason.
- **`<<` binds tighter than `<`/`>`** — parenthesize shift expressions in comparisons
  (a real bug fixed here: `ball_x < ((PADDLE_MARGIN + PADDLE_W) << 8)`).
- **The CPU's aim is the only difficulty knob it has; its speed is not.** Measured in
  the sim over every ball the CPU let through: the paddle arrives **0.9–1.2 px** from
  where it was aiming, and is more than 4 px behind in under 1 % of them. It is never
  late — when it looks sluggish on the cabinet it is standing exactly where it decided
  to stand, 15–20 px off the ball's line. So `AI_SPEED_*` buys almost nothing (it is
  kept as insurance for the turbo ball, which moves 5.5 px/frame) and every real
  change comes from `AI_ERROR_*`.
  The floor is hard: an aim error **smaller than half a paddle** (`PADDLE_H/2` = 12 px)
  is no error at all, because the ball still lands somewhere on the paddle. At 12 px a
  phase took over 10 minutes (measured back when `PHASE_WIN_SCORE` was 10); at 13 the
  CPU won 100 % of the last phase. The curve therefore runs 26 → 15, stopping two
  pixels above the cliff. Against a simulated player with a ±14 px error the CPU's
  average score per phase went from 0.2–2.3 (flat, and *falling* in the late phases,
  since their scenery favours whoever aims better) to 1.5 in phase 1 and 5.0–5.9 in
  phases 8–10. Phase geometry still dominates any single phase — TRIPLO's gapped
  paddle is a coin toss at any error, BARREIRA I a walkover — so read the curve across
  the ten, never one phase at a time.
- **Brick gaps have to be much bigger than the ball.** The ball is 3 px and only
  scores if it fits entirely inside a gap: back when MURALHA started with gaps already
  open, an 8 px gap measured 34 s per point and a 24 px gap 16 s. It now starts solid
  and the players cut their own gaps, but the arithmetic is the same — one missing
  brick is an 8 px hole, which is passable but not generous. Measure pacing in the sim
  before shipping a phase.
- **The ball can never be served from inside something.** Where the middle of the court
  is occupied, `phase_serve_x()` moves the serve to the *receiver's* own side — hugging
  the wall in the barrier phases, ±34 px from the centre in PINBALL, COLUNA and NAVE —
  which still leaves their whole half-court between the ball and their goal. Born inside
  an obstacle, it is the **first bounce** that picks the side, and half the time that is
  the side of whoever just scored. Three wrong turns here: serving 30 px from the goal
  made BARREIRA II close a 9-point phase in 10 s; "fixing" that by launching the ball
  *away* from the receiver read as the ball going to whoever just scored; and PHASE_NAVE
  kept serving from the centre long after the others were fixed, because the ship is
  neither a brick nor a solid — it is collided separately in `phase_ball_collide()`, so
  an audit that walks `brick_cols`/`solids` says the centre is clear. It is not: the ship
  resets to the middle of the court on *every* serve, and the ball was born inside it in
  100 % of the rounds (measured: 51 % of the serves left toward the scorer, and 99 % of
  them teleported the ball ~13 px vertically as `bounce_off()` pushed it out).
  `tools/sim.py` + a loop over `reset_round(scorer)` checks all ten phases at once; the
  test that catches this steps a few frames and looks at the sign of `vx` when the ball
  leaves the central band — not just at the serve. Keep that invariant.
  The condition that picks the hugging serve must describe the *geometry*, not some
  flag that happens to correlate with it. It used to read "has bricks and does not
  rebuild them", which meant the three barriers only because MURALHA was the one phase
  that rebuilt; the day MURALHA stopped rebuilding, that same branch would have served
  its ball at `brick_col_x[0] - 8 - BALL_SIZE` = **x = −11**, off screen, scoring a
  point on every serve. It now asks whether the first column starts past a quarter of
  the width, which is what "the barrier is in the middle" actually means.
- **Anything drawn in the middle column can collide with a phase's bricks** —
  BARREIRA III fills x≈117–139 for the full height, so the HUD keeps the phase score
  and totals outside the central band and the phase name only appears on the intro /
  phase-end screens. `phase_serve_x()` exists for the same reason: serving from the
  center would drop the ball inside the barrier. The HUD is a **single line**: the big
  phase score at cx±30 with the running total beside it, further out — it used to sit
  on a second line at y=34 and that ate a stripe of the court. The pinball diamond
  starts at y=44 for the same reason.
- **A goal has to take the ball off the screen, not just out of play.** Everywhere else
  the ball scores by fully leaving through the side, so the frozen `GS_ROUND_END` frame
  shows an empty court. REBOUND scores on the *floor*, and the ball used to stop there
  with a stripe of pixels still poking out of the bottom edge; `physics()` now parks it
  at `FB_HEIGHT` before `add_point()`.
- **The gameplay beeps are short, and a small TV speaker will not play them.** This
  cost three wrong diagnoses, so the order matters. What *was* real in the firmware:
  `clkdiv` saturates at 255, so with a 10-bit `PWM_TOP` no tone could go below 478 Hz,
  and `audio_paddle_hit()` (226 Hz) and `audio_wall_hit()` (246 Hz) were both silently
  pinned there — identical to each other and a hair from the 490 Hz point, so a wall
  bounce followed by a paddle hit was heard as one continuous tone and the second event
  simply did not exist. `PWM_TOP` = 4095 lifts the floor to 120 Hz, the tones are now
  spread by pitch (paddle 480, wall 640, brick 880, point 150 Hz / 450 ms) and
  `audio_beep()` inserts **one frame of silence** when it cuts a sounding tone short.
  What was **not** in the firmware: the hits that stayed missing after all that. The
  cabinet was being tested on a 10-inch TV whose speakers do not reproduce a 4-frame
  (67 ms) burst; on a normal TV every paddle and brick sound comes through. Two of my
  own theories died on the way — that the speaker's *low* end was the limit (the 150 Hz
  point is the most reliable sound in the game, which disproves it) and that the DC step
  through the 1 µF coupling cap was swallowing short beeps (a 30 kHz mute carrier was
  written to hold the output at 1.65 V, then removed: it fixed nothing measurable and
  put permanent switching on a pin whose noise has already disturbed the pot ADC here).
  The lesson for the next audio change: **replay the events through this module's own
  state machine first** — done here, and it showed every beep firing for its full length,
  which is what finally moved the search off the firmware — and only then suspect the
  reproduction chain, cheapest link first.

- **REBOUND is the phase that stresses the engine's assumptions** — it is the only one
  with horizontal paddles, gravity, scoring floor and bouncing side walls. When adding
  anything to `physics()` or the AI, check it against that phase: the AI, for instance,
  has to chase `ball_x` instead of `ball_y` there. Three things were wrong on the first
  cut and are worth remembering:
  - **A thin horizontal paddle needs a swept test.** The volley paddle is 4 px tall and
    the ball falls up to `BALL_VY_MAX_Q` px per frame, so an overlap test at the instant
    lets the ball jump straight through it. `physics()` checks whether the ball *crossed*
    the paddle's top between `prev_y` and now.
  - **A hit angle derived purely from the offset is unplayable.** With `vx` coming only
    from where the ball hit the paddle, a centered touch sent it straight back up onto
    the player. The touch now always carries `VOLLEY_VX_BASE_Q` toward the opponent and
    the offset only lengthens or shortens it.
  - **Flight time sets the range**: with gravity too low the ball flew the whole screen
    and slammed the far wall. Gravity and the up-impulse are tuned together (`GRAVITY_Q`
    / `VOLLEY_VY_Q`) so the arc clears `NET_TOP` with ~25 px to spare — measured in the
    sim, it crosses the net at y≈80 against a net top of 112. Changing either needs a
    re-measure of both the clearance and where the ball lands.
- **Ball tunneling is bounded by paddle width + ball size** (3 + 3 = 6 px) vs the
  max step `BALL_SPEED_MAX_Q` = 5 px/frame. Raising the max speed past 6 px/frame
  needs swept collision, not just a bigger constant.
- **Pick the bounce face from the crossing, not from the velocity and not from the
  smallest penetration.** Both of the simpler rules were shipped and both were wrong,
  in opposite directions:
  - *smallest penetration* — a ball entering a bumper from above near a corner gets
    pushed sideways, and since its horizontal velocity already points that way nothing
    is inverted: it sails through the obstacle keeping its trajectory.
  - *velocity* — fine while the scenery holds still, wrong the moment it moves. The
    COLUNA stack walks 1 px per frame and catches up with a ball that is already on its
    way out; the penetration is then measured against the face on the *far* side, comes
    out huge, and the other axis wins. In play that reads as the ball touching the top
    or bottom of a post and being sent straight back to whoever just hit it, even with
    the ball well past the middle of the post. Measured in the sim: **a quarter of every
    collision in COLUNA** inverted `vx` without the ball having crossed a vertical face
    at all.

  What `bounce_off()` does now is compare the ball's **previous** position with the
  rectangle's *current* one: whoever already straddled the obstacle's horizontal band
  can only have come in through the top or the bottom, whatever the velocity says. When
  the ball straddled both bands (the obstacle walked onto it) or neither (a true corner
  entry), the shortest way out wins. And the velocity is only inverted when it still
  points inward, so an obstacle that catches up with the ball **pushes** it instead of
  returning it. That is why moving scenery needs no separate routine. The spin applied
  after it must not flip the axis that just bounced, though — with the ball nearly
  vertical the rotation can, and then the clamp to `BALL_VX_MIN_Q` freezes that wrong
  sign and the ball heads back into the bumper it just left.
  The test that catches all of this is cheap: wrap `phase_ball_collide()` in the sim and
  count the hits where an axis was inverted although the previous position already
  straddled that band. It must be zero — for `vx`; a handful of `vy` cases in
  PINBALL/COLUNA are the spin doing its job, not the bounce.
- **Two parallel faces put the ball into orbit.** Bumpers return it at the same angle
  forever, so PINBALL/COLUNA — and REBOUND, whose net closes the same loop against a
  player who is holding still — rotate the velocity a few degrees on every solid hit
  (`BUMPER_SPIN_SHIFT`). The REBOUND case was found by measuring, not by playing: the
  phase stopped scoring at 123 s and ran to the 33-minute cap with the ball shuttling
  between one paddle and the net — 642 touches by that player, 6 by the other, 641
  scenery bounces, no points. A perfect tracker hits at the same offset every time, so
  `on_volley_hit()` returns the identical trajectory, which hits the net at the
  identical spot: a closed cycle. The spin broke it (1288 s → 136 s per phase, no hangs
  in 16 runs). Any deterministic returner — including a real player who simply is not
  moving the pot — can fall into it.
  It must be a *rotation*: the first attempt added a random
  offset to one axis, which random-walks the speed — the ball got slower and more
  vertical over a rally. Measured in the sim, the longest chain of scenery hits without
  touching a paddle dropped from 13 to 5 in COLUNA.
- **One pot value cannot drive both axes with the same sign.** Screen Y grows
  *downward* and X grows *rightward*, so whichever knob rotation feels right for the
  vertical paddles feels backwards for REBOUND's horizontal ones. `update_paddle_humano()`
  mirrors the reading (`lido = range - lido`) when `PF_PADDLE_HORIZ` is set — the
  vertical phases are the player's reference, being nine of the ten, so the volley is
  the one that gets flipped. It is flipped at the *pot reading* only: `paddle_pos[]`
  stays in screen coordinates, which keeps `update_paddle_ai()` and the pause takeover
  comparison untouched.
- **A pot spread over 26 letters is about 10° per letter, which no hand can hold.** The
  initials screen used to derive the letter straight from the pot every frame: a nudge
  of the knob skipped five letters and the one you wanted was already gone. It now has
  two brakes — the current letter's band is widened by `INITIALS_HIST` on both sides
  (hysteresis, which also stops the flicker between two neighbours) and the focus moves
  at most one letter per `INITIALS_STEP_FRAMES`, turning a fast sweep into a readable
  10 letters/s scroll. Both are needed: the rate limit alone still flickers at a band
  edge, the hysteresis alone still blurs on a fast turn. It stays *absolute* on purpose
  — relative stepping cannot reach every letter on a single-turn pot, since from the
  middle you only have half the travel in each direction.
- **The button is the SELETOR** in firmware, docs and diagrams; the v1 PCB silkscreen
  and the KiCad net are still `START` (same GP22). Renaming those means regenerating
  the board and the gerbers — do not do it silently.
- Git: files are authored LF; the LF→CRLF warnings on Windows are expected. Commit/push
  only when asked.
