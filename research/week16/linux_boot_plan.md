# 16주차 계획 (마지막 주) — Linux 부팅 데모 목표

> week14 피드백(범용 하드웨어 지원 포기, 타겟 축소/정적 포함, 타이머/인터럽트 우선)을 그대로 반영해 “최소 기능으로 Linux 커널이 init까지 올라오는 것”을 목표로 잡는다.

## 0) 이번 주 최종 목표(데모 정의)

- **성공 기준**: `./kvm-vmm --linux <bzImage> --cmdline "...console=ttyS0..."` 실행 시,
  - 커널 부팅 로그가 **시리얼(COM1)** 로 출력되고
  - `init`(BusyBox 기반) 실행 → 쉘 프롬프트까지 진입(또는 `/init`에서 “BOOT OK” 출력 후 대기)
- **범위에서 제외(의도적으로 안 함)**: 디스크(ATA), 파일시스템(ext4 등), ACPI 테이블, PCI/virtio
- **타겟 하드웨어(고정)**: “QEMU-레거시와 유사한 최소 장치” 수준
  - 출력: 16550 UART(COM1, `0x3f8`)
  - 인터럽트/타이머: KVM in-kernel `IRQCHIP`(+ PIT 에뮬레이션) 사용
  - 나머지는 커널 커맨드라인으로 최대한 비활성화(ACPI/APIC/PCI 등)

## 0.5) 업데이트(현재 상태 요약)

- VMM 기능 추가 완료: `--initrd`, `--linux-entry setup|code32`, `--linux-rsi base|hdr`
- 메모리 레이아웃: setup=`0x10000`, kernel=`0x100000`, boot_params(zero page)=`0x90000`, initrd=`상단 메모리(동적 배치)`
- 최소 장치: COM1(0x3f8~0x3ff) UART 에뮬 + 레거시 포트 일부 + MMIO 기본 핸들러 + Linux 모드에서 `IRQCHIP` 강제
- **부팅 상태(달성)**:
  - `code32` 경로로 커널 로그 출력 → initramfs `/init` 실행 → `sh-5.3#` 프롬프트까지 확인
  - userspace 출력/입력을 위해 COM1 `IRQ4`의 TX/RX pulse를 최소 구현
  - 프로젝트 내부에서 재현 가능한 최소 initramfs 빌더(`kvm-vmm-x86/tools/mkinitramfs.sh`) 추가

## 1) 현 상태에서 Linux 부팅이 막히는 포인트(정리)

- (기반은 갖춤) IRQCHIP/UART/메모리 확장까지는 정상 동작.
- (남은 핵심) **Linux boot protocol의 “진입 CPU 상태/레지스터/zero page 전달”을 커널이 기대하는 형태로 맞추는 것**이 현재 병목.
- 특히 `ESI`(= `RSI`)가 가리키는 구조(boot_params vs setup_header)와 커널이 초기 스택을 잡는 방식이 맞지 않으면,
  - 잘못된 물리주소로 접근(→ MMIO 폭주)
  - 예외 핸들러/IDT 설정 구간에서 triple fault(→ 리셋)
  로 이어진다.

## 2) 1주 완주용 로드맵(구현 우선순위)

### A. Linux 부트 프로토콜을 “가장 단순한 형태”로 맞추기

1. **진입점 우선순위: code32 직접 진입**
   - BIOS 의존성을 회피하기 위해 기본은 `code32_start` 직접 진입을 목표로 한다.
   - 실모드 setup 경로는 “정확한 DS:SI/IVT/BIOS 가정”을 맞춰야 하므로 플랜 B로 둔다.
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

> 현재 상태: M3 달성, M4도 최소 수준으로 달성(키 입력을 COM1으로 전달).

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
