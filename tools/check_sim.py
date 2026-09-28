#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare 2,200 physics scenarios in the simulator and the compiled firmware C."""
import subprocess
import sys
import sim


def main():
    rows = subprocess.check_output([sys.argv[1], "trace"], text=True).splitlines()
    rng = 42

    def rand32():
        nonlocal rng
        rng = (rng * 1664525 + 1013904223) & 0xFFFFFFFF
        return rng

    sim.random.getrandbits = lambda bits: rand32() & ((1 << bits) - 1)
    sim.random.randint = lambda lo, hi: lo + rand32() % (hi - lo + 1)
    for row in rows:
        phase, sample, *expected = map(int, row.split(","))
        game = sim.Game({}, {})
        game.mode = sim.MODE_VERSUS
        game.begin_phase(phase, 1)
        game.state = sim.GS_PLAY
        game.paddle_pos = [game.phase.paddle_range() // 2] * 2
        game.ball_x = ((sample * 37) % (sim.FB_W - sim.BALL_SIZE)) * 256 + sample
        game.ball_y = ((sample * 23) % (sim.FB_H - sim.BALL_SIZE)) * 256 + sample
        game.ball_vx = sim.TURBO_MAX_Q if sample & 1 else -sim.TURBO_MAX_Q
        game.ball_vy = (sample % 9 - 4) * 128
        game.ball_speed_q = sim.BALL_SPEED_MAX_Q
        game.turbo_frames = 60
        rng = 42
        game.physics()
        actual = [game.ball_x, game.ball_y, game.ball_vx, game.ball_vy,
                  game.last_hitter, game.state, *game.phase_score]
        assert actual == expected, (phase, sample, expected, actual)
    assert len(rows) == 2200
    print("PASS simulator: 2,200 scenarios match production C exactly")


if __name__ == "__main__":
    main()
