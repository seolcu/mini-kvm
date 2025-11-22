# 🎬 Mini-KVM 시연 가이드

발표 시 직접 시연할 수 있는 완전한 가이드입니다.

---

## 📋 사전 준비

### 1. 디렉토리 이동
```bash
cd ~/문서/코드/mini-kvm/kvm-vmm-x86
```

### 2. 빌드 확인 (필요시)
```bash
# VMM 빌드
make vmm

# 1K OS 빌드 확인
cd os-1k && make && cd ..
```

---

## 🎯 시연 시나리오

### Demo 1: 기본 Real Mode 게스트들

#### 1-1. Minimal Guest (가장 단순한 1바이트 게스트)
```bash
./kvm-vmm guest/minimal.bin
```
**설명**: 단 1바이트(HLT 명령어)로 구성된 가장 단순한 게스트. VMM이 정상 동작하는지 확인.

**예상 결과**: 즉시 "Guest halted" 메시지와 함께 종료

---

#### 1-2. Hello Guest (문자열 출력)
```bash
./kvm-vmm guest/hello.bin
```
**설명**: "Hello, KVM!" 메시지를 UART 포트(0x3f8)로 출력하는 28바이트 게스트.

**예상 결과**: 
```
Hello, KVM!
```
출력 후 종료

---

#### 1-3. Counter Guest (숫자 출력)
```bash
./kvm-vmm guest/counter.bin
```
**설명**: 0부터 9까지 순차적으로 출력하는 18바이트 게스트.

**예상 결과**: 
```
0123456789
```
출력 후 종료

---

### Demo 2: 멀티 vCPU (★ 가장 인상적!)

#### 2개 게스트 동시 실행
```bash
./kvm-vmm guest/multiplication.bin guest/counter.bin
```

**설명**: 
- **vCPU 0**: Multiplication guest가 " x = " 출력 (hypercall 사용)
- **vCPU 1**: Counter guest가 "0123456789" 출력 (UART 사용)
- **핵심**: 두 출력이 인터리브되어 나타남 → 진짜 병렬 실행!

**예상 결과**:
```
0 x 1 = 2 3 4 5 6 7 8 9
```
(출력이 섞여서 나옴)

**강조 포인트**:
- 각 vCPU가 독립적인 256KB 메모리 공간 사용
- vCPU 0: GPA 0x00000, vCPU 1: GPA 0x40000
- Real Mode에서 CS 레지스터로 주소 지정 (CS×16 + IP)

---

#### 4개 게스트 동시 실행 (최대 확장성)
```bash
./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin guest/minimal.bin
```

**설명**: 4개 vCPU가 각각 다른 프로그램을 동시에 실행
- vCPU 0: Counter (0-9)
- vCPU 1: Hello
- vCPU 2: Multiplication
- vCPU 3: Minimal (즉시 종료)

**예상 결과**: 4개 프로그램의 출력이 모두 섞여서 나옴

---

### Demo 3: 1K OS (Protected Mode with Paging) - ★ 9가지 프로그램!

#### 3-1. 구구단 출력
```bash
printf "1\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**설명**: 
- Protected Mode + 페이징 활성화
- 4MB 메모리, GDT/IDT 설정
- 타이머 & 키보드 인터럽트 처리
- User space 프로그램 실행 (9개 프로그램!)

**예상 결과**:
```
=== 1K OS Menu ===
  1. Multiplication Table (2x1 ~ 9x9)
  2. Counter (0-9)
  3. Echo (interactive)
  4. About 1K OS
  5. Fibonacci Sequence
  6. Prime Numbers (up to 100)
  7. ASCII Art
  8. Factorial (0! ~ 12!)
  9. GCD (Euclidean Algorithm)
  0. Exit

Select: 1

=== Multiplication Table ===
2*1=2 2*2=4 2*3=6 ... 9*9=81
```

---

#### 3-2. 카운터
```bash
printf "2\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**예상 결과**:
```
=== Counter 0-9 ===
0 1 2 3 4 5 6 7 8 9
```

---

#### 3-3. 대화형 에코
```bash
printf "3\nHello World!\nThis is amazing!\nquit\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**설명**: 
- User space에서 OUT+IN hypercall로 키보드 입력 받기
- IOPL=3 설정으로 user space I/O 허용

**예상 결과**:
```
=== Echo Program (type 'quit' to exit) ===
$ Hello World!
Echo: Hello World!
$ This is amazing!
Echo: This is amazing!
$ quit
Exiting echo program
```

---

#### 3-4. About 1K OS
```bash
printf "4\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**예상 결과**:
```
=== About 1K OS ===
1K OS: Operating System in 1000 Lines
Ported from RISC-V to x86 Protected Mode
Features:
  - Protected Mode with Paging
  - Keyboard and Timer Interrupts
  - Simple Shell
  - User Programs: 9 demos

Mini-KVM VMM Project
Educational hypervisor using KVM API

Exiting shell...
Thank you for using 1K OS!
```

---

#### 3-5. Fibonacci 수열
```bash
printf "5\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**예상 결과**:
```
=== Fibonacci Sequence ===
Calculating Fibonacci numbers up to 89:
0 1 1 2 3 5 8 13 21 34 55 89
```

---

#### 3-6. 소수 찾기
```bash
printf "6\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**예상 결과**:
```
=== Prime Numbers up to 100 ===
2 3 5 7 11 13 17 19 23 29 
31 37 41 43 47 53 59 61 67 71 
73 79 83 89 97 
Total: 25 primes
```

---

#### 3-7. ASCII Art
```bash
printf "7\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**예상 결과**:
```
=== 1K OS ASCII Art ===
  ___  _  __   ___  ____  
 / _ \/ |/ /  / _ \/ __/  
/ // /   /  / // /\ \    
\___/_/|_/   \___/___/    
                          
Mini-KVM Educational VMM Project
```

---

#### 3-8. Factorial (0! ~ 12!) - ★ 신규!
```bash
printf "8\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**설명**: 
- 팩토리얼 계산 (0! 부터 12!)
- 13! 이상은 32비트 오버플로 경고

**예상 결과**:
```
=== Factorial Calculator ===
0! = 1
1! = 1
2! = 2
3! = 6
4! = 24
5! = 120
6! = 720
7! = 5040
8! = 40320
9! = 362880
10! = 3628800
11! = 39916800
12! = 479001600

Note: 13! and above overflow 32-bit integers
```

---

#### 3-9. GCD (최대공약수) - ★ 신규!
```bash
printf "9\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

**설명**: 
- 유클리드 호제법 (Euclidean Algorithm) 시연
- 5가지 예제 쌍 계산

**예상 결과**:
```
=== GCD Calculator (Euclidean Algorithm) ===
GCD(48, 18) = 6
GCD(100, 35) = 5
GCD(81, 27) = 27
GCD(123, 456) = 3
GCD(17, 19) = 1 (coprime)
```

---

## 💡 시연 팁

### 1. 출력 정리
디버그 메시지가 너무 많으면:
```bash
./kvm-vmm guest/counter.bin 2>&1 | grep -v "IO\|EXIT\|HC"
```

### 2. 타임아웃 설정
무한 대기 방지:
```bash
timeout 5s ./kvm-vmm --paging os-1k/kernel.bin < input.txt
```

### 3. 깔끔한 화면
시연 전에:
```bash
clear
```

---

## 🎤 발표 스크립트 예시

### Demo 2 (멀티 vCPU) 시연 시
```
"이제 가장 인상적인 부분인 멀티 vCPU를 보여드리겠습니다.
두 개의 완전히 독립적인 프로그램이 동시에 실행됩니다.

[명령어 입력]

보시다시피 두 프로그램의 출력이 섞여 나오는데,
이것은 진짜 병렬 실행이 일어나고 있다는 증거입니다.

각 vCPU는 256KB의 독립적인 메모리 공간을 가지고 있으며,
Real Mode의 세그먼트 주소 지정 방식을 사용합니다."
```

### Demo 3-3 (에코) 시연 시
```
"이 에코 프로그램은 특별한 의미가 있습니다.
오늘 발견하고 수정한 버그의 결과물이기 때문입니다.

문제는 user space의 getchar 함수가 OUT 명령어만 사용해서
입력을 전혀 받지 못했던 것이었습니다.

이를 OUT+IN 프로토콜로 수정하고,
IOPL=3을 설정해서 user space에서 I/O 명령어를 
실행할 수 있게 했습니다.

[명령어 입력]

이제 입력이 정상적으로 에코되는 것을 볼 수 있습니다."
```

---

## 🐛 문제 해결

### 문제: "Permission denied" 또는 KVM 접근 불가
```bash
# KVM 모듈 확인
lsmod | grep kvm

# 권한 확인
ls -l /dev/kvm

# 그룹 추가 (필요시)
sudo usermod -aG kvm $USER
```

### 문제: 게스트가 실행 안 됨
```bash
# 재빌드
make clean
make
```

### 문제: 1K OS 입력 안 받음
- 파일 리다이렉션 사용: `< input.txt`
- 충분한 타임아웃 설정: `timeout 10s`

---

## 📊 핵심 수치

발표 시 언급할 수 있는 통계:
- **코드 크기**: minimal 1바이트 ~ 1K OS 12KB
- **메모리**: Real Mode 256KB, Protected Mode 4MB
- **최대 vCPU**: 4개 동시 실행
- **하이퍼콜 종류**: 3가지 (PUTCHAR, GETCHAR, EXIT)
- **VM exits**: 간단한 게스트 ~10회, 1K OS ~5000회
- **1K OS 프로그램**: 9개 (구구단, 카운터, 에코, About, 피보나치, 소수, ASCII Art, 팩토리얼, GCD)

---

## ✅ 시연 체크리스트

시연 전 확인:
- [ ] 디렉토리 위치 확인 (`kvm-vmm-x86`)
- [ ] 빌드 완료 확인 (`./kvm-vmm` 파일 존재)
- [ ] 게스트 바이너리 존재 확인 (`ls guest/*.bin`)
- [ ] 1K OS 빌드 확인 (`os-1k/kernel.bin` 존재)
- [ ] 입력 파일 준비 (`/tmp/demo_input.txt`)
- [ ] 터미널 폰트 크기 확대 (가독성)
- [ ] 화면 녹화/캡처 준비 (선택)

---

**준비 완료! 성공적인 발표 되시길 바랍니다! 🚀**
