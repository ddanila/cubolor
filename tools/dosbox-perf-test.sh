#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
target_cycles=10000
minimum_fps10=500
perf_log="$project_root/build/PERF.LOG"
rm -f "$perf_log" "$project_root/build/PERF-EMULATOR.LOG"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dosbox-x -conf "$project_root/dosbox-x.conf" \
    -set "cpu cycles=fixed $target_cycles" -silent -fastlaunch -time-limit 30 \
    -c "mount c $project_root" -c "c:" -c "cd build" \
    -c "CUBOLOR.EXE -benchmark > PERF.LOG" -c "exit" \
    >"$project_root/build/PERF-EMULATOR.LOG" 2>&1
fps10=$(sed -n 's/\r$//;s/^CUBOLOR BENCH .* fps10=\([0-9][0-9]*\) hz=.*$/\1/p' "$perf_log")
profile=$(sed -n 's/\r$//;s/^CUBOLOR PROFILE frames=200 clear=\([0-9][0-9]*\) board=\([0-9][0-9]*\) cube=\([0-9][0-9]*\) hud=\([0-9][0-9]*\) present=\([0-9][0-9]*\)$/\1 \2 \3 \4 \5/p' "$perf_log")
[[ -n "$fps10" && -n "$profile" ]] || { cat "$perf_log" >&2; exit 1; }
read -r clear board cube hud present <<<"$profile"
(( clear>0 && board>0 && cube>0 && hud>0 && present>0 )) || { echo "Empty profiler stage" >&2; exit 1; }
if (( fps10 < minimum_fps10 )); then
    printf '486DX-50 performance: FAIL (%d.%d fps; minimum 50.0)\n' "$((fps10/10))" "$((fps10%10))" >&2; exit 1
fi
printf '486DX-50 performance: PASS (%d.%d fps at %d cycles; minimum 50.0)\n' "$((fps10/10))" "$((fps10%10))" "$target_cycles"
printf 'Profile ticks: clear=%d board=%d cube=%d hud=%d present=%d\n' "$clear" "$board" "$cube" "$hud" "$present"
