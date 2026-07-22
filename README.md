# Mini-KVM

**A small x86 hypervisor for people who write their own kernels.**

Mini-KVM runs a guest directly on Linux KVM and tells you what it did — where it
faulted, what was in its descriptor tables, what its page tables actually
mapped. It is built on the raw KVM ioctl API in about 4,200 lines of C (5,300
with headers), small enough to read in an afternoon.

[한국어 README](README.ko.md) · [License: MIT](LICENSE)

> **Status: early but real.** Mini-KVM boots stock Multiboot and ELF kernels,
> renders VGA text mode, and runs its own real-mode guests and a bundled 32-bit
> teaching OS, with working timer interrupts. Its Linux boot support is
> incomplete, and the diagnostics that are the whole point of the project are
> still ahead. See
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
cd mini-kvm/kvm-vmm-x86
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

Everything in this section is exercised by `make test` (16 cases diffed against
stored baselines in `kvm-vmm-x86/tools/baseline/`).

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
| 1K OS | Bundled teaching OS with a 9-program interactive shell |
| Hypercall interface | Port `0x500`; `EXIT`, `PUTCHAR`, blocking `GETCHAR` |
| 16550 UART (COM1) | `0x3f8`–`0x3ff`, forwarded to stdout |
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
boots under GRUB.

**Not working yet**

| Gap | Consequence |
|---|---|
| No PS/2 keyboard or RTC | A kernel cannot read the clock or take keyboard input |
| No Multiboot 2 | Only the original 0x1BADB002 protocol is recognised |
| No a.out kludge | Multiboot images that are not ELF are rejected |
| Linux boot incomplete | `--linux` loads a bzImage but does not reach a shell |
| No virtio | No block or network devices |
| No diagnostics yet | The features that justify the project are Phase 3 |

---

## Roadmap

The goal is a tool people actually use to develop x86 kernels. Each phase has a
gate that must pass before the next begins.

**Phase 1 — run other people's kernels** *(in progress)*
Done: ELF32/64 loader, Multiboot 1, VGA text buffer, PIC and PIT interrupts.
Remaining: PS/2 keyboard, RTC, and Multiboot 2.
*Gate: three third-party hobby kernels boot unmodified, pinned in CI.*

**Phase 2 — boot Linux**
Finish the 64-bit entry path. An initramfs avoids needing virtio.
*Gate: a busybox shell prompt, pinned in CI.*

**Phase 3 — the diagnostics that justify the project**
`--explain` for post-mortem fault analysis, `inspect` for decoding live
GDT/IDT/page tables, mode-transition tracing, and pre-boot validation of guest
descriptor tables. Target output:

```
Triple fault at CS:EIP = 0x08:0x00100234
  Fault chain: #PF (CR2=0xdeadbeef) → #GP → #DF → reset
  Cause: IDT entry 14 (#PF) has P=0 — no handler installed.
  Your IDT at 0x00009000 has 3 valid entries of 256.
  Page tables (CR3=0x00100000): 0xdeadbeef is not mapped.
    PDE[891] = 0x00000000 (not present)
```

**Phase 4 — workflow**
A kernel test runner (boot, assert on output, exit code) and a GitHub Action.

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
│      loader.c    ELF / Multiboot images          │
│      devices.c    16550 UART, legacy ports       │
│      hypercall.c  port 0x500                     │
│      console.c    terminal, input, output        │
│      vga.c        0xB8000 text buffer            │
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
kvm-vmm-x86/          The hypervisor — this is the project
  src/                VMM source, one module per concern
    linux/            Experimental bzImage boot, quarantined
  guest/              Real-mode and protected-mode guest programs (assembly)
  os-1k/              The bundled 32-bit teaching OS
  examples/           Stock kernels that use no Mini-KVM facilities
  tools/smoke.sh      Verification harness (make test)
docs/  research/  meetings/    Reports and notes, mostly Korean
experimental/         Unrelated Rust experiments, not built here
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

Started as a self-directed university project (Ajou University, 2025-2) and
received an Encouragement Award in the SoftCon research division. The 1K OS
guest is an x86 port of the RISC-V kernel from
*[Operating System in 1,000 Lines](https://operating-system-in-1000-lines.vercel.app/)*.
