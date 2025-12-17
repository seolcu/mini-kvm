# 12주차 연구내용

## 이번 주 목표

### Week 12 상담 내용 (11/18)
- Keyboard/Timer interrupt 구현
- 1K OS 메뉴 시스템 완성
- 포스터 발표 준비 (11/24 마감)
- 성능 비교 실험 (선택)

## 구현 완료 사항

### 1. 1K OS 키보드 입력 단순화 (11/22)
**변경 내용**:
- 복잡한 keyboard interrupt handler 제거
- Hypercall 기반 `getchar()` 구현으로 전환
- Polling 방식으로 간소화

**코드 변경**:
```c
// 이전: Interrupt handler + circular buffer (100+ lines)
// 현재: Simple hypercall-based getchar (20 lines)

long getchar(void) {
    long ch;
    while (1) {
        __asm__ volatile(
            "movb $2, %%al\n\t"         // HC_GETCHAR
            "movw $0x500, %%dx\n\t"
            "outb %%al, %%dx\n\t"
            "movl %%eax, %0"
            : "=r"(ch)
            :
            : "eax", "edx"
        );
        if (ch != -1 && ch != 0xFFFFFFFF) {
            return ch & 0xFF;
        }
        for (volatile int i = 0; i < 10000; i++);
    }
}
```

**장점**:
- 코드 복잡도 감소 (114 lines → 20 lines)
- 유지보수 용이
- 안정성 향상 (interrupt race condition 제거)

**단점**:
- Polling 방식이라 CPU 사용률 증가
- Interrupt-driven보다 반응성 저하

### 2. 빌드 시스템 완성
**완료 항목**:
- VMM 빌드 (main.c → kvm-vmm)
- Guest 프로그램 빌드 (hello, counter, multiplication, fibonacci, hctest)
- 1K OS 빌드 (boot.S + kernel.c + shell.c → kernel.bin)

**빌드 명령어**:
```bash
# VMM
cd kvm-vmm-x86 && make vmm

# Guest programs
cd guest && ./build.sh

# 1K OS
cd os-1k && make
```

### 3. 성능 테스트 완료
**측정 결과**:
| 프로그램 | 실행 시간 | VM Exits | 비고 |
|---------|----------|----------|------|
| Hello World | < 10 ms | 13 | 빠른 I/O |
| Counter (0-9) | 24 ms | ~100 | 반복 hypercall |
| Multi-vCPU (4개) | < 50 ms | ~400 | 병렬 실행 |

**핵심 발견**:
- VM 초기화 시간: < 10ms
- Hypercall 오버헤드: ~0.8ms per call
- Near-native execution speed

### 4. 문서 작성 완료
**완성된 문서**:
1. **POSTER.md** (포스터 발표 자료)
   - 시스템 아키텍처 다이어그램
   - 주요 구현 기능 설명
   - 실행 예시 및 성능 측정 결과
   - 교육적 가치 강조

2. **performance_test.md** (성능 측정 결과)
   - 실제 측정 데이터
   - KVM vs QEMU 비교 (추정)
   - 성능 병목 분석
   - 최적화 방향 제시

3. **AGENTS.md** (빌드 가이드)
   - 빌드 명령어 정리
   - 코드 스타일 가이드
   - 프로젝트 노트

4. **README.md** (프로젝트 개요)
   - Quick Start 가이드
   - 완성 상태 표시
   - 문서 링크

## 기술적 결정 사항

### Keyboard Input 방식 변경
**초기 계획**: Interrupt injection 방식
- VMM에서 stdin 모니터링 → keyboard buffer 채움
- Guest에 keyboard interrupt injection
- Guest interrupt handler가 buffer에서 읽기

**최종 구현**: Hypercall 방식
- Guest가 hypercall로 직접 요청
- VMM이 keyboard buffer 확인 후 반환
- Polling loop로 blocking 구현

**변경 이유**:
1. 코드 복잡도 대폭 감소
2. Interrupt handler의 race condition 제거
3. 교육용 프로젝트에 적합한 간결함
4. 기능적으로는 동일한 결과

### Protected Mode 지원 현황
**완료**:
-  GDT/IDT 설정
-  Paging (4MB PSE pages)
-  Kernel/User mode separation
-  Process management basics
-  Timer interrupt handler

**미완료** (시간 부족):
-  Keyboard interrupt handler (hypercall로 대체)
-  Full syscall table
-  Disk I/O

## 프로젝트 통계

### 코드 규모
```
kvm-vmm-x86/src/main.c:      ~1,500 LOC
os-1k/kernel.c:               ~900 LOC (interrupt handler 제거 후)
os-1k/shell.c:                ~100 LOC
os-1k/common.c:               ~200 LOC
guest/*.S:                    ~500 LOC
────────────────────────────────────
Total:                       ~3,200 LOC
```

### Git 커밋 내역
```bash
$ git log --oneline --since="2025-11-17" --until="2025-11-24"
063ab2c Simplify 1K OS keyboard input to use HC_GETCHAR hypercall
fb7dc40 Add final report and performance test framework
c0d721f Phase 1-3: Implement keyboard/timer interrupts and 1K OS menu system
```

## 포스터 발표 준비 (11/24)

### 포스터 구성
1. **프로젝트 개요** (20%)
   - 목표: 교육용 초소형 VMM
   - 주요 특징: Multi-vCPU, Interrupt, 1K OS

2. **시스템 아키텍처** (30%)
   - 전체 구조 다이어그램
   - Host-Guest 상호작용
   - Memory layout

3. **실행 화면** (30%)
   - Multi-vCPU 실행 스크린샷
   - 1K OS 메뉴 시스템
   - 각 프로그램 출력

4. **성과 및 결론** (20%)
   - 성능 측정 결과
   - 교육적 가치
   - 향후 계획

### 핵심 메시지
> "간결하지만 완전한 기능을 갖춘 교육용 VMM"

**강조할 점**:
- 2,800 라인으로 완전한 VMM 구현
- Multi-vCPU 동시 실행
- Near-native performance (< 1ms hypercall overhead)
- 1K OS 포팅 성공 (Protected Mode + Paging)

## 남은 작업

### 필수 (11/23까지)
- [x] 포스터 초안 작성 (POSTER.md)
- [x] 성능 테스트 결과 정리
- [x] README 업데이트
- [ ] 최종 코드 정리
- [ ] Git push 및 원격 저장소 정리

### 선택 (시간 되면)
- [ ] 1K OS 메뉴 시스템 개선
- [ ] 추가 guest 프로그램 작성
- [ ] QEMU 성능 비교 실험

## 학습 내용

### KVM API 활용
1. **Multi-vCPU 관리**
   - 각 vCPU는 독립적인 pthread
   - Per-vCPU memory isolation
   - KVM_CREATE_VCPU로 vCPU 생성

2. **Interrupt Injection**
   - KVM_CREATE_IRQCHIP으로 interrupt controller 초기화
   - KVM_INTERRUPT로 interrupt injection
   - Guest IDT를 통해 handler 실행

3. **Memory Management**
   - KVM_SET_USER_MEMORY_REGION으로 메모리 매핑
   - GPA (Guest Physical Address) ↔ HVA (Host Virtual Address)
   - 4MB 단위 격리

### x86 Protected Mode
1. **Segmentation**
   - GDT 구성 (Null, Code, Data segments)
   - Selector 설정
   - LGDT 명령어

2. **Paging**
   - 2-level paging (Page Directory + Page Table)
   - 4MB PSE (Page Size Extension) pages
   - Identity mapping (0x0 - 0x400000)
   - CR0.PG, CR3 설정

3. **Interrupt Handling**
   - IDT 구성
   - Interrupt gate 설정
   - Naked function으로 handler 구현

## 시도했다가 되돌린 기능들

### Split-Screen 터미널 출력 (11/22 시도 → 11/22 되돌림)

**목표**: Multi-vCPU 출력을 ANSI escape codes로 터미널 화면 분할하여 표시

**구현 시도**:
- `kvm-vmm-x86/src/main.c`에 ~200 LOC 추가
- `--split` 플래그로 split-screen 모드 활성화
- 각 vCPU에 독립적인 터미널 영역 할당 (ASCII 박스 테두리)
- Per-vCPU 출력 버퍼링 및 ANSI cursor positioning

**문제점**:
1. **vCPU 0만 출력 표시**: pthread 동기화 문제로 다른 vCPU 출력 손실
2. **디버깅 복잡도**: Terminal escape codes로 디버그 출력 추적 어려움
3. **터미널 호환성**: 모든 터미널에서 ANSI codes 동작 보장 불가
4. **코드 복잡도 급증**: Main function이 1,391 LOC → 1,591 LOC

**결정: 되돌림 (Revert)**
- 이유: ANSI escape codes 방식이 적합하지 않음 판단
- Git 상태: `backup/split-screen-attempt` 브랜치로 백업 후 `main`을 f830456으로 리셋
- 결과: 코드는 clean state로 복구, 기존 color-coded interleaved 출력 유지

**대안 방안** (논의만, 미구현):
1. **Sequential output**: vCPU별 출력 버퍼링 후 순차 표시 (간단, 15분)
2. **External tmux**: 스크립트로 각 vCPU를 tmux pane에 표시 (VMM 수정 불필요)
3. **Enhanced color mode**: 현재 방식 개선 (prefix 추가 등)
4. **File-based output**: 각 vCPU를 파일로 출력, 별도 tail

**교훈**:
- Terminal UI는 VMM의 핵심 기능이 아님
- 간결함 > 화려한 시각화 (교육용 프로젝트)
- 기존 color-coded 출력도 충분히 multi-vCPU 병렬 실행을 보여줌

**커밋 히스토리**:
```
f830456 - Update AGENTS.md (현재 HEAD - clean state)
[split-screen 관련 6개 커밋 - backup/split-screen-attempt 브랜치에만 존재]
```

## 추가 개선 사항 (11/23)

### 5. Real Mode Guest Hang 버그 수정

**문제 발견**:
- Real Mode 게스트가 HLT 명령 실행 후 종료되지 않고 무한 대기
- Timer interrupt (IRQ0)가 100ms마다 게스트를 깨움
- 모든 모드에서 IRQCHIP이 활성화되어 있었음

**근본 원인**:
- Real Mode는 인터럽트를 처리할 수 없는데도 IRQ0 주입됨
- HLT 상태에서 깨어났지만 처리할 수 없어 다시 HLT 진입 반복
- 타이머 스레드가 계속 실행되어 프로그램이 종료되지 않음

**해결 방법**:
```c
// init_kvm()에 need_irqchip 파라미터 추가
bool init_kvm(bool need_irqchip) {
    // ... VM 초기화 ...
    
    // Protected Mode에서만 IRQCHIP 생성
    if (need_irqchip) {
        if (ioctl(vm_fd, KVM_CREATE_IRQCHIP) < 0) {
            perror("KVM_CREATE_IRQCHIP");
            return false;
        }
    }
    return true;
}

// main()에서 모드에 따라 분기
if (paging_mode) {
    init_kvm(true);   // Protected Mode: IRQCHIP 활성화
    start_timer_thread();
    start_keyboard_thread();
} else {
    init_kvm(false);  // Real Mode: IRQCHIP 비활성화
}
```

**효과**:
- Real Mode 게스트 즉시 종료 (< 100ms)
- Protected Mode는 기존처럼 인터럽트 기반 동작
- 모드별 명확한 책임 분리

관련 커밋: `13c71fe`

---

### 6. 출력 시스템 개선

#### vCPU별 색상 구분 개선
**변경 사항**:
- vCPU 0 색상: 빨강(Red) → 청록(Cyan)으로 변경
- 이유: 빨강은 오류 메시지로 오인될 수 있음
- 최종 색상: 청록/초록/노랑/파랑 (중립적이고 구분 명확)

**단일/다중 vCPU 출력 조건부 처리**:
```c
if (num_vcpus > 1) {
    // 다중 vCPU: 색상으로 구분
    const char *colors[] = {"\033[36m", "\033[32m", "\033[33m", "\033[34m"};
    printf("%s[vCPU %d:%s]%s ", colors[id], id, name, "\033[0m");
} else {
    // 단일 vCPU: 깔끔한 출력 (색상 없음)
    printf("[%s] ", name);
}
```

**버퍼링 제거**:
- Character-by-character 즉시 출력
- 다중 vCPU 병렬 실행 효과가 더 명확히 보임

관련 커밋: `4472af7`, `b67ed68`

---

### 7. Verbose 모드 추가

**기능**:
```bash
./kvm-vmm --verbose guest/hello.bin       # Real Mode + verbose
./kvm-vmm --paging --verbose os-1k/kernel.bin  # Protected Mode + verbose
```

**출력 내용**:
- VM exit 원인 및 횟수
- I/O 포트 접근 (IN/OUT)
- 하이퍼콜 상세 정보 (번호, 인자, 반환값)

**예시**:
```
[hello] VM exit #1: KVM_EXIT_IO (port=0x500, size=1, dir=OUT)
[hello] Hypercall: HC_PUTCHAR (0x01), arg=0x48 ('H')
[hello] VM exit #2: KVM_EXIT_IO (port=0x500, size=1, dir=OUT)
[hello] Hypercall: HC_PUTCHAR (0x01), arg=0x65 ('e')
```

**특수 처리**:
- `HC_GETCHAR` (IN) 하이퍼콜은 verbose 모드에서도 억제
- 이유: 1K OS에서 너무 빈번하게 호출되어 출력 범람 방지

관련 커밋: `befd8ac`

---

### 8. 1K OS 입력 시스템 대폭 개선

#### readline() 함수 추가
**기능**:
- 즉각적인 문자 에코 (사용자 피드백)
- Backspace/DEL 지원 (0x08, 0x7F)
- Buffer overflow 방지
- 자동 입력 truncation 및 경고

**구현** (`user.c`):
```c
int readline(char *buf, int bufsz) {
    int pos = 0;
    while (pos < bufsz - 1) {
        int ch = getchar();
        if (ch == '\n' || ch == '\r') {
            putchar('\n');
            buf[pos] = '\0';
            return pos;
        } 
        else if (ch == 0x08 || ch == 0x7F) {  // Backspace
            if (pos > 0) {
                pos--;
                putchar('\b'); putchar(' '); putchar('\b');  // Erase
            }
        }
        else if (ch >= 0x20 && ch < 0x7F) {  // Printable
            putchar((char)ch);
            buf[pos++] = (char)ch;
        }
    }
    // ... buffer full handling ...
}
```

#### Echo 프로그램 리팩터링
**변경 전** (30+ 줄):
- 수동 character-by-character 루프
- 경계 검사 누락
- Backspace 미지원

**변경 후** (15 줄):
```c
static void echo_demo(void) {
    printf("\n=== Echo Program (type 'quit' to exit) ===\n");
    while (1) {
        printf("$ ");
        char line[64];
        int len = readline(line, sizeof(line));
        
        if (strcmp(line, "quit") == 0) {
            printf("Exiting echo program\n");
            break;
        } else {
            printf("Echo: %s\n", line);
        }
    }
}
```

#### Calculator 프로그램 리팩터링
**변경 전**:
- 복잡한 character-by-character 파싱 (80+ 줄)
- 입력 버퍼 문제로 "10 + 5" 입력 시 "10"만 읽고 에러

**변경 후**:
- `readline()`로 전체 줄 읽기
- 한 번에 파싱 (더 간단하고 안전)
- 표현식 정상 처리: "10 + 5" → Result: 15

#### 메뉴 입력 개선
**문제**: 메뉴 선택 후 남은 줄바꿈이 다음 입력으로 전달됨

**해결**:
```c
char choice = getchar();
putchar(choice);  // 즉시 에코

// 남은 문자 모두 소비
char ch;
while ((ch = getchar()) != '\n' && ch != -1) {
    putchar(ch);
}
printf("\n");
```

**효과**:
- 일관된 입력 동작
- 향상된 사용자 경험
- 안전한 버퍼 처리
- 유지보수 용이

관련 커밋: `630b688`

---

### 9. 문서 업데이트

**업데이트된 문서**:
1. `README.md`: 새 실행 방식 및 개선사항 반영
2. `docs/데모가이드.md`: 색상 변경, 실행 명령어 업데이트
3. `docs/최종보고서.md`: 최근 개선사항 섹션 추가
4. 모든 마크다운 파일에서 이모지 제거 (전문성 향상)

관련 커밋: `c5b67e6`, `b67ed68`, `b2e396b`, `bb24e1f`

---

### 기술적 결정: 실행 방식 개선

**변경 전**:
```bash
make run-hello
make run-multi-2
make run-1k-os-multiplication
```

**변경 후**:
```bash
./kvm-vmm guest/hello.bin
./kvm-vmm guest/multiplication.bin guest/counter.bin
./kvm-vmm --paging os-1k/kernel.bin
./kvm-vmm --verbose --paging os-1k/kernel.bin
```

**장점**:
- 더 직관적이고 유연함
- 플래그 조합 가능 (`--verbose`, `--paging`)
- 임의의 게스트 조합 실행 가능
- Makefile에 덜 의존적

---

## 다음 주 계획 (Week 14: 12/1)

### 최종 정리
1. 코드 정리 및 주석 추가
2. 최종 보고서 작성
3. 시연 영상 녹화 (선택)
4. Git 저장소 정리

### 추가 개선 (선택)
1. Quiet mode 추가 (디버그 출력 비활성화)
2. 추가 guest 프로그램
3. Documentation 개선

## 결론

**Week 12 성과**:
-  1K OS 입력 시스템 단순화 완료
-  전체 빌드 시스템 안정화
-  성능 테스트 및 문서화 완료
-  포스터 발표 자료 작성 완료

**프로젝트 상태**: **Feature Complete** 

**남은 작업**: 최종 정리 및 문서화 (Week 14-16)

---

**작성일**: 2025-11-22  
**다음 미팅**: 2025-11-25 (포스터 발표 후)
