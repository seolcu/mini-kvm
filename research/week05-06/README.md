# 5-6주차 연구내용

목표: vCPU 생성 및 제어, 메모리 입출력, VM 종료 코드 작성

저번주 todo:

- 전체 점검

## 연구 내용

4주차까지 Bochs를 이용해 xv6를 부팅하려는 시도를 했으나, 여러 문제로 인해 큰 진전이 없었습니다. 교수님과의 상담을 통해, Bochs 접근 방식을 포기하고 새로운 전략을 시도하기로 했습니다.

교수님께서 x86은 너무 복잡하니, RISC-V로 하이퍼바이저 개념을 먼저 배우고 나중에 x86 KVM으로 전환하는 방법을 추천해 주셨습니다. 그래서 [RISC-V 기반 VMM 개발 튜토리얼](https://1000hv.seiya.me/en/)을 따라가기로 했습니다.

#### 튜토리얼 구성 확인

해당 튜토리얼을 훑어보니, RISC-V 아키텍처 기반으로 Rust를 사용해 bare-metal 하이퍼바이저를 만드는 방법을 설명하고 있습니다. 목표는 약 1000줄 정도의 코드로 Linux 커널을 부팅하는 것이고, QEMU 에뮬레이터를 사용한다고 합니다.

튜토리얼은 총 13개 챕터로 구성되어 있습니다:

1. Getting Started - 개발 환경 구성
2. Boot - 하이퍼바이저 부팅
3. Hello World - 기본 출력
4. Memory Allocation - 메모리 할당
5. Guest Mode - 게스트 모드 진입
6. Guest Page Table - 페이지 테이블
7. Hello from Guest - 게스트 실행
8. Build Linux Kernel - 리눅스 빌드
9. Boot Linux - 리눅스 부팅
10. Supervisor Binary Interface - SBI
11. Memory Mapped I/O - MMIO
12. Interrupt Injection - 인터럽트
13. Outro - 마무리

이 중 챕터 8부터는 미완성인 듯 합니다.

### Chapter 1-2: 개발 환경 및 부팅 구조 만들기

#### Rust 툴체인 설치

패키지 매니저 대신 튜토리얼에서 권장하는 Rustup으로 설치했습니다:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

QEMU는 이미 설치되어 있었기 때문에 넘어갔습니다.

#### Rust 프로젝트 생성

새 프로젝트를 만들었습니다:

```bash
cargo init --bin hypervisor
```

`rust-toolchain.toml` 파일을 만들어서 툴체인을 지정했습니다:

```toml
[toolchain]
channel = "stable"
targets = ["riscv64gc-unknown-none-elf"]
```

#### 최소 부팅 코드 작성

`src/main.rs`를 작성했습니다. `#![no_std]`와 `#![no_main]` 속성을 사용해 표준 라이브러리 없이 bare-metal 환경에서 실행되도록 했습니다:

```rust
#![no_std]
#![no_main]

use core::arch::asm;

#[unsafe(no_mangle)]
#[unsafe(link_section = ".text.boot")]
pub extern "C" fn boot() -> ! {
    unsafe {
        asm!(
            "la sp, __stack_top",
            "j {main}",
            main = sym main,
            options(noreturn)
        );
    }
}

fn main() -> ! {
    // BSS 초기화
    unsafe {
        let bss_start = &raw mut __bss;
        let bss_size = (&raw mut __bss_end as usize) - (&raw mut __bss as usize);
        core::ptr::write_bytes(bss_start, 0, bss_size);
    }

    loop {}
}
```

#### 링커 스크립트 작성

`hypervisor.ld`를 만들어 메모리 레이아웃을 정의했습니다:

```ld
ENTRY(boot)

SECTIONS {
    . = 0x80200000;  /* OpenSBI가 커널을 로드하는 주소 */

    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
    }

    .rodata : ALIGN(8) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(8) {
        *(.data .data.*);
    }

    .bss : ALIGN(8) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __bss_end = .;
    }

    . = ALIGN(16);
    . += 1024 * 1024; /* 1MB 스택 */
    __stack_top = .;
}
```

#### Panic Handler 추가

빌드하니 panic handler가 없다는 에러가 발생했습니다:

```
error: `#[panic_handler]` function required, but not found
```

`no_std` 환경에서는 panic handler를 직접 구현해야 합니다:

```rust
use core::panic::PanicInfo;

#[panic_handler]
pub fn panic_handler(_info: &PanicInfo) -> ! {
    loop {
        unsafe {
            core::arch::asm!("wfi");
        }
    }
}
```

#### 빌드 및 실행

`run.sh` 스크립트를 작성했습니다:

```bash
#!/bin/sh
set -ev

RUSTFLAGS="-C link-arg=-Thypervisor.ld -C linker=rust-lld" \
  cargo build --bin hypervisor --target riscv64gc-unknown-none-elf

cp target/riscv64gc-unknown-none-elf/debug/hypervisor hypervisor.elf

qemu-system-riscv64 \
    -machine virt \
    -cpu rv64 \
    -bios default \
    -smp 1 \
    -m 128M \
    -nographic \
    -d cpu_reset,unimp,guest_errors,int -D qemu.log \
    -serial mon:stdio \
    --no-reboot \
    -kernel hypervisor.elf
```

실행해봤습니다:

```bash
chmod +x run.sh
./run.sh
```

OpenSBI 부팅 메시지가 출력된 후 멈췄습니다. 아직 아무것도 출력하지 않지만, `main()` 함수의 무한 루프가 실행되고 있는 듯 합니다.

### Chapter 3: Hello World

#### SBI를 이용한 콘솔 출력

튜토리얼을 보니, OpenSBI 펌웨어가 제공하는 SBI(Supervisor Binary Interface)를 사용할 수 있다고 합니다. `src/print.rs`를 만들어서 SBI putchar 함수를 구현했습니다:

```rust
use core::arch::asm;

pub fn sbi_putchar(ch: u8) {
    unsafe {
        asm!(
            "ecall",
            in("a6") 0,
            in("a7") 1,
            inout("a0") ch as usize => _,
            out("a1") _
        );
    }
}
```

`ecall` 명령을 inline assembly로 호출합니다. `a7`=1은 Console Putchar extension입니다.

#### println! 매크로 구현

Rust의 `fmt::Write` 트레이트를 구현해서 `println!` 매크로를 만들었습니다:

```rust
pub struct Printer;

impl core::fmt::Write for Printer {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for byte in s.bytes() {
            sbi_putchar(byte);
        }
        Ok(())
    }
}

#[macro_export]
macro_rules! println {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = writeln!($crate::print::Printer, $($arg)*);
    }};
}
```

#### Trap Handler 구현

CPU가 exception이나 interrupt를 만나면 trap handler가 호출됩니다. `src/trap.rs`를 만들었습니다:

```rust
#[macro_export]
macro_rules! read_csr {
    ($csr:expr) => {{
        let mut value: u64;
        unsafe {
            ::core::arch::asm!(concat!("csrr {}, ", $csr), out(reg) value);
        }
        value
    }};
}

#[unsafe(link_section = ".text.stvec")]
pub fn trap_handler() -> ! {
    let scause = read_csr!("scause");
    let sepc = read_csr!("sepc");
    let stval = read_csr!("stval");
    
    panic!("trap: scause={:#x} at {:#x} (stval={:#x})", scause, sepc, stval);
}
```

Panic handler도 수정해서 panic 정보를 출력하도록 했습니다:

```rust
#[panic_handler]
pub fn panic_handler(info: &PanicInfo) -> ! {
    println!("panic: {}", info);
    loop {
        unsafe {
            core::arch::asm!("wfi");
        }
    }
}
```

#### main() 수정

`src/main.rs`를 수정해서 trap handler를 등록하고 부팅 메시지를 출력했습니다:

```rust
#[macro_use]
mod print;
mod trap;

fn main() -> ! {
    unsafe {
        let bss_start = &raw mut __bss;
        let bss_size = (&raw mut __bss_end as usize) - (&raw mut __bss as usize);
        core::ptr::write_bytes(bss_start, 0, bss_size);

        asm!("csrw stvec, {}", in(reg) trap::trap_handler as usize);
    }

    println!("\nBooting hypervisor...");
    loop {}
}
```

#### 실행 결과

실행해보니:

```
Booting hypervisor...
```

드디어 출력에 성공했습니다.

### Chapter 4: Memory Allocation

#### Bump Allocator 구현

`no_std` 환경에서 `Vec`, `Box` 같은 자료구조를 쓰려면 메모리 할당자를 직접 구현해야 합니다. 가장 단순한 bump allocator를 구현했습니다.

`spin` 크레이트를 추가합니다.

```bash
cargo add spin
```

`src/allocator.rs`를 만듭니다.

```rust
use core::alloc::{GlobalAlloc, Layout};
use spin::Mutex;

pub struct BumpAllocator {
    next: usize,
    end: usize,
}

unsafe impl GlobalAlloc for BumpAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let mut next = self.next;
        let align = layout.align();
        let size = layout.size();
        
        // Align
        next = (next + align - 1) & !(align - 1);
        
        let new_next = next + size;
        if new_next > self.end {
            return core::ptr::null_mut();
        }
        
        self.next = new_next;
        next as *mut u8
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
        // Bump allocator는 해제를 지원하지 않음
    }
}

#[global_allocator]
static ALLOCATOR: Mutex<BumpAllocator> = Mutex::new(BumpAllocator {
    next: 0,
    end: 0,
});
```

링커 스크립트에 힙 영역을 추가합니다.

```ld
    . = ALIGN(16);
    __heap = .;
    . += 8 * 1024 * 1024; /* 8MB 힙 */
    __heap_end = .;
```

`main()`에서 할당자를 초기화했습니다:

```rust
unsafe extern "C" {
    static mut __heap: u8;
    static mut __heap_end: u8;
}

fn main() -> ! {
    // ... BSS 초기화 ...
    
    unsafe {
        let heap_start = &raw mut __heap as usize;
        let heap_size = (&raw mut __heap_end as usize) - heap_start;
        allocator::init(heap_start, heap_size);
    }
    
    // ...
}
```

#### Page Allocator 구현

Bump allocator 위에 4KB 단위로 메모리를 할당하는 page allocator를 만들었습니다:

```rust
extern crate alloc;
use alloc::vec::Vec;

const PAGE_SIZE: usize = 4096;

pub fn alloc_pages(size: usize) -> *mut u8 {
    let pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    let mut v = Vec::with_capacity(pages * PAGE_SIZE);
    v.resize(pages * PAGE_SIZE, 0);
    let ptr = v.as_mut_ptr();
    core::mem::forget(v);
    ptr
}
```

#### 테스트

`Vec`을 테스트해봤습니다

```rust
extern crate alloc;
use alloc::vec::Vec;

fn main() -> ! {
    // ... 초기화 ...
    
    println!("\nBooting hypervisor...");
    
    let mut v = Vec::new();
    for i in 0..10 {
        v.push(i);
    }
    println!("vec test: {:?}", v);
    
    loop {}
}
```

다음과 같은 실행 결과가 출력되었습니다:

```
Booting hypervisor...
vec test: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
```

잘 작동합니다.

### Chapter 5-6: Guest Mode 및 Page Table

#### 게스트 모드 진입 시도

RISC-V 가상화 모드를 활성화하려면 H-extension을 켜야 합니다. `run.sh`에 `-cpu rv64,h=true`를 추가했습니다:

```bash
qemu-system-riscv64 \
    -machine virt \
    -cpu rv64,h=true \
    ...
```

게스트 모드 진입 코드를 작성했습니다:

```rust
fn main() -> ! {
    // ... 초기화 ...
    
    println!("\nBooting hypervisor...");
    
    let guest_code: [u8; 4] = [0x73, 0x00, 0x00, 0x00]; // wfi 명령
    
    unsafe {
        // hstatus 설정
        let mut hstatus: u64 = 0;
        hstatus |= 2 << 32; // VSXL=2 (64-bit mode)
        hstatus |= 1 << 7;  // SPV=1 (virtualization mode)
        asm!("csrw hstatus, {}", in(reg) hstatus);
        
        // sstatus 설정
        let mut sstatus: u64 = 0;
        sstatus |= 1 << 8; // SPP=1 (return to supervisor mode)
        asm!("csrw sstatus, {}", in(reg) sstatus);
        
        // sepc 설정
        asm!("csrw sepc, {}", in(reg) guest_code.as_ptr() as u64);
        
        // 게스트로 진입
        asm!("sret");
    }
    
    loop {}
}
```

실행하니 바로 트랩이 발생했습니다:

```
panic: trap: scause=0xc at 0x80200b7a (stval=0x80200b7a)
```

`scause=0xc`는 instruction page fault입니다. 게스트 코드가 직접 실행되려 했지만 주소 변환이 안 돼서 발생한 에러입니다.

#### Guest Page Table 구현

2-stage address translation을 구현해야 합니다. `src/guest_page_table.rs`를 만듭니다.

```rust
use crate::allocator::alloc_pages;

pub const PTE_V: u64 = 1 << 0;
pub const PTE_R: u64 = 1 << 1;
pub const PTE_W: u64 = 1 << 2;
pub const PTE_X: u64 = 1 << 3;

pub struct GuestPageTable {
    root: *mut u64,
}

impl GuestPageTable {
    pub fn new() -> Self {
        let root = alloc_pages(4096) as *mut u64;
        unsafe {
            core::ptr::write_bytes(root, 0, 4096);
        }
        Self { root }
    }
    
    pub fn map(&mut self, guest_pa: u64, host_pa: u64, flags: u64) {
        // 4-level page table (Sv48x4) 구현
        // ...
    }
    
    pub fn hgatp(&self) -> u64 {
        let mode = 9u64 << 60; // Sv48x4 mode
        mode | ((self.root as u64) >> 12)
    }
}
```

Page table 구현은 4단계로 나뉩니다:

1. VPN[3]으로 L3 테이블 인덱스
2. VPN[2]로 L2 테이블 인덱스
3. VPN[1]로 L1 테이블 인덱스
4. VPN[0]으로 L0 테이블 인덱스

각 단계에서 PTE가 없으면 새 페이지를 할당하고, 마지막 단계에서 host PA를 매핑합니다.

#### 게스트 코드 컴파일

간단한 어셈블리 코드를 작성했습니다:

```asm
.section .text
.global guest_boot
guest_boot:
    j guest_boot
```

GCC로 컴파일하고 바이너리로 변환했습니다:

```bash
riscv64-linux-gnu-as -o guest.o guest.S
riscv64-linux-gnu-ld -Ttext=0x100000 -o guest.elf guest.o
riscv64-linux-gnu-objcopy -O binary guest.elf guest.bin
```

#### 메모리 로딩 및 매핑

게스트 코드를 메모리에 로드하고 page table에 매핑합니다.

```rust
fn main() -> ! {
    // ... 초기화 ...
    
    println!("\nBooting hypervisor...");
    
    // 게스트 코드 로드
    let guest_bin = include_bytes!("../guest.bin");
    let kernel_memory = alloc_pages(guest_bin.len());
    unsafe {
        core::ptr::copy_nonoverlapping(
            guest_bin.as_ptr(),
            kernel_memory,
            guest_bin.len()
        );
    }
    
    // Page table 설정
    let guest_entry = 0x100000u64;
    let mut table = GuestPageTable::new();
    table.map(guest_entry, kernel_memory as u64, PTE_R | PTE_W | PTE_X);
    
    println!("map: {:08x} -> {:08x}", guest_entry, kernel_memory as u64);
    
    // 게스트 모드 진입
    unsafe {
        let mut hstatus: u64 = 0;
        hstatus |= 2 << 32;
        hstatus |= 1 << 7;
        asm!("csrw hstatus, {}", in(reg) hstatus);
        
        let sstatus: u64 = 1 << 8;
        asm!("csrw sstatus, {}", in(reg) sstatus);
        
        asm!("csrw hgatp, {}", in(reg) table.hgatp());
        asm!("csrw sepc, {}", in(reg) guest_entry);
        
        asm!("sret");
    }
    
    loop {}
}
```

#### 실행 결과

실행해보니:

```
Booting hypervisor...
map: 00100000 -> 80305000
```

멈췄습니다. 게스트 코드가 무한 루프를 실행하고 있기 때문으로 보입니다.

#### 문제: alloc_pages 길이 처리

튜토리얼과 다르게 한 가지 문제를 발견했습니다. 튜토리얼 코드는 `alloc_pages(1)`을 호출해 1페이지를 할당하는데, 제 구현은 `alloc_pages(size)`가 바이트 단위입니다.

따라서 `alloc_pages(4096)`로 수정해야 했습니다. 아니면 `alloc_pages`의 시그니처를 바꿔서 페이지 수를 받도록 할 수도 있지만, 우선은 그대로 두기로 했습니다.

### Chapter 7: Hello from Guest (Hypercall)

Chapter 7의 목표는 게스트에서 하이퍼바이저로 통신하는 것입니다.

#### 게스트 코드 수정

게스트가 SBI의 "Console Putchar"를 호출하도록 `guest.S`를 수정했습니다:

```asm
.section .text
.global guest_boot
guest_boot:
    li a7, 1        # EID=1 (legacy console)
    li a6, 0        # FID=0 (putchar)
    li a0, 'A'      # Parameter: 'A'
    ecall           # Call SBI

    li a7, 1
    li a6, 0
    li a0, 'B'
    ecall

    li a7, 1
    li a6, 0
    li a0, 'C'
    ecall

halt:
    j halt
```

빌드하고 실행하면 이렇게 트랩이 발생합니다.

```
panic: trap: scause=0xa at 0x100008 (stval=0x0)
```

`scause=0xa`는 "environment call from VS-mode"입니다. 게스트가 `ecall`을 호출한 거라 정확히 우리가 원하는 트랩입니다.

#### VCpu 구조체 도입

Hypercall을 구현하기 전에, 게스트 상태를 관리할 `VCpu` 구조체를 만들겠습니다. 새 파일 `src/vcpu.rs`를 만듭니다.

```rust
use core::{arch::asm, mem::offset_of};
use crate::{allocator::alloc_pages, guest_page_table::GuestPageTable};

#[derive(Debug, Default)]
pub struct VCpu {
    pub hstatus: u64,
    pub hgatp: u64,
    pub sstatus: u64,
    pub sepc: u64,
    pub host_sp: u64,
    pub ra: u64,
    pub sp: u64,
    pub gp: u64,
    pub tp: u64,
    pub t0: u64,
    pub t1: u64,
    pub t2: u64,
    pub s0: u64,
    pub s1: u64,
    pub a0: u64,
    pub a1: u64,
    pub a2: u64,
    pub a3: u64,
    pub a4: u64,
    pub a5: u64,
    pub a6: u64,
    pub a7: u64,
    pub s2: u64,
    pub s3: u64,
    pub s4: u64,
    pub s5: u64,
    pub s6: u64,
    pub s7: u64,
    pub s8: u64,
    pub s9: u64,
    pub s10: u64,
    pub s11: u64,
    pub t3: u64,
    pub t4: u64,
    pub t5: u64,
    pub t6: u64,
}

impl VCpu {
    pub fn new(table: &GuestPageTable, guest_entry: u64) -> Self {
        let mut hstatus: u64 = 0;
        hstatus |= 2 << 32; // VSXL=2
        hstatus |= 1 << 7;  // SPV=1

        let sstatus: u64 = 1 << 8; // SPP=1

        let stack_size = 512 * 1024;
        let host_sp = alloc_pages(stack_size) as u64 + stack_size as u64;
        
        Self {
            hstatus,
            hgatp: table.hgatp(),
            sstatus,
            sepc: guest_entry,
            host_sp,
            ..Default::default()
        }
    }

    pub fn run(&mut self) -> ! {
        unsafe {
            asm!(
                "csrw hstatus, {hstatus}",
                "csrw sstatus, {sstatus}",
                "csrw sscratch, {sscratch}",
                "csrw hgatp, {hgatp}",
                "csrw sepc, {sepc}",

                "mv a0, {sscratch}",
                "ld ra, {ra_offset}(a0)",
                "ld sp, {sp_offset}(a0)",
                "ld gp, {gp_offset}(a0)",
                "ld tp, {tp_offset}(a0)",
                "ld t0, {t0_offset}(a0)",
                "ld t1, {t1_offset}(a0)",
                "ld t2, {t2_offset}(a0)",
                "ld s0, {s0_offset}(a0)",
                "ld s1, {s1_offset}(a0)",
                "ld a1, {a1_offset}(a0)",
                "ld a2, {a2_offset}(a0)",
                "ld a3, {a3_offset}(a0)",
                "ld a4, {a4_offset}(a0)",
                "ld a5, {a5_offset}(a0)",
                "ld a6, {a6_offset}(a0)",
                "ld a7, {a7_offset}(a0)",
                "ld s2, {s2_offset}(a0)",
                "ld s3, {s3_offset}(a0)",
                "ld s4, {s4_offset}(a0)",
                "ld s5, {s5_offset}(a0)",
                "ld s6, {s6_offset}(a0)",
                "ld s7, {s7_offset}(a0)",
                "ld s8, {s8_offset}(a0)",
                "ld s9, {s9_offset}(a0)",
                "ld s10, {s10_offset}(a0)",
                "ld s11, {s11_offset}(a0)",
                "ld t3, {t3_offset}(a0)",
                "ld t4, {t4_offset}(a0)",
                "ld t5, {t5_offset}(a0)",
                "ld t6, {t6_offset}(a0)",
                "ld a0, {a0_offset}(a0)",

                "sret",
                hstatus = in(reg) self.hstatus,
                sstatus = in(reg) self.sstatus,
                hgatp = in(reg) self.hgatp,
                sepc = in(reg) self.sepc,
                sscratch = in(reg) (self as *mut VCpu as usize),
                ra_offset = const offset_of!(VCpu, ra),
                sp_offset = const offset_of!(VCpu, sp),
                gp_offset = const offset_of!(VCpu, gp),
                tp_offset = const offset_of!(VCpu, tp),
                t0_offset = const offset_of!(VCpu, t0),
                t1_offset = const offset_of!(VCpu, t1),
                t2_offset = const offset_of!(VCpu, t2),
                s0_offset = const offset_of!(VCpu, s0),
                s1_offset = const offset_of!(VCpu, s1),
                a0_offset = const offset_of!(VCpu, a0),
                a1_offset = const offset_of!(VCpu, a1),
                a2_offset = const offset_of!(VCpu, a2),
                a3_offset = const offset_of!(VCpu, a3),
                a4_offset = const offset_of!(VCpu, a4),
                a5_offset = const offset_of!(VCpu, a5),
                a6_offset = const offset_of!(VCpu, a6),
                a7_offset = const offset_of!(VCpu, a7),
                s2_offset = const offset_of!(VCpu, s2),
                s3_offset = const offset_of!(VCpu, s3),
                s4_offset = const offset_of!(VCpu, s4),
                s5_offset = const offset_of!(VCpu, s5),
                s6_offset = const offset_of!(VCpu, s6),
                s7_offset = const offset_of!(VCpu, s7),
                s8_offset = const offset_of!(VCpu, s8),
                s9_offset = const offset_of!(VCpu, s9),
                s10_offset = const offset_of!(VCpu, s10),
                s11_offset = const offset_of!(VCpu, s11),
                t3_offset = const offset_of!(VCpu, t3),
                t4_offset = const offset_of!(VCpu, t4),
                t5_offset = const offset_of!(VCpu, t5),
                t6_offset = const offset_of!(VCpu, t6),
            );
        }

        unreachable!();
    }
}
```

`sscratch`에 `VCpu` 구조체 포인터를 저장합니다. Trap handler에서 이 포인터로 게스트 상태를 저장/복원합니다.

#### main() 수정

`src/main.rs`에서 `VCpu`를 사용하도록 수정합니다.

```rust
mod vcpu;

use crate::{
    allocator::alloc_pages,
    guest_page_table::{GuestPageTable, PTE_R, PTE_W, PTE_X},
    vcpu::VCpu,
};

fn main() -> ! {
    // ... 초기화 및 게스트 코드 로드 ...
    
    let mut vcpu = VCpu::new(&table, guest_entry);
    vcpu.run();
}
```

#### Trap Handler 수정

`src/trap.rs`를 `#[naked]` 함수로 수정해서 게스트 상태를 저장하고 복원하도록 했습니다:

```rust
use core::{arch::naked_asm, mem::offset_of};
use crate::vcpu::VCpu;

#[unsafe(link_section = ".text.stvec")]
#[naked]
pub unsafe extern "C" fn trap_handler() {
    naked_asm!(
        // sscratch와 a0 교환 (VCpu 포인터 가져오기)
        "csrrw a0, sscratch, a0",
        
        // 모든 레지스터 저장
        "sd ra, {ra_offset}(a0)",
        "sd sp, {sp_offset}(a0)",
        // ... (생략) ...
        "sd t6, {t6_offset}(a0)",
        
        // CSR 저장
        "csrr t0, sepc",
        "sd t0, {sepc_offset}(a0)",
        
        // sscratch 복구 (원래 a0 값 저장)
        "csrr t0, sscratch",
        "sd t0, {a0_offset}(a0)",
        
        // 하이퍼바이저 스택으로 전환
        "ld sp, {host_sp_offset}(a0)",
        
        // handle_trap 호출
        "call {handle_trap}",
        
        ra_offset = const offset_of!(VCpu, ra),
        sp_offset = const offset_of!(VCpu, sp),
        // ... (생략) ...
        sepc_offset = const offset_of!(VCpu, sepc),
        a0_offset = const offset_of!(VCpu, a0),
        host_sp_offset = const offset_of!(VCpu, host_sp),
        handle_trap = sym handle_trap,
    );
}

fn handle_trap(vcpu: &mut VCpu) -> ! {
    let scause = read_csr!("scause");
    
    if scause == 10 {
        // VS-mode ecall
        println!("SBI call: eid={:#x}, fid={:#x}, a0={:#x} ('{}')",
            vcpu.a7, vcpu.a6, vcpu.a0, vcpu.a0 as u8 as char);
        
        // sepc += 4 (ecall 명령 건너뛰기)
        vcpu.sepc += 4;
        
        vcpu.run();
    }
    
    panic!("trap: scause={:#x} at {:#x}", scause, vcpu.sepc);
}
```

중요한 부분은:
- `csrrw a0, sscratch, a0`로 VCpu 포인터와 a0 교환
- 모든 레지스터를 VCpu 구조체에 저장
- `host_sp`로 스택 전환
- `handle_trap()` 호출
- `sepc += 4`로 ecall 명령 건너뛰기
- `vcpu.run()`으로 게스트 복귀

#### 실행 결과

실행해보니:

```
Booting hypervisor...
map: 00100000 -> 80306000
SBI call: eid=0x1, fid=0x0, a0=0x41 ('A')
SBI call: eid=0x1, fid=0x0, a0=0x42 ('B')
SBI call: eid=0x1, fid=0x0, a0=0x43 ('C')
```

성공했습니다. 게스트에서 하이퍼바이저로 문자를 전달해서 출력되는 모습입니다.

### 튜토리얼 8-10챕터 확인

Chapter 7까지 끝내고, 남은 챕터들을 봤습니다.

#### 나머지 챕터

챕터 8~10까지는 튜토리얼에는 미완성이라고 되어있지만, 튜토리얼 저장소를 보니까 Chapter 8-10 구현이 어느정도 되어있는 것 같았습니다.

그래서 코드를 복사해서 실행해봤는데, Linux 부팅 시도하니까 초기 커널 메시지 몇 개 나오다가 guest page fault 나면서 멈췄습니다.

Chapter 11(MMIO), 12(Interrupt), 13(Outro)는 아예 작업이 되지 않은 것 같습니다.

따라서 우선은 여기까지 하기로 했습니다.

### x86 KVM으로 전환 준비

RISC-V 튜토리얼로 하이퍼바이저의 핵심 구조를 어느 정도 이해했으니, 이제 x86 KVM으로 전환할 계획을 세웠습니다.

RISC-V에서 배운 걸 정리하면:

- OpenSBI가 하이퍼바이저 `boot()` 호출 → 스택/BSS 초기화 → `main()`
- Heap 위에 page allocator (4KB 단위)
- Guest memory는 page allocator에서 할당 → guest page table에 매핑
- 2-stage translation: guest PA → host PA
- Guest 실행: CSR 설정 (`hgatp`, `hstatus`, `sepc`) → `sret`
- VM Exit: trap handler → 레지스터 저장 → Rust 코드 처리 → 레지스터 복원 → `sret`

이걸 x86 KVM에 대응하는 코드로 바꾸면 될 것 같습니다.


### 다음주 todo

- KVM: 간단한 게스트 코드 실행

### 참고 자료

- [RISC-V Hypervisor Tutorial](https://1000hv.seiya.me/en/)
- [Tutorial GitHub Repository](https://github.com/nuta/tutorial-risc-v)

구현한 코드는 `hypervisor/` 폴더에 있습니다.

