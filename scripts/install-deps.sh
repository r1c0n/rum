#!/usr/bin/env bash
set -euo pipefail
if (( EUID != 0 )); then
    echo 'Run this script with sudo in Ubuntu.' >&2
    exit 1
fi
apt-get update
apt-get install -y --no-install-recommends build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo curl ca-certificates xz-utils grub-common grub-pc-bin xorriso mtools python3 qemu-system-x86 gdb
