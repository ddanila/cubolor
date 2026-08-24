# shellcheck shell=bash
# Shared pinned Open Watcom host/target selection.
watcom_release="2026-08-01-Build"
watcom_release_base="https://github.com/open-watcom/open-watcom-v2/releases/download/$watcom_release"
watcom_env_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

case "$(uname -s):$(uname -m)" in
    Linux:x86_64|Linux:amd64) watcom_host="linux-x64"; watcom_bindir="binl64" ;;
    Darwin:arm64) watcom_host="macos-arm64"; watcom_bindir="armo64" ;;
    Darwin:x86_64) watcom_host="macos-x64"; watcom_bindir="bino64" ;;
    *) echo "Unsupported build host: $(uname -s) $(uname -m)" >&2; return 1 2>/dev/null || exit 1 ;;
esac
watcom_root="$watcom_env_root/.tools/watcom-$watcom_host"

watcom_verify_sha256() {
    local expected="$1" file="$2" actual
    if command -v sha256sum >/dev/null 2>&1; then actual=$(sha256sum "$file"|awk '{print $1}')
    else actual=$(shasum -a 256 "$file"|awk '{print $1}'); fi
    [[ "$actual" == "$expected" ]] || { echo "Checksum mismatch: $file" >&2; return 1; }
}
