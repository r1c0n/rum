#!/usr/bin/env bash
# Build OSDev's bare-metal compiler; all temporary compilation stays in Linux.
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix="$project_dir/.tools/cross"
target=i686-elf
gcc_version=15.2.0
binutils_version=2.45
jobs=${JOBS:-6}
build_root=${RUM_TOOLCHAIN_BUILD_DIR:-/tmp/rum-toolchain-$(id -u)}
export PATH="$prefix/bin:$PATH"

for tool in gcc g++ make bison flex makeinfo curl tar xz; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; see docs/setup.md." >&2; exit 1; }
done
if command -v "$target-gcc" >/dev/null && test -f "$("$target-gcc" -print-libgcc-file-name)"; then
    echo "Cross-compiler already available: $(command -v "$target-gcc")"
    exit 0
fi
mkdir -p "$prefix" "$build_root/sources" "$build_root/build-binutils" "$build_root/build-gcc"
log="$build_root/build.log"
echo "Building $target with GCC $gcc_version / Binutils $binutils_version ($jobs jobs)."
echo "Build directory: $build_root"
echo "Detailed log: $log"
trap 'echo "Toolchain build failed. Last log lines:" >&2; tail -n 50 "$log" >&2' ERR

download() {
    local name=$1 url=$2
    if ! test -f "$build_root/sources/$name"; then
        curl --fail --location --retry 3 --output "$build_root/sources/$name.part" "$url"
        mv -- "$build_root/sources/$name.part" "$build_root/sources/$name"
    fi
}
download "binutils-$binutils_version.tar.xz" "https://ftp.gnu.org/gnu/binutils/binutils-$binutils_version.tar.xz"
download "gcc-$gcc_version.tar.xz" "https://ftp.gnu.org/gnu/gcc/gcc-$gcc_version/gcc-$gcc_version.tar.xz"
echo 'Extracting GNU sources...'
test -d "$build_root/sources/binutils-$binutils_version" || tar -xf "$build_root/sources/binutils-$binutils_version.tar.xz" -C "$build_root/sources"
test -d "$build_root/sources/gcc-$gcc_version" || tar -xf "$build_root/sources/gcc-$gcc_version.tar.xz" -C "$build_root/sources"

cd "$build_root/build-binutils"
echo 'Configuring and building Binutils...'
if ! test -f Makefile; then
    "$build_root/sources/binutils-$binutils_version/configure" --target="$target" --prefix="$prefix" --with-sysroot --disable-nls --disable-werror --enable-default-execstack=no >"$log" 2>&1
fi
make -j "$jobs" >>"$log" 2>&1
make install >>"$log" 2>&1
echo 'Binutils installed. Configuring and building GCC...'

cd "$build_root/build-gcc"
if ! test -f Makefile; then
    "$build_root/sources/gcc-$gcc_version/configure" --target="$target" --prefix="$prefix" --disable-nls --enable-languages=c --without-headers --disable-multilib --disable-bootstrap --enable-initfini-array >>"$log" 2>&1
fi
make -j "$jobs" all-gcc >>"$log" 2>&1
echo 'GCC built. Building target libgcc...'
make -j "$jobs" all-target-libgcc >>"$log" 2>&1
echo 'Installing GCC and libgcc...'
make install-gcc install-target-libgcc >>"$log" 2>&1
"$target-gcc" --version | head -n 1
test "$("$target-gcc" -dumpmachine)" = "$target"
test -f "$("$target-gcc" -print-libgcc-file-name)"
echo "Ready: $prefix"
