#!/usr/bin/env bash
#
# third-party.sh - fetch and build kernels written by other people, so that
#                  "Mini-KVM runs someone else's work" is a checkable claim
#                  rather than an assertion.
#
# The kernels here were not written for Mini-KVM and know nothing about it.
# None of their source is modified: where a build fails on a modern toolchain
# it is given compiler *flags* to restore the older default, which is what any
# distribution does when building aging software.
#
# Usage:  ./tools/third-party.sh [fetch|build|clean]   (default: fetch build)
#
# Output lands in third-party/, which is gitignored. tools/smoke.sh picks the
# kernels up if they are there and skips those cases if they are not, so this
# is optional and needs network access only once.

set -uo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/.." && pwd)"
work="${root_dir}/third-party"

log() { printf '\n=== %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

fetch_one() {
    local name="$1" url="$2"
    if [[ -d "${work}/${name}" ]]; then
        echo "  ${name}: already fetched"
        return 0
    fi
    echo "  ${name}: cloning"
    git clone --depth 1 -q "${url}" "${work}/${name}" || {
        echo "  ${name}: clone failed" >&2
        return 1
    }
}

do_fetch() {
    log "Fetching"
    mkdir -p "${work}"
    fetch_one soso        https://github.com/ozkl/soso.git
    fetch_one RetrOS-32   https://github.com/joexbayer/RetrOS-32.git
    fetch_one os-tutorial https://github.com/cfenollosa/os-tutorial.git
}

build_soso() {
    # Needs clang and nasm. Newer clang rejects the implicit pointer
    # conversions this code was written against, so those are turned back into
    # warnings; no source is touched.
    if ! have clang || ! have nasm; then
        echo "  soso: needs clang and nasm, skipping"
        return 0
    fi
    ( cd "${work}/soso" && make -s \
        CFLAGS="-nostdlib -nostdinc -fno-builtin -m32 -c \
                -Wno-incompatible-pointer-types -Wno-int-conversion" \
        >/dev/null 2>&1 ) && echo "  soso: built kernel.bin" \
        || echo "  soso: build failed" >&2
}

build_retros() {
    # Ships a GRUB ISO built by its authors. Taking the kernel out of that is
    # the strongest form of "unmodified": the binary is theirs, not ours.
    local iso="${work}/RetrOS-32/RetrOS-32-grub.iso"
    if [[ ! -f "$iso" ]]; then
        echo "  RetrOS-32: no ISO in the repository"
        return 0
    fi
    if ! have bsdtar && ! have 7z; then
        echo "  RetrOS-32: needs bsdtar or 7z, skipping"
        return 0
    fi
    local out="${work}/RetrOS-32/extracted"
    mkdir -p "$out"
    ( cd "$out" && { bsdtar -xf "$iso" 2>/dev/null || 7z x -y "$iso" >/dev/null 2>&1; } )
    if [[ -f "${out}/boot/myos.bin" ]]; then
        echo "  RetrOS-32: extracted boot/myos.bin"
    else
        echo "  RetrOS-32: kernel not found in ISO" >&2
    fi
}

build_os_tutorial() {
    # Its Makefile wants an i386-elf cross toolchain; host gcc -m32 produces
    # the same flat binary. -fcommon restores the tentative-definition
    # behaviour this code predates.
    local d="${work}/os-tutorial/24-el-capitan"
    [[ -d "$d" ]] || { echo "  os-tutorial: stage not found"; return 0; }

    ( cd "$d" && make -s clean >/dev/null 2>&1
      make -s CC=gcc \
        CFLAGS="-g -ffreestanding -Wall -Wextra -fno-exceptions -m32 -fno-pie -fcommon" \
        >/dev/null 2>&1
      # Its link step hardcodes i386-elf-ld, so do that step here instead.
      ld -m elf_i386 -o kernel.bin -Ttext 0x1000 \
         boot/kernel_entry.o kernel/kernel.o cpu/interrupt.o drivers/keyboard.o \
         drivers/screen.o cpu/idt.o cpu/isr.o cpu/ports.o cpu/timer.o \
         libc/mem.o libc/string.o --oformat binary 2>/dev/null )

    [[ -f "${d}/kernel.bin" ]] && echo "  os-tutorial: built kernel.bin" \
        || echo "  os-tutorial: build failed" >&2
}

do_build() {
    log "Building"
    build_soso
    build_retros
    build_os_tutorial

    log "Result"
    for k in "soso/kernel.bin" "RetrOS-32/extracted/boot/myos.bin" \
             "os-tutorial/24-el-capitan/kernel.bin"; do
        if [[ -f "${work}/${k}" ]]; then
            printf '  %-40s %s\n' "$k" "$(stat -c%s "${work}/${k}") bytes"
        else
            printf '  %-40s missing\n' "$k"
        fi
    done
    echo
    echo "Run them with:"
    echo "  ./kvm-vmm --fb-dump /tmp/soso.ppm --module <fat-image> third-party/soso/kernel.bin"
    echo "  ./kvm-vmm --fb-dump /tmp/retros.ppm third-party/RetrOS-32/extracted/boot/myos.bin"
    echo "  ./kvm-vmm --flat32 --load 0x1000 --vga third-party/os-tutorial/24-el-capitan/kernel.bin"
}

case "${1:-all}" in
    fetch) do_fetch ;;
    build) do_build ;;
    clean) rm -rf "${work}"; echo "removed ${work}" ;;
    all)   do_fetch; do_build ;;
    *)     echo "usage: $0 [fetch|build|clean]" >&2; exit 2 ;;
esac
