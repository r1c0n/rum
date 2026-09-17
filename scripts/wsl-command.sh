#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
case "${1:-build}" in
    setup)
        sudo bash scripts/install-deps.sh
        bash scripts/build-toolchain.sh
        bash scripts/doctor.sh
        ;;
    doctor) bash scripts/doctor.sh ;;
    build) make all ;;
    run-kernel) make check ;;
    test) make test ;;
    clean) make clean ;;
    *) echo "Unknown action: $1" >&2; exit 2 ;;
esac
