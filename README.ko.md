# Mini-KVM: 자기 커널을 만드는 사람을 위한 x86 하이퍼바이저

> 2025-2 Ajou SoftCon 연구부문 장려상 수상

Linux KVM API 위에 약 4,000줄의 C로 작성한 x86 하이퍼바이저입니다.
게스트를 실행하고, **왜 죽었는지 설명하는 것**을 목표로 합니다.

[English README](README.md) · [MIT License](LICENSE)

> **현재 상태: 초기 단계.** 지금은 Mini-KVM 전용으로 작성된 게스트(리얼 모드
> 프로그램들과 내장 32비트 교육용 OS)만 실행됩니다. ELF/Multiboot 커널 로딩은
> 아직 지원하지 않고, Linux 부팅도 미완성입니다. 정확한 동작 범위와 로드맵은
> [영문 README](README.md#current-state)를 참고하세요.
>
> 아래 문서는 프로젝트 초기(대학 과제 제출 시점) 기준이며 일부 내용이
> 현재 코드와 다릅니다. 최신 사실은 영문 README와 `AGENTS.md`가 기준입니다.

- 과목명: 자기주도프로젝트
- 주제: 리눅스 KVM API를 이용한 초소형 가상 머신 모니터(VMM) 개발
- 담당 교수: 김상훈

---

## 프로젝트 개요

Mini-KVM은 Linux KVM API를 사용하여 핵심 가상화 개념을 시연하는 교육용 하이퍼바이저입니다. 약 4,000줄의 C 코드로 이루어진 작은 크기에도 불구하고 다음과 같은 기능을 지원합니다.

- **다중 vCPU**: 최대 4개의 가상 CPU를 동시에 실행
- **리얼 모드 게스트**: 간단한 16비트 x86 프로그램 실행
- **보호 모드 및 페이징**: 32비트 운영체제 '1K OS' 완벽 지원
- **9개의 사용자 프로그램**: 수학/유틸리티 프로그램을 포함한 대화형 셸
- **네이티브에 가까운 성능**: 최소한의 가상화 오버헤드

이 프로젝트는 완전한 기능의 하이퍼바이저를 합리적인 시간 안에 처음부터 이해하고 구축할 수 있음을 증명합니다.

---

## 주요 기능

### VMM 핵심 기능
- **다중 vCPU 지원**: 최대 4개의 게스트 프로그램을 병렬로 실행
  - 색상 출력으로 vCPU 구분 (빨강/초록/노랑/파랑)
  - 진짜 병렬 실행: 출력이 실시간으로 섞임
- **리얼 모드 (16비트)**: 레거시 x86 코드 직접 지원
  - 조건부 IRQCHIP: 불필요한 인터럽트 없이 즉시 실행/종료
- **보호 모드 (32비트)**: 세그멘테이션 및 페이징 완벽 지원
  - 4MB 페이지 (PSE), GDT/IDT 완벽 지원
- **인터럽트 처리**: 타이머 및 키보드 인터럽트
  - Protected Mode에서만 활성화 (성능 최적화)
- **하이퍼콜 인터페이스**: 효율적인 게스트-호스트 통신
  - PUTCHAR, GETCHAR, EXIT 하이퍼콜
- **I/O 에뮬레이션**: UART 시리얼 포트, 키보드 입력

### 게스트 운영체제
1. **리얼 모드 게스트** (8개 프로그램)
   - `minimal.bin`: 가장 간단한 1바이트 HLT 명령어 게스트
   - `hello.bin`: UART를 통해 "Hello, KVM!" 출력
   - `counter.bin`: 0부터 9까지 카운트
   - `multiplication.bin`: 하이퍼콜을 이용한 구구단 출력
   - `multiplication_short.bin`: 간단한 구구단 (2-4단)
   - `fibonacci.bin`: 피보나치 수열 생성기
   - `matrix.bin`: 행렬 연산 데모
   - `hctest.bin`: 하이퍼콜 테스트 모음

2. **1K OS** (보호 모드)
   - **9개의 대화형 프로그램**:
     1. 구구단 (2단 ~ 9단)
     2. 카운터 (0~9)
     3. 에코 (대화형 입출력)
     4. 피보나치 수열 (처음 15개 숫자)
     5. 소수 (100까지)
     6. 계산기 (+, -, *, /)
     7. 팩토리얼 (0! ~ 12!)
     8. 최대공약수 (유클리드 호제법)
     9. 1K OS 정보
   - GDT/IDT를 포함한 커널 공간
   - 시스템 콜을 통한 사용자 공간 프로그램
   - 하이퍼콜 기반 I/O
   - 타이머 인터럽트

---

## 빠른 시작

`kvm-vmm-x86` 디렉토리의 `Makefile`을 통해 모든 빌드와 실행을 한번에 관리할 수 있습니다.

### 사전 요구사항
```bash
# Fedora/RHEL
sudo dnf install gcc make binutils qemu-kvm

# Ubuntu/Debian
sudo apt install gcc make binutils qemu-kvm

# KVM 지원 확인
lsmod | grep kvm
ls -l /dev/kvm
```

### 빌드 및 실행

```bash
# 저장소 복제
git clone https://github.com/seolcu/mini-kvm.git
cd mini-kvm/kvm-vmm-x86

# 모든 컴포넌트 빌드 (VMM, 게스트, 1K OS)
make all

# 사용법 보기 (모든 명령어 확인)
make help
```

### 실행 예제

빌드 후 `./kvm-vmm` 바이너리로 게스트를 직접 실행합니다.

**1. 단일 게스트 실행 (리얼 모드)**
```bash
# "Hello, KVM!" 출력
./kvm-vmm guest/hello.bin

# 0-9 카운터
./kvm-vmm guest/counter.bin

# 2-9단 구구단
./kvm-vmm guest/multiplication.bin

# 최소 게스트 (HLT)
./kvm-vmm guest/minimal.bin
```

**2. 다중 vCPU 병렬 실행** (색상으로 구분)
```bash
# 2개 게스트 동시 실행 (빨강/초록 색상)
./kvm-vmm guest/multiplication.bin guest/counter.bin

# 4개 게스트 동시 실행 (빨강/초록/노랑/파랑 색상)
./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin guest/minimal.bin
```

**3. 1K OS (보호 모드) 실행**
```bash
# 대화형 셸 실행
./kvm-vmm --paging os-1k/kernel.bin

# 구구단 프로그램 바로 실행 (프로그램 1 선택 후 종료)
printf '1\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin

# 에코 프로그램 실행 (프로그램 3 선택, 메시지 입력, quit 후 종료)
printf '3\nHello\nquit\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin

# 계산기 실행 (프로그램 6 선택, 10+5 계산, quit 후 종료)
printf '6\n10+5\nquit\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin
```

**4. Verbose 모드** (디버깅용)
```bash
# VM exit 및 하이퍼콜 상세 로그 출력
./kvm-vmm --verbose guest/hello.bin
```

---

## 아키텍처

### 시스템 개요
```
┌─────────────────────────────────────────────┐
│           사용자 공간 (호스트)              │
│  ┌───────────────────────────────────────┐  │
│  │  Mini-KVM VMM (main.c)                │  │
│  │  - VM 생성 및 관리                      │  │
│  │  - vCPU 스레드 (pthreads)               │  │
│  │  - I/O 처리 (UART, 하이퍼콜)            │  │
│  │  - 인터럽트 주입                        │  │
│  └───────────────────────────────────────┘  │
│              ↕ KVM ioctl()                   │
├─────────────────────────────────────────────┤
│           커널 공간 (호스트)                │
│  ┌───────────────────────────────────────┐  │
│  │  Linux KVM 모듈                       │  │
│  │  - 하드웨어 가상화 (Intel VT)           │  │
│  │  - VM exit 처리                       │  │
│  │  - 메모리 관리 (EPT)                    │  │
│  └───────────────────────────────────────┘  │
│              ↕ 하드웨어                      │
├─────────────────────────────────────────────┤
│           게스트 (가상 머신)                │
│  ┌───────────────────────────────────────┐  │
│  │  리얼 모드 게스트                     │  │
│  │  - 직접 x86 16비트 코드 실행          │  │
│  │  - UART I/O (port 0x3f8)              │  │
│  │  - 하이퍼콜 (port 0x500)                │  │
│  └───────────────────────────────────────┘  │
│                   또는                         │
│  ┌───────────────────────────────────────┐  │
│  │  1K OS (보호 모드)                    │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │ 커널 공간                       │  │  │
│  │  │ - GDT/IDT                       │  │  │
│  │  │ - 페이징 (4MB 페이지)             │  │  │
│  │  │ - 인터럽트 핸들러                 │  │  │
│  │  └─────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │ 사용자 공간                     │  │  │
│  │  │ - 셸 (9개 프로그램)             │  │  │
│  │  │ - 하이퍼콜을 통한 시스템 콜     │  │  │
│  │  └─────────────────────────────────┘  │  │
│  └───────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

---

## 프로젝트 구조

```
mini-kvm/
├── kvm-vmm-x86/              # 하이퍼바이저 본체
│   ├── src/                  # VMM 소스 (관심사별 모듈)
│   │   └── linux/            # 격리된 bzImage 부팅 지원
│   ├── guest/                # 리얼/보호/롱 모드 게스트 프로그램
│   ├── os-1k/                # 1K OS (내장 32비트 교육용 OS)
│   ├── examples/             # Mini-KVM 기능을 쓰지 않는 스톡 커널들
│   └── tools/                # smoke.sh, ktest, third-party.sh
└── docs/investigations/      # 왜 그 설정이어야 하는지에 대한 기록
```

---

## 문서

- **[README.md](README.md)**: 현재 상태, 동작 예시, 로드맵 (기준 문서)
- **[AGENTS.md](AGENTS.md)**: 아키텍처 불변조건과 모듈 구조
- **[CONTRIBUTING.md](CONTRIBUTING.md)**: 기여 방법
- **[docs/investigations/](docs/investigations/)**: `-march=i686`이나 PSE off처럼
  임의로 보이지만 실은 필수인 설정들의 근거

대학 과제 시절의 보고서·주간 노트·발표 자료는 저장소에서 제거했습니다.
git 히스토리에는 남아 있습니다 (`git log --all -- 결과보고서.md`).

---

## 라이선스

이 프로젝트는 MIT 라이선스 하에 배포됩니다. 자세한 내용은 [LICENSE](LICENSE) 파일을 참고하세요.
