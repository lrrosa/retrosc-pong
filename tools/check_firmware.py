#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the real firmware C on the host. Requires a native GCC or Clang."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--sanitize", action="store_true",
                        help="trap undefined behavior (GCC/Clang)")
    args = parser.parse_args()
    out = ROOT / "build" / "tests"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ("test_firmware.exe" if os.name == "nt" else "test_firmware")
    env = os.environ.copy()
    compiler = shutil.which(args.cc) or args.cc
    env["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + env.get("PATH", "")
    sanitize = ["-fsanitize=undefined", "-fsanitize-undefined-trap-on-error"] if args.sanitize else []
    subprocess.run([compiler, "-std=c11", "-O2", "-funsigned-char", "-Wall", "-Wextra", "-Werror",
                    *sanitize,
                    "-Itests/stubs", "-Isrc", "tests/test_firmware.c",
                    "src/gfx.c", "src/font.c", "src/assets.c", "-o", str(exe)],
                   cwd=ROOT, env=env, check=True)
    failures = []
    for case in ("turbo", "spin", "input", "button", "menu", "scores", "phases", "pause",
                 "motion", "geometry", "timeouts", "soak"):
        result = subprocess.run([str(exe), case], cwd=ROOT, env=env, timeout=60)
        if result.returncode:
            failures.append(case)
    if failures:
        raise SystemExit("FAILED: " + ", ".join(failures))
    subprocess.run([os.sys.executable, str(ROOT / "tools" / "check_sim.py"), str(exe)],
                   cwd=ROOT, env=env, check=True)
    print("All firmware regression tests passed.")


if __name__ == "__main__":
    main()
