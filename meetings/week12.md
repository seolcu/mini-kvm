# 12주차 상담내용 (11/18)

## 현황 보고

### 진행 상황

- Multi-instance 구현 완료 (최대 4개 VM 동시 실행)
- 각 vCPU별로 다른 게스트 프로그램 로드 가능
- 색상 코드로 출력 구분 (red, green, yellow, blue)
- 1K OS x86 포팅 진행 중 (Protected Mode with Paging 완료)

### 구현된 기능

- Pthread 기반 멀티 vCPU 실행
- Per-vCPU 메모리 격리 (각 256KB)
- Thread-safe 출력 (mutex 기반)
- Protected Mode + Paging 지원

### 데모 가능 구성

```bash
./kvm-vmm guest/multiplication.bin guest/counter.bin \
          guest/hello.bin guest/hctest.bin
```

4개의 서로 다른 프로그램이 병렬로 실행되며 색상으로 구분된 출력 생성

## 기술 논의

### 현재 이슈: Keyboard Input 처리

**문제 상황:**

- 1K OS에서 키보드 입력이 동작하지 않음
- IN 명령어가 VM exit를 발생시키지 않음
- OUT 명령어는 정상 동작 (hypercall 출력)
- 메모리 기반 통신도 cache coherency 문제로 실패

**근본 원인:**

호스트에서 키보드를 누르면 인터럽트가 Hypervisor로 전달되지만, Guest VM으로 injection되지 않음

### 해결 방안: Interrupt Injection

**아키텍처:**

```
Host Keyboard → Hypervisor Interrupt Handler
              → VM 선택 (VM 0번)
              → Guest로 Interrupt Injection
              → Guest Interrupt Handler 실행
```

**구현 단계:**

1. Hypervisor에서 키보드 인터럽트 감지 (select/poll)
2. 입력 문자를 버퍼에 저장
3. 대상 VM 결정 (Multi-instance 환경에서 VM 0번만 입력 받기)
4. KVM interrupt injection API 사용하여 Guest에 전달
5. Guest IDT를 통해 interrupt handler 실행

**참고 구현:**

- RISC-V hypervisor에 유사한 입력 포워딩 코드 존재
- 폴링 방식을 인터럽트 방식으로 변환 필요

### 구현할 인터럽트 종류

교수님 조언: "키보드 인터럽트랑 타이머 인터럽트 두 개만 일단 만들어 보시면 나머지는 다 구현이거든요. 나머지는 일의 양이 많은 거지 기술적 난이도는 낮을 거예요."

#### 1. Keyboard Interrupt

- Host stdin에서 키 입력 감지
- KVM_SET_INTERRUPT로 Guest에 전달
- 용도: Shell 명령어 입력, Echo 프로그램

#### 2. Timer Interrupt

- setitimer/timerfd로 주기적 인터럽트 생성
- Guest에 timer interrupt injection
- 용도: 시간 기반 애니메이션 (예: "Hello, World"를 1초마다 한 글자씩 출력)

이 두 인터럽트만 구현하면 나머지(disk I/O, network 등)는 같은 패턴을 따르면 됨

## 자율연구 포스터 발표 준비

### 일정

- **마감:** 11월 24일 (일요일)
- **리뷰:** 주말에 교수님께 초안 전송

### 포스터 구성

#### 1. 스크린샷 (전체의 1/3 ~ 1/2)

교수님 강조: "사람들이 지나가다 잘 모르거든요. 화면을 이쁘게 잘 만드셔야 돼요."

**포함할 내용:**

- Multi-instance 실행 화면 (4개 VM, 색상 구분)
- 1K OS 메뉴 시스템
- 각 프로그램 실행 결과

#### 2. 아키텍처 다이어그램

- Hypervisor 전체 구조
- Memory layout
- Interrupt flow

#### 3. 프로세스 설명

- VM 인스턴스 생성 과정
- Multi-instance 실행
- Interrupt injection 프로세스

#### 4. 기본 아이디어 (나머지 1/3 ~ 1/2)

- 프로젝트 목표
- 주요 기능
- 기술적 특징
- 응용 가능성

## 1K OS 데모 시나리오

### 메뉴 시스템 (VM 0에서 실행)

```
=== 1K OS on x86 KVM ===

Select program to run:
  1. Multiplication Table (2x1 ~ 9x9)
  2. Number Counter (0-9)
  3. Echo (repeat your input)
  4. Timer Hello (animated)

Choice: _
```

### I/O 라우팅

- VM 0번만 키보드 입력 받기
- 나머지 VM 1, 2, 3은 자동 실행
- 모든 출력은 하나의 콘솔에 색상 구분

교수님 조언: "그런 거는 문제 그런 거 하지 마시고 핵심적인 필요한 게 나은 게 중요하니까" (복잡한 화면 전환 구현하지 말 것)

### 구현 방식

- 1K OS 내부에 4개 모듈 구현
- Multiprocessing 없이 단일 프로세스로 처리
- 메뉴 선택에 따라 해당 함수 호출

## 성능 비교 실험

### 목표

KVM 기반 VMM vs QEMU emulation 성능 비교

### 주의사항

교수님 경고: "QEMU가 인스트럭션 바이 인스트럭션으로 에뮬레이션 하지 않고 블록 단위로 JIT 컴파일하기 때문에 성능 차이가 안 날 수도 있다"

**해결 방안:**

- QEMU에서 JIT 비활성화 옵션 찾기
- instruction-by-instruction mode 활성화
- 옵션이 없으면 결과에 설명 추가

## 이번 주(Week 12) 목표

### 필수 구현

1. **Keyboard Interrupt**
   - Host stdin 감지
   - KVM interrupt injection
   - Guest keyboard interrupt handler
   - 1K OS getchar() 동작 확인

2. **Timer Interrupt**
   - Host timer 설정
   - 주기적 Guest interrupt injection
   - Timer 기반 애니메이션 데모

3. **1K OS 메뉴 시스템**
   - 4가지 옵션 구현
   - 메뉴 선택 로직
   - 각 프로그램 모듈화

### 자율연구 포스터

- 스크린샷 촬영 및 편집
- 아키텍처 다이어그램 작성
- 프로세스 플로우 작성
- 레이아웃 디자인
- 교수님 리뷰 및 수정

### 선택 사항

- 성능 비교 실험 (KVM vs QEMU)

## 다음 미팅 예정

- 다음 주도 미팅 예정
- 포스터 발표 후 추가 개발 계획 논의
- 남은 몇 주간 추가 기능 구현 가능

## 요점 정리

### 교수 피드백

- "요거 한 번 키보드 인터럽트랑 타이머 인터럽트 두 개만 일단 만들어 보시면 나머지는 다 구현이거든요"
- "핵심적인 필요한 게 나은 게 중요하니까" (최소 기능 구현 집중)
- "화면을 이쁘게 잘 만드셔야 돼요" (포스터 시각적 요소 중요)

### 핵심 과제

- Interrupt injection 메커니즘이 전체 프로젝트의 핵심
- 이것만 성공하면 나머지는 같은 패턴으로 쉽게 확장
- 포스터는 기술적 세부사항보다 시각적 명확성 중요

### 우선순위

1. Keyboard + Timer interrupt 구현 (최우선)
2. 1K OS 메뉴 시스템 완성
3. 포스터 작성 및 리뷰
4. 성능 비교 (시간 되면)
