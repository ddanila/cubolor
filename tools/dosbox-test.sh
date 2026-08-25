#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
rm -f "$project_root/build/SELFTEST.LOG" "$project_root/build/SELFTEST-EMULATOR.LOG"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dosbox-x -conf "$project_root/dosbox-x.conf" \
    -silent -fastlaunch -time-limit 30 -c "mount c $project_root" -c "c:" -c "cd build" \
    -c "CUBOLOR.EXE -selftest > SELFTEST.LOG" -c "exit" \
    >"$project_root/build/SELFTEST-EMULATOR.LOG" 2>&1
if ! grep -q '^CUBOLOR SELFTEST PASS CRC=697130A6' "$project_root/build/SELFTEST.LOG"; then
    sed -n '1,80p' "$project_root/build/SELFTEST.LOG" >&2 || true
    tail -n 40 "$project_root/build/SELFTEST-EMULATOR.LOG" >&2 || true
    exit 1
fi
sed -n '1,5p' "$project_root/build/SELFTEST.LOG"
