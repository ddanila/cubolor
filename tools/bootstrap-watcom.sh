#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_root/tools/watcom-env.sh"
tool_root="$project_root/.tools"
linux_sha="e1bc4e88fa47191118f29e53731e7f4542803c7f4c503e15d72f1f571ac0832f"
snapshot_sha="6279e1bf7aea4ceba24539d7924f095142047fa55d352b3ba96f33c81ededd32"
# Return the path of a verified archive. Publish completed downloads atomically.
watcom_archive() (
    set -e
    local sha=$1 asset=$2 legacy=$3
    local dir="${XDG_CACHE_HOME:-$HOME/.cache}/open-watcom/$sha"
    local archive="$dir/$asset"
    mkdir -p "$dir"
    if [[ ! -f "$archive" ]]; then
        tmp=$(mktemp "$dir/.download.XXXXXXXX")
        trap 'rm -f -- "$tmp"' EXIT
        if [[ -f "$legacy" ]]; then
            cp "$legacy" "$tmp"
        else
            curl -fL --retry 2 -o "$tmp" "$watcom_release_base/$asset"
        fi
        watcom_verify_sha256 "$sha" "$tmp"
        mv "$tmp" "$archive"
    else
        watcom_verify_sha256 "$sha" "$archive"
    fi
    printf '%s\n' "$archive"
)

if [[ -x "$watcom_root/$watcom_bindir/wcl" ]]; then echo "Open Watcom: $watcom_root"; exit 0; fi
mkdir -p "$tool_root" "$watcom_root"
if [[ "$watcom_host" == linux-x64 ]]; then
    archive=$(watcom_archive "$linux_sha" "open-watcom-2_0-c-linux-x64" "$tool_root/open-watcom-linux-x64")
    unzip -q -o "$archive" -d "$watcom_root"
else
    archive=$(watcom_archive "$snapshot_sha" "ow-snapshot.tar.xz" "$tool_root/ow-snapshot.tar.xz")
    tar -xJf "$archive" -C "$watcom_root" --strip-components=1 "./$watcom_bindir" ./h ./lib286 ./lib386
    xattr -dr com.apple.quarantine "$watcom_root" 2>/dev/null || true
fi
chmod +x "$watcom_root/$watcom_bindir"/* 2>/dev/null || true
[[ -x "$watcom_root/$watcom_bindir/wcl" ]] || { echo "Open Watcom install failed" >&2; exit 1; }
echo "Open Watcom: installed $watcom_host"
