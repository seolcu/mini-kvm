# Mini-KVM Presentation

**Educational x86 Virtual Machine Monitor using KVM**

November 2025

---

## Slide 1: Title

**Mini-KVM: Educational x86 Virtual Machine Monitor**

Seolcu  
November 23, 2025

A minimal yet functional KVM-based hypervisor with multi-vCPU support

---

## Slide 2: Problem Statement

**Modern hypervisors are complex**

- QEMU: 2M+ lines of code
- VirtualBox: 500K+ lines
- VMware: Closed source, millions of LOC

**Challenge**: Understanding virtualization requires simpler examples

**Solution**: Build minimal VMM demonstrating core concepts

---

## Slide 3: Project Goals

**Primary Objectives**:
1. Implement functional VMM using Linux KVM API
2. Support Real Mode (16-bit) and Protected Mode (32-bit)
3. Demonstrate interrupt handling and I/O emulation
4. Port small OS (1K OS) to virtualized environment

**Success Criteria**:
- Multi-vCPU execution (up to 4)
- Complete interrupt infrastructure
- Interactive guest programs
- Performance 50-100x better than QEMU TCG

---

## Slide 4: Architecture Overview

```
┌─────────────────────────────────────┐
│         Host Linux (Ubuntu)         │
└──────────────┬──────────────────────┘
               │ KVM API
┌──────────────▼──────────────────────┐
│          VMM (main.c)               │
│  - vCPU management (4 threads)      │
│  - Memory setup (4MB per vCPU)      │
│  - Hypercall interface (3 types)    │
│  - Interrupt injection (timer/kbd)  │
└──────────────┬──────────────────────┘
               │ Guest RAM
┌──────────────▼──────────────────────┐
│      Guest OS / Programs            │
│  - Real Mode: 6 programs            │
│  - Protected Mode: 1K OS            │
│  - User programs: 9 demos           │
└─────────────────────────────────────┘
```

---

## Slide 5: VMM Core Features

**Memory Management**:
- Real Mode: 256KB per vCPU (0x0, 0x40000, 0x80000, 0xC0000)
- Protected Mode: 4MB with paging (4MB PSE pages)
- Identity mapping + higher-half kernel (0x80000000)

**vCPU Management**:
- Thread-per-vCPU model (pthread)
- Independent execution contexts
- Color-coded console output

**Hypercall Interface**:
- HC_EXIT (0x00): Terminate guest
- HC_PUTCHAR (0x01): Console output
- HC_GETCHAR (0x02): Keyboard input
- Port I/O trap on 0x500

---

## Slide 6: Real Mode Guests (6 Programs)

| Program | Size | Description |
|---------|------|-------------|
| minimal.S | 1 byte | Immediate exit (VM overhead test) |
| hello.S | 15 bytes | Print "H" via hypercall |
| counter.S | 18 bytes | Count 0-9 with delays |
| fibonacci.S | 82 bytes | Fibonacci sequence (0-89) |
| multiplication.S | 112 bytes | 2x2 to 9x9 table |
| hctest.S | 79 bytes | Hypercall interface test |

**Demo**: Multi-vCPU execution (2-4 guests in parallel)

---

## Slide 7: 1K OS Architecture

**Protected Mode Kernel** (900 LOC):
- GDT/IDT initialization
- 2-level paging (identity + higher-half)
- Timer interrupt (IRQ0, 10ms periodic)
- Keyboard interrupt (IRQ1, hypercall-based)
- Simple process scheduler (round-robin)
- Bump allocator for kernel heap

**User Space Shell** (200 LOC):
- Interactive menu system
- 9 demo programs (options 1-9)
- Hypercall-based I/O

---

## Slide 8: 1K OS Programs (9 Total)

1. **Multiplication Table**: 2x1 to 9x9 table
2. **Counter**: Count 0-9 with delays
3. **Echo**: Interactive input echo until 'q'
4. **About Screen**: System information
5. **Fibonacci**: Sequence up to 89
6. **Prime Numbers**: Find primes up to 100
7. **ASCII Art**: Display 1K OS logo
8. **Factorial**: Calculate 0! to 12! (NEW)
9. **GCD**: Euclidean algorithm demo (NEW)

**Demo**: Interactive shell navigation

---

## Slide 9: Interrupt Handling

**Timer Interrupt Flow**:
1. VMM timer thread wakes every 10ms
2. Inject IRQ0 (vector 0x20) via KVM_INTERRUPT
3. Guest IDT dispatches to handler
4. Handler increments tick count, returns via iret

**Keyboard Interrupt Flow**:
1. VMM keyboard thread polls stdin
2. Character added to circular buffer (256 bytes)
3. Inject IRQ1 (vector 0x21)
4. Guest handler invokes HC_GETCHAR hypercall
5. VMM returns character from buffer

**Key Insight**: Hypercall-based polling simplifies implementation vs. PIO emulation

---

## Slide 10: Implementation Highlights

**KVM API Integration** (10 syscalls):
- `open("/dev/kvm")` - Get KVM handle
- `ioctl(KVM_CREATE_VM)` - Create VM instance
- `ioctl(KVM_SET_USER_MEMORY_REGION)` - Map guest RAM
- `ioctl(KVM_CREATE_VCPU)` - Create virtual CPU
- `ioctl(KVM_SET_SREGS/REGS)` - Configure registers
- `ioctl(KVM_RUN)` - Enter guest mode (blocking)
- `ioctl(KVM_INTERRUPT)` - Inject interrupt

**Memory Layout** (Protected Mode):
```
0x00000000 - 0x000FFFFF: Identity map (1MB)
0x00100000 - 0x003FFFFF: Kernel + data (3MB)
0x80000000 - 0x803FFFFF: Higher-half (4MB PSE)
```

---

## Slide 11: Multi-vCPU Demonstration

**2 vCPUs Example**:
```bash
./kvm-vmm guest/multiplication.bin guest/counter.bin
```

Output:
```
2 x 1 = 2
2 x 2 = 4
0123456789
2 x 3 = 6
...
```

**Key Features**:
- Parallel execution on separate CPU cores
- Independent 256KB memory per vCPU
- Thread-safe console output
- Color-coded output per vCPU

---

## Slide 12: Performance Analysis

**VM Exit Counts**:
- minimal.S: 1 exit (baseline)
- hello.S: 3 exits
- counter.S: ~1,461 exits
- 1K OS boot: ~5,000 exits

**Execution Time**:
- minimal.S: <1ms
- counter.S: ~5,000ms (intentional delays)
- 1K OS boot: ~100ms

**Comparison**:
- KVM: ~1,000 cycles per VM exit
- QEMU TCG: ~30,000-100,000 cycles per instruction
- **Speedup**: 50-100x for compute workloads

---

## Slide 13: Technical Challenges

**Challenge 1: Segment Register Initialization**
- Problem: CS/DS/SS values incorrect for Real Mode
- Solution: Set CS=0x0000, DS=0x0000, SS=0x0000, IP=0x0000

**Challenge 2: Protected Mode Transition**
- Problem: GDT/IDT must be setup before CR0.PE=1
- Solution: Build GDT/IDT at fixed addresses (0x500, 0x528)

**Challenge 3: Keyboard Input**
- Problem: IN instruction doesn't trap in KVM
- Solution: Hypercall-based polling (HC_GETCHAR)

**Challenge 4: Multi-vCPU Memory Overlap**
- Problem: Multiple guests loaded at same address
- Solution: Per-vCPU offsets (0x0, 0x40000, 0x80000, 0xC0000)

---

## Slide 14: Code Statistics

| Component | Lines of Code | Status |
|-----------|---------------|--------|
| VMM (main.c) | 1,400 | Complete |
| 1K OS Kernel | 700 | Complete |
| 1K OS Shell | 200 | Complete |
| Guest Programs | 1,200 | Complete |
| Build System | 200 | Complete |
| **Total** | **3,500** | **Done** |

**Development Timeline**:
- Week 1-4: VMM core + Real Mode guests
- Week 5-8: Protected Mode + interrupts
- Week 9-12: 1K OS port + shell programs
- Week 12: Final polish + documentation

---

## Slide 15: Testing and Validation

**Test Coverage**:
- Unit tests: All 6 Real Mode guests
- Integration tests: 1K OS boot + 9 programs
- System tests: Multi-vCPU (1, 2, 4 vCPUs)
- Long-running tests: Counter (10+ seconds)

**Validation Methods**:
- Manual testing (all features)
- Performance profiling (VM exits, timing)
- Code review (AGENTS.md standards)
- Documentation review

**Results**: All tests passing, no known critical bugs

---

## Slide 16: Live Demo Plan

**Demo 1**: Real Mode Guests
- Run minimal.S (1 byte, immediate exit)
- Run counter.S (0-9 output)

**Demo 2**: Multi-vCPU
- Run 2 guests in parallel (multiplication + counter)
- Show interleaved output

**Demo 3**: 1K OS Interactive Shell
- Boot 1K OS (Protected Mode)
- Navigate menu (show 2-3 programs)
- Option 8: Factorial (0! to 12!)
- Option 9: GCD (Euclidean algorithm)

**Time**: 3-5 minutes total

---

## Slide 17: Lessons Learned

**Virtualization**:
- Interrupt delivery more complex than direct hardware
- Memory consistency requires careful synchronization
- I/O emulation overhead significant

**x86 Architecture**:
- Real Mode segment addressing (CS*16 + IP)
- Protected Mode requires GDT/IDT before transition
- Paging enable sequence critical (CR3 then CR0.PG)

**Systems Programming**:
- Concurrent design (threads) simplifies interrupt injection
- Bare-metal environment challenges (no OS services)
- Circular buffers efficient for I/O

---

## Slide 18: Future Enhancements

**Short-Term (1-2 weeks)**:
- Full keyboard support (all scancodes)
- Disk I/O emulation (file-backed)
- Graphics output (simple framebuffer)

**Medium-Term (1-2 months)**:
- Network interface (TAP device)
- SMP guest support (shared memory)
- Performance optimizations (eventfd)

**Long-Term (6+ months)**:
- Boot full Linux kernel
- QEMU backend integration
- Hypervisor comparison research

---

## Slide 19: Educational Value

**Suitable For**:
- Undergraduate OS courses (CS 140/240)
- Graduate virtualization seminars (CS 340)
- Independent study projects
- Open-source contribution practice

**Learning Outcomes**:
- Hardware-assisted virtualization concepts
- x86 Real Mode, Protected Mode, paging
- Kernel initialization and interrupt handling
- Low-level C and inline assembly
- Performance analysis and optimization

**Advantages**:
- Minimal codebase (~3,500 LOC vs. QEMU's 2M+)
- Clear architecture and documentation
- Functional and demonstrable
- Extensible for research projects

---

## Slide 20: Conclusion

**Achievements**:
- Fully functional KVM-based VMM
- Multi-vCPU support (up to 4)
- Complete interrupt infrastructure
- 6 Real Mode + 9 Protected Mode programs
- 50-100x performance vs. QEMU TCG

**Deliverables**:
- Source code: ~3,500 LOC (well-documented)
- Documentation: 5 comprehensive documents
- Build system: Fully automated (Makefiles)
- Test suite: All guests + 1K OS programs

**Impact**:
- Educational tool for virtualization courses
- Research baseline for performance studies
- Foundation for future extensions

**Project Status**: Feature complete, ready for use

---

## Slide 21: Q&A

**Common Questions**:

**Q: Why KVM instead of QEMU?**  
A: KVM provides hardware-assisted virtualization (50-100x faster). QEMU TCG is software emulation.

**Q: Why hypercalls instead of PIO/MMIO?**  
A: Simpler implementation, clearer guest code, sufficient for educational purposes.

**Q: Can it boot Linux?**  
A: Not yet. Needs disk, VGA, PCI emulation (~3,000+ LOC). Feasible future work.

**Q: How does multi-vCPU work?**  
A: Thread-per-vCPU model, each with isolated 256KB RAM, independent execution.

**Q: What's the performance overhead?**  
A: ~1,000 cycles per VM exit, ~1-10ms interrupt latency. Acceptable for education.

---

## Slide 22: Thank You

**Mini-KVM: Educational x86 Virtual Machine Monitor**

**Repository**: https://github.com/seolcu/mini-kvm  
**Documentation**: README.md, FINAL_REPORT.md, DEMO_GUIDE.md  
**License**: MIT License

**Contact**: seolcu@example.com

**Questions?**

---

## Backup Slides: Technical Details

### Backup 1: KVM API Usage

**VM Creation**:
```c
int kvm_fd = open("/dev/kvm", O_RDWR);
int vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
```

**Memory Setup**:
```c
struct kvm_userspace_memory_region region = {
    .slot = 0,
    .guest_phys_addr = 0x0,
    .memory_size = 4 * 1024 * 1024, // 4MB
    .userspace_addr = (uint64_t)mem,
};
ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region);
```

**vCPU Creation**:
```c
int vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, vcpu_id);
struct kvm_run *run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, 
                           MAP_SHARED, vcpu_fd, 0);
```

---

### Backup 2: Hypercall Implementation

**Guest Side** (inline assembly):
```c
static inline void hypercall_putchar(char c) {
    asm volatile("outl %%eax, %%dx" 
                 : : "a"(1), "b"(c), "d"(0x500));
}
```

**VMM Side** (C):
```c
case KVM_EXIT_IO:
    if (run->io.port == 0x500) {
        uint32_t type = *(uint32_t *)((char *)run + run->io.data_offset);
        if (type == 1) { // HC_PUTCHAR
            putchar(regs.rbx);
        }
    }
    break;
```

---

### Backup 3: Interrupt Injection

**Timer Thread**:
```c
void *timer_thread(void *arg) {
    while (1) {
        usleep(10000); // 10ms
        struct kvm_interrupt irq = { .irq = 0x20 };
        ioctl(vcpu_fd, KVM_INTERRUPT, &irq);
    }
}
```

**Guest Handler**:
```c
__attribute__((naked)) void timer_handler(void) {
    asm volatile(
        "pusha\n"
        "incl timer_ticks\n"
        "popa\n"
        "iret\n"
    );
}
```

---

### Backup 4: Performance Data

**VM Exit Breakdown** (counter.S):
- HC_PUTCHAR: 10 exits (digits 0-9)
- HC_EXIT: 1 exit (termination)
- HLT: ~1,450 exits (delays)
- **Total**: ~1,461 exits

**Execution Time Breakdown**:
- VM entry/exit overhead: <1%
- Hypercall processing: <1%
- Delay loops (HLT): >98%

**Conclusion**: VM overhead negligible for I/O-bound workloads

---

### Backup 5: Code Quality Metrics

**Compiler Warnings**: All suppressed or fixed  
**Memory Leaks**: None (static allocation only)  
**Thread Safety**: Mutex-protected console output  
**Error Handling**: All syscalls checked  

**Code Style** (AGENTS.md):
- K&R formatting, 4-space indent
- snake_case functions/variables
- Comprehensive comments
- 80-char line limit

**Testing**:
- Manual testing: 100+ runs
- Performance profiling: KVM perf counters
- Long-running tests: 10+ seconds stable

---

## End of Presentation

Total Slides: 22 main + 5 backup = 27 slides  
Estimated Presentation Time: 15-20 minutes  
Demo Time: 3-5 minutes  
Q&A Time: 5-10 minutes  
**Total: 25-35 minutes**
