# 16주차 계획 (마지막 주) — Linux 부팅 데모 목표

> week14 피드백(범용 하드웨어 지원 포기, 타겟 축소/정적 포함, 타이머/인터럽트 우선)을 그대로 반영해 “최소 기능으로 Linux 커널이 init까지 올라오는 것”을 목표로 잡는다.

## 0) 이번 주 최종 목표(데모 정의)

- **성공 기준**: `./kvm-vmm --linux <bzImage> --cmdline "...console=ttyS0..."` 실행 시,
  - 커널 부팅 로그가 **시리얼(COM1)** 로 출력되고
  - `init`(BusyBox 기반) 실행 → 쉘 프롬프트까지 진입(또는 `/init`에서 “BOOT OK” 출력 후 대기)
- **범위에서 제외(의도적으로 안 함)**: 디스크(ATA), 파일시스템(ext4 등), ACPI 테이블, PCI/virtio, initramfs “로더” 구현(=VMM이 initrd를 외부에서 로드/배치하는 기능)
- **타겟 하드웨어(고정)**: “QEMU-레거시와 유사한 최소 장치” 수준
  - 출력: 16550 UART(COM1, `0x3f8`)
  - 인터럽트/타이머: KVM in-kernel `IRQCHIP`(+ PIT 에뮬레이션) 사용
  - 나머지는 커널 커맨드라인으로 최대한 비활성화(ACPI/APIC/PCI 등)

## 1) 현 상태에서 Linux 부팅이 막히는 포인트(정리)

- `kvm-vmm-x86/src/main.c`의 `KVM_CREATE_IRQCHIP`가 비활성화되어 있어 **타이머/인터럽트가 사실상 없는 상태**.
- 시리얼은 `OUT 0x3f8`만 처리하고, Linux가 사용하는 16550 레지스터 읽기(`0x3f8~0x3ff`, 특히 LSR `+5`)가 없어 **커널 earlycon/console이 진행 중 폴링에서 막힐 가능성**이 큼.
- Linux boot path가 `code32_start`로 바로 점프 + (경로에 따라) paging/long-mode를 켜는 방식이라면, **Linux boot protocol 기대 CPU 상태**(real-mode setup 진입 또는 32-bit protected entry 조건)와 어긋날 수 있음.
- Linux+init 환경은 **4MB 게스트 메모리로는 거의 불가능**(커널+압축해제+initramfs를 고려하면 수십~수백 MB 권장).

## 2) 1주 완주용 로드맵(구현 우선순위)

### A. Linux 부트 프로토콜을 “가장 단순한 형태”로 맞추기

1. **진입점은 real-mode setup로 통일**
   - bzImage의 setup 섹터를 `0x90000`에 로드하고, vCPU를 **real mode** 상태로 두고 `CS:IP = 0x9000:0x0200`(= `0x90200`)로 진입.
   - boot params(“zero page”)는 setup가 올라간 영역 내(통상 `0x90000`)에 존재하므로, 필요한 필드를 **그 자리에서 채우는 방식**으로 단순화.
2. 필수 boot params 최소 세팅
   - E820 메모리 맵(저메모리/예약영역/상메모리)
   - `hdr.cmd_line_ptr` + cmdline 문자열 복사
   - (가능하면) `type_of_loader`, `heap_end_ptr`, `initrd_addr_max` 등 프로토콜 권장 필드

### B. 인터럽트/타이머: userspace 주입 대신 KVM IRQCHIP 사용

1. Linux 모드에서는 `KVM_CREATE_IRQCHIP`를 **반드시 활성화**
2. “IDT 미구현으로 triple fault” 문제는 1K OS 쪽 이슈이므로,
   - **Linux 모드에서만 IRQCHIP 사용**(플래그로 분기) → 이번 주 데모 리스크 최소화

### C. 16550 UART 최소 에뮬레이션(부팅 로그 확보)

1. `0x3f8~0x3ff` 포트의 IN/OUT 처리 추가(최소 세트)
   - `THR`(base+0) write → stdout에 출력
   - `LSR`(base+5) read → `THRE|TEMT` 항상 1로 반환(송신 가능)
   - 나머지 레지스터는 “그럴듯한 값”으로 고정 반환(또는 내부 상태 저장)
2. 커널 cmdline 추천(디버그/단순화)
   - `console=ttyS0 earlycon=uart,io,0x3f8,115200 loglevel=8`
   - 필요 시: `acpi=off noapic nolapic pci=off nokaslr panic=-1`

### D. 메모리 확장

- Linux 모드 전용으로 게스트 메모리를 **최소 128MB~256MB**로 상향.

## 3) “하드웨어 의존성 폭발”을 피하는 Linux 이미지 전략

week14 조언대로 “드라이버/루트FS 동적 로딩”을 피하기 위해, 이번 주는 아래 중 하나로 간다.

- **추천(가장 단순)**: 커널에 **built-in initramfs**를 포함(`CONFIG_INITRAMFS_SOURCE=...`)
  - VMM 입장에서는 “initrd 로더 기능”이 필요 없어짐(커널 파일 1개만 로드).
  - `/init`는 BusyBox 기반으로 구성하고, 성공 시 메시지 출력 후 `sh` 실행.
- 대안(리스크 큼): initramfs 없이 가려면 디스크/FS 드라이버/이미지 제공까지 필요해서 이번 주 범위를 초과.

## 4) 단계별 마일스톤/검증(하루 단위로 쪼개기)

1) **M1: 시리얼로 한 글자라도 출력**
- 게스트가 `earlycon`로 UART에 찍는 첫 로그가 호스트 stdout에 보임.

2) **M2: 커널 초기화 로그 진행**
- `Decompressing Linux...` 이후 로그가 계속 진행(무한 폴링/멈춤 없음).

3) **M3: init 실행 확인**
- `/init` 실행 메시지(예: `BOOT OK`) 또는 BusyBox 쉘 프롬프트 진입.

4) **M4(선택): 최소 상호작용**
- 입력은 이번 주 범위 밖으로 두되(수신 UART), 최소한 “부팅 성공 스크린샷/로그” 확보.

## 5) 리스크 & 즉시 전환 가능한 플랜 B

- **리스크: 부트 프로토콜 CPU 상태 불일치**
  - 플랜 B: real-mode setup 대신 “32-bit protected entry 조건”으로 진입(페이징 OFF), `ESI`로 boot_params 포인터 전달하는 방식으로 전환.
- **리스크: 인터럽트로 인한 early fault**
  - 플랜 B: cmdline에서 `noapic nolapic`로 최대한 단순화 + IRQCHIP는 유지(PIT 없이도 어느 정도 진행되는지 관찰).
- **리스크: 로그가 안 나와 디버깅 불가**
  - 플랜 B: UART 레지스터 에뮬레이션을 먼저 완성(LSR/IER/LCR 최소), `earlycon` 출력만 확보한 뒤 부트 프로토콜을 디버깅.

## 6) 작업 체크리스트(파일 기준)

- `kvm-vmm-x86/src/main.c`
  - Linux 모드에서 `KVM_CREATE_IRQCHIP` 활성화
  - Linux 모드 메모리 크기 상향
  - Linux 부팅 진입 레지스터/세그먼트(Real Mode setup 진입) 경로 추가
  - `KVM_EXIT_IO`에서 `0x3f8~0x3ff` IN/OUT 처리 추가
- `kvm-vmm-x86/src/linux_boot.c`
  - bzImage setup/커널 로드 이후 boot params를 “setup 영역(0x90000)”에 직접 채우는 형태로 정리

