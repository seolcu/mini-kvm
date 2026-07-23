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

# Refuse to "verify" a stale build: a failed build leaves the previous
# artifacts in place and every case passes against old code. Each artifact is
# compared against the sources that actually produce it -- comparing
# everything against kvm-vmm would flag guest sources that cannot affect it.
check_stale() {
    local artifact="$1"; shift
    if [[ ! -e "${artifact}" ]]; then
        echo "ERROR: ${artifact} is missing - run 'make all'." >&2
        return 1
    fi
    local newer
    newer="$(find "$@" -newer "${artifact}" \
             \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' \) 2>/dev/null)"
    if [[ -n "${newer}" ]]; then
        echo "ERROR: these sources are newer than ${artifact} - run 'make all':" >&2
        printf '  %s\n' ${newer} >&2
        return 1
    fi
    return 0
}

stale_found=0
check_stale "${vmm}" src || stale_found=1
check_stale os-1k/kernel os-1k || stale_found=1
check_stale examples/multiboot-barebones/kernel.elf examples/multiboot-barebones || stale_found=1
check_stale examples/multiboot-keyboard/kernel.elf examples/multiboot-keyboard || stale_found=1
check_stale examples/multiboot2/kernel.elf examples/multiboot2 || stale_found=1
check_stale examples/faults/no-idt.elf \
    examples/faults/no-idt.S examples/faults/linker.ld || stale_found=1
check_stale examples/faults/bad-stack.elf \
    examples/faults/bad-stack.S examples/faults/linker.ld || stale_found=1
check_stale examples/faults/bad-entry.elf \
    examples/faults/bad-entry.S examples/faults/bad-entry.ld || stale_found=1
# Each guest is built from one source and one linker script; pairing them
# exactly is what keeps an unrelated guest from being reported as stale.
check_stale guest/hello    guest/hello.S    guest/guest.ld    || stale_found=1
check_stale guest/hctest   guest/hctest.S   guest/guest.ld    || stale_found=1
check_stale guest/vgademo  guest/vgademo.S  guest/guest32.ld  || stale_found=1
check_stale guest/fault64  guest/fault64.S  guest/guest_64.ld || stale_found=1
(( stale_found )) && exit 1

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

# check_case NAME DESCRIPTION PREDICATE STDIN -- ARGV...
#   For output that must not be pinned byte for byte: either it is genuinely
#   nondeterministic, or it contains addresses the compiler chooses, which
#   differ between toolchains and would make the baseline pass only on the
#   machine that produced it. PREDICATE is a shell snippet reading the
#   normalized output from $out; it must return 0.
check_case() {
    local name="$1" desc="$2" predicate="$3" stdin_data="$4"; shift 5
    if (( ${#only[@]} )) && [[ ! " ${only[*]} " == *" ${name} "* ]]; then
        return
    fi
    if (( update )); then
        echo "SKIP ${name}: predicate case, nothing to baseline"
        return
    fi

    local out
    out="$(printf '%b' "${stdin_data}" | timeout "${TIMEOUT}" "${vmm}" "$@" 2>&1 | normalize)"
    if eval "${predicate}"; then
        echo "PASS ${name}"
        ((pass++))
    else
        echo "FAIL ${name}: ${desc}"
        printf '%s\n' "${out}" | sed 's/^/    /' | head -20
        failed+=("${name}"); ((fail++))
    fi
}

# linux_case NAME -- feeds the guest shell after it has had time to start.
#   Skipped unless a kernel and initramfs are present, since neither belongs
#   in the repository. Provide them with:
#     cp /boot/vmlinuz-$(uname -r) bzImage
#     ./tools/mkinitramfs.sh initramfs.cpio
linux_case() {
    local name="$1"
    if (( ${#only[@]} )) && [[ ! " ${only[*]} " == *" ${name} "* ]]; then
        return
    fi
    if [[ ! -f bzImage || ! -f initramfs.cpio ]]; then
        echo "SKIP ${name}: no bzImage/initramfs.cpio (see tools/smoke.sh)"
        ((skip++)); return
    fi

    # The kernel probes the UART during boot, and those probe reads consume
    # anything already waiting -- exactly as they would on real hardware. So
    # the commands are sent only once the shell is up.
    local out
    out="$( { sleep 6; printf 'uname -s\nls /bin\nexit\n'; sleep 3; } \
            | timeout 90 "${vmm}" --linux bzImage --initrd initramfs.cpio \
                --cmdline "console=ttyS0 rdinit=/init" 2>&1 | normalize )"

    if [[ "$out" == *"Run /init as init process"* ]] &&
       [[ "$out" == *"userspace init started"* ]] &&
       [[ "$out" == *"sh-"* ]] &&
       [[ "$out" == *"miniutils"* ]]; then
        echo "PASS ${name}"
        ((pass++))
    else
        echo "FAIL ${name}: expected Linux to reach a shell and run 'ls /bin'"
        printf '%s\n' "$out" | tail -20 | sed 's/^/    /'
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
    '' -- guest/counter guest/hello

# --- VGA text mode ---------------------------------------------------------
# With stdout redirected the renderer emits no escape sequences, just the
# final screen as plain text -- which is what makes this diffable.
run_case vga          ''            -- --paging --vga guest/vgademo

# --- Third-party kernel formats --------------------------------------------
# A stock Multiboot kernel that uses no Mini-KVM facilities at all. This is
# the case that decides whether the VMM can run someone else's work.
run_case multiboot    ''            -- --vga examples/multiboot-barebones/kernel.elf
# Interactive: PS/2 scancodes on IRQ1 and the CRTC cursor registers, which is
# a completely different device path from the barebones case.
run_case multiboot_kbd 'help\necho hi\nnope\nhalt\n' \
    -- --vga examples/multiboot-keyboard/kernel.elf

# Multiboot modules are how a kernel receives an initrd; without them a
# kernel that needs one simply fails.
run_case multiboot_mod ''          -- --vga --module tools/testmodule.txt \
    examples/multiboot-barebones/kernel.elf

# Multiboot 2 replaces the fixed info structure with a tag list; the kernel
# walks it, so this checks the whole block rather than one field.
run_case multiboot2   '' -- --vga --module tools/testmodule.txt \
    --cmdline "root=/dev/null quiet" examples/multiboot2/kernel.elf

# --- Fault analysis --------------------------------------------------------
# Deliberately broken kernels. These check that --explain names the actual
# cause, not merely that the guest died.
check_case explain_no_idt \
    'expected the divide by zero and the empty IDT to be named' \
    '[[ "$out" == *"raises #DE divide error"* ]] &&
     [[ "$out" == *"divisor register is zero"* ]] &&
     [[ "$out" == *"Not one entry is present"* ]]' \
    '' -- --explain examples/faults/no-idt.elf

check_case explain_bad_stack \
    'expected the unusable stack to be named as the cause' \
    '[[ "$out" == *"raises #UD invalid opcode"* ]] &&
     [[ "$out" == *"Stack pointer"* ]] &&
     [[ "$out" == *"is not usable"* ]] &&
     [[ "$out" == *"becomes a triple fault"* ]]' \
    '' -- --explain examples/faults/bad-stack.elf

# Without --explain the state is gone, and saying so is the correct answer.
run_case explain_absent    '' -- examples/faults/no-idt.elf
# Long mode resolves addresses through 4-level page tables, a different walk
# from the 32-bit one every other case uses.
run_case explain_long64    '' -- --long-mode --explain guest/fault64

# Preflight catches an entry point outside the loaded image before the guest
# runs, and the internal error is explained rather than dumped raw.
check_case explain_bad_entry \
    'expected the entry point to be flagged before boot, then explained' \
    '[[ "$out" == *"is outside the loaded image"* ]] &&
     [[ "$out" == *"There is no memory at that address"* ]]' \
    '' -- examples/faults/bad-entry.elf

# --- Inspection ------------------------------------------------------------
# Decoding descriptor and page tables is the other half of the diagnostics:
# a wrong bit shows up here without needing the guest to crash first.
check_case inspect_1kos \
    'expected the GDT, IDT and page tables to be decoded' \
    '[[ "$out" == *"GDT at 0x500, limit 0x27 (5 entries)"* ]] &&
     [[ "$out" == *"32-bit present code, readable, ring 0"* ]] &&
     [[ "$out" == *"32-bit present data, writable, ring 3"* ]] &&
     [[ "$out" == *"IRQ0 timer selector 0x08"* ]] &&
     [[ "$out" == *"1 of 256 entries present"* ]] &&
     [[ "$out" == *"0x80000000-0x803fffff"* ]]' \
    '0\n' -- --paging --inspect os-1k/kernel
run_case trace_modes   ''    -- --trace-modes examples/multiboot-barebones/kernel.elf

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

# --- Third-party kernels ---------------------------------------------------
# The Phase 1 gate: kernels written by other people, running unmodified.
# Skipped unless ./tools/third-party.sh has fetched and built them.
third_party_case() {
    local name="$1" kernel="$2" want="$3"; shift 3
    if (( ${#only[@]} )) && [[ ! " ${only[*]} " == *" ${name} "* ]]; then
        return
    fi
    if [[ ! -f "$kernel" ]]; then
        echo "SKIP ${name}: not fetched (run ./tools/third-party.sh)"
        ((skip++)); return
    fi

    local out
    out="$( { printf 'hello\nend\n'; sleep 1; } \
            | timeout 40 "${vmm}" "$@" 2>&1 | normalize )"

    if eval "${want}"; then
        echo "PASS ${name}"
        ((pass++))
    else
        echo "FAIL ${name}"
        printf '%s\n' "$out" | tail -12 | sed 's/^/    /'
        failed+=("${name}"); ((fail++))
    fi
}

# Renders a full 1024x768x32 screen. Checked through the PPM rather than the
# terminal, since Mini-KVM cannot display graphics.
third_party_case soso third-party/soso/kernel.bin \
    '[[ -s /tmp/smoke-soso.ppm ]] && head -2 /tmp/smoke-soso.ppm | tail -1 | grep -q "1024 768"' \
    --fb-dump /tmp/smoke-soso.ppm --module tools/testmodule.txt \
    third-party/soso/kernel.bin

# A binary built by its own authors and taken straight out of their GRUB ISO.
third_party_case retros third-party/RetrOS-32/extracted/boot/myos.bin \
    '[[ -s /tmp/smoke-retros.ppm ]] && head -2 /tmp/smoke-retros.ppm | tail -1 | grep -q "640 480"' \
    --fb-dump /tmp/smoke-retros.ppm third-party/RetrOS-32/extracted/boot/myos.bin

# Interactive: takes keyboard input and halts on command.
third_party_case os_tutorial third-party/os-tutorial/24-el-capitan/kernel.bin \
    '[[ "$out" == *"You said: HELLO"* ]] && [[ "$out" == *"Stopping the CPU"* ]]' \
    --flat32 --load 0x1000 --vga third-party/os-tutorial/24-el-capitan/kernel.bin

# --- Linux ----------------------------------------------------------------
linux_case linux_shell

echo
echo "=== ${pass} passed, ${fail} failed, ${skip} skipped ==="
if (( fail )); then
    printf 'Failed: %s\n' "${failed[*]}"
    exit 1
fi
exit 0
