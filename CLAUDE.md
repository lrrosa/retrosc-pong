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
the court (the countdown digits, the bonus mascot's "BONUS") clears a black rectangle
first — `center_text_boxed()` in `game.c`.

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
  - Both screens that wait for a person (`GS_PAUSE`, `GS_ENTER_INITIALS`) time out
    after 30 s — `PAUSE_TIMEOUT_S` applies the highlighted item, `INITIALS_TIMEOUT_S`
    saves whatever was typed. An arcade cabinet cannot be left parked on a menu.
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
  drifts to center otherwise, and re-rolls `ai_bias` (aim error) on every hit.

**Phases** (`phases.{c,h}`): each phase is just a *scenario*; the rules live in
`game.c` and are identical for both modes. A phase controls five things:
- **paddles** — `phase_paddle_segments()` (up to `PADDLE_SEG_MAX` rectangles) and
  `phase_paddle_range()`, which is the pot's travel (vertical normally, *horizontal*
  in REBOUND). `game.c` never assumes a paddle shape or orientation.
- **obstacles** — *bricks* (columns of `BRICK_W`×`BRICK_H` that disappear when hit;
  each column's live rows are a **32-bit mask**, so "rebuild the wall" is a mask copy)
  and *solids* (rectangles that only bounce: pinball bumpers, the moving COLUNA stack,
  the volley net). Both go through `phase_ball_collide()` → `bounce_off()`.
- **serve** — `phase_serve_x()` / `phase_serve_y()`. `begin_phase(idx, scorer)` takes
  the previous phase's winner, so each phase opens with the ball heading to whoever
  lost the last one (random for phase 1). **Whoever scores, the ball always leaves
  toward the other side** — no exceptions per phase, and that has to hold a few frames
  in, not only at the instant of the serve (see the serve gotcha).
- **live things** — `phase_update()` runs once per play frame and owns the bonus
  mascot, the ship, its shots, the moving column and the shrink timers; it returns
  per-player `bonus[]` points (the mascot pays the last hitter). `frame_play()` adds those to
  `total_score[]` **only** — a bonus never touches the phase score, so it cannot close
  a phase; it just flashes the total (`total_flash[]`).
- **flags** — `phase_flags()` returns `PF_GRAVITY` / `PF_FLOOR_SCORES` /
  `PF_SIDE_WALLS` / `PF_PADDLE_HORIZ` / `PF_NO_CENTER_LINE` / `PF_TEM_BONUS`;
  `physics()` branches on these instead of special-casing phase ids.

Phase order **is** the difficulty curve (see `phase_id_t`) and ends on
`PHASE_BARREIRA3`. The three barriers keep their damage for the whole phase — the
2→3→4 column progression exists because a full barrier from the start means the player
spends most of the time hitting their own wall; BARREIRA III is also pre-cut into
blocks (`barreira3_blocos` = 5-4-2-4-5 rows plus the four corridors, which is exactly
the 24 rows a column has). `PHASE_MURALHA` rebuilds every point
(`brick_rebuild_round`). The **bonus mascot is not a phase**: any phase with
`PF_TEM_BONUS` gets it on a random timer (at most `BONUS_PASSES_MAX` passes per phase),
crossing on a diagonal from the top or the bottom with "BONUS" blinking beside it.
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
- **A CPU aim error smaller than half a paddle is no error at all.** With
  `AI_ERROR_PX = 12` and `PADDLE_H = 24` the CPU still returned every ball on the
  paddle's edge and a phase took over 10 minutes (measured back when
  `PHASE_WIN_SCORE` was 10); `AI_ERROR_PX = 26` (>
  `PADDLE_H/2`) brought it to ~40–120 s. Same trap for any "make it miss sometimes"
  tuning.
- **Brick gaps have to be much bigger than the ball.** The ball is 3 px and only
  scores if it fits entirely inside a gap: an 8 px gap in MURALHA measured 34 s per
  point, a 24 px gap 16 s. Measure pacing in the sim before shipping a phase.
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
- **Anything drawn in the middle column can collide with a phase's bricks** —
  BARREIRA III fills x≈117–139 for the full height, so the HUD keeps the phase score
  and totals outside the central band and the phase name only appears on the intro /
  phase-end screens. `phase_serve_x()` exists for the same reason: serving from the
  center would drop the ball inside the barrier. The HUD is a **single line**: the big
  phase score at cx±30 with the running total beside it, further out — it used to sit
  on a second line at y=34 and that ate a stripe of the court. The pinball diamond
  starts at y=44 for the same reason.
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
- **Pick the bounce face from the ball's direction, never from the smallest
  penetration.** `bounce_off()` resolves on the axis whose *opposing* face the ball
  crossed. The obvious "push out the nearest side" version has a hole: a ball entering
  a bumper from above near a corner gets pushed sideways, and since its horizontal
  velocity already points that way nothing is inverted — it sails through the obstacle
  keeping its trajectory. This also removed the need for a separate routine for moving
  obstacles (the ship, the COLUNA stack): direction-based resolution never leaves the
  ball inside. The spin applied after it must not flip the axis that just bounced,
  though — with the ball nearly vertical the rotation can, and then the clamp to
  `BALL_VX_MIN_Q` freezes that wrong sign and the ball heads back into the bumper it
  just left.
- **Two parallel faces put the ball into orbit.** Bumpers return it at the same angle
  forever, so PINBALL/COLUNA rotate the velocity a few degrees on every bumper hit
  (`BUMPER_SPIN_SHIFT`). It must be a *rotation*: the first attempt added a random
  offset to one axis, which random-walks the speed — the ball got slower and more
  vertical over a rally. Measured in the sim, the longest chain of scenery hits without
  touching a paddle dropped from 13 to 5 in COLUNA.
- **The button is the SELETOR** in firmware, docs and diagrams; the v1 PCB silkscreen
  and the KiCad net are still `START` (same GP22). Renaming those means regenerating
  the board and the gerbers — do not do it silently.
- Git: files are authored LF; the LF→CRLF warnings on Windows are expected. Commit/push
  only when asked.
