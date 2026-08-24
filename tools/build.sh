#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_root/tools/watcom-env.sh"
export WATCOM="$watcom_root"
export INCLUDE="$watcom_root/h"
export PATH="$watcom_root/$watcom_bindir:$PATH"
mkdir -p "$project_root/build"
rm -f "$project_root/build/CUBOLOR.EXE" "$project_root/build/WATCOM.LOG"
pushd "$project_root/build" >/dev/null
if ! wcl -q -bt=dos -ms -3 -zastd=c++0x -ox -s -k8192 -i="$project_root/src" \
    -fe=CUBOLOR.EXE "$project_root/src/main.cpp" "$project_root/src/game.cpp" \
    "$project_root/src/platform.cpp" "$project_root/src/video.cpp" >WATCOM.LOG 2>&1; then
    popd >/dev/null; sed -n '1,240p' "$project_root/build/WATCOM.LOG" >&2; exit 1
fi
popd >/dev/null
if [[ -s "$project_root/build/WATCOM.LOG" ]]; then cat "$project_root/build/WATCOM.LOG"; exit 1; fi
echo "DOS build: PASS"
