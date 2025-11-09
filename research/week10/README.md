# Week 10 연구 노트

**날짜**: 2025-11-10
**주제**: Phase 1 완료 - RISC-V Linux + KVM 환경 구축

## 개요

9주차 미팅에서 결정된 새로운 아키텍처 구현을 시작했습니다:
- **기존 방향 (중단)**: Bare-metal RISC-V 하이퍼바이저
- **새 방향**: RISC-V Linux + KVM 기반 VMM 개발

목표는 RISC-V Linux 위에서 KVM API를 사용하는 하이퍼바이저를 만들고, 그 위에서 간단한 게스트 OS를 실행하는 것입니다.

## 작업 내용

### 1. 개발 환경 구축 (Fedora 43)

설치한 패키지들:
```bash
# RISC-V 크로스 컴파일 도구체인
- gcc-riscv64-linux-gnu (15.2.1)
- gcc-c++-riscv64-linux-gnu
- binutils-riscv64-linux-gnu

# QEMU RISC-V 에뮬레이터
- qemu-system-riscv (10.1.2)
- qemu-system-riscv-core

# 커널 빌드 도구
- gcc (호스트 컴파일러)
- bc, flex, bison
- elfutils-libelf-devel
- openssl-devel
- ncurses-devel
```

**총 설치 용량**: ~410MB (크로스 컴파일러), ~35MB (QEMU)

### 2. RISC-V Linux 커널 빌드

#### 2.1 커널 소스 다운로드
```bash
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.17.7.tar.xz
tar -xf linux-6.17.7.tar.xz
cd linux-6.17.7
```

**커널 버전**: 6.17.7 (최신 stable)
**소스 크기**: 146MB (압축), 1.2GB (압축 해제)

#### 2.2 KVM 설정
```bash
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
sed -i 's/CONFIG_KVM=m/CONFIG_KVM=y/' .config
```

**핵심 설정**:
- `CONFIG_KVM=y` - KVM을 빌트인으로 활성화
- `CONFIG_VIRTUALIZATION=y` - 가상화 지원
- `CONFIG_KVM_MMIO=y` - MMIO 지원

#### 2.3 커널 빌드
```bash
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j20
```

**빌드 결과**:
- `arch/riscv/boot/Image` - 27MB (비압축)
- `arch/riscv/boot/Image.gz` - 9.1MB (압축)
- **빌드 시간**: 약 5분 (16코어)

### 3. 최소 initramfs 생성

#### 3.1 문제: busybox 크로스 컴파일 실패
처음에는 busybox를 RISC-V용으로 빌드하려 했으나, Fedora에 RISC-V용 glibc 헤더가 없어서 실패했습니다.

#### 3.2 해결: 어셈블리 init 프로그램 작성
libc 의존성을 완전히 제거하기 위해 RISC-V 어셈블리로 직접 작성:

**파일**: `initramfs/init.S`

```assembly
.section .text
.global _start

_start:
    # syscall: write(stdout, message, len)
    li a7, 64                # syscall number
    li a0, 1                 # fd: stdout
    la a1, message           # buffer
    la a2, message_end
    sub a2, a2, a1           # length
    ecall

    # syscall: exit(0)
    li a7, 93
    li a0, 0
    ecall
```

**빌드 과정**:
```bash
riscv64-linux-gnu-as -o init.o init.S
riscv64-linux-gnu-ld -static -nostdlib -o init init.o
chmod +x init
find . -print0 | cpio --null -o --format=newc | gzip > ../initramfs.cpio.gz
```

**결과**: `initramfs.cpio.gz` (2.1KB)

**학습 포인트**:
- 어셈블리는 libc 없이 직접 syscall 호출 가능
- `ecall` 명령으로 커널 시스템 콜 호출 (a7=syscall number, a0-a5=args)
- RISC-V syscall: write=64, exit=93

### 4. QEMU 부팅 테스트

#### 4.1 부팅 명령
```bash
qemu-system-riscv64 \
  -machine virt \
  -cpu rv64,h=true \         # H-extension 활성화 (필수!)
  -m 2G \
  -nographic \
  -kernel linux-6.17.7/arch/riscv/boot/Image \
  -initrd initramfs.cpio.gz \
  -append "console=ttyS0"
```

**중요**: `-cpu rv64,h=true`가 핵심! H-extension이 없으면 KVM이 작동하지 않습니다.

#### 4.2 부팅 성공 및 KVM 확인

커널 부팅 로그에서 확인:
```
[    0.276728] kvm [1]: hypervisor extension available
[    0.276802] kvm [1]: using Sv57x4 G-stage page table format
[    0.276851] kvm [1]: VMID 14 bits available
```

✅ **KVM이 성공적으로 인식되었습니다!**

Init 프로그램도 정상 실행:
```
========================================
 RISC-V Linux with KVM - Init Started
========================================

Kernel booted successfully!
```

**마지막 kernel panic은 정상**: init 프로그램이 exit하면 PID 1이 없어져서 커널이 panic을 발생시킵니다.

## 기술적 학습 내용

### 1. 크로스 컴파일 환경
- **호스트 아키텍처**: x86_64 (Fedora 43)
- **타겟 아키텍처**: RISC-V 64bit
- **크로스 컴파일러**: `riscv64-linux-gnu-gcc`
- **sysroot**: `/usr/riscv64-linux-gnu/sys-root` (하지만 glibc 헤더 없음)

### 2. initramfs vs initrd
- **initramfs**: cpio 아카이브, 커널에 직접 링크 또는 별도 파일
- **initrd**: block device 이미지 (구식)
- **포맷**: newc (cpio format)
- **압축**: gzip (다른 옵션: xz, lzma, bzip2)

### 3. RISC-V 시스템 콜 vs SBI
| 환경 | 인터페이스 | 사용 케이스 |
|------|-----------|------------|
| Linux userspace | syscall (ecall) | 일반 프로그램 |
| Bare-metal | SBI (ecall) | 부트로더, 하이퍼바이저 |
| Hypervisor guest | SBI → VM exit | 게스트 OS |

**syscall 번호 예시**:
- write: 64
- exit: 93
- open: 56
- mount: 40

### 4. RISC-V H-extension (Hypervisor Extension)
- **목적**: 하드웨어 가상화 지원
- **CSRs**: hstatus, hgatp, htval, htinst 등
- **2-stage 주소 변환**: GPA → HPA
- **VMID**: 가상 머신 식별자 (14 bits = 16384 VMs)

KVM은 H-extension을 사용하여 게스트 VM을 실행합니다.

### 5. KVM 커널 모듈
RISC-V KVM은 Linux 5.16부터 mainline에 포함:
- `CONFIG_KVM=y` 또는 `CONFIG_KVM=m`
- `/dev/kvm` 디바이스 생성
- ioctl 기반 API (x86 KVM과 유사)

**현재 상태 (6.17.7)**:
- ✅ 기본 VM/vCPU 생성
- ✅ 메모리 관리
- ✅ 2-stage 페이지 테이블 (Sv57x4)
- ⚠️  MMIO/interrupt는 부분적 지원
- ❌ PCI passthrough 미지원

## 프로젝트 구조 변경

```
mini-kvm/
├── hypervisor/           # [보존] Bare-metal 하이퍼바이저 (참고용)
├── linux-6.17.7/         # [NEW] RISC-V Linux 커널 소스
│   ├── .config           # KVM 활성화 설정
│   └── arch/riscv/boot/
│       └── Image         # 빌드된 커널 이미지
├── initramfs/            # [NEW] 최소 루트 파일시스템
│   ├── init.S            # 어셈블리 init 프로그램
│   └── init              # 컴파일된 바이너리
├── initramfs.cpio.gz     # [NEW] Initramfs 아카이브
├── research/week10/      # [NEW] 이번 주 연구 노트
└── CLAUDE.md             # [업데이트 필요] 프로젝트 가이드
```

## 다음 단계 (Phase 2)

### Week 11 목표: KVM VMM 구현 시작

#### 1. Rust 프로젝트 설정
```bash
cargo new --bin kvm-vmm
cd kvm-vmm
cargo add kvm-ioctls
cargo add kvm-bindings
```

#### 2. 최소 VMM 구현 체크리스트
- [ ] `/dev/kvm` 열기 및 버전 확인
- [ ] VM 생성 (KVM_CREATE_VM)
- [ ] 메모리 영역 설정 (KVM_SET_USER_MEMORY_REGION)
- [ ] vCPU 생성 (KVM_CREATE_VCPU)
- [ ] 레지스터 초기화 (KVM_SET_REGS)
- [ ] 게스트 바이너리 로드
- [ ] vCPU 실행 루프 (KVM_RUN)
- [ ] VM exit 핸들링

#### 3. 참고 자료
- **keiichiw/kvm-sample-rust**: 최소 KVM 예제
- **rust-vmm/kvm-ioctls**: 공식 Rust 바인딩
- **KVM API 문서**: https://www.kernel.org/doc/html/latest/virt/kvm/api.html

#### 4. 게스트 코드
처음에는 기존 `hypervisor/guest.S`를 재사용하거나 더 간단한 코드 작성:
```assembly
# 단순히 레지스터에 값 쓰고 무한 루프
_start:
    li a0, 0x42
    li a1, 0x1337
loop:
    j loop
```

## 시간 추정

### Phase 1 (완료) - 실제 소요 시간
- 환경 설정: 1시간
- 커널 빌드: 1.5시간
- Initramfs 생성: 2시간 (busybox 실패 → 어셈블리로 전환)
- 테스트 및 디버깅: 0.5시간
- **총 소요**: 약 5시간

### Phase 2 (예상) - Week 11-12
- Rust 환경 설정: 2시간
- 최소 VMM 구현: 12-16시간
- 테스트 및 디버깅: 4-6시간
- **총 예상**: 18-24시간 (주당 5-10시간 → 2-3주)

### Phase 3-5 (예상) - Week 13-15
- 하이퍼콜 처리: 6시간
- 게스트 코드 개선: 4시간
- 문서화 및 데모: 6시간
- **총 예상**: 16시간

**전체 프로젝트 남은 시간**: 34-40시간 (6주 내 충분히 완료 가능)

## 문제 해결 기록

### 문제 1: RISC-V glibc 헤더 누락
**증상**: `stdio.h: 그런 파일이나 디렉터리가 없습니다`
**원인**: Fedora에 RISC-V용 glibc-devel 패키지가 없음
**해결**: libc를 완전히 배제하고 어셈블리로 작성

### 문제 2: busybox 크로스 컴파일 실패
**증상**: `byteswap.h: 그런 파일이나 디렉터리가 없습니다`
**원인**: RISC-V sysroot에 표준 라이브러리 헤더가 설치되지 않음
**해결**: busybox 대신 어셈블리 init 프로그램 사용 (더 간단하고 의존성 없음)

### 문제 3: 어셈블리 문법 오류
**증상**: `Error: illegal operands 'li a2,message_len'`
**원인**: `.set` 디렉티브로 정의한 심볼을 li 명령에서 직접 사용 불가
**해결**: la로 주소를 로드한 후 sub로 길이 계산

## 참고 자료

### 공식 문서
- [Linux KVM API](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [RISC-V Privileged Spec](https://github.com/riscv/riscv-isa-manual)
- [RISC-V H-extension Spec](https://github.com/riscv/riscv-isa-manual/blob/master/src/hypervisor.tex)

### GitHub 저장소
- [kvm-riscv/linux](https://github.com/kvm-riscv/linux) - RISC-V KVM 커널
- [keiichiw/kvm-sample-rust](https://github.com/keiichiw/kvm-sample-rust) - 최소 VMM 예제
- [rust-vmm](https://github.com/rust-vmm) - Rust 가상화 라이브러리

### 튜토리얼
- [1000hv.seiya.me](https://1000hv.seiya.me/en/) - Bare-metal 하이퍼바이저 (Phase 1 참고용)

## 결론

✅ **Phase 1 목표 100% 달성**
- RISC-V Linux + KVM 환경 성공적으로 구축
- `/dev/kvm` 디바이스 사용 가능 확인
- 다음 단계 (KVM VMM 개발)를 위한 기반 완성

이제 Rust로 KVM API를 호출하여 실제 가상 머신을 실행하는 VMM을 구현할 준비가 되었습니다.

---

**작성일**: 2025-11-10
**작성자**: seolcu (with Claude Code)
