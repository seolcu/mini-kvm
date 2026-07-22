#!/usr/bin/env bash
#
# smoke.sh - end-to-end verification for Mini-KVM
#
# There is no unit test suite: verification is running guests and reading
# stdout. This script makes that repeatable. It runs every supported guest
# path and diffs the (normalized) output against a stored baseline.
#
# Usage:
#   ./tools/smoke.sh            Run all cases, diff against tools/baseline/
#   ./tools/smoke.sh --update   Regenerate the baseline from current output
#   ./tools/smoke.sh NAME ...   Run only the named cases
#
# Requires /dev/kvm. Mini-KVM guests depend on Mini-KVM hypercalls, so there
# is no QEMU/TCG fallback: if KVM is unavailable this skips loudly rather
# than pretending to have verified anything.

set -uo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/.." && pwd)"
baseline_dir="${script_dir}/baseline"
vmm="${root_dir}/kvm-vmm"

cd "${root_dir}"

TIMEOUT=20
update=0
declare -a only=()

for arg in "$@"; do
    case "${arg}" in
        --update) update=1 ;;
        -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
        -*) echo "smoke.sh: unknown option ${arg}" >&2; exit 2 ;;
        *) only+=("${arg}") ;;
    esac
done

if [[ ! -c /dev/kvm ]]; then
    echo "SKIP: /dev/kvm not present - cannot verify anything without KVM." >&2
    exit 77
fi
if [[ ! -r /dev/kvm || ! -w /dev/kvm ]]; then
    echo "SKIP: /dev/kvm not readable/writable by $(id -un) - add yourself to the 'kvm' group." >&2
    exit 77
fi
if [[ ! -x "${vmm}" ]]; then
    echo "ERROR: ${vmm} not built. Run 'make all' first." >&2
    exit 1
fi

# Refuse to "verify" a stale binary: a failed build would otherwise leave the
# previous kvm-vmm in place and every case would pass against old code.
stale="$(find src os-1k guest examples -newer "${vmm}" \
         \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' \) 2>/dev/null)"
if [[ -n "${stale}" ]]; then
    echo "ERROR: these sources are newer than ${vmm##*/} - rebuild first (make all):" >&2
    printf '  %s\n' ${stale} >&2
    exit 1
fi

mkdir -p "${baseline_dir}"

# Strip run-to-run noise so the diff only shows real behavior changes:
# ANSI color codes, kernel-assigned fd numbers, and host pointer values.
normalize() {
    sed -e 's/\x1b\[[0-9;]*m//g' \
        -e 's/fd=[0-9][0-9]*/fd=N/g' \
        -e 's/at 0x[0-9a-f]\{6,\}/at 0xHOSTPTR/g' \
        -e 's/[[:space:]]*$//'
}

pass=0; fail=0; skip=0
declare -a failed=()

# run_case NAME STDIN -- ARGV...
#   Compares normalized output against tools/baseline/NAME.txt.
run_case() {
    local name="$1" stdin_data="$2"; shift 3   # shift past the "--"
    if (( ${#only[@]} )) && [[ ! " ${only[*]} " == *" ${name} "* ]]; then
        return
    fi

    local out
    out="$(printf '%b' "${stdin_data}" | timeout "${TIMEOUT}" "${vmm}" "$@" 2>&1 | normalize)"
    local rc=$?
    if (( rc == 124 )); then
        echo "FAIL ${name}: timed out after ${TIMEOUT}s"
        failed+=("${name}"); ((fail++)); return
    fi

    local golden="${baseline_dir}/${name}.txt"
    if (( update )); then
        printf '%s\n' "${out}" > "${golden}"
        echo "UPDATED ${name}"
        return
    fi
    if [[ ! -f "${golden}" ]]; then
        echo "SKIP ${name}: no baseline (run --update)"
        ((skip++)); return
    fi
    if diff -u "${golden}" <(printf '%s\n' "${out}") > /tmp/smoke.$$.diff 2>&1; then
        echo "PASS ${name}"
        ((pass++))
    else
        echo "FAIL ${name}:"
        sed 's/^/    /' /tmp/smoke.$$.diff | head -40
        failed+=("${name}"); ((fail++))
    fi
    rm -f /tmp/smoke.$$.diff
}

# check_case NAME DESCRIPTION PREDICATE -- ARGV...
#   For cases whose output is legitimately nondeterministic. PREDICATE is a
#   shell snippet reading the normalized output from $out; it must return 0.
check_case() {
    local name="$1" desc="$2" predicate="$3"; shift 4
    if (( ${#only[@]} )) && [[ ! " ${only[*]} " == *" ${name} "* ]]; then
        return
    fi
    if (( update )); then
        echo "SKIP ${name}: predicate case, nothing to baseline"
        return
    fi

    local out
    out="$(timeout "${TIMEOUT}" "${vmm}" "$@" 2>&1 | normalize)"
    if eval "${predicate}"; then
        echo "PASS ${name}"
        ((pass++))
    else
        echo "FAIL ${name}: ${desc}"
        printf '%s\n' "${out}" | sed 's/^/    /' | head -20
        failed+=("${name}"); ((fail++))
    fi
}

echo "=== Mini-KVM smoke tests ==="

# --- Real mode -------------------------------------------------------------
run_case minimal      ''            -- guest/minimal
run_case hello        ''            -- guest/hello
run_case counter      ''            -- guest/counter
run_case multiplication '' -- guest/multiplication
run_case fibonacci    ''            -- guest/fibonacci
# Exercises the whole hypercall ABI including a blocking GETCHAR that must
# terminate at end of input rather than spin.
run_case hctest       'abc'         -- guest/hctest

# --- Multi-vCPU ------------------------------------------------------------
# Output interleaving is genuinely nondeterministic (that is the demo), so
# assert on content rather than order: digits must spell the counter's output
# and the remaining characters must spell hello's.
check_case multi_vcpu \
    'expected counter digits 0-9 and "Hello, KVM!" interleaved' \
    '[[ "$(printf "%s" "$out" | tr -cd "0-9")" == *"0123456789"* ]] &&
     [[ "$(printf "%s" "$out" | grep -o "H.*!" | tr -d "0-9")" == *"Hello, KVM!"* ]]' \
    -- guest/counter guest/hello

# --- VGA text mode ---------------------------------------------------------
# With stdout redirected the renderer emits no escape sequences, just the
# final screen as plain text -- which is what makes this diffable.
run_case vga          ''            -- --paging --vga guest/vgademo

# --- Third-party kernel formats --------------------------------------------
# A stock Multiboot kernel that uses no Mini-KVM facilities at all. This is
# the case that decides whether the VMM can run someone else's work.
run_case multiboot    ''            -- --vga examples/multiboot-barebones/kernel.elf

# --- Long mode -------------------------------------------------------------
run_case long_mode    ''            -- --long-mode guest/hello_64

# --- 1K OS (protected mode + paging) ---------------------------------------
# Piped stdin drives the menu and skips terminal raw mode, making these
# deterministic.
run_case os1k_exit     '0\n'                      -- --paging os-1k/kernel
run_case os1k_multtab  '1\n0\n'                   -- --paging os-1k/kernel
run_case os1k_counter   '2\n0\n'                  -- --paging os-1k/kernel
run_case os1k_echo     '3\nHello\nquit\n0\n'      -- --paging os-1k/kernel
run_case os1k_fib      '4\n0\n'                   -- --paging os-1k/kernel
# NB: the calculator quits on 'q' (not 'quit') and its parser wants spaces
# around the operator. AGENTS.md documents '6\n10+5\nquit\n0\n', which hangs.
run_case os1k_calc     '6\n10 + 5\nq\n0\n'        -- --paging os-1k/kernel

echo
echo "=== ${pass} passed, ${fail} failed, ${skip} skipped ==="
if (( fail )); then
    printf 'Failed: %s\n' "${failed[*]}"
    exit 1
fi
exit 0
