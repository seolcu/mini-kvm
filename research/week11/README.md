# 11주차 연구내용

목표: 1K OS 포팅 준비 - Protected Mode with Paging 지원 구현

저번주 todo:
- [x] Multi-vCPU 지원 구현 (2-4 guests 동시 실행)
- [x] 1K OS 포팅 시작 (Protected Mode + Paging)

## 연구 내용

### Multi-vCPU 지원 구현

Week 11의 첫 번째 목표는 여러 게스트 프로그램을 동시에 실행할 수 있도록 멀티 vCPU 지원을 추가하는 것이었습니다.

#### 아키텍처 설계

기존 단일 vCPU 구조를 확장하여 최대 4개의 vCPU를 동시에 실행할 수 있도록 설계했습니다:

```
┌─────────────────────────────────────┐
│  Host (Linux x86_64)                │
│  ┌───────────────────────────────┐  │
│  │ VMM Process (kvm-vmm)         │  │
│  │  ┌─────────┬─────────┬─────┐ │  │
│  │  │Thread 0 │Thread 1 │ ... │ │  │
│  │  │vCPU 0   │vCPU 1   │     │ │  │
│  │  └─────────┴─────────┴─────┘ │  │
│  └───────────────────────────────┘  │
│  ┌───────────────────────────────┐  │
│  │ KVM (/dev/kvm)                │  │
│  │  VM (single VM, multiple vCPUs)│ │
│  └───────────────────────────────┘  │
└─────────────────────────────────────┘

Memory Layout:
  vCPU 0: GPA 0x00000 - 0x3FFFF (256KB)
  vCPU 1: GPA 0x40000 - 0x7FFFF (256KB)
  vCPU 2: GPA 0x80000 - 0xBFFFF (256KB)
  vCPU 3: GPA 0xC0000 - 0xFFFFF (256KB)
```

각 vCPU는 독립적인 메모리 영역과 레지스터 상태를 가지며, pthread를 통해 병렬 실행됩니다.

#### 구현 상세

**1. Per-vCPU Context 구조체**

```c
typedef struct {
    int vcpu_id;                  // vCPU index (0-3)
    int vcpu_fd;                  // KVM vCPU file descriptor
    struct kvm_run *kvm_run;      // Per-vCPU run structure
    void *guest_mem;              // Per-guest memory region
    size_t mem_size;              // Memory size (256KB)
    const char *guest_binary;     // Binary filename
    char name[256];               // Display name
    int exit_count;               // VM exit counter
    bool running;                 // Execution state
} vcpu_context_t;
```

각 vCPU가 독립적인 상태를 유지하도록 모든 정보를 context 구조체에 캡슐화했습니다.

**2. 메모리 할당 및 매핑**

```c
static int setup_vcpu_memory(vcpu_context_t *ctx) {
    ctx->mem_size = 256 * 1024;  // 256KB per vCPU
    ctx->guest_mem = mmap(NULL, ctx->mem_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    struct kvm_userspace_memory_region mem_region = {
        .slot = ctx->vcpu_id,
        .flags = 0,
        .guest_phys_addr = ctx->vcpu_id * ctx->mem_size,  // Offset GPA
        .memory_size = ctx->mem_size,
        .userspace_addr = (uint64_t)ctx->guest_mem,
    };

    ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region);
}
```

각 vCPU의 메모리를 서로 다른 GPA에 매핑하여 격리를 보장합니다.

**3. Thread-safe 출력**

여러 vCPU가 동시에 출력할 때 섞이지 않도록 mutex와 ANSI 색상 코드를 사용했습니다:

```c
static pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

static void vcpu_printf(vcpu_context_t *ctx, const char *fmt, ...) {
    pthread_mutex_lock(&stdout_mutex);

    // vCPU별 색상: red, green, yellow, blue
    const char *colors[] = {"\033[31m", "\033[32m", "\033[33m", "\033[34m"};

    printf("%s[vCPU %d:%s]%s ", colors[ctx->vcpu_id],
           ctx->vcpu_id, ctx->name, "\033[0m");

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    pthread_mutex_unlock(&stdout_mutex);
}
```

**4. Pthread 기반 실행**

```c
static void *vcpu_thread(void *arg) {
    vcpu_context_t *ctx = (vcpu_context_t *)arg;

    while (ctx->running) {
        ioctl(ctx->vcpu_fd, KVM_RUN, 0);
        handle_vm_exit(ctx);
    }

    return NULL;
}

int main(int argc, char **argv) {
    pthread_t threads[MAX_VCPUS];

    // Spawn vCPU threads
    for (int i = 0; i < num_vcpus; i++) {
        pthread_create(&threads[i], NULL, vcpu_thread, &vcpus[i]);
    }

    // Wait for all vCPUs
    for (int i = 0; i < num_vcpus; i++) {
        pthread_join(threads[i], NULL);
    }
}
```

#### 테스트 결과

**2-vCPU 테스트:**
```bash
./kvm-vmm guest/multiplication.bin guest/counter.bin
```

출력:
```
[vCPU 0:multiplication] 2 x 1 = 2
[vCPU 1:counter] 0
[vCPU 0:multiplication] 2 x 2 = 4
[vCPU 1:counter] 1
...
```

두 게스트가 병렬로 실행되면서 출력이 인터리빙되는 것을 확인했습니다.

**4-vCPU 테스트:**
```bash
./kvm-vmm guest/multiplication.bin guest/counter.bin \
          guest/hello.bin guest/hctest.bin
```

4개의 게스트가 동시에 실행되며, 각각 색상으로 구분된 출력을 생성했습니다.

#### 주요 도전과제

**1. Static Buffer 버그**

초기 구현에서 `extract_guest_name()` 함수가 static buffer를 사용하여 여러 스레드에서 동시에 호출될 때 race condition이 발생했습니다. 이를 per-context buffer로 변경하여 해결했습니다.

**2. Real Mode CS:IP 설정**

각 vCPU는 서로 다른 GPA에서 시작해야 하므로, CS:IP를 적절히 설정해야 했습니다:
- vCPU 0: CS=0x0000, IP=0x0 (물리 주소 0x00000)
- vCPU 1: CS=0x4000, IP=0x0 (물리 주소 0x40000)
- vCPU 2: CS=0x8000, IP=0x0 (물리 주소 0x80000)
- vCPU 3: CS=0xC000, IP=0x0 (물리 주소 0xC0000)

**3. VM Exit 동기화**

각 vCPU의 VM exit을 독립적으로 처리하면서도 출력이 섞이지 않도록 mutex로 보호했습니다.

### 1K OS 프로젝트 분석

Week 11에서는 [Operating System in 1000 Lines](https://operating-system-in-1000-lines.vercel.app/en/) 프로젝트를 x86 KVM VMM으로 포팅하는 작업을 시작했습니다. 이 프로젝트는 원래 RISC-V RV32 아키텍처를 대상으로 작성된 교육용 OS로, 약 1000줄의 코드로 다음 기능을 구현합니다:

- Paging을 통한 메모리 관리
- 멀티태스킹 및 프로세스 관리
- Tar 파일시스템
- 간단한 Shell

RISC-V에서 x86으로 포팅하기 위해서는 먼저 x86 Protected Mode with Paging 환경을 구축해야 했습니다.

### Phase 2: Protected Mode with Paging 구현

#### 아키텍처 설계

x86 Protected Mode with Paging을 지원하기 위해 다음과 같은 메모리 레이아웃을 설계했습니다:

```
Physical Memory:
  0x00000000 - 0x003FFFFF  (4MB)

Virtual Memory (with paging):
  0x00000000 - 0x003FFFFF  (Identity mapping)
  0x80000000 - 0x803FFFFF  (Kernel space → 물리 0x0)

Page Directory:
  GPA 0x00100000 (1MB offset)
  - PDE[0]   = 0x00000083  (0x0-0x3FFFFF 매핑)
  - PDE[512] = 0x00000083  (0x80000000 가상 → 0x0 물리)
```

이 구조는 다음과 같은 장점이 있습니다:
- Identity mapping으로 부트 코드 실행 가능
- Higher-half kernel (0x80000000 이상)로 일반적인 OS 구조 유사
- 4MB PSE 페이지로 페이지 테이블 구조 단순화

#### VMM 확장

기존 Real Mode 전용 VMM을 확장하여 Protected Mode with Paging을 지원하도록 수정했습니다.

**1. 명령줄 인자 파싱 추가**

```c
bool enable_paging = false;
uint32_t entry_point = 0x80001000;
uint32_t load_offset = 0x1000;

// --paging 플래그 처리
if (strcmp(argv[i], "--paging") == 0) {
    enable_paging = true;
}
```

**2. 페이지 테이블 설정 함수**

```c
static int setup_page_tables(vcpu_context_t *ctx) {
    const uint32_t page_dir_offset = 0x00100000;
    uint32_t *page_dir = (uint32_t *)(ctx->guest_mem + page_dir_offset);

    memset(page_dir, 0, 4096);

    // Identity map: 0x0-0x3FFFFF
    page_dir[0] = 0x00000083;  // Present, R/W, 4MB page

    // Kernel space: 0x80000000-0x803FFFFF → 0x0-0x3FFFFF
    page_dir[512] = 0x00000083;

    return page_dir_offset;
}
```

**3. CR 레지스터 설정**

```c
sregs.cr3 = page_dir_offset;
sregs.cr0 |= 0x00000001;  // PE bit (Protected Mode)
sregs.cr0 |= 0x80000000;  // PG bit (Paging enabled)
sregs.cr4 |= 0x00000010;  // PSE bit (4MB pages)
```

**4. Flat Segment 설정**

GDT 없이 KVM의 unrestricted guest 모드를 활용하여 세그먼트를 직접 설정:

```c
sregs.cs.base = 0;
sregs.cs.limit = 0xFFFFFFFF;
sregs.cs.selector = 0x08;
sregs.cs.type = 0x0B;  // Execute/Read
sregs.cs.present = 1;
sregs.cs.dpl = 0;      // Ring 0
sregs.cs.db = 1;       // 32-bit
```

#### 테스트 커널 개발

Protected Mode with Paging을 테스트하기 위한 간단한 커널을 개발했습니다.

**1. boot.S - 부트 코드**

```asm
.code32
.section .text.boot
.global _start

_start:
    # VMM이 이미 Protected Mode + Paging 설정 완료
    # 세그먼트 레지스터 재로드 금지 (GDT 없음)

    # 스택 설정
    movl $__stack_top, %esp
    movl $0, %ebp

    # BSS 클리어
    movl $__bss, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb

    # kernel_main 호출
    call kernel_main

    # Halt
1:  hlt
    jmp 1b
```

**2. kernel.ld - 링커 스크립트**

```ld
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS {
    . = 0x80001000;  /* 가상 주소 */

    __kernel_base = .;

    .text.boot : { *(.text.boot) }
    .text : { *(.text*) }
    .rodata : ALIGN(4) { *(.rodata*) }
    .data : ALIGN(4) { *(.data*) }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss*)
        __bss_end = .;
    }

    __stack_bottom = .;
    . += 0x2000;  /* 8KB stack */
    __stack_top = .;

    __free_ram = .;
    __free_ram_end = 0x80400000;
}
```

**3. test_kernel.c - 테스트 코드**

Hypercall을 통한 출력 및 메모리 접근 테스트:

```c
static inline void hypercall_putchar(char c) {
    __asm__ volatile(
        "mov %0, %%bl\n\t"
        "mov %1, %%al\n\t"
        "mov %2, %%dx\n\t"
        "outb %%al, %%dx"
        :
        : "q"((uint8_t)c), "i"(0x01), "i"(0x500)
        : "al", "bl", "dx"
    );
}

void kernel_main(void) {
    puts("\n=== 1K OS x86 Test Kernel ===\n");

    // 커널 정보 출력
    print_hex((uint32_t)__bss);

    // 메모리 테스트
    volatile uint32_t *test_addr = (volatile uint32_t *)0x80010000;
    *test_addr = 0xDEADBEEF;
    if (*test_addr == 0xDEADBEEF) {
        puts("Memory test passed\n");
    }

    while (1) __asm__ volatile("hlt");
}
```

#### 트러블슈팅: Triple Fault 문제

초기 테스트에서 커널이 즉시 triple fault로 종료되는 문제가 발생했습니다.

**문제 발견 과정:**

1. **증상**: VM이 시작 직후 `KVM_EXIT_SHUTDOWN`으로 종료
2. **디버깅 1**: 단순한 어셈블리 코드는 정상 작동 확인
3. **디버깅 2**: CPU 상태 덤프 추가
   ```
   SHUTDOWN at RIP=0xfff0, RSP=0x0
   CR0=0x60000010  (PE, PG 비트 꺼짐!)
   CR3=0x0
   CS=0xf000  (Reset vector!)
   ```

4. **원인 분석**: CPU가 reset vector로 돌아감 → 설정이 적용 안됨

**근본 원인:**

boot.S의 다음 코드가 문제였습니다:

```asm
movl $0x10, %eax
movl %eax, %ds  # 세그먼트 레지스터 재로드!
```

Protected Mode에서 세그먼트 레지스터를 로드하면 GDT를 참조합니다. 하지만 우리는 GDT를 설정하지 않고 KVM의 unrestricted guest 모드로 세그먼트를 직접 설정했기 때문에, 이 명령이 protection fault를 발생시켰습니다.

**해결책:**

VMM이 이미 세그먼트 레지스터를 올바르게 설정했으므로, boot.S에서 세그먼트 레지스터 재로드 코드를 제거했습니다:

```asm
_start:
    # VMM이 이미 설정 완료 - 세그먼트 재로드 금지!
    movl $__stack_top, %esp  # 스택만 설정
    # ...
```

이 수정 후 커널이 정상적으로 실행되었습니다.

### 테스트 결과

최종 테스트 커널 실행 결과:

```
=== 1K OS x86 Test Kernel ===
Protected Mode with Paging Enabled

Kernel base:   0x80001000
BSS start:     0x80001624
BSS end:       0x80001624
Free RAM:      0x80004000 - 0x80400000

Testing memory access...
Memory test passed: 0x80010000 is writable

Test kernel completed successfully!
Halting CPU...
Guest halted after 306 exits
```

**주요 메트릭:**
- 커널 크기: 1.6KB (바이너리)
- VM exits: 306회 (대부분 hypercall)
- 메모리: 4MB 할당
- 실행 모드: Protected Mode with Paging

### 1K OS 포팅 시작 및 Hypercall 이슈

Week 11에서 1K OS를 x86 KVM VMM으로 포팅하는 작업을 시작했습니다. RISC-V 버전의 1K OS를 분석하고 x86 32-bit Protected Mode로 변환하는 과정에서 몇 가지 중요한 문제를 발견하고 해결했습니다.

#### 1K OS 아키텍처 분석

1K OS는 다음과 같은 구조로 구성됩니다:

```
Kernel (kernel.c):
- Process management (create_process, yield, switch_context)
- Memory allocator (alloc_pages, map_page)
- Syscall handling
- Tar filesystem (fs_init, fs_lookup)

User Space (user.c, shell.c):
- Shell process with command line parsing
- Basic commands: hello, echo, readfile, exit
- Syscall wrappers (getchar, putchar, etc.)
```

#### GDT/IDT 설정

x86 Protected Mode에서는 GDT(Global Descriptor Table)와 IDT(Interrupt Descriptor Table)가 필수입니다. 게스트 메모리 내부에 이들을 직접 구성했습니다:

**GDT 설정 (물리 주소 0x500):**
```c
static void setup_gdt(void) {
    struct gdt_entry *gdt = (struct gdt_entry *)(guest_mem + GDT_ADDR);
    
    // Null descriptor
    gdt[0] = (struct gdt_entry){0};
    
    // Kernel code segment (selector 0x08)
    gdt[1] = (struct gdt_entry){
        .limit_low = 0xFFFF,
        .base_low = 0,
        .base_mid = 0,
        .access = 0x9A,  // Present, Ring 0, Code, Readable
        .granularity = 0xCF,  // 4KB pages, 32-bit
        .base_high = 0
    };
    
    // Kernel data segment (selector 0x10)
    gdt[2] = (struct gdt_entry){
        .limit_low = 0xFFFF,
        .base_low = 0,
        .base_mid = 0,
        .access = 0x92,  // Present, Ring 0, Data, Writable
        .granularity = 0xCF,
        .base_high = 0
    };
    
    // User code/data segments 추가...
}
```

**IDT 설정 (물리 주소 0x528):**
```c
static void setup_idt(void) {
    struct idt_entry *idt = (struct idt_entry *)(guest_mem + IDT_ADDR);
    
    // 256 entries 모두 null로 초기화
    memset(idt, 0, 256 * sizeof(struct idt_entry));
}
```

KVM의 unrestricted guest 모드와 달리, 게스트 메모리에 실제 GDT/IDT를 구성하여 일반적인 Protected Mode 환경을 제공했습니다.

#### User Space 프로세스 생성

1K OS의 핵심 기능 중 하나는 user space 프로세스를 생성하고 관리하는 것입니다. Shell을 별도의 user process로 실행하기 위한 구현:

**1. 프로세스 페이지 테이블 설정**

User process는 다음과 같은 가상 메모리 레이아웃을 가집니다:
```
0x00000000 - 0x003FFFFF: Identity mapping (커널 공유)
0x01000000 - 0x01003FFF: User space (16KB)
0x80000000 - 0x803FFFFF: Kernel space (커널만 접근)
```

**2. Shell 바이너리 임베딩**

Shell 프로그램을 컴파일한 후 objcopy로 커널에 임베드:
```makefile
objcopy -I binary -O elf32-i386 -B i386 shell.bin shell.bin.o
ld -m elf_i386 -T kernel.ld -o kernel.elf \
    boot.o kernel.o shell.bin.o
```

링커가 제공하는 심볼을 통해 접근:
```c
extern char _binary_shell_bin_start[];
extern char _binary_shell_bin_size[];

struct process *shell_proc = create_process(
    _binary_shell_bin_start, 
    (size_t)_binary_shell_bin_size
);
```

**3. Context Switch 구현**

User space로 전환하기 위한 어셈블리 코드:
```c
__attribute__((naked)) void user_entry(void) {
    __asm__ volatile(
        "movl $0x01000000, %%eax\n\t"  // USER_BASE
        "jmp *%%eax\n\t"
        ::: "eax"
    );
}

// Bootstrap into shell
current_proc = shell_proc;
__asm__ volatile(
    "movl %0, %%cr3\n\t"      // Load shell's page table
    "movl %1, %%esp\n\t"      // Set stack pointer
    "jmp user_entry\n\t"
    :
    : "r" ((uint32_t)shell_proc->page_table),
      "r" (shell_proc->sp)
    : "memory"
);
```

#### 트러블슈팅: 스택 페이지 누락

Shell 프로세스가 시작 직후 Page Fault로 종료되는 문제가 발생했습니다.

**증상:**
```
Created shell process (pid=2)
Starting shell process...
Page Fault! addr=0x01003ffc
```

**원인 분석:**

Shell의 스택은 0x01003FFC에서 시작하는데 (아래로 성장), 처음에는 user code page (0x01000000-0x01000FFF)만 매핑하고 스택 페이지를 매핑하지 않았습니다.

**해결:**

User space에 총 4개 페이지를 매핑하도록 수정:
```c
void map_user_pages(struct process *proc) {
    // Page 0: Code/Data (0x01000000-0x01000FFF)
    map_page(proc, 0x01000000, phys_base, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    
    // Page 1-3: Stack (0x01001000-0x01003FFF)
    for (int i = 1; i < 4; i++) {
        uint32_t vaddr = USER_BASE + i * 0x1000;
        uint32_t paddr = phys_base + i * 0x1000;
        map_page(proc, vaddr, paddr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
}
```

이 수정 후 Shell이 정상적으로 부팅되었습니다:
```
=== Kernel Initialization Complete ===
Starting shell process (PID 2)...

> 
```

#### Hypercall 문제: IN 명령어가 Trap되지 않음

Shell이 정상적으로 부팅된 후, getchar() syscall을 통해 사용자 입력을 받으려 할 때 심각한 문제가 발견되었습니다.

**증상:**

```
> [ch=0] [ch=0] [ch=0] [ch=0] [ch=0]
command line too long
```

Shell이 입력을 기다리지 않고 ch=0을 계속 읽어들여 즉시 "command line too long" 에러를 발생시켰습니다.

**원인 조사 1: VM Exit 로깅**

모든 VM exit의 종류를 로깅하는 코드를 추가했습니다:

```c
static int handle_vm_exit(vcpu_context_t *ctx) {
    static int io_count = 0;
    if (io_count++ < 100) {
        vcpu_printf(ctx, "IO[%d]: dir=%s port=0x%x size=%d\n",
                   io_count,
                   (ctx->kvm_run->io.direction == KVM_EXIT_IO_OUT) ? "OUT" : "IN",
                   ctx->kvm_run->io.port,
                   ctx->kvm_run->io.size);
    }
    // ...
}
```

**결과:**
```
IO[1]: dir=OUT port=0x500 size=1
IO[2]: dir=OUT port=0x500 size=1
...
IO[100]: dir=OUT port=0x500 size=1
GETCHAR[1] request, setting pending_getchar
GETCHAR[2] request, setting pending_getchar
...
```

**중요한 발견:** 10,001개의 VM exit가 모두 `KVM_EXIT_IO`였고, 그 중 **단 하나의 IN 명령어도 trap되지 않았습니다**. 모든 I/O exit이 `dir=OUT`이었습니다.

**원인 분석:**

원래 getchar() 구현은 다음과 같았습니다:

```c
long getchar(void) {
    long ch;
    __asm__ volatile(
        "movb $2, %%al\n\t"        // HC_GETCHAR
        "movw $0x500, %%dx\n\t"    // Port 0x500
        "outb %%al, %%dx\n\t"      // Trigger GETCHAR hypercall (OUT)
        "inb (%%dx), %%al\n\t"     // Read character from port (IN)
        "movsbl %%al, %0"          // Sign-extend AL to long
        : "=r"(ch)
        :
        : "al", "dx"
    );
    return ch;
}
```

OUT 명령어는 정상적으로 VM exit를 발생시키지만, **IN 명령어는 전혀 trap되지 않았습니다**. 이는 아마도 다음 이유 때문으로 추정됩니다:

1. **AMD SVM의 I/O 처리 방식:** AMD-V (SVM)에서는 IN 명령어가 특정 조건에서 trap되지 않을 수 있음
2. **CPL=0 권한:** 커널 코드가 Ring 0에서 실행되므로 IOPL 체크 없이 직접 I/O 실행 가능
3. **KVM 구현 세부사항:** 특정 I/O port range에 대한 최적화

**Workaround 시도 1: 메모리 기반 통신**

IN 명령어를 사용하는 대신, VMM이 결과를 게스트 메모리에 직접 쓰는 방식을 시도했습니다:

```c
// Guest kernel
#define HYPERCALL_RESULT_ADDR ((volatile int *)0x4000)

long getchar(void) {
    // Request via OUT
    __asm__ volatile(
        "movb $2, %%al\n\t"
        "movw $0x500, %%dx\n\t"
        "outb %%al, %%dx\n\t"
        ::: "al", "dx", "memory"
    );
    
    // Read result from memory
    return *HYPERCALL_RESULT_ADDR;
}
```

```c
// VMM
case HC_GETCHAR: {
    // Read character from stdin
    int ch = -1;
    if (select(...) > 0) {
        ch = getchar();
    }
    
    // Write to guest memory at physical 0x4000
    volatile int32_t *result_ptr = 
        (volatile int32_t *)(ctx->guest_mem + 0x4000);
    *result_ptr = (int32_t)ch;
    break;
}
```

**결과:**

VMM은 성공적으로 값을 쓰고 읽을 수 있었습니다:
```
Verification: VMM wrote -1, readback -1 at phys 0x4000
```

하지만 게스트는 여전히 0을 읽었습니다:
```
> [ch=0] [ch=0] [ch=0] ...
```

**Workaround 시도 2: TLB Invalidation**

캐시 일관성 문제를 의심하여 TLB invalidation을 추가했습니다:

```c
long getchar(void) {
    // Request via OUT
    __asm__ volatile(
        "movb $2, %%al\n\t"
        "movw $0x500, %%dx\n\t"
        "outb %%al, %%dx\n\t"
        ::: "al", "dx", "memory"
    );
    
    // Invalidate TLB for result address
    __asm__ volatile(
        "invlpg (%0)"
        :: "r" (HYPERCALL_RESULT_ADDR)
        : "memory"
    );
    
    return *HYPERCALL_RESULT_ADDR;
}
```

**결과:** 여전히 동일한 문제 발생

**근본 원인 추정:**

VMM userspace와 게스트 실행 컨텍스트 간의 **메모리 일관성 문제**입니다:

1. VMM이 `mmap`으로 매핑한 메모리에 값을 씀
2. 게스트 CPU는 자체 캐시/TLB를 가지고 있음
3. 일반적인 x86 캐시 일관성 프로토콜이 이 두 컨텍스트 사이에 적용되지 않음
4. 게스트는 stale value를 계속 읽음

#### 미해결 문제 및 향후 계획

현재 getchar() hypercall은 작동하지 않으며, 다음 해결 방법을 검토 중입니다:

**옵션 1: CPUID-based Hypercalls**
- CPUID 명령어는 항상 VM exit를 발생시킴
- 레지스터를 통한 양방향 통신 가능

**옵션 2: VMCALL/VMMCALL 명령어**
- 명시적 hypercall 명령어
- 표준적인 가상화 인터페이스

**옵션 3: kvm_run I/O data 영역 활용**
- KVM API의 공유 메모리 영역 사용
- 확실한 메모리 일관성 보장

**옵션 4: Interrupt 기반 Syscall**
- Port I/O 대신 INT 0x80 스타일 syscall
- IDT를 통한 trap

**옵션 5: KVM dirty page tracking**
- KVM의 메모리 동기화 메커니즘 활용
- 명시적 cache flush API 사용

이 문제는 x86 가상화의 저수준 메커니즘과 KVM의 메모리 세맨틱스에 대한 깊은 이해가 필요합니다. Week 12에서 이 문제를 해결하여 완전히 작동하는 1K OS를 구현할 계획입니다.

### 주요 학습 포인트

#### 1. KVM Unrestricted Guest Mode

KVM의 unrestricted guest 기능을 사용하면 GDT 없이 세그먼트 레지스터를 직접 설정할 수 있습니다. 이는 VMM에서 유용하지만, 게스트 코드가 세그먼트 레지스터를 재로드하려고 시도하면 문제가 발생합니다.

**핵심 교훈:**
- VMM이 unrestricted guest로 세그먼트 설정 시, 게스트는 세그먼트 재로드 금지
- 전통적인 Protected Mode 진입 시퀀스와 다른 초기화 방식
- 부트 코드 작성 시 VMM 환경 고려 필수

#### 2. x86 Page Table Structure

4MB PSE 페이지를 사용하면:
- 페이지 디렉토리만으로 충분 (페이지 테이블 불필요)
- PDE 플래그: bit 0 (Present), bit 1 (R/W), bit 7 (PS=Page Size)
- 간단한 구조로 디버깅 용이

#### 3. Higher-Half Kernel

Higher-half kernel 구조 (0x80000000 이상)의 장점:
- User space와 kernel space 명확한 분리
- 일반적인 OS 구조와 유사
- 향후 프로세스 관리 구현 시 유리

### 코드 통계

**추가된 파일:**
- `kvm-vmm-x86/src/main.c`: 897줄 추가/수정 (페이징 지원)
- `kvm-vmm-x86/os-1k/boot.S`: 52줄 (부트 코드)
- `kvm-vmm-x86/os-1k/kernel.ld`: 65줄 (링커 스크립트)
- `kvm-vmm-x86/os-1k/test_kernel.c`: 135줄 (테스트 커널)
- `kvm-vmm-x86/os-1k/Makefile`: 62줄 (빌드 시스템)
- `kvm-vmm-x86/os-1k/DESIGN.md`: 설계 문서

**VMM 기능:**
- Real Mode 지원 (기존)
- Protected Mode with Paging 지원 (신규)
- 멀티 vCPU 지원 (Week 10)
- 총 라인 수: ~1,200줄

## 다음 계획

### Week 12 목표

1. **1K OS 컴포넌트 포팅**
   - [ ] Process management (create_process, yield, switch_context)
   - [ ] Memory allocator (alloc_pages, map_page)
   - [ ] Exception/interrupt handling
   - [ ] Syscall 인터페이스

2. **Filesystem 구현**
   - [ ] Embedded tar filesystem
   - [ ] File I/O 함수들

3. **Shell 및 User Programs**
   - [ ] 간단한 shell 포팅
   - [ ] Echo, ls 등 기본 프로그램

### 추가 고려사항

- Performance 분석: KVM vs QEMU 비교
- Interrupt/Exception handling 개선
- 더 많은 테스트 케이스 작성

## 결론

**Week 11 완료 내용:**
1. ✅ 1K OS 프로젝트 분석 및 포팅 전략 수립
2. ✅ VMM에 Protected Mode with Paging 지원 추가
3. ✅ 4MB PSE 페이징 구현 (Identity + Higher-half mapping)
4. ✅ 테스트 커널 개발 및 검증 완료
5. ✅ Triple fault 디버깅 및 해결

**프로젝트 상태:**
- **완료**: Phase 0 (Bare-metal), Phase 1 (RISC-V KVM), Phase 2 (x86 Protected Mode + Paging)
- **진행 중**: Phase 3 (1K OS 포팅)
- **남은 기간**: 5주 (Week 12-16)

Week 11에서 1K OS 포팅의 핵심 기반인 Protected Mode with Paging 지원을 성공적으로 구현했습니다. 가장 큰 도전은 KVM unrestricted guest 모드에서의 세그먼트 처리 방식을 이해하는 것이었습니다. Triple fault 문제를 해결하면서 x86 Protected Mode의 세그먼트 메커니즘과 KVM의 가상화 방식에 대한 깊은 이해를 얻을 수 있었습니다.

다음 주에는 실제 1K OS 컴포넌트들을 포팅하여 완전한 작동 가능한 OS를 만드는 작업에 집중할 예정입니다.

## 참고 자료

- [Operating System in 1000 Lines](https://operating-system-in-1000-lines.vercel.app/en/)
- [Intel® 64 and IA-32 Architectures Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [KVM API Documentation](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [OSDev Wiki - Paging](https://wiki.osdev.org/Paging)
- [OSDev Wiki - Higher Half Kernel](https://wiki.osdev.org/Higher_Half_Kernel)
