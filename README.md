# Mini-KVM

> 2025-2 Ajou SoftCon 연구부문 장려상 수상

**A small x86 hypervisor for people who write their own kernels.**

Mini-KVM runs a guest directly on Linux KVM and tells you what it did — where it
faulted, what was in its descriptor tables, what its page tables actually
mapped. It is built on the raw KVM ioctl API in about 4,200 lines of C (5,300
with headers), small enough to read in an afternoon.

[License: MIT](LICENSE)

> **Status: early but real.** Mini-KVM boots stock Multiboot and ELF kernels,
> renders VGA text mode, and runs its own real-mode guests and a bundled 32-bit
> teaching OS, runs three third-party kernels unmodified, boots a stock Linux
> kernel to a shell, and can explain why a guest triple-faulted. See
> [Current state](#current-state) for exactly what works and
> [Roadmap](#roadmap) for where it is going. Nothing below is aspirational: if
> it is listed as working, `make test` covers it.

---

## Why this exists

If you are writing a kernel, your tools are QEMU and GDB. They work, but they
are general-purpose: QEMU is two million lines that will happily run anything,
and when your kernel triple-faults it resets the CPU and says nothing useful.
Production VMMs like Firecracker and libkrun are the opposite extreme — by
design they are black boxes that assume your guest is correct.

Nobody is building the tool in between: a hypervisor whose product is
**insight** rather than throughput. That is what Mini-KVM is aiming at.

**What Mini-KVM is:**

- A KVM-based runner for kernels you wrote yourself
- A diagnostic tool that explains guest failures instead of just reporting them
- A readable reference implementation of x86 virtualization, host *and* guest

**What Mini-KVM is not:**

- Not a production VMM. Use QEMU, Firecracker, or cloud-hypervisor.
- Not a cross-architecture emulator. It is built on KVM, so it runs x86 guests
  on x86 hosts and structurally cannot do otherwise. If you need to run an ARM
  kernel on an x86 laptop, you need QEMU.
- Not a container runtime or a sandbox.

---

## Quick start

Requires Linux with KVM enabled and read/write access to `/dev/kvm`.

```bash
# Fedora/RHEL
sudo dnf install gcc make binutils
# Ubuntu/Debian
sudo apt install gcc make binutils

ls -l /dev/kvm          # must exist and be accessible
```

```bash
git clone https://github.com/seolcu/mini-kvm.git
cd mini-kvm
make all
```

Then run a guest. A plain run prints only the guest's own output:

```bash
$ ./kvm-vmm guest/hello
Hello, KVM!
```

Run several guests at once — one per vCPU, each with its own memory, colored so
you can see them interleave:

```bash
./kvm-vmm guest/counter guest/hello guest/multiplication
```

Boot the bundled 32-bit OS into an interactive shell:

```bash
./kvm-vmm --paging os-1k/kernel
```

Enter 64-bit long mode:

```bash
$ ./kvm-vmm --long-mode guest/hello_64
Hello from 64-bit!
```

`make help` lists everything; `./kvm-vmm --help` lists all options.

---

## Current state

Everything in this section is exercised by `make test` (30 cases diffed against
stored baselines in `tools/baseline/`).

**Working**

| Capability | Notes |
|---|---|
| Real-mode (16-bit) guests | Flat binaries loaded at physical 0 |
| Multiple vCPUs | Up to 4, one guest program each, real pthreads |
| Protected mode + paging | 32-bit, 4KB pages, GDT/IDT built by the VMM |
| Long mode | 64-bit, PAE page tables |
| **ELF kernels** | Program headers loaded at their physical addresses |
| **Multiboot kernels** | 0x1BADB002 header, boot info structure, memory map |
| **VGA text mode** | 0xB8000 rendered to the terminal with `--vga` |
| **Interrupts** | In-kernel 8259 PIC, IOAPIC, LAPIC and 8254 PIT for kernel guests |
| **PS/2 keyboard** | Host input translated to set 1 scancodes, delivered on IRQ1 |
| **VGA cursor** | CRTC location registers tracked and rendered |
| 1K OS | Bundled teaching OS with a 9-program interactive shell |
| Hypercall interface | Port `0x500`; `EXIT`, `PUTCHAR`, blocking `GETCHAR` |
| 16550 UART (COM1) | `0x3f8`–`0x3ff`, forwarded to stdout |
| **Multiboot 2** | Tag-based information block, modules, EGA text framebuffer |
| **Multiboot modules** | `--module FILE`, which is how a kernel gets an initrd |
| **RTC / CMOS** | MC146818 read-only, giving the guest the host wall clock |
| **Linear framebuffer** | `--fb` honours the mode a kernel asks for; `--fb-dump` writes a PPM |
| **Flat protected mode** | `--flat32` for kernels whose own bootloader would enter it |
| **Fault analysis** | `--explain` names the cause; 32-bit, PAE and long-mode walks |
| **Linux** | A stock distribution kernel boots to a shell with an initramfs |
| **Inspection** | `--inspect` decodes GDT/IDT/page tables; `--trace-modes` follows transitions |
| **Preflight checks** | Warns before boot when an image's entry point cannot work |
| **CI integration** | `tools/ktest` and a GitHub Action |
| Diagnostics | `--verbose`, `--debug 0..3`, `--dump-regs`, `--dump-mem` |

A stock Multiboot kernel that knows nothing about Mini-KVM boots and runs:

```bash
$ make all
$ ./kvm-vmm --vga examples/multiboot-barebones/kernel.elf
 Multiboot bare bones kernel

Bootloader magic: 0x2BADB002  OK
Info structure:   0x00007000
Lower memory:     640 KB
Upper memory:     127 MB
Booted by:        Mini-KVM

Memory map:
  0x00000000 + 0x0009FC00  available
  0x0009FC00 + 0x00000400  reserved
  0x000F0000 + 0x00010000  reserved
  0x00100000 + 0x07F00000  available

Enabling interrupts...
Timer interrupts received: 10

Kernel reached the end of main. Halting.
```

That example uses no Mini-KVM headers and no hypercalls; the same binary
boots under GRUB. A second one under `examples/multiboot-keyboard` adds a
command prompt driven by PS/2 scancodes:

```bash
$ ./kvm-vmm --vga examples/multiboot-keyboard/kernel.elf
Multiboot keyboard example
Commands: help, clear, echo <text>, halt

> echo hello from the guest
hello from the guest
> halt
Halting.
```

When a guest dies, `--explain` single-steps it so the state from just before
the fault survives the CPU reset, then reads the guest's own tables to work
out the cause:

```bash
$ ./kvm-vmm --explain examples/faults/no-idt.elf
Guest triple-faulted (the CPU gave up and reset).
  Last instruction before the fault, at step 4:
    CS:EIP = 0x0008:0x0010001a, in protected mode, paging off
    bytes: f7 f1 f4 00 00 00 04 00
  That instruction raises #DE divide error - the divisor register is zero.

  IDT at 0x528, limit 0x7ff: 0 of 256 entries present.
  Not one entry is present, so no exception of any kind can
  be dispatched. The guest needs to build an IDT and lidt it
  before it can survive a fault.
```

Without `--explain` the state is already gone, and it says so rather than
presenting reset-vector registers as if they meant something.

**Not working yet**

| Gap | Consequence |
|---|---|
| No a.out kludge | Multiboot images that are not ELF are rejected |
| Graphics are not displayed | A framebuffer guest runs and can be dumped to a PPM, but not watched live |
| No virtio | Linux has no disk or network; an initramfs is the only root filesystem |
| Serial input races early boot | The kernel's UART probe eats anything typed before the shell starts, as on real hardware |

---

## Roadmap

The goal is a tool people actually use to develop x86 kernels. Each phase had a
gate that must pass before the next begins; all four are now met.

**Phase 1 — run other people's kernels** *(done)*
ELF32/64 loader, Multiboot 1 and 2, modules, VGA text buffer and cursor, a
linear framebuffer for kernels that ask for graphics, PIC and PIT interrupts,
PS/2 keyboard, and the RTC.
*Gate met.* Three kernels written by other people run unmodified, pinned in
`make test` (skipped unless `./tools/third-party.sh` has fetched them):

| Kernel | How it was obtained | Result |
|---|---|---|
| [ozkl/soso](https://github.com/ozkl/soso) | built from source, 45 C files | boots, renders 1024×768×32 |
| [joexbayer/RetrOS-32](https://github.com/joexbayer/RetrOS-32) | taken from the authors' own GRUB ISO | boots, renders 640×480×8 |
| [cfenollosa/os-tutorial](https://github.com/cfenollosa/os-tutorial) | built from source | interactive; takes keyboard input, halts on command |

No source was modified. Where a build failed on a modern toolchain it was
given compiler *flags* to restore the older default, which is what any
distribution does with aging software.

**Phase 2 — boot Linux** *(gate met)*
A stock Fedora kernel boots to an interactive shell on an initramfs. Pinned in
`make test`, which skips the case unless a kernel and initramfs are present —
neither belongs in the repository:

```bash
cp /boot/vmlinuz-$(uname -r) bzImage
./tools/mkinitramfs.sh initramfs.cpio
./kvm-vmm --linux bzImage --initrd initramfs.cpio --cmdline "console=ttyS0 rdinit=/init"
...
[    1.254616] Run /init as init process
[mini-kvm] userspace init started
sh-5.3# uname -s
Linux
```

**Phase 3 — the diagnostics that justify the project** *(done)*
`--explain` post-mortem fault analysis across 32-bit, PAE and long mode;
`--inspect` for decoding a live guest's GDT, IDT and page tables;
`--trace-modes` for real → protected → long transitions; and preflight checks
that catch an unusable entry point before the guest starts.

Using `--inspect` on the bundled 1K OS found a real bug in it on the first
run: its IDT gate descriptors had the selector and offset fields the wrong way
round.

**Phase 4 — workflow** *(done)*
`tools/ktest` boots a kernel, asserts on what it printed, and exits non-zero
if the assertions do not hold — the piece a kernel project needs to turn "it
compiles" into "it boots" in CI:

```bash
tools/ktest --name "my kernel boots" \
    --expect "Hello from my kernel" --forbid "panic" \
    build/kernel.elf -- --vga
```

It takes `--expect`, `--expect-re`, `--forbid` and `--expect-file`, can feed
the guest stdin (with a delay, for consoles that come up late), and exits 77
when `/dev/kvm` is unavailable so a skipped run is never mistaken for a pass.
Anything after `--` goes to Mini-KVM.

`action.yml` wraps it as a GitHub Action:

```yaml
- uses: seolcu/mini-kvm@main
  with:
    kernel: build/kernel.elf
    expect: |
      Hello from my kernel
    forbid: |
      panic
    vmm-args: --vga
```

Deliberately out of scope: virtio-blk/net, microVM and AI-sandbox use cases,
and ARM/RISC-V ports.

---

## How it works

```
┌──────────────────────────────────────────────────┐
│  Host userspace                                  │
│    Mini-KVM                                      │
│      cli.c        argv → config                  │
│      vm.c         /dev/kvm, the VM, memory slots │
│      vcpu.c       one pthread per vCPU, KVM_RUN  │
│      cpu_modes.c  real → protected → long        │
│      loader.c     ELF / Multiboot images         │
│      devices.c    UART, CRTC, legacy ports       │
│      ps2.c        i8042 keyboard                 │
│      hypercall.c  port 0x500                     │
│      console.c    terminal, input, output        │
│      vga.c        0xB8000 text buffer            │
│      explain.c    why the guest died              │
│                    ↕ ioctl()                     │
├──────────────────────────────────────────────────┤
│  Host kernel — KVM (hardware virtualization)     │
├──────────────────────────────────────────────────┤
│  Guest                                           │
│    a Multiboot or ELF kernel, a real-mode        │
│    program, the 1K OS, or a long-mode program    │
└──────────────────────────────────────────────────┘
```

Two design choices are worth knowing before reading the code:

**The VMM enters protected mode, not the guest.** Before the first `KVM_RUN`,
Mini-KVM builds the GDT, IDT, and page tables in guest memory and hands the vCPU
over with paging already live. A guest kernel starts executing in its target
mode and must not reload its segment registers.

**vCPUs are independent guests, not an SMP system.** Each vCPU gets its own
memory mapping at guest physical address `vcpu_id * mem_size` and runs a
different program. This is a teaching decision, not a virtualization one.

---

## Repository layout

```
src/          VMM source, one module per concern
  linux/      bzImage boot, quarantined from the core
guest/        Real-mode, protected-mode and long-mode guest programs
os-1k/        The bundled 32-bit teaching OS
examples/     Stock kernels that use no Mini-KVM facilities
initramfs/    Sources for the initramfs the Linux guest boots on
tools/        smoke.sh (make test), ktest, third-party.sh
```

`AGENTS.md` documents the architecture invariants and is the best starting point
for contributors.

## Contributing

Issues and pull requests are welcome — especially from anyone trying to boot
their own kernel and hitting the gaps above. Please run `make test` before
submitting; it must stay green, and any intended output change should be
re-baselined deliberately with `./tools/smoke.sh --update` and called out in the
commit message.

## Acknowledgements

Started as a self-directed project at Ajou University. The 1K OS guest is an
x86 port of the RISC-V kernel from
*[Operating System in 1,000 Lines](https://operating-system-in-1000-lines.vercel.app/)*.
