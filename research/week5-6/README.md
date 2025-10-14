# 5-6주차 연구내용

목표: vCPU 생성 및 제어, 메모리 입출력, VM 종료 코드 작성

저번주 todo:

- 전체 점검

## 연구 내용

추석 연휴로 인해 5주차 상담이 없어, 5-6주차 연구를 통합하여 진행합니다.

4주차까지 Bochs를 이용해 xv6를 부팅하려는 시도를 했으나, 여러 문제로 인해 큰 진전이 없었습니다. 교수님과의 상담을 통해, Bochs 접근 방식을 포기하고 새로운 전략을 시도하기로 했습니다.

### 방향 전환: RISC-V 튜토리얼로 시작하기

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



### 튜토리얼 따라가기 (Chapter 1-4)

#### Chapter 1: Getting Started

Chapter 1은 개발 환경 구성입니다.

먼저 Rust 툴체인을 설치해야 합니다. [Rustup](https://rustup.rs/)을 사용해서 설치할 수 있습니다:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

설치가 완료되면 Rust 컴파일러가 제대로 설치되었는지 확인합니다:

```bash
rustc --version
```

QEMU는 이미 설치되어 있었기 때문에 넘어갔습니다.

#### Chapter 2: Boot

Chapter 2부터 본격적으로 코드를 작성하기 시작합니다.

##### OpenSBI

QEMU `virt` 머신은 하이퍼바이저를 직접 부팅하지 않고, OpenSBI라는 펌웨어(BIOS/UEFI와 유사)를 먼저 실행합니다. OpenSBI는 펌웨어로서 하드웨어를 초기화하고 하이퍼바이저를 메모리에 로드한 후 제어를 넘깁니다.

##### Rust 프로젝트 생성

새 Rust 프로젝트를 만듭니다:

```bash
cargo init --bin hypervisor
```

`rust-toolchain.toml` 파일을 만들어서 필요한 툴체인을 지정합니다. 이 파일이 있으면 Cargo가 자동으로 올바른 툴체인을 설치합니다:

```toml
[toolchain]
channel = "stable"
targets = ["riscv64gc-unknown-none-elf"]
```

##### 최소 부팅 코드

`src/main.rs`를 작성합니다:

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

unsafe extern "C" {
    static mut __bss: u8;
    static mut __bss_end: u8;
}

fn main() -> ! {
    unsafe {
        let bss_start = &raw mut __bss;
        let bss_size = (&raw mut __bss_end as usize) - (&raw mut __bss as usize);
        core::ptr::write_bytes(bss_start, 0, bss_size);
    }

    loop {}
}
```

`boot()` 함수는 스택 포인터를 설정하고 `main()`으로 점프합니다. `main()`은 BSS 섹션을 0으로 초기화한 후 무한 루프를 실행합니다.

##### 링커 스크립트

`hypervisor.ld`를 만들어서 메모리 레이아웃을 정의합니다:

```ld
ENTRY(boot)

SECTIONS {
    . = 0x80200000;

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
    . += 1024 * 1024; /* 1MB */
    __stack_top = .;

    /DISCARD/ : {
        *(.eh_frame);
    }
}
```

주요 사항:
- 엔트리 포인트: `boot` 함수
- 베이스 주소: `0x80200000` (OpenSBI가 커널을 로드하는 주소)
- `.text.boot` 섹션이 맨 앞에 배치
- 커널 스택 크기: 1MB

##### 빌드 및 실행

`run.sh` 스크립트를 작성합니다:

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

실행 가능하도록 권한을 줍니다:

```bash
chmod +x run.sh
```

빌드를 시도하면 에러가 발생합니다:

```
error: `#[panic_handler]` function required, but not found
```

##### Panic Handler

`#![no_std]` 환경에서는 panic handler를 직접 구현해야 합니다. `src/main.rs`에 추가합니다:

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

이제 다시 빌드하고 실행하면 성공합니다:

```bash
./run.sh
```

OpenSBI 부팅 메시지가 출력된 후 멈춥니다. 하이퍼바이저는 아무것도 출력하지 않지만, `main()` 함수의 무한 루프가 실행되고 있습니다.

`llvm-objdump`로 확인하면 `boot` 함수가 `0x80200000`에 위치하고, `main()` 함수로 점프하는 것을 볼 수 있습니다:

```
0000000080200000 <boot>:
80200000: auipc sp, 0x100
80200004: addi  sp, sp, 0x550
80200008: j     0x8020000e <hypervisor::main::...>
```

#### Chapter 3: Hello World

Chapter 3에서는 콘솔 출력을 구현합니다.

##### SBI(Supervisor Binary Interface)

튜토리얼을 보니, OpenSBI 펌웨어가 제공하는 SBI(Supervisor Binary Interface)라는 서비스를 사용할 수 있다고 합니다. SBI는 펌웨어가 OS에게 제공하는 API로, 콘솔 출력, 타이머 설정, 리부트/셧다운 같은 기능을 제공합니다.

[SBI 스펙](https://github.com/riscv-non-isa/riscv-sbi-doc/releases)을 보면 다양한 기능들이 정의되어 있는데, 여기서는 serial port로 문자를 출력하는 기능만 사용할 것입니다.

##### SBI Putchar 구현

`src/print.rs`를 만들어서 SBI putchar 함수를 구현했습니다:

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

`ecall` 명령을 inline assembly로 호출합니다. 레지스터 용도는:
- `a6`, `a7`: SBI 기능 ID 지정 (a7=1은 Console Putchar extension)
- `a0`: 출력할 문자 (동시에 에러 코드 반환용)
- `a1`: 반환값 (여기서는 사용 안 함)

##### println! 매크로

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

`no_std` 환경에서는 표준 `println!` 매크로를 쓸 수 없지만, 직접 구현하면 됩니다. `writeln!` 매크로가 formatting을 처리해주고, `Printer`가 각 문자를 SBI로 출력합니다.

##### Panic Handler 개선

기존 panic handler는 아무것도 출력하지 않아서 디버깅이 힘들었습니다. `println!` 매크로를 사용해서 panic 정보를 출력하도록 수정했습니다:

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

##### Trap Handler

CPU가 exception이나 interrupt를 만나면 trap handler가 호출됩니다. Trap handler를 구현해야 invalid memory access 같은 에러를 디버깅할 수 있습니다.

`src/trap.rs`를 만들었습니다:

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
    let scause_str = match scause {
        0 => "instruction address misaligned",
        1 => "instruction access fault",
        2 => "illegal instruction",
        3 => "breakpoint",
        4 => "load address misaligned",
        5 => "load access fault",
        6 => "store/AMO address misaligned",
        7 => "store/AMO access fault",
        8 => "environment call from U/VU-mode",
        9 => "environment call from HS-mode",
        10 => "environment call from VS-mode",
        11 => "environment call from M-mode",
        12 => "instruction page fault",
        13 => "load page fault",
        15 => "store/AMO page fault",
        20 => "instruction guest-page fault",
        21 => "load guest-page fault",
        22 => "virtual instruction",
        23 => "store/AMO guest-page fault",
        0x8000_0000_0000_0000 => "user software interrupt",
        0x8000_0000_0000_0001 => "supervisor software interrupt",
        0x8000_0000_0000_0002 => "hypervisor software interrupt",
        0x8000_0000_0000_0003 => "machine software interrupt",
        0x8000_0000_0000_0004 => "user timer interrupt",
        0x8000_0000_0000_0005 => "supervisor timer interrupt",
        0x8000_0000_0000_0006 => "hypervisor timer interrupt",
        0x8000_0000_0000_0007 => "machine timer interrupt",
        0x8000_0000_0000_0008 => "user external interrupt",
        0x8000_0000_0000_0009 => "supervisor external interrupt",
        0x8000_0000_0000_000a => "hypervisor external interrupt",
        0x8000_0000_0000_000b => "machine external interrupt",
        _ => panic!("unknown scause: {:#x}", scause),
    };

    panic!("trap handler: {} at {:#x} (stval={:#x})", scause_str, sepc, stval);
}
```

주요 CSR:
- `scause`: Trap 발생 원인
- `sepc`: Trap 발생 시점의 PC
- `stval`: Trap 관련 추가 정보 (예: page fault의 가상 주소)

`read_csr!` 매크로는 CSR 값을 읽는 헬퍼입니다. `asm!`을 직접 쓸 수도 있지만 매크로로 만들면 편합니다.

##### 링커 스크립트 수정

Trap handler가 align된 주소에 위치해야 합니다. `stvec` CSR의 하위 비트가 trap handler 모드 설정에 쓰이기 때문입니다.

`hypervisor.ld`를 수정했습니다:

```ld
    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
        . = ALIGN(8);
        *(.text.stvec);
    }
```

##### main() 수정

`src/main.rs`를 수정해서 trap handler를 등록하고 부팅 메시지를 출력하도록 했습니다:

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

`stvec` CSR에 trap handler 주소를 설정합니다.

##### 실행 결과

실행해보니:

```bash
$ ./run.sh
...
Boot HART MEDELEG         : 0x0000000000f0b509

Booting hypervisor...
```

드디어 출력이 됐습니다!

##### Trap Handler 테스트

Trap handler가 제대로 동작하는지 테스트하기 위해, `unimp` 명령(illegal instruction)을 일부러 실행해봤습니다:

```rust
fn main() -> ! {
    unsafe {
        let bss_start = &raw mut __bss;
        let bss_size = (&raw mut __bss_end as usize) - (&raw mut __bss as usize);
        core::ptr::write_bytes(bss_start, 0, bss_size);

        asm!("csrw stvec, {}", in(reg) trap::trap_handler as usize);
        asm!("unimp");
    }

    println!("\nBooting hypervisor...");
    loop {}
}
```

실행 결과:

```
Booting hypervisor...
panic: panicked at src/trap.rs:52:5:
trap handler: illegal instruction at 0x80200536 (stval=0x0)
```

Trap handler가 정상적으로 호출되고, panic 메시지가 출력됩니다. `llvm-addr2line -e hypervisor.elf 0x80200536`으로 정확한 소스 라인도 찾을 수 있습니다.

테스트를 마치고 `unimp` 라인은 다시 제거했습니다.

#### Chapter 4: Memory Allocation

Chapter 4에서는 동적 메모리 할당을 구현합니다.

##### no_std 환경과 메모리 할당

`no_std` 환경에서는 `String`, `Box`, `Vec` 같은 일상적으로 쓰는 자료구조들을 사용할 수 없습니다. 이들은 모두 동적 메모리 할당이 필요한데, `no_std`에서는 이게 기본으로 제공되지 않기 때문입니다.

하지만 메모리 할당자를 직접 구현하면 이런 자료구조들을 쓸 수 있게 됩니다.

##### Bump Allocator 알고리즘

튜토리얼에서는 가장 단순한 형태의 할당 알고리즘인 bump allocator를 구현합니다.

Bump allocator는 케이크를 자르는 것으로 비유할 수 있습니다. 케이크가 있고, 위에서부터 아래로 순차적으로 잘라나가는 겁니다. `next` 포인터(칼의 위치)와 `end` 포인터(케이크의 끝)가 있고, 메모리를 할당할 때마다 `next` 포인터를 아래로 내려가면서 메모리를 주는 식입니다.

단점은 메모리를 해제할 수 없다는 점입니다. 하지만 구현이 매우 단순하고 빠르기 때문에, 실제 프로덕션에서도 쓰이는 경우가 있습니다.

##### spin 크레이트 추가

구현하기 전에 먼저 의존성을 추가해야 합니다. Rust는 `no_std` 환경을 잘 지원하도록 설계되어 있어서, 서드파티 라이브러리(crate)를 한 줄 명령어로 추가할 수 있습니다:

```bash
cargo add spin
```

`spin` 크레이트는 `std::sync::Mutex`를 spinlock 방식으로 구현한 것입니다. 이를 사용하면 allocator를 thread-safe하게 만들 수 있습니다.

##### BumpAllocator 구조체

`src/allocator.rs`를 만들고 다음 코드를 작성했습니다:

```rust
use core::alloc::{GlobalAlloc, Layout};

struct Mutable {
    next: usize,
    end: usize,
}

pub struct BumpAllocator {
    mutable: spin::Mutex<Option<Mutable>>,
}

impl BumpAllocator {
    const fn new() -> Self {
        Self {
            mutable: spin::Mutex::new(None),
        }
    }

    pub fn init(&self, start: *mut u8, end: *mut u8) {
        self.mutable.lock().replace(Mutable {
            next: start as usize,
            end: end as usize,
        });
    }
}
```

`Mutable` 구조체는 실제 할당 상태를 담고 있습니다:
- `next`: 다음 할당이 시작될 주소
- `end`: 힙 영역의 끝 주소

이를 `spin::Mutex<Option<Mutable>>`로 감싸서 thread-safe하게 만듭니다. `Option`을 쓰는 이유는 초기화 전에는 할당할 수 없기 때문입니다.

##### GlobalAlloc 트레이트 구현

Rust의 전역 할당자가 되려면 `GlobalAlloc` 트레이트를 구현해야 합니다:

```rust
unsafe impl GlobalAlloc for BumpAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let mut mutable_lock = self.mutable.lock();
        let mutable = mutable_lock.as_mut().expect("allocator not initialized");

        let addr = mutable.next.next_multiple_of(layout.align());
        assert!(addr.saturating_add(layout.size()) <= mutable.end, "out of memory");

        mutable.next = addr + layout.size();
        addr as *mut u8
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {}
}

#[global_allocator]
pub static GLOBAL_ALLOCATOR: BumpAllocator = BumpAllocator::new();
```

주요 포인트:
- `alloc()`: 메모리를 할당합니다. `layout.align()`에 맞춰 주소를 정렬하고, 메모리가 부족하면 panic합니다.
- `dealloc()`: Bump allocator는 메모리 해제를 지원하지 않으므로 빈 함수입니다.
- `#[global_allocator]`: 이 속성을 붙이면 Rust가 이 할당자를 기본 할당자로 사용합니다.

`next_multiple_of()`는 alignment를 맞추는 함수입니다. 예를 들어 8-byte alignment가 필요하면 주소를 8의 배수로 올립니다. `saturating_add()`는 오버플로우를 방지합니다.

##### 링커 스크립트에 힙 영역 추가

할당자가 사용할 메모리 영역을 링커 스크립트에 정의해야 합니다. `hypervisor.ld`를 수정했습니다:

```ld
    . = ALIGN(16);
    . += 1024 * 1024; /* 1MB */
    __stack_top = .;

    __heap = .;
    . += 100 * 1024 * 1024; /* 100MB */
    __heap_end = .;

    /DISCARD/ : {
        *(.eh_frame);
    }
```

스택 다음에 100MB 크기의 힙 영역을 추가했습니다. `__heap`과 `__heap_end` 심볼은 코드에서 참조할 수 있습니다.

##### 할당자 초기화

`src/main.rs`를 수정해서 할당자를 초기화합니다:

```rust
extern crate alloc;

mod allocator;

unsafe extern "C" {
    static mut __bss: u8;
    static mut __bss_end: u8;
    static mut __heap: u8;
    static mut __heap_end: u8;
}

fn main() -> ! {
    unsafe {
        let bss_start = &raw mut __bss;
        let bss_size = (&raw mut __bss_end as usize) - (&raw mut __bss as usize);
        core::ptr::write_bytes(bss_start, 0, bss_size);

        asm!("csrw stvec, {}", in(reg) trap::trap_handler as usize);
    }

    println!("\nBooting hypervisor...");

    allocator::GLOBAL_ALLOCATOR.init(&raw mut __heap, &raw mut __heap_end);
```

`extern crate alloc;`를 추가하면 `alloc` 크레이트를 사용할 수 있게 됩니다. 이 크레이트는 `Cargo.toml`에 추가할 필요가 없습니다. 표준 라이브러리에 번들로 포함되어 있기 때문입니다.

`__heap`과 `__heap_end` 심볼을 `unsafe extern "C"`로 선언해서 링커 스크립트의 심볼을 참조할 수 있게 합니다.

##### Vec 테스트

할당자가 제대로 작동하는지 테스트하기 위해 `Vec`을 사용해봤습니다:

```rust
    allocator::GLOBAL_ALLOCATOR.init(&raw mut __heap, &raw mut __heap_end);

    let mut v = alloc::vec::Vec::new();
    v.push('a');
    v.push('b');
    v.push('c');
    println!("v = {:?}", v);

    loop {}
}
```

`alloc::vec::Vec`을 명시적으로 사용해야 합니다. `std::vec::Vec`은 `no_std` 환경에서는 사용할 수 없습니다.

실행해보니:

```bash
$ ./run.sh
...
Booting hypervisor...
v = ['a', 'b', 'c']
```

성공입니다! 동적 메모리 할당이 정상적으로 작동합니다.

##### Page Allocator

튜토리얼 마지막 부분에서는 페이지 단위(4KB) 할당 함수도 추가합니다:

```rust
pub fn alloc_pages(len: usize) -> *mut u8 {
    let layout = Layout::from_size_align(len, 0x1000).unwrap();
    unsafe { GLOBAL_ALLOCATOR.alloc_zeroed(layout) as *mut u8 }
}
```

이 함수는 나중에 guest page table을 만들 때 사용됩니다. `0x1000`(4096 bytes)은 RISC-V의 기본 페이지 크기입니다.

`alloc_zeroed()`는 할당한 메모리를 0으로 초기화합니다. Page table entry들은 초기값이 0이어야 하므로 유용합니다.

RISC-V와 다른 현대 CPU들은 더 큰 페이지 크기(2MB, 1GB)도 지원하지만, 이 프로젝트에서는 4KB만 사용합니다.

### Chapter 5-6: Guest Mode 구현

#### Chapter 5: Guest Mode

Chapter 5부터 본격적으로 게스트 모드를 다룹니다. 드디어 하이퍼바이저의 핵심 기능인 가상 CPU 실행을 해볼 차례입니다.

##### RISC-V 가상화 모드 이해하기

튜토리얼을 읽어보니, RISC-V의 하이퍼바이저 확장(H-extension)은 세 가지 가상화 모드를 제공한다고 합니다:

- **HS-mode (Hypervisor-extended Supervisor mode)**: 하이퍼바이저가 실행되는 호스트 커널 모드입니다. 지금까지 우리 코드가 실행되던 모드입니다.
- **VS-mode (Virtual Supervisor mode)**: 게스트 커널이 실행되는 모드입니다. 게스트 OS는 자신이 supervisor mode에서 실행되고 있다고 착각하지만, 실제로는 하이퍼바이저의 통제를 받습니다.
- **VU-mode (Virtual User mode)**: 게스트 유저 프로세스가 실행되는 모드입니다.

이 구조가 흥미로웠던 게, x86의 VMX(Virtual Machine Extensions)처럼 완전히 별개의 root/non-root 구조가 아니라, 기존 privilege level을 확장한 형태라는 점입니다. RISC-V답게 단순하고 일관성 있는 디자인인 것 같습니다.

##### QEMU에서 H-extension 활성화

QEMU에서 H-extension을 활성화하려면 CPU 옵션을 수정해야 한다고 해서 `run.sh`를 수정했습니다:

```bash
qemu-system-riscv64 \
    -machine virt \
    -cpu rv64,h=true \
    -smp 1 \
    -m 128M \
    -nographic \
    -serial mon:stdio \
    -bios default \
    -kernel hypervisor.elf
```

기존 `-cpu rv64`에서 `-cpu rv64,h=true`로 변경했습니다. 이 옵션이 없으면 하이퍼바이저 관련 CSR(hstatus, hgatp 등)에 접근할 때 illegal instruction 에러가 발생한다고 합니다.

##### Vec 테스트 코드 제거

Chapter 4에서 메모리 할당자 테스트용으로 작성했던 Vec 코드는 이제 필요 없으니 제거했습니다:

```rust
// 이 부분 삭제
let mut v = alloc::vec::Vec::new();
v.push('a');
v.push('b');
v.push('c');
println!("v = {:?}", v);
```

##### 게스트 모드 진입 코드 작성

튜토리얼에 나온 대로 게스트 모드 진입 코드를 작성했습니다. 우선 필요한 CSR을 설정해야 합니다:

```rust
let mut hstatus: u64 = 0;
hstatus |= 2 << 32;  // VSXL: XLEN for VS-mode (64-bit)
hstatus |= 1 << 7;   // SPV: Supervisor Previous Virtualization mode

let sepc: u64 = 0x1234abcd;

unsafe {
    asm!(
        "csrw hstatus, {hstatus}",
        "csrw sepc, {sepc}",
        "sret",
        hstatus = in(reg) hstatus,
        sepc = in(reg) sepc,
    );
}
```

각 CSR과 비트의 의미를 자세히 살펴봤습니다:

- **hstatus.VSXL (bits 33:32)**: VS-mode의 XLEN을 설정합니다. `2`는 64-bit를 의미합니다. 게스트가 32-bit라면 `1`로 설정하면 됩니다.
- **hstatus.SPV (bit 7)**: Supervisor Previous Virtualization mode입니다. `sret` 명령이 실행될 때 이 비트가 1이면 VS-mode로, 0이면 일반 S-mode로 전환됩니다.
- **sepc**: `sret` 명령이 점프할 주소입니다. 게스트 코드의 진입점이 됩니다.

이 코드를 보면서 정말 신기했던 점이, `sret` 명령 하나로 게스트 모드 진입이 된다는 것입니다. x86의 `vmlaunch`/`vmresume` 같은 전용 명령이 필요 없습니다. RISC-V는 일관성 있게 `sret` 하나로 유저 모드 복귀, 게스트 모드 진입을 모두 처리합니다. 설계 철학이 느껴지는 부분입니다.

##### 첫 번째 실행 시도와 문제 발생

코드를 작성하고 실행해봤습니다:

```bash
$ ./run.sh
```

그런데 예상치 못한 에러가 발생했습니다:

```
Booting hypervisor...
panic: panicked at src/trap.rs:52:5:
trap handler: illegal instruction at 0x80200542 (stval=0x0)
```

Illegal instruction이 발생했습니다. `llvm-addr2line`으로 확인해보니 `sret` 명령 위치였습니다. RISC-V 스펙을 찾아보니, `sret`로 VS-mode로 진입하려면 `sstatus.SPP` 비트가 1이어야 한다고 나와 있었습니다.

튜토리얼을 다시 읽어봤는데, 이 부분이 빠져있더군요. 아마 OpenSBI가 SPP를 설정해줄 거라고 가정한 것 같은데, 실제로는 그렇지 않았습니다.

##### sstatus.SPP 설정 추가

문제를 해결하기 위해 `sstatus` CSR을 읽어서 SPP 비트를 설정하는 코드를 추가했습니다:

```rust
let mut hstatus: u64 = 0;
hstatus |= 2 << 32;  // VSXL
hstatus |= 1 << 7;   // SPV

let mut sstatus: u64;
unsafe {
    asm!("csrr {}, sstatus", out(reg) sstatus);
}
sstatus |= 1 << 8;  // SPP: Supervisor Previous Privilege mode (1 = S-mode)

let sepc: u64 = 0x1234abcd;

unsafe {
    asm!(
        "csrw hstatus, {hstatus}",
        "csrw sstatus, {sstatus}",
        "csrw sepc, {sepc}",
        "sret",
        hstatus = in(reg) hstatus,
        sstatus = in(reg) sstatus,
        sepc = in(reg) sepc,
    );
}
```

**sstatus.SPP (bit 8)**: `sret`가 반환할 privilege level을 지정합니다. 0이면 U-mode, 1이면 S-mode로 전환됩니다. VS-mode는 가상화된 S-mode이므로 1로 설정해야 합니다.

이 부분이 튜토리얼에 없어서 처음엔 당황했는데, RISC-V 스펙을 직접 읽으면서 이해할 수 있었습니다. 튜토리얼만 맹목적으로 따라하면 안 되고, 기본 원리를 이해해야 문제를 해결할 수 있다는 걸 배웠습니다.

##### 두 번째 실행과 예상된 Fault

코드를 수정하고 다시 실행해봤습니다:

```bash
$ ./run.sh
```

이번엔 다른 에러가 나왔습니다:

```
Booting hypervisor...
panic: panicked at src/trap.rs:52:5:
trap handler: instruction access fault at 0x1234abcc (stval=0x1234abcc)
```

`scause=1`은 instruction access fault입니다. `sepc`는 `0x1234abcc`인데, 우리가 설정한 `0x1234abcd`와 1바이트 차이가 납니다. 이건 RISC-V가 명령어 주소를 4바이트 경계로 정렬하기 때문입니다.

튜토리얼을 보니, 여기서 instruction guest-page fault (scause=20)가 발생해야 한다고 나와있었습니다. 그런데 저는 instruction access fault (scause=1)가 나왔습니다. 왜 다를까요?

##### Fault Type 차이 분석

이 차이가 궁금해서 조사를 해봤습니다. 우선 정말 VS-mode에 진입했는지 확인하기 위해 trap handler를 수정했습니다:

```rust
pub fn trap_handler() -> ! {
    let scause = read_csr!("scause");
    let sepc = read_csr!("sepc");
    let stval = read_csr!("stval");
    let hstatus = read_csr!("hstatus");
    
    println!("hstatus.SPV = {}", (hstatus >> 7) & 1);
    
    // ... 나머지 코드
}
```

실행해보니:

```
hstatus.SPV = 1
trap handler: instruction access fault at 0x1234abcc (stval=0x1234abcc)
```

`hstatus.SPV=1`이 나왔습니다! 이는 우리가 VS-mode에서 실행 중이라는 뜻입니다. 게스트 모드 진입에는 성공한 것입니다.

그럼 왜 fault type이 다를까요? RISC-V 스펙을 찾아보니:

- **Instruction access fault (scause=1)**: 물리 메모리 접근 자체가 실패한 경우입니다. 해당 주소에 메모리가 매핑되지 않았거나, 권한이 없을 때 발생합니다.
- **Instruction guest-page fault (scause=20)**: Guest page table(2단계 변환)에서 변환이 실패한 경우입니다. `hgatp`가 설정되어 있고, 해당 페이지가 guest page table에 없을 때 발생합니다.

차이가 명확해졌습니다. 우리는 아직 `hgatp`를 설정하지 않았습니다. `hgatp`는 guest physical address를 host physical address로 변환하는 page table의 베이스 주소를 담고 있는 CSR입니다. 이게 설정되지 않으면 RISC-V는 guest physical address를 그대로 host physical address로 사용하려고 시도합니다. `0x1234abcd`는 매핑되지 않은 주소이므로 일반 access fault가 발생하는 것입니다.

튜토리얼에서 scause=20이 나온 건 아마 다른 QEMU 버전을 사용했거나, 코드가 일부 생략되었을 가능성이 있습니다. 제가 사용하는 QEMU 10.1.0에서는 `hgatp`가 설정되지 않으면 scause=1이 나오는 게 맞는 것 같습니다.

##### 결론: 게스트 모드 진입 성공

중요한 것은, `hstatus.SPV=1`을 확인했으므로 우리는 VS-mode 진입에 성공했다는 것입니다. Fault type의 차이는 중요하지 않습니다. Chapter 6에서 guest page table을 설정하고 실제 게스트 코드를 매핑하면 정상적으로 실행될 것입니다.

이번 챕터에서 배운 핵심 내용:

1. RISC-V는 `sret` 명령 하나로 게스트 모드 진입을 처리합니다. x86처럼 `vmlaunch` 같은 전용 명령이 필요 없습니다.
2. `hstatus.SPV=1`과 `sstatus.SPP=1`을 설정하면 `sret`가 VS-mode로 전환합니다.
3. `sepc`에 게스트 진입점 주소를 설정하면 됩니다.
4. 튜토리얼에 없는 `sstatus.SPP` 설정이 필요했습니다. 스펙을 읽고 이해하는 것이 중요합니다.
5. `hgatp`가 설정되지 않으면 guest page fault가 아닌 일반 access fault가 발생할 수 있습니다.

다음 챕터에서는 guest page table을 구현해서 실제 게스트 코드를 실행해볼 것입니다.

#### Chapter 6: Guest Page Table

Chapter 5에서 VS-mode 진입에는 성공했지만, 임의의 주소(`0x1234abcd`)로 점프했기 때문에 바로 trap이 발생했습니다. 이번 챕터에서는 실제로 게스트 코드를 메모리에 올리고, 2단계 주소 변환(two-stage address translation)을 통해 게스트가 실행될 수 있도록 만들어야 합니다.

##### 2단계 주소 변환의 필요성

튜토리얼을 읽어보니 하드웨어 가상화가 활성화된 환경에서는 4가지 주소 공간이 존재한다고 합니다:

- **Guest-virtual address**: 게스트 입장에서의 가상 주소
- **Guest-physical address**: 게스트가 생각하는 "물리 주소"
- **Host-virtual address**: 호스트(하이퍼바이저) 입장에서의 가상 주소  
- **Host-physical address**: 실제 물리 메모리 주소

호스트 모드에서는 일반적인 페이지 테이블을 통해 host-virtual → host-physical 변환만 일어납니다. 하지만 게스트 모드에서는 2단계로 변환이 일어납니다:

- **Stage 1**: Guest-virtual → Guest-physical (`satp` 레지스터의 페이지 테이블 사용)
- **Stage 2**: Guest-physical → Host-physical (`hgatp` 레지스터의 guest page table 사용)

이렇게 2단계로 나누는 이유는 게스트를 격리하기 위해서입니다. 각 게스트는 자신이 물리 주소 `0x0`부터 독점적으로 사용한다고 착각하지만, 실제로는 하이퍼바이저가 `hgatp`를 통해 각 게스트를 다른 호스트 물리 메모리 영역으로 매핑해줍니다. 여러 VM이 동시에 실행될 수 있는 핵심 메커니즘입니다.

##### Page Allocator 함수 작성

Guest page table을 만들기 전에 먼저 페이지 단위로 메모리를 할당하는 함수가 필요합니다. RISC-V를 포함한 대부분의 현대 CPU는 4KiB(4096바이트) 페이지를 기본으로 사용합니다.

튜토리얼에 나온 대로 `allocator.rs`에 함수를 추가했습니다:

```rust
pub fn alloc_pages(len: usize) -> *mut u8 {
    debug_assert!(len % 4096 == 0, "len must be a multiple of 4096");
    let layout = Layout::from_size_align(len, 4096).unwrap();
    unsafe { GLOBAL_ALLOCATOR.alloc_zeroed(layout) as *mut u8 }
}
```

`alloc_zeroed`를 사용하면 할당된 메모리가 모두 0으로 초기화됩니다. 페이지 테이블 구조체를 할당할 때 유용합니다. `debug_assert!`로 길이가 4096의 배수인지 검증하는 것도 추가했습니다.

##### Guest Page Table 구조 설계

Guest page table은 Sv48x4 방식을 사용합니다. "48"은 48비트 주소 공간을 의미하고, "x4"는 Stage 2 변환임을 나타냅니다. 4-level 페이지 테이블 구조입니다.

각 레벨은 512개의 entry를 가지고, 각 entry는 8바이트이므로 `512 * 8 = 4096`바이트, 즉 정확히 한 페이지에 딱 맞습니다.

새로운 파일 `src/guest_page_table.rs`를 만들었습니다. 먼저 Page Table Entry(PTE)의 플래그 비트들을 정의합니다:

```rust
pub const PTE_R: u64 = 1 << 1;  // Readable
pub const PTE_W: u64 = 1 << 2;  // Writable
pub const PTE_X: u64 = 1 << 3;  // Executable
const PTE_V: u64 = 1 << 0;      // Valid
const PTE_U: u64 = 1 << 4;      // User
const PPN_SHIFT: usize = 12;
const PTE_PPN_SHIFT: usize = 10;
```

PTE의 구조는 이렇습니다:
- 비트 0: V (Valid) - 이 엔트리가 유효한지
- 비트 1-3: R, W, X - 읽기/쓰기/실행 권한
- 비트 4: U (User) - 유저 모드에서 접근 가능한지
- 비트 10-53: PPN (Physical Page Number) - 다음 레벨 테이블이나 최종 물리 페이지의 주소

`Entry` 구조체를 만들어서 PTE를 표현합니다:

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
struct Entry(u64);

impl Entry {
    pub fn new(paddr: u64, flags: u64) -> Self {
        let ppn = (paddr as u64) >> PPN_SHIFT;
        Self(ppn << PTE_PPN_SHIFT | flags)
    }

    pub fn is_valid(&self) -> bool {
        self.0 & PTE_V != 0
    }

    pub fn paddr(&self) -> u64 {
        (self.0 >> PTE_PPN_SHIFT) << PPN_SHIFT
    }
}
```

`new`로 물리 주소와 플래그를 받아서 PTE를 만들고, `is_valid`로 유효성을 확인하고, `paddr`로 가리키는 물리 주소를 추출합니다.

다음으로 한 레벨의 페이지 테이블을 표현하는 `Table` 구조체입니다:

```rust
#[repr(transparent)]
struct Table([Entry; 512]);

impl Table {
    pub fn alloc() -> *mut Table {
        crate::allocator::alloc_pages(size_of::<Table>()) as *mut Table
    }

    pub fn entry_by_addr(&mut self, guest_paddr: u64, level: usize) -> &mut Entry {
        let index = (guest_paddr >> (12 + 9 * level)) & 0x1ff;
        &mut self.0[index as usize]
    }
}
```

`alloc()`으로 4KB 페이지를 할당받아 테이블로 사용하고, `entry_by_addr()`로 특정 게스트 물리 주소와 레벨에 해당하는 엔트리를 찾습니다. `0x1ff`는 9비트 마스크입니다 (512 = 2^9).

이제 메인 구조체인 `GuestPageTable`을 만듭니다:

```rust
pub struct GuestPageTable {
    table: *mut Table,
}

impl GuestPageTable {
    pub fn new() -> Self {
        Self {
            table: Table::alloc(),
        }
    }

    pub fn hgatp(&self) -> u64 {
        (9u64 << 60) | (self.table as u64 >> PPN_SHIFT)
    }

    pub fn map(&mut self, guest_paddr: u64, host_paddr: u64, flags: u64) {
        let mut table = unsafe { &mut *self.table };
        for level in (1..=3).rev() {
            let entry = table.entry_by_addr(guest_paddr, level);
            if !entry.is_valid() {
                let new_table_ptr = Table::alloc();
                *entry = Entry::new(new_table_ptr as u64, PTE_V);
            }

            table = unsafe { &mut *(entry.paddr() as *mut Table) };
        }

        let entry = table.entry_by_addr(guest_paddr, 0);
        println!("map: {:08x} -> {:08x}", guest_paddr, host_paddr);
        assert!(!entry.is_valid(), "already mapped");
        *entry = Entry::new(host_paddr, flags | PTE_V | PTE_U);
    }
}
```

`hgatp()` 함수는 `hgatp` CSR에 넣을 값을 생성합니다. 상위 4비트(60-63)에 `9`를 넣는 것은 Sv48x4 모드를 의미합니다. 나머지 비트는 최상위 페이지 테이블의 PPN입니다.

`map()` 함수가 핵심입니다. 4-level 구조를 따라 내려가면서:
1. Level 3, 2, 1을 순회합니다 (최상위 레벨부터)
2. 각 레벨에서 해당 엔트리가 유효하지 않으면 새 테이블을 할당합니다
3. 최종 레벨(level 0)에서 실제 매핑을 설정합니다

##### 간단한 게스트 코드 작성

이제 실제로 실행할 게스트 코드가 필요합니다. 일단 간단하게 무한 루프만 도는 코드를 어셈블리로 작성했습니다. `guest.S` 파일을 만들었습니다:

```asm
.section .text
.global guest_boot
guest_boot:
    j guest_boot
```

`j guest_boot`는 RISC-V의 무조건 점프 명령입니다. 자기 자신으로 점프하므로 무한 루프가 됩니다.

##### 게스트 코드 컴파일

튜토리얼에 나온 대로 Clang과 LLVM을 사용해 컴파일합니다. Rust 컴파일러 대신 Clang을 쓰는 이유는 간단한 어셈블리 파일을 바이너리로 만드는 데는 Clang이 더 직관적이기 때문입니다.

`build.sh`라는 별도의 빌드 스크립트를 만들었습니다:

```bash
#!/bin/sh
set -ev

clang \
    -Wall -Wextra --target=riscv64-unknown-elf -ffreestanding -nostdlib \
    -Wl,-eguest_boot -Wl,-Ttext=0x100000 -Wl,-Map=guest.map \
    guest.S -o guest.elf

llvm-objcopy -O binary guest.elf guest.bin

RUSTFLAGS="-C link-arg=-Thypervisor.ld -C linker=rust-lld" \
  cargo build --bin hypervisor --target riscv64gc-unknown-none-elf

cp target/riscv64gc-unknown-none-elf/debug/hypervisor hypervisor.elf
```

링커 옵션 설명:
- `--target=riscv64-unknown-elf`: RISC-V 64비트 bare-metal 타겟
- `-ffreestanding -nostdlib`: 표준 라이브러리 없이 컴파일
- `-Wl,-eguest_boot`: 엔트리 포인트를 `guest_boot` 심볼로 설정
- `-Wl,-Ttext=0x100000`: 코드 섹션을 주소 `0x100000`에 배치
- `-Wl,-Map=guest.map`: 링커 맵 파일 생성 (디버깅용)

`llvm-objcopy -O binary`는 ELF 파일에서 실제 코드 부분만 추출해 raw 바이너리 파일로 만듭니다. 이 파일을 그대로 메모리에 복사하면 실행 가능합니다.

빌드해보니 `guest.bin`이 4바이트만 생성되었습니다. `hexdump`로 확인해보니 `6f 00 00 00`인데, 이게 바로 `j 0` (현재 위치로 점프) 명령의 인코딩입니다.

##### 메모리 로딩 및 매핑

이제 `main.rs`를 수정해서 게스트 코드를 로드하고 매핑합니다. 먼저 필요한 모듈을 추가합니다:

```rust
mod guest_page_table;

use crate::{
    allocator::alloc_pages,
    guest_page_table::{GuestPageTable, PTE_R, PTE_W, PTE_X},
};
```

`main()` 함수에서 게스트 코드를 로드합니다:

```rust
let kernel_image = include_bytes!("../guest.bin");
let guest_entry = 0x100000;
```

`include_bytes!` 매크로는 컴파일 타임에 파일을 읽어서 바이트 배열로 포함시킵니다. `guest_entry`는 게스트 코드가 실행될 주소입니다. 링커 스크립트에서 `-Ttext=0x100000`으로 지정했던 주소와 같습니다.

호스트 메모리에 게스트 코드를 복사합니다:

```rust
let kernel_len = (kernel_image.len() + 4095) & !4095;
let kernel_memory = alloc_pages(kernel_len);
unsafe {
    let dst = kernel_memory as *mut u8;
    let src = kernel_image.as_ptr();
    core::ptr::copy_nonoverlapping(src, dst, kernel_image.len());
}
```

`(len + 4095) & !4095`는 4096으로 올림하는 트릭입니다. `alloc_pages`는 4096의 배수를 요구하기 때문입니다. 4바이트 이미지도 한 페이지(4096바이트)를 할당받습니다.

Guest page table을 만들고 매핑합니다:

```rust
let mut table = GuestPageTable::new();
table.map(guest_entry, kernel_memory as u64, PTE_R | PTE_W | PTE_X);
```

`PTE_R | PTE_W | PTE_X`는 읽기/쓰기/실행 모두 가능하도록 권한을 설정합니다.

##### CSR 설정 및 게스트 진입

이제 CSR을 설정하고 게스트로 진입합니다. Chapter 5와 거의 같지만 `hgatp`가 추가되었습니다:

```rust
let mut hstatus: u64 = 0;
hstatus |= 2 << 32;  // VSXL: 64-bit
hstatus |= 1 << 7;   // SPV: VS-mode

let mut sstatus: u64;
unsafe {
    asm!("csrr {}, sstatus", out(reg) sstatus);
}
sstatus |= 1 << 8;   // SPP: Supervisor

unsafe {
    asm!(
        "csrw hstatus, {hstatus}",
        "csrw sstatus, {sstatus}",
        "csrw hgatp, {hgatp}",
        "csrw sepc, {sepc}",
        "sret",
        hstatus = in(reg) hstatus,
        sstatus = in(reg) sstatus,
        hgatp = in(reg) table.hgatp(),
        sepc = in(reg) guest_entry,
    );
}
```

중요한 건 `csrw hgatp`가 추가된 것입니다. 이제 게스트가 `0x100000`에 접근하면 RISC-V 하드웨어가 자동으로 `hgatp`의 페이지 테이블을 참조해서 실제 호스트 물리 주소로 변환합니다.

##### 실행 및 검증

빌드하고 실행했습니다:

```bash
$ ./build.sh
$ timeout 3 ./run.sh
```

출력:

```
Booting hypervisor...
map: 00100000 -> 80305000
```

Perfect! 게스트 물리 주소 `0x100000`이 호스트 물리 주소 `0x80305000`에 매핑되었습니다. 그리고 더 이상 trap이 발생하지 않고 그대로 멈춰있습니다.

이게 정말 게스트 모드에서 무한 루프를 돌고 있는 건지 확인하고 싶었습니다. QEMU monitor를 사용해서 CPU 상태를 확인해보기로 했습니다.

Python 스크립트를 작성해서 QEMU를 실행하고 monitor 명령을 보냈습니다:

```python
import subprocess
import time
import signal

proc = subprocess.Popen([
    'qemu-system-riscv64', '-machine', 'virt', '-cpu', 'rv64,h=true',
    '-bios', 'default', '-smp', '1', '-m', '128M', '-nographic',
    '-serial', 'mon:stdio', '--no-reboot', '-kernel', 'hypervisor.elf'
], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
   text=True, bufsize=0)

time.sleep(2)
proc.stdin.write('\x01c')  # Ctrl+A C (switch to monitor)
proc.stdin.flush()
time.sleep(0.1)
proc.stdin.write('info registers\n')
proc.stdin.flush()
time.sleep(0.5)
proc.send_signal(signal.SIGTERM)
output, _ = proc.communicate(timeout=2)
print(output)
```

실행 결과:

```
CPU#0
 V      =   1
 pc       0000000000100000
 mhartid  0000000000000000
 mstatus  0000000a00000080
 hstatus  0000000200000000
 ...
```

완벽합니다! 핵심 정보:
- **V = 1**: CPU가 가상화 모드(VS-mode)에 있습니다
- **pc = 0x100000**: 프로그램 카운터가 정확히 `0x100000`에 있습니다

게스트 코드가 정말로 무한 루프를 실행하고 있는 것입니다. `j guest_boot` 명령이 자기 자신으로 계속 점프하고 있습니다.

##### 발생한 문제: alloc_pages 길이 처리

처음에는 `alloc_pages(kernel_image.len())`를 바로 호출했는데, 4바이트는 4096의 배수가 아니라서 `debug_assert!`에서 패닉이 발생할 것 같았습니다. 

튜토리얼에는 이 부분이 명시적으로 나와있지 않았지만, 페이지 단위로 할당해야 하므로 올림 처리가 필요했습니다:

```rust
let kernel_len = (kernel_image.len() + 4095) & !4095;
```

비트 연산으로 4096으로 올림합니다. 예를 들어 4바이트는 4096바이트로 올림됩니다.

##### 정리

Chapter 6에서 배운 내용:

1. **2단계 주소 변환의 구조**: Guest-virtual → Guest-physical → Host-physical로 변환되어 게스트 격리를 구현합니다.

2. **Sv48x4 페이지 테이블**: 4-level 구조로, 각 레벨은 512개의 8바이트 엔트리를 가집니다. 48비트 주소 공간을 지원합니다.

3. **PTE 구조**: 비트 0-9는 플래그(V, R, W, X, U 등), 비트 10-53은 PPN(Physical Page Number)입니다.

4. **Guest page table 구현**: 재귀적으로 레벨을 내려가면서 필요하면 중간 테이블을 할당하고, 최종 레벨에서 실제 매핑을 설정합니다.

5. **`hgatp` CSR**: 상위 4비트는 모드(Sv48x4는 9), 나머지는 페이지 테이블 베이스 주소의 PPN입니다.

6. **게스트 코드 로딩**: `include_bytes!`로 바이너리를 포함시키고, 호스트 메모리에 복사한 후, guest page table로 매핑합니다.

7. **QEMU monitor 활용**: `info registers` 명령으로 CPU 상태를 확인해서 게스트가 정말로 실행 중인지 검증할 수 있습니다.

이제 게스트 코드가 정상적으로 실행되고 있습니다. 다음 챕터에서는 게스트가 하이퍼바이저와 통신할 수 있도록 hypercall을 구현할 것입니다.

### 배운 내용 정리

여기까지 챕터 1-6을 따라가면서 하이퍼바이저의 핵심 개념들을 배웠습니다.

부팅 과정은 OpenSBI 펌웨어가 먼저 실행되고, 그 다음 하이퍼바이저로 넘어오는 식입니다. 스택과 BSS를 초기화하고, trap handler를 등록하는 것도 해봤습니다.

SBI(Supervisor Binary Interface)는 펌웨어가 제공하는 서비스 인터페이스입니다. `ecall` 명령으로 호출할 수 있고, console I/O 같은 기능을 제공합니다.

메모리 관리는 bump allocator로 구현했는데, 순차적으로 할당만 하고 해제는 안 되는 단순한 구조입니다. Page allocator는 4KB 단위로 메모리를 할당하는 식입니다.

게스트 모드 전환은 `sret` 명령으로 하는데, HS-mode에서 VS-mode로 전환할 수 있습니다. 2단계 주소 변환을 통해 게스트 주소를 호스트 주소로 매핑하는 방법도 배웠습니다. 4-level page table(Sv48x4) 방식을 사용합니다.

Bare-metal Rust로 개발하면서 `#![no_std]`로 표준 라이브러리 없이 코드를 작성하는 법도 배웠습니다. Custom panic handler도 직접 만들어야 하고, inline assembly(`asm!` 매크로)로 CSR 레지스터에 접근하는 것도 처음 해봤습니다.

### 튜토리얼에서 발견한 문제들

튜토리얼을 따라가다가 몇 가지 문제가 있었습니다.

우선 Rust edition을 `2024`로 설정하라고 나와있었는데, 아직 그런 edition은 없습니다. `rustc --version`으로 확인해보니 2021까지만 지원되더군요. 그래서 `2021`로 수정했습니다.

`.cargo/config.toml` 파일 설정이 빠져있어서 타겟과 링커를 제대로 지정하지 못했습니다. 직접 파일을 추가해서 해결했습니다.

튜토리얼은 macOS 기준이었는데 저는 Arch Linux를 쓰고 있어서 일부 패키지 경로가 달랐습니다. 큰 문제는 아니었지만요.

Chapter 5에서 page table 설정 없이 임의의 주소(`0x1234abcd`)로 진입하면 guest page fault가 발생했습니다. 처음엔 제가 뭘 잘못한 줄 알았는데, 튜토리얼을 다시 읽어보니 이게 의도된 거더군요. Chapter 6에서 page table을 제대로 설정하면 정상 작동합니다.

프로젝트 구조는 이렇게 됐습니다:

```
my-hypervisor/
├── src/
│   ├── main.rs
│   ├── print.rs
│   ├── trap.rs
│   ├── allocator.rs
│   └── guest_page_table.rs
├── .cargo/
│   └── config.toml
├── hypervisor.ld
├── guest.S
├── run.sh
└── Cargo.toml
```

### Chapter 7: Hello from Guest

Chapter 7의 목표는 게스트에서 하이퍼바이저로 통신하는 것입니다. 즉, 게스트가 "Hello, world!" 같은 메시지를 출력하려면 하이퍼바이저에게 요청해야 합니다.

#### Hypercall의 개념

Hypercall은 시스템 콜과 비슷한데, 게스트 모드에서 하이퍼바이저를 호출하는 것입니다. 둘 다 `ecall` 명령어를 쓰지만 호출하는 주체가 다릅니다.

흐름은 이렇습니다:
1. 게스트 프로그램이 레지스터에 파라미터를 설정하고 `ecall` 명령을 실행합니다
2. CPU가 HS-mode로 전환되고, `stvec` CSR에 설정된 trap handler로 점프합니다
3. Trap handler가 게스트 상태를 저장하고, `scause` CSR을 읽어서 원인(게스트의 `ecall`)을 판단합니다
4. Hypercall을 처리한 후, 게스트 상태를 복원하고 게스트로 돌아갑니다

시스템 콜 처리 흐름과 거의 똑같습니다. 차이점은 호출 레벨입니다.

#### Hypercall 인터페이스: SBI

시스템 콜에는 Linux syscall 같은 표준 인터페이스가 있습니다. Hypercall도 마찬가지로 SBI(Supervisor Binary Interface)라는 표준이 있습니다.

SBI는 펌웨어나 하이퍼바이저가 제공하는 서비스 인터페이스입니다. 이미 우리 하이퍼바이저도 SBI를 사용해서 OpenSBI 펌웨어에 문자를 출력하고 있습니다. 게스트 OS도 똑같이 SBI를 사용해서 하이퍼바이저에게 요청할 수 있습니다.

#### 게스트 코드 수정

우선 게스트가 SBI의 "Console Putchar" 기능을 호출하도록 `guest.S`를 수정했습니다:

```asm
.section .text
.global guest_boot
guest_boot:
    li a7, 1        # Extension ID: 1 (legacy console)
    li a6, 0        # Function ID: 0 (putchar)
    li a0, 'A'      # Parameter: 'A'
    ecall           # Call SBI (hypervisor)

halt:
    j halt          # Infinite loop
```

`a7`에 Extension ID(EID), `a6`에 Function ID(FID), `a0`에 파라미터('A')를 설정하고 `ecall`을 호출합니다.

빌드하고 실행하면 이제 트랩이 발생합니다:

```
$ ./run.sh
Booting hypervisor...
map: 00100000 -> 80305000
panic: panicked at src/trap.rs:51:5:
trap handler: environment call from VS-mode at 0x100008 (stval=0x0)
```

"environment call from VS-mode"는 게스트 커널 모드(VS-mode)에서 `ecall`이 발생했다는 뜻입니다. 정확히 우리가 원하는 트랩입니다.

#### VCpu 구조체 도입

Hypercall을 구현하기 전에, 게스트 상태를 관리할 `VCpu` 구조체를 먼저 만들겠습니다.

새 파일 `src/vcpu.rs`를 만들었습니다:

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
```

CSR들(`hstatus`, `hgatp`, `sstatus`, `sepc`)과 모든 범용 레지스터를 저장합니다. `host_sp`는 하이퍼바이저의 스택 포인터입니다.

VM-exit가 발생하면 스택 포인터가 여전히 게스트의 것입니다. Rust 코드로 진입하기 전에 하이퍼바이저 스택으로 전환해야 합니다.

`VCpu::new` 함수에서 하이퍼바이저 스택을 할당합니다:

```rust
impl VCpu {
    pub fn new(table: &GuestPageTable, guest_entry: u64) -> Self {
        let mut hstatus: u64 = 0;
        hstatus |= 2 << 32;
        hstatus |= 1 << 7;

        let sstatus: u64 = 1 << 8;

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
}
```

512KB 스택을 할당합니다. `alloc_pages`는 시작 주소를 반환하므로, `stack_size`를 더해서 스택 최상단 주소를 만듭니다. (RISC-V 스택은 아래로 자랍니다.)

`VCpu::run` 함수는 게스트로 진입하는 역할입니다:

```rust
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
            // ... (모든 레지스터 복원)
            "ld a0, {a0_offset}(a0)",

            "sret",
            hstatus = in(reg) self.hstatus,
            sstatus = in(reg) self.sstatus,
            hgatp = in(reg) self.hgatp,
            sepc = in(reg) self.sepc,
            sscratch = in(reg) (self as *mut VCpu as usize),
            ra_offset = const offset_of!(VCpu, ra),
            // ... (모든 레지스터 오프셋)
        );
    }

    unreachable!();
}
```

중요한 부분은 `sscratch`에 `VCpu` 구조체의 포인터를 저장하는 것입니다. Trap handler에서 이 포인터를 사용해 게스트 상태를 저장하고 복원합니다.

`mem::offset_of!` 매크로는 구조체의 각 필드가 몇 바이트 오프셋에 있는지 계산합니다. Assembly에서 `ld` 명령으로 필드에 접근할 때 사용합니다.

#### src/main.rs 수정

`src/main.rs`에서 `VCpu`를 사용하도록 수정합니다:

```rust
mod vcpu;

use crate::{
    allocator::alloc_pages,
    guest_page_table::{GuestPageTable, PTE_R, PTE_W, PTE_X},
    vcpu::VCpu,
};
```

`main` 함수 끝 부분을 변경합니다:

```rust
let mut table = GuestPageTable::new();
table.map(guest_entry, kernel_memory as u64, PTE_R | PTE_W | PTE_X);

let mut vcpu = VCpu::new(&table, guest_entry);
vcpu.run();
```

이전의 CSR 설정 코드를 `VCpu`가 대신합니다. 리팩토링일 뿐이고 기능은 동일합니다.

#### 게스트 상태 저장

이제 trap handler를 수정해서 게스트 상태를 저장합니다.

`src/trap.rs`를 수정합니다:

```rust
use core::{arch::naked_asm, mem::offset_of};
use crate::vcpu::VCpu;

#[unsafe(link_section = ".text.stvec")]
#[unsafe(naked)]
pub extern "C" fn trap_handler() -> ! {
    naked_asm!(
        "csrrw a0, sscratch, a0",

        "sd ra, {ra_offset}(a0)",
        "sd sp, {sp_offset}(a0)",
        "sd gp, {gp_offset}(a0)",
        // ... (모든 레지스터 저장)
        "sd t6, {t6_offset}(a0)",

        "csrr t0, sscratch",
        "sd t0, {a0_offset}(a0)",

        "ld sp, {host_sp_offset}(a0)",

        "call {handle_trap}",
        handle_trap = sym handle_trap,
        host_sp_offset = const offset_of!(VCpu, host_sp),
        ra_offset = const offset_of!(VCpu, ra),
        // ... (모든 레지스터 오프셋)
    );
}
```

중요한 부분을 설명하겠습니다:

**1. `csrrw a0, sscratch, a0`**

`csrrw`는 "CSR Read-Write"입니다. 두 가지 동작을 동시에 합니다:
- `sscratch`의 값을 `a0`로 읽어옵니다
- `a0`의 기존 값을 `sscratch`에 씁니다

이렇게 하면 `a0`에는 `VCpu` 포인터가 들어오고, 게스트의 원래 `a0` 값은 `sscratch`에 백업됩니다.

**2. 레지스터 저장**

`sd ra, {ra_offset}(a0)`는 "Store Doubleword"입니다. `ra` 레지스터를 `a0 + ra_offset` 주소에 8바이트 저장합니다. 즉, `VCpu` 구조체의 `ra` 필드에 저장됩니다.

모든 범용 레지스터를 같은 방식으로 저장합니다.

**3. `a0` 레지스터 저장**

```asm
"csrr t0, sscratch",
"sd t0, {a0_offset}(a0)",
```

아까 `sscratch`에 백업한 원래 `a0` 값을 `t0`로 읽어온 후, `VCpu` 구조체에 저장합니다.

**4. 스택 전환**

```asm
"ld sp, {host_sp_offset}(a0)",
```

`VCpu`의 `host_sp` 필드를 `sp` 레지스터로 로드합니다. 이제 스택 포인터가 하이퍼바이저 스택을 가리킵니다.

**5. Rust 함수 호출**

```asm
"call {handle_trap}",
```

`handle_trap` 함수를 호출합니다. `a0`는 여전히 `VCpu` 포인터이므로 첫 번째 인자로 자동으로 전달됩니다. (RISC-V calling convention)

**naked function**

`#[naked]`는 컴파일러가 함수 프롤로그/에필로그를 생성하지 않도록 합니다. 스택 프레임 설정 같은 걸 자동으로 안 넣습니다. 우리가 어셈블리를 완전히 제어할 수 있습니다.

`naked_asm!`은 `#[naked]` 함수 안에서만 쓸 수 있는 매크로입니다.

#### Hypercall 처리

이제 `handle_trap` 함수를 구현합니다:

```rust
pub fn handle_trap(vcpu: *mut VCpu) -> ! {
    let scause = read_csr!("scause");
    let sepc = read_csr!("sepc");
    let stval = read_csr!("stval");
    let scause_str = match scause {
        0 => "instruction address misaligned",
        // ... (trap 종류들)
        10 => "environment call from VS-mode",
        // ... (나머지)
        _ => panic!("unknown scause: {:#x}", scause),
    };

    let vcpu = unsafe { &mut *vcpu };
    if scause == 10 {
        println!("SBI call: eid={:#x}, fid={:#x}, a0={:#x} ('{}')", vcpu.a7, vcpu.a6, vcpu.a0, vcpu.a0 as u8 as char);
        vcpu.sepc = sepc + 4;
    } else {
        panic!("trap handler: {} at {:#x} (stval={:#x})", scause_str, sepc, stval);
    }

    vcpu.run();
}
```

`scause == 10`은 "environment call from VS-mode"입니다. 게스트의 `ecall`입니다.

SBI 스펙에 따르면:
- `a7`: Extension ID (EID)
- `a6`: Function ID (FID)
- `a0`: 파라미터
- 리턴 값: `a0` (에러 코드), `a1` (결과)

우리는 일단 출력만 하고 있습니다. `vcpu.a7`, `vcpu.a6`, `vcpu.a0`을 읽어서 hypercall 내용을 확인합니다.

**중요**: `vcpu.sepc = sepc + 4`로 프로그램 카운터를 4바이트 증가시킵니다. `ecall` 명령은 4바이트이므로, 다음 명령으로 넘어가야 합니다. 안 그러면 무한 루프에 빠집니다.

마지막에 `vcpu.run()`을 호출해서 게스트로 돌아갑니다.

#### 게스트 상태 복원

`VCpu::run` 함수는 이미 모든 레지스터를 복원하도록 구현했습니다. CSR들을 설정하고, `VCpu` 구조체에서 범용 레지스터들을 로드한 후 `sret`으로 게스트로 돌아갑니다.

#### 테스트

빌드하고 실행했습니다:

```bash
$ ./run.sh
Booting hypervisor...
map: 00100000 -> 80306000
SBI call: eid=0x1, fid=0x0, a0=0x41 ('A')
```

성공입니다! 게스트가 'A'를 출력하려고 하이퍼바이저에게 hypercall을 보냈고, 하이퍼바이저가 이를 인식했습니다.

이제 여러 문자를 출력하도록 `guest.S`를 수정합니다:

```asm
guest_boot:
    li a7, 1        # Extension ID: 1 (legacy console)
    li a6, 0        # Function ID: 0 (putchar)
    li a0, 'A'      # Parameter: 'A'
    ecall           # Call SBI (hypervisor)
    li a0, 'B'      # Parameter: 'B'
    ecall           # Call SBI (hypervisor)
    li a0, 'C'      # Parameter: 'C'
    ecall           # Call SBI (hypervisor)

halt:
    j halt          # Infinite loop
```

다시 실행합니다:

```bash
$ timeout 5 ./run.sh
Booting hypervisor...
map: 00100000 -> 80306000
SBI call: eid=0x1, fid=0x0, a0=0x41 ('A')
SBI call: eid=0x1, fid=0x0, a0=0x42 ('B')
SBI call: eid=0x1, fid=0x0, a0=0x43 ('C')
```

완벽합니다! 게스트가 'A', 'B', 'C'를 순서대로 호출했고, 모두 정상적으로 처리되었습니다.

#### 발생한 문제

이번 챕터는 비교적 순탄했습니다. 튜토리얼을 그대로 따라가면 문제없이 작동했습니다.

한 가지 주의할 점은 `vcpu.sepc = sepc + 4`를 꼭 해야 한다는 것입니다. 처음엔 왜 필요한지 헷갈렸는데, `ecall` 명령 자체가 4바이트이고 `sepc`는 트랩을 발생시킨 명령의 주소를 가리키므로, 다음 명령으로 넘어가려면 4를 더해야 합니다. 안 그러면 같은 `ecall`을 계속 실행합니다.

또 하나는 `naked_asm!` 매크로입니다. 처음 보는 기능이었는데, bare-metal 코드에서 어셈블리를 완전히 제어하려면 필요한 기능입니다. 일반 `asm!`은 컴파일러가 스택 프레임 같은 걸 자동으로 추가하는데, trap handler에서는 그런 게 없어야 합니다.

`offset_of!` 매크로도 유용했습니다. C의 `offsetof`와 같은 기능인데, 구조체 필드의 바이트 오프셋을 컴파일 타임에 계산합니다. 어셈블리에서 구조체 필드에 접근할 때 필수입니다.

#### 정리

Chapter 7에서 배운 내용:

1. **Hypercall의 개념**: 게스트가 `ecall`로 하이퍼바이저를 호출하는 메커니즘. 시스템 콜과 구조가 비슷합니다.

2. **SBI (Supervisor Binary Interface)**: 하이퍼바이저/펌웨어가 제공하는 표준 인터페이스. `a7` (EID), `a6` (FID), `a0-a1` (파라미터/리턴)으로 호출합니다.

3. **VCpu 구조체**: 게스트 상태(CSR + 범용 레지스터)를 관리하는 구조체. `host_sp`로 하이퍼바이저 스택도 관리합니다.

4. **Naked function**: `#[naked]`과 `naked_asm!`으로 컴파일러 개입 없이 어셈블리를 작성할 수 있습니다.

5. **게스트 상태 저장/복원**: Trap handler에서 `sscratch`를 이용해 `VCpu` 포인터를 전달하고, 모든 레지스터를 저장/복원합니다.

6. **스택 전환**: VM-exit 시 게스트 스택에서 하이퍼바이저 스택으로 전환해야 Rust 코드를 안전하게 실행할 수 있습니다.

7. **`csrrw` 명령**: CSR 읽기와 쓰기를 동시에 수행. `a0`와 `sscratch`를 스왑하는 데 사용했습니다.

8. **`sepc` 증가**: Trap에서 복귀할 때 프로그램 카운터를 명령 크기만큼 증가시켜야 합니다. (`ecall`은 4바이트)

이제 게스트와 하이퍼바이저가 통신할 수 있습니다. 다음 챕터에서는 리눅스 커널을 게스트로 부팅할 것입니다.

#### VCpu 구조체

튜토리얼을 보니, 게스트 CPU 상태를 저장하려고 `VCpu` 구조체를 만들어야 한다고 합니다. `src/vcpu.rs`를 만들었습니다:

```rust
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
```

CSR 레지스터들과 범용 레지스터 31개를 개별 필드로 저장합니다. x0는 항상 0이니까 저장 안 해도 된다고 하네요. VM exit이 발생하면 여기에 전부 저장하고, 다시 들어갈 때 복원하는 식입니다.

`host_sp`는 하이퍼바이저의 trap handler 스택 포인터라고 합니다.

#### Trap Handler 수정

기존 trap handler를 naked function으로 다시 짜야 한다고 합니다. 이전엔 Rust 함수로 대충 했었는데, 레지스터를 제대로 저장하고 복원하려면 순수 어셈블리로 짜야 한다고 하네요.

`src/trap.rs`를 수정했습니다:

```rust
#[unsafe(link_section = ".text.stvec")]
#[unsafe(naked)]
pub extern "C" fn trap_handler() -> ! {
    naked_asm!(
        "csrrw a0, sscratch, a0",
        "sd ra, {ra_offset}(a0)",
        "sd sp, {sp_offset}(a0)",
        // ... (나머지 레지스터들)
        "csrr t0, sscratch",
        "sd t0, {a0_offset}(a0)",
        "ld sp, {host_sp_offset}(a0)",
        "call {handle_trap}",
        handle_trap = sym handle_trap,
        // ... (오프셋 정의)
    );
}
```

`sscratch` CSR에는 VCpu 구조체 포인터가 들어있다고 합니다. `csrrw`로 a0와 sscratch를 교환해서 VCpu 포인터를 가져오고, 모든 레지스터를 VCpu 구조체에 저장합니다. 그 다음 호스트 스택으로 전환하고 Rust 함수를 호출하는 식입니다.

이 부분이 좀 복잡해서 튜토리얼 코드를 거의 그대로 가져왔습니다.

#### Hypercall 처리

튜토리얼을 보니 `ecall`이 발생하면 이렇게 처리한다고 합니다:

```rust
pub fn handle_trap(vcpu: *mut VCpu) -> ! {
    let vcpu = unsafe { &mut *vcpu };
    let scause = read_csr!("scause");
    let sepc = read_csr!("sepc");
    
    if scause == 10 {  // environment call from VS-mode
        println!("SBI call: eid={:#x}, fid={:#x}, a0={:#x} ('{}')", 
                 vcpu.a7, vcpu.a6, vcpu.a0, vcpu.a0 as u8 as char);
        vcpu.sepc = sepc + 4;
    } else {
        panic!("trap handler: {} at {:#x}", scause, sepc);
    }
    
    vcpu.run();
}
```

`scause=10`이 VS-mode의 `ecall`이라고 합니다. SBI 규약에 따르면 `a7`에 extension ID, `a6`에 function ID가 들어있다고 하네요. `console_putchar`는 extension=0x1, function=0x0이고, `a0`에 문자가 들어있습니다.

`sepc`를 4 증가시켜야 다음 명령으로 넘어간다고 합니다. 이거 안 하면 같은 `ecall`을 계속 반복한다고 하더군요.

#### 게스트 코드 수정

게스트 코드(`guest.S`)를 수정했습니다:

```asm
.section .text
.global guest_boot
guest_boot:
    li a7, 1
    li a6, 0
    
    li a0, 'A'
    ecall
    
    li a0, 'B'
    ecall
    
    li a0, 'C'
    ecall
    
loop:
    j loop
```

'A', 'B', 'C'를 차례로 출력하는 코드입니다.

실행해보니:

```
Booting hypervisor...
v = ['a', 'b', 'c']
map: 00100000 -> 80309000
SBI call: eid=0x1, fid=0x0, a0=0x41 ('A')
SBI call: eid=0x1, fid=0x0, a0=0x42 ('B')
SBI call: eid=0x1, fid=0x0, a0=0x43 ('C')
```

처음엔 이렇게만 나왔는데, 실제로 'ABC'를 출력하려면 하이퍼바이저가 받은 문자를 다시 SBI로 출력해야 한다고 하더군요. `handle_trap` 함수를 수정했습니다:

```rust
if scause == 10 {
    if vcpu.a7 == 1 && vcpu.a6 == 0 {
        print!("{}", vcpu.a0 as u8 as char);
    }
    vcpu.sepc = sepc + 4;
}
```

다시 실행하니:

```
Booting hypervisor...
v = ['a', 'b', 'c']
map: 00100000 -> 80309000
ABC
```

됐습니다! 게스트에서 하이퍼바이저로 문자를 전달해서 출력하는 데 성공했습니다.

이 챕터에서 배운 게 몇 가지 있는데, VM exit 시 레지스터 상태를 전부 저장해야 한다는 거, `sscratch`로 스택을 교환하는 트릭, 그리고 `ecall` 같은 trap instruction은 PC를 수동으로 증가시켜야 한다는 점입니다. Rust의 naked function도 처음 써봤는데 컴파일러 간섭 없이 순수 어셈블리를 쓸 수 있어서 괜찮았습니다.

### 튜토리얼 8-10챕터 확인

Chapter 7까지 끝내고, 남은 챕터들을 봤습니다.

#### Chapter 8-10 코드가 이미 있음

튜토리얼 저장소(`tutorial-risc-v/src/`)를 보니까 Chapter 8-10 구현이 다 돼있었습니다:

- **Chapter 8**: Docker로 Linux 커널 크로스 컴파일하는 스크립트가 `linux/Dockerfile`, `build.sh`에 있습니다.
- **Chapter 9**: `linux_loader.rs`에 커널 로더랑 device tree 생성하는 코드 다 있습니다. `hedeleg`로 exception을 게스트가 직접 처리하도록 설정하는 것도 포함입니다.
- **Chapter 10**: `trap.rs`에 여러 SBI extension 구현돼있습니다. Console buffering, timer, PLIC MMIO 처리까지 있습니다.

코드를 복사해서 실행해봤는데, Linux 부팅 시도하니까 초기 커널 메시지 몇 개 나오다가 PLIC 접근할 때 guest page fault 나면서 멈췄습니다.

```
[    0.000000] Linux version 5.15.0 ...
[    0.000000] Machine model: riscv-virtio,qemu
...
trap handler: 12 at 0xc000000
```

#### Chapter 11-13은 빈껍데기

Chapter 11(Memory Mapped I/O), 12(Interrupt Injection), 13(Outro)는 stub만 있고 실제 코드가 없었습니다. 파일은 있는데 함수 시그니처만 있고 구현이 비어있더군요.

#### 따라가지 않기로 함

Chapter 8-10을 할지 말지 고민했는데, 안 하기로 했습니다.

코드가 이미 다 있어서 복사만 하는 꼴이 될 것 같았습니다. Linux도 제대로 부팅이 안 되니까 성공 경험도 못 얻고요.

그리고 PLIC, device tree, SBI 같은 건 RISC-V 전용이라 x86 갈 때는 어차피 못 씁니다. (x86은 ACPI, BIOS/UEFI 씀)

Chapter 1-7에서 게스트 모드 진입, VM exit 처리, hypercall, 2-stage translation 같은 핵심 개념은 다 배운 것 같습니다. 이걸로 충분한 것 같습니다.

### x86 KVM으로 전환 준비

RISC-V 튜토리얼로 하이퍼바이저의 핵심 구조를 이해했으니, 이제 x86 KVM으로 전환할 차례입니다.

RISC-V에서 배운 걸 정리해보면:

펌웨어(OpenSBI)가 하이퍼바이저 boot() 함수를 호출하고, 거기서 스택과 BSS를 초기화한 다음 main()으로 넘어갑니다.

메모리는 계층적으로 관리됩니다. Heap이 있고, 그 위에 page allocator가 4KB 단위로 메모리를 할당합니다. Guest memory는 이 page allocator에서 할당받아서 guest page table에 매핑합니다. 2-stage translation으로 guest PA(0x100000)가 host PA(0x80309000)로 변환됩니다.

Guest 실행 흐름은 이렇습니다:

```rust
// 1. Guest 메모리 할당
let guest_memory = alloc_pages(size);

// 2. Guest 코드 복사
copy_nonoverlapping(guest_code, guest_memory, size);

// 3. Page table 매핑
guest_page_table.map(guest_pa, host_pa, flags);

// 4. CSR 설정 및 진입
csrw hgatp, guest_page_table.hgatp();
csrw hstatus, (1 << 7);  // SPV 비트 설정
csrw sepc, entry_point;
sret;

// 5. VM Exit 처리
trap_handler() {
    match scause {
        GUEST_PAGE_FAULT => handle_page_fault(),
        ECALL => handle_syscall(),
        ...
    }
}
```

이걸 x86 KVM으로 대응시켜보면:

- RISC-V의 `alloc_pages()`는 x86에서 `mmap()` + `KVM_SET_USER_MEMORY_REGION`입니다.
- `guest_page_table.map()`는 `KVM_SET_USER_MEMORY_REGION`의 slot 설정입니다.
- `csrw hgatp`는 `KVM_SET_SREGS`로 CR3, EFER 같은 걸 설정하는 겁니다.
- `csrw sepc; sret`는 `KVM_SET_REGS`로 RIP를 설정하고 `KVM_RUN`을 호출하는 겁니다.
- `trap_handler()`는 VM exit reason을 처리하는 루프입니다.

#### KVM API 문서 살펴보기

Linux 커널의 [KVM API 문서](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)를 봤습니다.

RISC-V 실습을 기반으로 핵심 API를 정리하면:

```c
// RISC-V: open("/dev/kvm")
int kvm_fd = open("/dev/kvm", O_RDWR);

// RISC-V: 전체 하이퍼바이저 생성
int vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);

// RISC-V: alloc_pages() + guest_page_table.map()
struct kvm_userspace_memory_region region = {
    .slot = 0,
    .guest_phys_addr = 0x100000,
    .memory_size = size,
    .userspace_addr = (uint64_t)mmap(...),
};
ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region);

// RISC-V: vCPU 생성
int vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);

// RISC-V: csrw sepc, entry_point
struct kvm_regs regs;
regs.rip = entry_point;
ioctl(vcpu_fd, KVM_SET_REGS, &regs);

// RISC-V: csrw hgatp; csrw hstatus; sret
struct kvm_sregs sregs;
sregs.cr3 = page_table_base;
sregs.cr0 = ...; sregs.cr4 = ...;
ioctl(vcpu_fd, KVM_SET_SREGS, &sregs);

// RISC-V: sret + trap_handler()
while (1) {
    ioctl(vcpu_fd, KVM_RUN, 0);
    
    switch (run->exit_reason) {
        case KVM_EXIT_IO:
        case KVM_EXIT_MMIO:
        case KVM_EXIT_HLT:
        ...
    }
}
```

워크플로우를 비교해보면:

```
1. [RISC-V] OpenSBI 펌웨어 준비
   [x86 KVM] /dev/kvm 열기

2. [RISC-V] 하이퍼바이저 초기화 (boot, BSS)
   [x86 KVM] KVM_CREATE_VM

3. [RISC-V] alloc_pages()로 게스트 메모리 할당
   [x86 KVM] mmap()으로 호스트 메모리 할당

4. [RISC-V] guest_page_table.map()
   [x86 KVM] KVM_SET_USER_MEMORY_REGION

5. [RISC-V] 게스트 코드를 메모리에 복사
   [x86 KVM] 게스트 코드를 mmap 영역에 복사

6. [RISC-V] csrw sepc, entry; sret
   [x86 KVM] KVM_SET_REGS (RIP); KVM_RUN

7. [RISC-V] trap_handler()에서 VM exit 처리
   [x86 KVM] KVM_RUN 반환 후 exit_reason 확인
```

[Hypervisor From Scratch](https://rayanfam.com/topics/hypervisor-from-scratch-part-1/) 시리즈도 찾아봤는데, x86 아키텍처 기반으로 Intel VT-x 기술을 다루더군요. Windows 환경 중심이고 더 저수준의 하드웨어 가상화를 설명합니다. 참고는 하되 KVM API 중심으로 가는 게 나을 것 같습니다.

"simple kvm example c" 같은 키워드로 예제 코드를 검색했는데, 대부분 QEMU 같은 복잡한 VMM의 일부거나, 오래돼서 현재 KVM API와 맞지 않는 경우가 많았습니다.

### 앞으로 할 일

RISC-V 튜토리얼로 하이퍼바이저의 핵심 구조를 이해했으니, 이제 x86 KVM으로 전환할 시점입니다.

**Week 7-8: 최소 KVM 프로그램**

RISC-V Chapter 1-6 수준으로, 간단한 게스트 코드를 실행하는 걸 목표로 합니다:

```c
.code16
start:
    jmp start
```

구현 단계는:
1. KVM 초기화 (`/dev/kvm`, `KVM_CREATE_VM`)
2. 게스트 메모리 매핑 (`mmap` + `KVM_SET_USER_MEMORY_REGION`)
3. vCPU 생성 및 레지스터 설정 (real mode 또는 protected mode)
4. 게스트 코드 실행 (`KVM_RUN`)
5. VM exit 처리 루프 (최소한 HLT만)

**Week 8-9: I/O 에뮬레이션**

RISC-V의 SBI putchar처럼, port I/O 처리(`KVM_EXIT_IO`)를 구현합니다. Serial port(0x3F8)를 에뮬레이션해서 게스트에서 문자를 출력할 수 있도록 합니다.

**Week 9-10: Protected Mode 전환**

RISC-V는 처음부터 64-bit 모드였지만, x86은 모드 전환이 필요합니다. GDT를 설정하고 real mode에서 protected mode로 전환합니다. Page table도 설정합니다.

**Week 10-11: 부트로더 실행**

MBR 부트 섹터를 실행하고, xv6 부트로더를 분석해서 첫 번째 부트 단계를 성공시킵니다.

**Week 11-15: xv6 커널 부팅**

커널을 로딩하고, 인터럽트와 디스크 I/O를 에뮬레이션해서 xv6 전체 부팅에 성공합니다.

### 참고 자료

- [RISC-V Hypervisor Tutorial](https://1000hv.seiya.me/en/)
- [Tutorial GitHub Repository](https://github.com/nuta/tutorial-risc-v)
- [KVM API Documentation](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [RISC-V Privileged Specification](https://riscv.org/technical/specifications/)
- [Hypervisor From Scratch](https://rayanfam.com/topics/hypervisor-from-scratch-part-1/)
- [OSDev Wiki](https://wiki.osdev.org/)

구현한 코드는 `/home/seolcu/문서/코드/mini-kvm/my-hypervisor/`에 있고, 참고용 튜토리얼 저장소는 `/home/seolcu/문서/코드/mini-kvm/tutorial-risc-v/`에 있습니다.

## 결론

이번 5-6주차에는 교수님이 추천하신 RISC-V 튜토리얼로 하이퍼바이저의 핵심 구조를 직접 구현해봤습니다.

총 13개 챕터 중 1-7 챕터를 완료했습니다:
1. Getting Started - 개발 환경 구성
2. Boot - 하이퍼바이저 부팅
3. Hello World - SBI 콘솔 출력
4. Memory Allocation - Bump allocator, page allocator 구현
5. Guest Mode - 게스트 모드 진입
6. Guest Page Table - 2-stage address translation 구현
7. VCpu - VCpu 추상화와 hypercall 구현

약 500줄 정도의 Rust 코드로 bare-metal 하이퍼바이저를 만들었습니다. OpenSBI 펌웨어 위에서 부팅하고, SBI 인터페이스로 I/O를 하고, bump allocator와 page allocator로 메모리를 관리합니다. 4-level guest page table(Sv48x4)로 2-stage address translation을 구현해서 게스트 PA를 호스트 PA로 매핑했습니다. VCpu 구조체로 게스트 상태를 관리하고, hypercall을 통해 게스트-호스트 간 통신을 구현했습니다. 게스트 코드 실행도 성공했습니다.

하이퍼바이저가 어떻게 작동하는지 이해할 수 있었습니다. 부팅 과정, 메모리 관리, 게스트 모드 전환(`sret` 명령), VM exit 처리(trap handler) 같은 것들입니다. 2-stage address translation으로 각 VM이 독립적인 메모리를 가진 것처럼 격리하는 방법도 배웠습니다.

RISC-V와 x86을 비교해보니, RISC-V는 `sret` 명령으로 일관되게 모드 전환을 하는데 x86은 `vmlaunch`/`vmresume` 같은 전용 명령이 있습니다. RISC-V가 단순해서 개념 이해에는 더 좋은 것 같습니다.

Bare-metal programming도 처음 해봤습니다. `#![no_std]`로 표준 라이브러리 없이 개발하고, 링커 스크립트로 메모리 레이아웃을 제어하고, CSR 레지스터를 직접 제어하고, inline assembly를 쓰는 것들입니다.

튜토리얼 8장 이후는 미완성이었지만, 초반 7개 챕터만으로도 핵심 개념은 충분히 배운 것 같습니다. RISC-V에서 배운 걸 x86 KVM으로 적용할 준비가 됐습니다:

- RISC-V의 `hstatus`, `hgatp` CSR → x86의 KVM API(`KVM_SET_SREGS`)
- `sret` 명령 → `KVM_RUN` ioctl
- Guest page table → `KVM_SET_USER_MEMORY_REGION`
- Trap handler → VM exit 처리 루프
- SBI 인터페이스 → MMIO/Port I/O 에뮬레이션

RISC-V가 단순하고 일관성 있어서 하이퍼바이저 개념을 이해하기 좋았습니다. 문서만 읽는 것보다 직접 구현하면서 훨씬 깊이 이해할 수 있었습니다. 교수님이 제안하신 "RISC-V에서 배우고 x86에 적용" 접근법이 좋은 것 같습니다.

다음 주차부터는 x86 KVM API로 최소한의 VMM을 구현할 계획입니다.
