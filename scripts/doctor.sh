#!/usr/bin/env bash
set -uo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PATH="$project_dir/.tools/cross/bin:$PATH"
missing=0
for tool in i686-elf-gcc i686-elf-as i686-elf-ld make grub-file grub-mkrescue xorriso mcopy python3 qemu-system-i386; do
    if location=$(command -v "$tool"); then
        printf '[ok]      %-20s %s\n' "$tool" "$location"
    else
        printf '[missing] %s\n' "$tool"
        missing=1
    fi
done
if command -v i686-elf-gcc >/dev/null; then
    if test "$(i686-elf-gcc -dumpmachine)" != i686-elf; then
        echo '[missing] Correct i686-elf compiler target'
        missing=1
    fi
    if test -f "$(i686-elf-gcc -print-libgcc-file-name)"; then
        echo '[ok]      Target libgcc'
    else
        echo '[missing] Target libgcc'
        missing=1
    fi
fi
if test -f /usr/lib/grub/i386-pc/modinfo.sh; then
    echo '[ok]      GRUB BIOS modules'
else
    echo '[missing] GRUB BIOS modules (install grub-pc-bin)'
    missing=1
fi
exit "$missing"
