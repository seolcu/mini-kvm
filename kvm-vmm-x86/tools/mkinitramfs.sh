#!/usr/bin/env bash
set -euo pipefail

out="${1:-initramfs.cpio}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

tmp_root="$(mktemp -d)"
cleanup() { rm -rf -- "${tmp_root}"; }
trap cleanup EXIT

mkdir -p "${tmp_root}/"{bin,dev,proc,sys,etc,tmp,root}

if ! command -v gcc >/dev/null 2>&1; then
  echo "Error: gcc not found in PATH (needed to build init binary)" >&2
  exit 1
fi
gcc -O2 -s "${repo_root}/initramfs/init.c" -o "${tmp_root}/init"
gcc -O2 -s "${repo_root}/initramfs/miniutils.c" -o "${tmp_root}/bin/miniutils"

ln -sf miniutils "${tmp_root}/bin/ls"
ln -sf miniutils "${tmp_root}/bin/cat"
ln -sf miniutils "${tmp_root}/bin/uname"
ln -sf miniutils "${tmp_root}/bin/halt"
ln -sf miniutils "${tmp_root}/bin/poweroff"

bash_bin="$(command -v bash)"
if [[ -z "${bash_bin}" ]]; then
  echo "Error: bash not found in PATH" >&2
  exit 1
fi

install -m 0755 "${bash_bin}" "${tmp_root}/bin/bash"
ln -sf bash "${tmp_root}/bin/sh"

mapfile -t libs < <(ldd "${bash_bin}" | awk '{for (i=1; i<=NF; i++) if ($i ~ /^\//) print $i}')
if [[ "${#libs[@]}" -eq 0 ]]; then
  echo "Error: failed to discover shared libraries via ldd" >&2
  exit 1
fi

for lib in "${libs[@]}"; do
  dst_dir="${tmp_root}$(dirname "${lib}")"
  mkdir -p "${dst_dir}"
  cp -L "${lib}" "${tmp_root}${lib}"
done

(cd "${tmp_root}" && find . -print0 | cpio --null -o --format=newc) > "${repo_root}/${out}"
echo "Wrote ${repo_root}/${out}"
