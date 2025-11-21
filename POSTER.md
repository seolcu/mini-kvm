# Mini-KVM: 초소형 가상 머신 모니터 개발

## 프로젝트 개요

**목표**: Linux KVM API를 이용한 교육용 초소형 VMM 개발

**주요 특징**:
- Multi-vCPU 지원 (최대 4개 동시 실행)
- Real Mode / Protected Mode with Paging
- Interrupt injection (Keyboard + Timer)
- 1K OS 포팅 완료
- 2,800 라인의 간결한 코드

---

## 시스템 아키텍처

```
┌─────────────────────────────────────────────────────────┐
│                    Host (Linux)                          │
│  ┌───────────────────────────────────────────────────┐  │
│  │          KVM VMM (main.c)                         │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐       │  │
│  │  │ Keyboard │  │  Timer   │  │ Hypercall│       │  │
│  │  │  Thread  │  │  Thread  │  │  Handler │       │  │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘       │  │
│  │       │             │              │              │  │
│  │       └─────────────┴──────────────┘              │  │
│  │                     │                              │  │
│  │       ┌─────────────┴─────────────┐               │  │
│  │       │   Interrupt Injection     │               │  │
│  │       └─────────────┬─────────────┘               │  │
│  └─────────────────────┼─────────────────────────────┘  │
│                        │                                 │
│  ┌─────────────────────┼─────────────────────────────┐  │
│  │              Linux KVM Module                     │  │
│  └─────────────────────┼─────────────────────────────┘  │
└────────────────────────┼─────────────────────────────────┘
                         │ Hardware Virtualization
                         │ (VT-x / AMD-V)
┌────────────────────────┼─────────────────────────────────┐
│                   Guest VM                               │
│  ┌─────────────────────────────────────────────────┐    │
│  │ vCPU 0      │ vCPU 1      │ vCPU 2  │ vCPU 3    │    │
│  │ 4MB Memory  │ 4MB Memory  │ 4MB     │ 4MB       │    │
│  │ 0x00000     │ 0x400000    │ 0x80000 │ 0xC00000  │    │
│  └─────────────────────────────────────────────────┘    │
│                                                           │
│  Guest Programs:                                         │
│  - hello.bin (Real Mode assembly)                        │
│  - counter.bin (0-9 loop)                                │
│  - multiplication.bin (구구단)                            │
│  - kernel.bin (1K OS - Protected Mode with Paging)       │
└───────────────────────────────────────────────────────────┘
```

---

## 주요 구현 기능

### 1. Multi-vCPU 지원
- **Pthread 기반** 멀티 스레딩
- **메모리 격리**: 각 vCPU당 4MB 독립 메모리
- **Thread-safe 출력**: Mutex 기반 동기화
- **색상 구분**: vCPU별 출력 색상 (red, green, yellow, blue)

### 2. Interrupt Injection
- **Keyboard Interrupt** (IRQ 1 / Vector 0x21)
  - Stdin 모니터링 스레드
  - 256 바이트 circular buffer
  - 비동기 입력 처리
  
- **Timer Interrupt** (IRQ 0 / Vector 0x20)
  - 10ms 주기 타이머
  - 주기적 인터럽트 생성
  - Guest OS 스케줄링 지원

### 3. 1K OS 포팅 (Protected Mode)
- **Protected Mode with Paging**
  - GDT/IDT 설정
  - 2-level paging (4MB PSE pages)
  - Identity mapping + Higher-half kernel
  
- **Process Management**
  - Idle process (PID 0)
  - Shell process (PID 1)
  - Context switching
  
- **File System**
  - TAR format 지원
  - 프로그램 로딩
  
- **Interactive Shell**
  - 메뉴 기반 인터페이스
  - 4가지 데모 프로그램

### 4. Hypercall Interface
```c
#define HC_EXIT       0x00   // Guest 종료
#define HC_PUTCHAR    0x01   // 문자 출력 (BL)
#define HC_GETCHAR    0x02   // 문자 입력 (AL)
```

---

## 실행 예시

### Single vCPU (Hello World)
```bash
$ ./kvm-vmm guest/hello.bin
Hello, KVM!
```

### Multi-vCPU (4개 동시 실행)
```bash
$ ./kvm-vmm guest/multiplication.bin guest/counter.bin \
              guest/hello.bin guest/hctest.bin

# 4개 프로그램이 동시에 실행되며 색상으로 구분된 출력
[vCPU 0] 2 x 1 = 2
[vCPU 1] 0
[vCPU 2] Hello, KVM!
[vCPU 3] Hypercall test: OK
[vCPU 0] 2 x 2 = 4
[vCPU 1] 1
...
```

### 1K OS (Protected Mode)
```bash
$ ./kvm-vmm os-1k/kernel.bin

=== 1K OS x86 ===
Booting in Protected Mode with Paging...
Interrupt handlers registered
  Timer (IRQ 0, vector 0x20)
  Keyboard input via HC_GETCHAR hypercall
Filesystem initialized
Created idle process (pid=0)
Created shell process (pid=1)
=== Kernel Initialization Complete ===
Starting shell process (PID 1)...

=== 1K OS Menu ===
  1. Multiplication (2x1 ~ 9x9)
  2. Counter (0-9)
  3. Echo (echo your input)
  4. About 1K OS
  0. Exit

Select: _
```

---

## 성능 측정 결과

### VM 실행 성능
| 프로그램 | 실행 시간 | VM Exits | 비고 |
|---------|----------|----------|------|
| Hello World | < 10 ms | 13 | 문자 출력 12회 + HLT |
| Counter (0-9) | 24 ms | ~100 | 주로 Hypercall exits |
| Multi-vCPU (4개) | < 50 ms | ~400 | 병렬 실행, 간섭 최소 |

### 오버헤드 분석
- **VM 초기화**: < 10ms
- **Hypercall 처리**: ~0.8ms per call
- **vCPU 스레드 생성**: < 1ms per vCPU
- **인터럽트 injection**: ~1-10ms latency

### KVM vs QEMU (추정)
- **KVM VMM**: Near-native speed
- **QEMU TCG**: 30-100x slower (emulation mode)
- **QEMU + KVM**: Similar performance, more features

---

## 기술적 도전 과제

### 1. Keyboard Input 처리
**문제**: IN 명령어가 KVM에서 VM exit를 발생시키지 않음

**해결**: 
- Hypercall 기반 getchar() 구현
- Polling 방식으로 간소화
- Blocking I/O 지원

### 2. Protected Mode 전환
**문제**: Real Mode → Protected Mode 전환 시 GDT/IDT 설정

**해결**:
- Boot code에서 GDT/IDT 테이블 구성
- 페이지 테이블 설정 (4MB PSE)
- CR0/CR3 레지스터 설정

### 3. Multi-vCPU Memory Isolation
**문제**: 각 vCPU가 독립된 메모리 공간 필요

**해결**:
- Per-vCPU memory allocation (mmap)
- KVM_SET_USER_MEMORY_REGION으로 격리
- 각 vCPU는 0x0부터 시작하는 독립 주소 공간

---

## 코드 구조

```
mini-kvm/
├── kvm-vmm-x86/           # x86 KVM VMM
│   ├── src/
│   │   └── main.c         # VMM 핵심 코드 (~1,500 LOC)
│   ├── guest/             # Real Mode guest 프로그램
│   │   ├── hello.S
│   │   ├── counter.S
│   │   └── multiplication.S
│   └── os-1k/             # 1K OS (Protected Mode)
│       ├── boot.S         # Boot loader
│       ├── kernel.c       # Kernel (~900 LOC)
│       └── shell.c        # Shell (~100 LOC)
├── hypervisor/            # RISC-V Hypervisor (참고용)
└── HLeOs/                 # Toy OS (참고용)
```

**총 코드량**: ~2,800 lines (주석 제외)

---

## 교육적 가치

### 학습 주제
1. **가상화 기초**
   - Hardware-assisted virtualization (VT-x)
   - VM entry/exit 메커니즘
   - Memory virtualization (EPT/NPT)

2. **OS 개념**
   - Real Mode vs Protected Mode
   - Interrupt handling
   - Process management
   - Memory management (Paging)

3. **System Programming**
   - KVM API 활용
   - Multi-threading (pthread)
   - Assembly language (x86)
   - Bare-metal programming

### 장점
- **간결한 코드**: 핵심 기능만 구현 (QEMU의 1/100 크기)
- **쉬운 이해**: 명확한 구조, 풍부한 주석
- **실습 가능**: 실제 동작하는 완전한 시스템
- **확장 가능**: 추가 기능 구현 용이

---

## 향후 개선 방향

### 단기
- [ ] Full keyboard scancode 지원 (현재: 숫자/Enter/Space만)
- [ ] Disk I/O emulation (block device)
- [ ] VGA text mode 출력
- [ ] 더 정밀한 timer (< 10ms)

### 중기
- [ ] vCPU 간 동기화 (Mutex/Semaphore)
- [ ] Shared memory 지원
- [ ] NUMA awareness
- [ ] Performance profiling tools

### 장기
- [ ] Full OS 포팅 (Linux/xv6)
- [ ] Graphics output (framebuffer)
- [ ] Network interface (virtio-net)
- [ ] PCIe device emulation

---

## 결론

**성과**:
- ✅ Multi-vCPU 지원 VMM 완성
- ✅ Interrupt injection 구현
- ✅ 1K OS 포팅 성공
- ✅ 교육용으로 최적화된 간결한 코드

**기대 효과**:
- OS 개발 교육에 활용
- 가상화 기술 학습 자료
- 연구 프로토타입 기반

**개발 기간**: 13주 (2024.09 - 2024.11)

**GitHub**: https://github.com/seolcu/mini-kvm

---

## References

1. Linux KVM API Documentation  
   https://www.kernel.org/doc/html/latest/virt/kvm/api.html

2. Intel Software Developer Manual  
   Volume 3: System Programming Guide

3. OSDev Wiki - Protected Mode  
   https://wiki.osdev.org/Protected_Mode

4. 1K OS Project  
   https://github.com/seolcu/mini-kvm/tree/main/kvm-vmm-x86/os-1k

---

**연구자**: 서울대학교 컴퓨터공학부  
**지도교수**: [교수님 성함]  
**프로젝트 기간**: 2024.09 - 2024.11
