# 아키텍처 변경 제안: RISC-V → x86 KVM 개발

**작성일**: 2025-11-10
**주차**: Week 10/16
**제안자**: 학생

---

## 1. 요약 (Executive Summary)

RISC-V 타겟으로 KVM VMM 개발을 진행하던 중 **크로스 컴파일 환경의 구조적 한계**에 직면했습니다.

**핵심 문제**: Fedora의 `riscv64-linux-gnu-gcc` 툴체인은 컴파일러만 제공하며, RISC-V 타겟용 표준 라이브러리(glibc, libgcc 등)가 없어 실질적인 C/Rust 프로그램 개발이 불가능합니다.

**제안**: 남은 6주 내 프로젝트 완성을 위해 **x86 타겟으로 개발**하고, KVM API 학습 및 하이퍼바이저 핵심 개념 습득이라는 학습 목표는 동일하게 달성합니다.

---

## 2. 기술적 문제 상황

### 2.1 Phase 1 (완료) ✅

- **성과**: RISC-V Linux 6.17.7 커널 + KVM 성공적으로 빌드 및 부팅
- **환경**: QEMU에서 H-extension 활성화, KVM 모듈 정상 작동 확인
- **소요 시간**: 약 2주

### 2.2 Phase 2 (현재 블로킹됨) ⚠️

**목표**: x86 호스트에서 RISC-V 타겟 KVM VMM 개발

**시도 1 - Rust 구현**:
```bash
$ cargo build --target riscv64gc-unknown-linux-gnu
# ERROR: cannot find Scrt1.o, libgcc_s, libc, etc.
```

**시도 2 - C 구현**:
```bash
$ riscv64-linux-gnu-gcc -o kvm-vmm src/main.c
# ERROR: stdio.h: 그런 파일이나 디렉터리가 없습니다
```

**근본 원인**:
- Fedora는 RISC-V 크로스 컴파일러(`riscv64-linux-gnu-gcc`)만 제공
- RISC-V sysroot(표준 라이브러리, 헤더 파일)는 미제공
- 검증 완료: `dnf search glibc | grep -i riscv` → 패키지 없음

**성공한 부분**:
- 게스트 코드(어셈블리, 36바이트): ✅ 컴파일 성공
- 이유: libc 의존성 없음

**실패한 부분**:
- VMM 코드(C/Rust): ❌ 컴파일 실패
- 이유: stdio.h, stdlib.h 등 표준 라이브러리 필요

### 2.3 대안 분석

#### Option A: **x86 타겟으로 개발** (제안안) ⭐
- **장점**:
  - 즉시 개발 가능 (툴체인 완비)
  - KVM API는 아키텍처 독립적 (학습 목표 달성)
  - 6주 내 완성 가능성 높음
- **단점**:
  - RISC-V 특화 학습 내용 일부 제외
  - 최종 결과물이 x86 전용

#### Option B: RISC-V 네이티브 컴파일
- **방법**: RISC-V Linux 내부에서 개발 도구 설치 후 컴파일
- **장점**: RISC-V 타겟 유지
- **단점**:
  - 현재 initramfs 크기: 2.1KB → 수백 MB rootfs 필요
  - gcc, make, 헤더 파일 등 모두 설치 필요
  - 추가 소요 시간: **최소 2-3주**
  - QEMU 내부 개발 속도 저하

#### Option C: 수동 툴체인 구축
- **방법**: crosstool-NG 등으로 RISC-V sysroot 직접 빌드
- **단점**:
  - 가장 시간 소모적 (3-4주 예상)
  - 디버깅 복잡성 증가
  - 프로젝트 본질과 무관한 작업

---

## 3. 학습 목표 달성 여부

### 3.1 원래 학습 목표
1. **하이퍼바이저 기본 개념** (가상화, VM, vCPU)
2. **KVM API 사용법** (ioctl, memory region, vCPU 관리)
3. **2단계 주소 변환** (Guest PA → Host PA)
4. **VM Exit 처리** (trap handling, hypercall)

### 3.2 x86 개발 시 목표 달성도

| 학습 목표 | x86 개발 | RISC-V 개발 | 비고 |
|----------|----------|-------------|------|
| KVM API | ✅ 100% | ✅ 100% | API는 아키텍처 독립적 |
| 가상화 개념 | ✅ 100% | ✅ 100% | 동일한 개념 적용 |
| 메모리 관리 | ✅ 100% | ✅ 100% | KVM_SET_USER_MEMORY_REGION 동일 |
| vCPU 관리 | ✅ 100% | ✅ 100% | KVM_RUN, VM exit 처리 동일 |
| 레지스터 초기화 | ⚠️ 90% | ✅ 100% | x86: RIP/RSP, RISC-V: PC/SP (개념 동일) |
| H-extension | ❌ 0% | ✅ 100% | RISC-V 특화 |

**결론**: 핵심 학습 목표의 **95% 이상**을 x86 개발로도 달성 가능

---

## 4. 일정 및 리스크 분석

### 4.1 현재 일정 상황
- **전체 기간**: 16주
- **현재 주차**: Week 10
- **남은 기간**: 6주
- **중간 보고**: Week 8 제출 완료
- **최종 보고**: Week 16 (12월 19일)

### 4.2 시나리오별 소요 시간

| 단계 | x86 개발 | RISC-V Option B | RISC-V Option C |
|------|----------|-----------------|-----------------|
| 환경 구축 | ✅ 완료 | 2-3주 | 3-4주 |
| VMM 개발 | 2주 | 2주 | 2주 |
| 테스트/디버깅 | 1주 | 2주 (QEMU 내) | 2주 |
| 문서화 | 1주 | 1주 | 1주 |
| **총 소요** | **4주** | **7-8주** | **8-9주** |
| **남은 여유** | **2주** ✅ | **-1~-2주** ❌ | **-2~-3주** ❌ |

### 4.3 리스크 평가

**x86 개발 (제안안)**:
- ✅ 확실한 완성 가능
- ✅ 버퍼 시간 2주 확보
- ✅ 예상치 못한 문제 대응 가능

**RISC-V 유지**:
- ❌ 일정 초과 가능성 높음
- ❌ 디버깅 시간 부족
- ❌ 최종 보고서 품질 저하 우려

---

## 5. Phase 1 성과 활용

RISC-V 환경 구축 과정에서 습득한 내용:

### 5.1 기술적 성과
- ✅ RISC-V 아키텍처 이해 (레지스터, 어셈블리)
- ✅ 커널 빌드 및 설정 경험
- ✅ KVM 모듈 설정 및 검증
- ✅ initramfs 생성 및 부팅 프로세스 이해
- ✅ QEMU 가상화 환경 활용

### 5.2 문서화 완료
- `research/week10/README.md`: Phase 1 전체 과정 상세 기록
- 커널 설정, 빌드 명령어, 문제 해결 과정 모두 문서화
- 향후 RISC-V 연구 시 재활용 가능

**결론**: Phase 1은 실패가 아닌 **귀중한 학습 자산**

---

## 6. 제안 사항

### 6.1 단기 계획 (Week 10-16)

**Week 10-11**: x86 KVM VMM 개발
- KVM 초기화 및 VM 생성
- 게스트 메모리 매핑
- vCPU 생성 및 레지스터 초기화

**Week 12**: 게스트 실행 및 VM Exit 처리
- KVM_RUN 루프 구현
- 기본 hypercall 처리 (console I/O)

**Week 13**: 확장 기능
- 멀티 vCPU 지원
- 간단한 디바이스 에뮬레이션 (선택)

**Week 14**: 테스트 및 안정화
- 다양한 게스트 코드 테스트
- 에러 처리 강화

**Week 15-16**: 문서화 및 최종 보고서
- 코드 정리 및 주석 추가
- 최종 보고서 작성

### 6.2 장기 계획 (선택 사항)

프로젝트 완성 후 시간이 허락한다면:
1. **x86 → RISC-V 포팅**: 학습 내용을 바탕으로 RISC-V 버전 재작성
2. **비교 연구**: x86 vs RISC-V 가상화 차이점 분석
3. **논문 작성**: 크로스 플랫폼 VMM 개발 경험 정리

---

## 7. 결론

### 7.1 핵심 메시지
- **문제**: RISC-V 크로스 컴파일 환경 부재로 개발 불가
- **해결**: x86 타겟으로 개발, 학습 목표 95% 달성 가능
- **근거**: 6주 내 완성 vs 일정 초과 리스크

### 7.2 요청 사항
RISC-V 타겟 개발을 **x86 타겟으로 변경**하여 프로젝트를 완성하고, 향후 여유가 있을 경우 RISC-V 포팅을 시도하는 것을 승인해주시기 바랍니다.

### 7.3 기대 효과
- ✅ 확실한 프로젝트 완성
- ✅ KVM 및 하이퍼바이저 핵심 학습 목표 달성
- ✅ 최종 보고서 품질 향상
- ✅ Phase 1의 RISC-V 학습 성과 보존

---

## 부록: 기술 검증 로그

### A.1 Rust 컴파일 시도
```bash
$ cd /home/seolcu/문서/코드/mini-kvm/kvm-vmm
$ cargo build --target riscv64gc-unknown-linux-gnu

error: linking with `riscv64-linux-gnu-gcc` failed
/usr/bin/riscv64-linux-gnu-ld: cannot find Scrt1.o: No such file or directory
/usr/bin/riscv64-linux-gnu-ld: cannot find -lgcc_s
/usr/bin/riscv64-linux-gnu-ld: cannot find -lutil
/usr/bin/riscv64-linux-gnu-ld: cannot find -lrt
/usr/bin/riscv64-linux-gnu-ld: cannot find -lpthread
/usr/bin/riscv64-linux-gnu-ld: cannot find -lm
/usr/bin/riscv64-linux-gnu-ld: cannot find -ldl
/usr/bin/riscv64-linux-gnu-ld: cannot find -lc
```

### A.2 C 컴파일 시도
```bash
$ cd /home/seolcu/문서/코드/mini-kvm/kvm-vmm
$ make all

Building VMM...
riscv64-linux-gnu-gcc -Wall -O2 -static -o kvm-vmm src/main.c -static
src/main.c:6:10: fatal error: stdio.h: 그런 파일이나 디렉터리가 없습니다
    6 | #include <stdio.h>
      |          ^~~~~~~~~
```

### A.3 패키지 검색
```bash
$ dnf search glibc | grep -i riscv
# (출력 없음)

$ dnf list installed | grep riscv
binutils-riscv64-linux-gnu.x86_64    2.43.50.20241212-2.fc43
gcc-c++-riscv64-linux-gnu.x86_64     14.2.1-6.fc43
gcc-riscv64-linux-gnu.x86_64         14.2.1-6.fc43
```

### A.4 게스트 코드 컴파일 (성공)
```bash
$ cd /home/seolcu/문서/코드/mini-kvm/kvm-vmm/guest
$ ./build.sh

Building guest code...
Guest binary info:
-rwxrwxr-x. 1 seolcu seolcu 36 11월 10 12:34 minimal.bin

minimal.elf:     파일 형식 elf64-littleriscv

Disassembly of section .text:

0000000000000000 <_start>:
   0:   04200513                li      a0,66
   4:   337010b7                lui     ra,0x13370
   8:   eef00637                lui     a2,0xdeadb
   c:   eef60613                addi    a2,a2,-273 # deadbeef <loop+0xdeadbee7>
  10:   00001137                lui     sp,0x1

0000000000000014 <loop>:
  14:   0000006f                j       14 <loop>
Guest code built successfully!
```

---

**작성자**: 학생
**승인 요청**: 지도 교수님
