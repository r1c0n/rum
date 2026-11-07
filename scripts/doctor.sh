#!/usr/bin/env bash
set -uo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=${CROSS_PREFIX:-$project_dir/.tools/cross/bin/i686-elf-}
missing=0
check_tool() {
    local name=$1 location
    if location=$(command -v "$name"); then
        printf '[ok]      %-22s %s\n' "${name##*/}" "$location"
    else
        printf '[missing] %s\n' "$name"
        missing=1
    fi
}
for suffix in gcc as ld objcopy nm readelf; do check_tool "$prefix$suffix"; done
for tool in gcc make grub-file grub-mkrescue xorriso mformat mcopy mmd mdir fsck.fat python3 qemu-img git "${QEMU:-qemu-system-i386}"; do
    check_tool "$tool"
done
if command -v "${prefix}gcc" >/dev/null; then
    if test "$("${prefix}gcc" -dumpmachine)" != i686-elf; then
        echo '[missing] Correct i686-elf compiler target'; missing=1
    fi
    if test -f "$("${prefix}gcc" -print-libgcc-file-name)"; then
        echo '[ok]      Target libgcc'
    else
        echo '[missing] Target libgcc'; missing=1
    fi
fi
if command -v python3 >/dev/null && ! python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))'; then
    echo '[missing] Python 3.10 or newer'; missing=1
fi
if test -f /usr/lib/grub/i386-pc/modinfo.sh; then
    echo '[ok]      GRUB BIOS modules'
else
    echo '[missing] GRUB BIOS modules (install grub-pc-bin)'; missing=1
fi
if (( missing )); then echo 'Run ./rum setup to install Ubuntu dependencies and the local toolchain.' >&2; fi
exit "$missing"
