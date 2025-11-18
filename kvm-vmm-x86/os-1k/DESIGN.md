# 1K OS x86 Port Design Document

## Overview

Porting "Operating System in 1000 Lines" (RISC-V RV32) to x86 Protected Mode (32-bit) for execution in KVM VMM.

## Memory Layout

### Physical Memory (Guest Physical Address)
```
0x00000000 - 0x00000FFF (4KB):   Null page (unmapped for safety)
0x00001000 - 0x00100FFF (1MB):   Kernel code + data + BSS
0x00101000 - 0x00102FFF (8KB):   Kernel stack
0x00103000 - 0x003FFFFF (~3MB):  Free RAM for page allocation
0x00400000 - 0x00FFFFFF (12MB):  Reserved for future expansion
```

### Virtual Memory (32-bit, 2-level paging)
```
0x00000000 - 0x7FFFFFFF (2GB):   User space
  0x01000000 (USER_BASE):        User program entry point
0x80000000 - 0xFFFFFFFF (2GB):   Kernel space (identity mapped)
  0x80000000:                    Kernel base (maps to physical 0x00000000)
```

### Page Table Structure (x86 32-bit, PAE off)
```
Page Directory (1024 entries * 4 bytes = 4KB)
├─ PDE[0-511]:   User space (0x00000000 - 0x7FFFFFFF)
└─ PDE[512-1023]: Kernel space (0x80000000 - 0xFFFFFFFF)

Page Table (1024 entries * 4 bytes = 4KB per table)
└─ PTE[0-1023]:   4KB pages

PDE/PTE Format: [31:12] PPN | [11:0] Flags
Flags: G(8), PAT(7), D(6), A(5), PCD(4), PWT(3), U/S(2), R/W(1), P(0)
```

## Architecture Translation

### Register Mapping

| RISC-V | x86 | Purpose |
|--------|-----|---------|
| a0-a2  | EBX, ECX, EDX | Syscall arguments |
| a3     | EAX | Syscall number |
| ra     | Return addr on stack | Return address |
| sp     | ESP | Stack pointer |
| s0-s11 | Pushed to stack | Callee-saved registers |

### Syscall Interface

**RISC-V Original:**
- Instruction: `ecall`
- Number: a3 register
- Args: a0, a1, a2
- Return: a0

**x86 Port (Hypercall-based):**
- Instruction: `out %al, $0x500`
- Number: EAX register
- Args: EBX, ECX, EDX
- Return: EAX

**Syscall Numbers:**
```c
#define SYS_PUTCHAR     1  // EBX = character
#define SYS_GETCHAR     2  // Returns char in EAX
#define SYS_EXIT        3  // Process exit
#define SYS_READFILE    4  // EBX=filename, ECX=buf, EDX=len
#define SYS_WRITEFILE   5  // EBX=filename, ECX=buf, EDX=len
```

### Page Table Translation

**RISC-V Sv32:**
```
VPN[1] = vaddr[31:22] (10 bits) -> 1st level index
VPN[0] = vaddr[21:12] (10 bits) -> 2nd level index
Offset = vaddr[11:0]  (12 bits)

PTE = [31:10] PPN | [9:8] RSW | [7:0] DAGUXWRV
```

**x86 32-bit (PAE off):**
```
PD Index  = vaddr[31:22] (10 bits)
PT Index  = vaddr[21:12] (10 bits)
Offset    = vaddr[11:0]  (12 bits)

PDE/PTE = [31:12] PPN | [11:0] Flags
```

**Flag Mapping:**
| RISC-V Sv32 | x86 32-bit | Description |
|-------------|------------|-------------|
| V (Valid)   | P (Present) | Page is valid/present |
| R (Read)    | P + ~R/W   | Read-only |
| W (Write)   | R/W        | Writable |
| X (Execute) | (no NX)    | Executable (always on x86) |
| U (User)    | U/S        | User-accessible |

### Context Switch Translation

**RISC-V:**
```asm
switch_context:
    addi sp, sp, -52    # Save 13 regs * 4 bytes
    sw ra,  0(sp)
    sw s0,  4(sp)
    ...
    sw s11, 48(sp)
    sw sp, (a0)         # Save SP to prev_sp
    lw sp, (a1)         # Load SP from next_sp
    lw ra,  0(sp)
    ...
    addi sp, sp, 52
    ret
```

**x86 Translation:**
```asm
switch_context:
    pushl %ebx          # Save callee-saved regs
    pushl %esi
    pushl %edi
    pushl %ebp
    movl 20(%esp), %eax # prev_sp (adjusted for pushes)
    movl %esp, (%eax)   # Save ESP
    movl 24(%esp), %eax # next_sp
    movl (%eax), %esp   # Load ESP
    popl %ebp           # Restore regs
    popl %edi
    popl %esi
    popl %ebx
    ret
```

## VMM Modifications

### Required Changes to src/main.c

1. **Add Paging Support**
   ```c
   static int setup_page_tables(vcpu_context_t *ctx) {
       // Allocate page directory at guest physical 0x00100000
       uint32_t *page_dir = (uint32_t *)(ctx->guest_mem + 0x00100000);

       // Identity map kernel space (0x80000000 - 0xFFFFFFFF)
       for (int i = 512; i < 1024; i++) {
           uint32_t phys_addr = (i - 512) * 0x400000;  // 4MB increments
           page_dir[i] = phys_addr | 0x83;  // Present, R/W, 4MB pages
       }

       return 0;
   }
   ```

2. **Enable Paging in Guest**
   ```c
   // In setup_vcpu_context()
   sregs.cr3 = 0x00100000;  // Page directory physical address
   sregs.cr0 |= 0x80000000;  // Enable paging (PG bit)
   sregs.cr4 &= ~0x00000020;  // Disable PAE
   ```

3. **Handle Syscall Hypercalls**
   ```c
   case KVM_EXIT_IO:
       if (kvm_run->io.port == 0x500 && kvm_run->io.direction == KVM_EXIT_IO_OUT) {
           struct kvm_regs regs;
           ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs);

           int sysno = regs.rax & 0xFF;
           switch (sysno) {
               case SYS_PUTCHAR:
                   vcpu_putchar(ctx, regs.rbx & 0xFF);
                   break;
               case SYS_EXIT:
                   ctx->running = false;
                   break;
               // ... other syscalls
           }
       }
       break;
   ```

## Boot Sequence

### x86 boot.S
```asm
.code32
.section .text.boot
.global _start

_start:
    # Setup segments
    movl $0x10, %eax
    movl %eax, %ds
    movl %eax, %es
    movl %eax, %fs
    movl %eax, %gs
    movl %eax, %ss

    # Setup stack
    movl $__stack_top, %esp

    # Clear BSS
    movl $__bss, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb

    # Jump to kernel_main
    call kernel_main

    # Halt if kernel_main returns
1:  hlt
    jmp 1b
```

## Build Process

### Toolchain
- Compiler: `gcc -m32` or `clang --target=i386-unknown-elf`
- Assembler: `as --32`
- Linker: `ld -m elf_i386`
- Flags: `-std=c11 -O2 -ffreestanding -nostdlib -fno-stack-protector -fno-pie`

### Build Steps
1. Compile shell: `gcc -m32 -c shell.c user.c common.c`
2. Link shell: `ld -m elf_i386 -T user.ld -o shell.elf`
3. Extract shell binary: `objcopy -O binary shell.elf shell.bin`
4. Embed shell: `objcopy -I binary -O elf32-i386 shell.bin shell.bin.o`
5. Compile kernel: `gcc -m32 -c kernel.c common.c`
6. Link kernel: `ld -m elf_i386 -T kernel.ld -o kernel.elf kernel.o common.o shell.bin.o`
7. Extract kernel binary: `objcopy -O binary kernel.elf kernel.bin`

## Testing Strategy

### Phase 1: Boot Test
- Goal: Kernel boots and prints "Hello from kernel_main"
- Test: Load kernel.bin at GPA 0x00001000, set EIP to entry point
- Success: VMM sees output via hypercall

### Phase 2: Memory Test
- Goal: Page allocation and mapping works
- Test: Allocate pages, map virtual addresses, access them
- Success: No page faults, memory reads/writes work

### Phase 3: Process Test
- Goal: Create process and switch context
- Test: Create 2 processes, yield() between them
- Success: Both processes execute, context switch works

### Phase 4: Syscall Test
- Goal: All syscalls work
- Test: Call each syscall from user process
- Success: Correct behavior for putchar, getchar, file ops, exit

### Phase 5: Shell Test
- Goal: Interactive shell works
- Test: Boot to shell, run commands (ls, cat, echo)
- Success: Commands execute, file I/O works

## Simplified Scope (MVP)

### Included
- Kernel boot in Protected Mode with paging
- Process creation and cooperative scheduling
- 5 syscalls (putchar, getchar, exit, readfile, writefile)
- Embedded tar filesystem (no virtio-blk)
- Shell with basic commands

### Excluded (Future Work)
- Virtio-blk device emulation
- Timer interrupts (cooperative scheduling only)
- Preemptive multitasking
- Advanced optimizations

## Risk Mitigation

### High-Risk Areas
1. **Page Table Setup**: Use identity mapping first, add user space later
2. **Syscall Interface**: Test each syscall individually
3. **Context Switching**: Log register values before/after
4. **Memory Layout**: Use simple flat layout initially

### Fallback Plans
1. If paging fails: Use flat memory model temporarily
2. If embedded FS fails: Use simple memory-based files
3. If context switch fails: Single-process kernel first

## Timeline

- **Week 12 Day 1-2**: VMM paging support + boot code (8-10 hours)
- **Week 12 Day 3-4**: Memory management + syscalls (6-8 hours)
- **Week 12 Day 5-7**: Context switching + process mgmt (8-10 hours)
- **Week 13 Day 1-2**: Embedded filesystem (4-6 hours)
- **Week 13 Day 3-4**: Shell port and testing (6-8 hours)
- **Week 13 Day 5-7**: Integration testing and debugging (6-8 hours)

**Total: 38-50 hours over 2 weeks**
