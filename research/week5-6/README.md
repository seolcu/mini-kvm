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

### 환경 구성

튜토리얼이 Rust 기반이라서 우선 제 시스템에 Rust가 설치되어 있는지 확인해봤습니다.

```bash
$ rustc --version
rustc 1.90.0 (5bc8c1c31 2024-12-11)
$ qemu-system-riscv64 --version
QEMU emulator version 10.1.0
```

다행히 Rust 툴체인(1.90.0)과 QEMU(10.1.0)가 설치되어 있었습니다. 튜토리얼에서 추가로 요구하는 패키지들을 확인해보니, 게스트 코드 컴파일을 위해 `clang`, `llvm`, `lld`가 필요했습니다. Arch Linux를 쓰고 있어서 `pacman`으로 설치했습니다.

```bash
sudo pacman -S clang llvm lld
```

### 튜토리얼 따라가기 (Chapter 1-4)

#### Chapter 1: Getting Started

Chapter 1은 개발 환경 구성입니다. Rust 툴체인과 QEMU를 설치하는 부분인데, 이미 설치되어 있어서 바로 넘어갔습니다. RISC-V 64비트 타겟(`riscv64gc-unknown-none-elf`)을 추가해야 한다고 해서 추가했습니다.

```bash
rustup target add riscv64gc-unknown-none-elf
```

#### Chapter 2: Boot

Chapter 2부터 본격적으로 코드를 작성하기 시작합니다.

우선 Rust bare-metal 프로젝트를 생성했습니다. 튜토리얼에는 `cargo new my-hypervisor`라고 나와있어서 그대로 따라했습니다.

```bash
cargo new my-hypervisor
cd my-hypervisor
```

`src/main.rs`에 `#![no_std]`, `#![no_main]` 속성을 추가해야 한다고 합니다. 표준 라이브러리 없이 bare-metal로 개발하려면 이게 필요하다고 하네요.

부팅 과정을 구현하는 부분이 좀 복잡했습니다. 튜토리얼에서 제공하는 `boot()` 함수 코드를 그대로 가져왔는데, 어셈블리로 스택 포인터를 초기화하고 main으로 점프하는 코드였습니다.

링커 스크립트(`hypervisor.ld`)도 만들어야 했습니다. 베이스 주소를 `0x80200000`으로 설정하고, BSS 섹션을 초기화하도록 했습니다. 커널 스택 크기는 1MB입니다.

Panic handler도 구현해야 했는데, 이건 그냥 무한 루프를 도는 함수를 만들었습니다.

```rust
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
```

여기서 빌드를 시도해봤는데, 에러가 발생했습니다.

```
error: linker `rust-lld` not found
```

튜토리얼을 다시 읽어보니, `.cargo/config.toml` 파일 설정이 빠져있다는 걸 발견했습니다. 튜토리얼에는 macOS 기준으로 설명되어 있어서 제 환경(Arch Linux)에 맞게 수정했습니다.

```toml
[build]
target = "riscv64gc-unknown-none-elf"

[target.riscv64gc-unknown-none-elf]
rustflags = ["-C", "link-arg=-Thypervisor.ld"]
```

이후 다시 빌드하니 성공했습니다.

```bash
cargo build
```

QEMU로 실행해보기 위해 `run.sh` 스크립트를 만들었습니다. 튜토리얼에 나온 QEMU 명령어를 그대로 가져왔습니다.

```bash
#!/bin/bash
qemu-system-riscv64 -machine virt -cpu rv64 -smp 1 -m 128M \
    -nographic -serial mon:stdio -bios default \
    -kernel target/riscv64gc-unknown-none-elf/debug/my-hypervisor
```

실행하니 다음과 같은 출력이 나왔습니다:

```
OpenSBI v1.5.1
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

...
```

QEMU virt 머신에서 OpenSBI 펌웨어가 먼저 실행되고, 그 이후 하이퍼바이저로 제어권을 넘겨주는 것 같습니다. 하지만 아직 출력 기능을 구현하지 않아서 아무것도 화면에 나오지 않았습니다.

#### Chapter 3: Hello World

Chapter 3에서는 콘솔 출력을 구현합니다.

튜토리얼을 보니, SBI(Supervisor Binary Interface)라는 걸 사용해야 한다고 합니다. OpenSBI 펌웨어가 제공하는 서비스 인터페이스인데, `ecall` 명령으로 호출할 수 있다고 하네요.

`src/print.rs`를 만들어서 SBI putchar 함수를 구현했습니다. `ecall` 명령을 inline assembly로 호출하도록 했습니다.

```rust
fn sbi_putchar(c: u8) {
    unsafe {
        asm!(
            "ecall",
            in("a0") c as u64,
            in("a6") 0,
            in("a7") 1,
        );
    }
}
```

Rust의 `fmt::Write` 트레이트를 구현해서 `println!` 매크로를 만들었습니다. 이 부분은 튜토리얼 코드를 거의 그대로 가져왔습니다.

Trap handler도 구현해야 했습니다(`src/trap.rs`). `stvec` CSR에 trap handler를 등록하는 코드였는데, 이것도 튜토리얼을 따라했습니다.

```rust
pub fn init() {
    extern "C" {
        fn trap_entry();
    }
    unsafe {
        asm!("csrw stvec, {}", in(reg) trap_entry as usize);
    }
}
```

`main()`에서 trap handler를 초기화하고, "Booting hypervisor..." 메시지를 출력하도록 했습니다.

```rust
#[no_mangle]
pub extern "C" fn main() -> ! {
    trap::init();
    println!("Booting hypervisor...");
    loop {}
}
```

실행해보니:

```
Booting hypervisor...
```

드디어 출력이 됐습니다!

#### Chapter 4: Memory Allocation

Chapter 4는 메모리 할당자를 구현하는 챕터입니다.

Bump allocator라는 걸 구현해야 한다고 합니다. 메모리를 순차적으로 할당만 하고 해제는 지원하지 않는 단순한 구조라고 하네요.

`src/allocator.rs`를 만들었습니다. 튜토리얼에 나온 코드를 보면서 따라했는데, `spin::Mutex`를 사용해서 동기화를 하더군요. `Cargo.toml`에 `spin` 의존성을 추가했습니다.

```toml
[dependencies]
spin = "0.9"
```

4KB 단위로 할당하는 page allocator도 추가했습니다.

```rust
pub fn alloc_pages(size: usize) -> *mut u8 {
    let pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    ALLOCATOR.lock().alloc(pages * PAGE_SIZE)
}
```

`#[global_allocator]` 속성으로 전역 할당자를 설정했습니다. 링커 스크립트에 100MB heap 영역도 추가했습니다.

```rust
#[global_allocator]
static ALLOCATOR: LockedAllocator = LockedAllocator::new();
```

`Cargo.toml`에서 `alloc` crate를 활성화하니 `Vec`, `Box` 같은 표준 자료구조를 쓸 수 있게 됐습니다.

튜토리얼에는 테스트 코드로 `Vec::new()`를 사용하는 예제가 있어서 그대로 따라했습니다.

```rust
let mut v = Vec::new();
v.push('a');
v.push('b');
v.push('c');
println!("v = {:?}", v);
```

실행하니:

```
Booting hypervisor...
v = ['a', 'b', 'c']
```

메모리 할당이 제대로 작동하는 것 같습니다.

### Chapter 5-6: Guest Mode 구현

#### Chapter 5: Guest Mode

Chapter 5부터 본격적으로 게스트 모드를 다룹니다.

튜토리얼을 읽어보니, RISC-V의 하이퍼바이저 확장은 HS-mode(호스트), VS-mode(게스트 커널), VU-mode(게스트 유저) 세 가지 모드를 제공한다고 합니다.

QEMU에서 H-extension을 활성화하려면 `-cpu rv64,h=true` 옵션을 추가해야 한다고 해서 `run.sh`를 수정했습니다.

```bash
qemu-system-riscv64 -machine virt -cpu rv64,h=true -smp 1 -m 128M \
    -nographic -serial mon:stdio -bios default \
    -kernel target/riscv64gc-unknown-none-elf/debug/my-hypervisor
```

게스트 모드 진입 코드는 튜토리얼에 나온 대로 작성했습니다:

```rust
let mut hstatus: u64 = 0;
hstatus |= 2 << 32;  // VSXL: XLEN for VS-mode (64-bit)
hstatus |= 1 << 7;   // SPV: Supervisor Previous Virtualization mode

unsafe {
    asm!(
        "csrw hstatus, {hstatus}",
        "csrw sepc, {sepc}",
        "sret",
        hstatus = in(reg) hstatus,
        sepc = in(reg) 0x1234abcd,
    );
}
```

이 코드가 좀 신기했습니다. `sret` 명령이 유저 모드 복귀뿐만 아니라 게스트 모드 진입에도 쓰인다는 게 흥미로웠습니다. x86처럼 `vmlaunch` 같은 전용 명령이 없어도 되는 것 같습니다.

실행해보니 에러가 발생했습니다:

```
Booting hypervisor...
v = ['a', 'b', 'c']
trap handler: 12 at 0x1234abcd
```

`scause=12`는 instruction page fault라고 합니다. `0x1234abcd`는 임의의 주소니까 당연히 매핑이 안 돼있어서 에러가 나는 것 같습니다. 튜토리얼을 보니 이게 정상이고, Chapter 6에서 page table을 제대로 설정하면 해결된다고 하네요.

#### Chapter 6: Guest Page Table

Chapter 6에서 드디어 2단계 주소 변환을 구현합니다.

튜토리얼 설명을 읽어보니:
- Stage 1: Guest-virtual → Guest-physical (게스트의 `satp` 사용)
- Stage 2: Guest-physical → Host-physical (하이퍼바이저의 `hgatp` 사용)

이렇게 하면 여러 VM이 각자 독립적인 물리 주소 공간을 가진 것처럼 격리할 수 있다고 합니다.

Guest page table은 Sv48x4 방식으로 4-level 구조라고 합니다. 각 레벨은 512개의 entry를 가지고, 각 entry는 8바이트라서 정확히 4KB입니다.

`src/guest_page_table.rs`를 만들어서 page table 코드를 작성했습니다. 튜토리얼에 나온 코드를 참고했는데, PTE(Page Table Entry) 구조가 복잡하더군요. 상위 54비트가 PPN(Physical Page Number)이고, 하위 비트들이 V(Valid), R(Read), W(Write), X(Execute), U(User) 같은 플래그입니다.

간단한 게스트 코드를 어셈블리로 작성했습니다(`guest.S`):

```asm
.section .text
.global guest_boot
guest_boot:
    j guest_boot  # 무한 루프
```

튜토리얼에 나온 대로 Clang/LLVM으로 컴파일했습니다:

```bash
clang --target=riscv64-unknown-elf -ffreestanding -nostdlib \
    -Wl,-eguest_boot -Wl,-Ttext=0x100000 \
    guest.S -o guest.elf
llvm-objcopy -O binary guest.elf guest.bin
```

빌드 스크립트에 이 명령들을 추가했습니다.

메모리 매핑 코드는 이런 식으로 작성했습니다:

```rust
let kernel_image = include_bytes!("../guest.bin");
let guest_entry = 0x100000;

// 호스트 메모리에 게스트 코드 복사
let kernel_memory = alloc_pages(kernel_image.len());
unsafe {
    core::ptr::copy_nonoverlapping(
        kernel_image.as_ptr(),
        kernel_memory as *mut u8,
        kernel_image.len()
    );
}

// Guest page table에 매핑
let mut table = GuestPageTable::new();
table.map(guest_entry, kernel_memory as u64, PTE_R | PTE_W | PTE_X);

// CSR 설정 및 게스트 진입
let mut hstatus = 0;
hstatus |= 2u64 << 32;
hstatus |= 1u64 << 7;

unsafe {
    asm!(
        "csrw hstatus, {hstatus}",
        "csrw hgatp, {hgatp}",
        "csrw sepc, {sepc}",
        "sret",
        hstatus = in(reg) hstatus,
        hgatp = in(reg) table.hgatp(),
        sepc = in(reg) guest_entry,
    );
}
```

실행해보니:

```
Booting hypervisor...
v = ['a', 'b', 'c']
map: 00100000 -> 80309000
```

게스트 물리 주소 `0x100000`이 호스트 물리 주소 `0x80309000`에 매핑됐다고 나옵니다. 그리고 더 이상 trap이 발생하지 않고 멈춰있는 걸 보니, 게스트 코드가 무한 루프를 제대로 실행하고 있는 것 같습니다.

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

Chapter 7에서 드디어 게스트가 하이퍼바이저랑 통신하는 부분을 구현합니다. 게스트가 `ecall`로 하이퍼콜을 호출하면 하이퍼바이저가 받아서 처리하는 식입니다.

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
