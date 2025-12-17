# 13주차 연구내용

## 이번 주 목표

### Week 13 상담 내용 (11/25)
- 프로젝트 완성 확인 및 데모
- 최종 발표 준비 (포스터 영상 제출)
- 문서화 및 코드 정리
- 버그 수정 및 사용성 개선

## 구현 완료 사항

### 1. 치명적 버그 수정: Multi-vCPU DS Segment 설정 오류 (11/28)

**문제 상황**:
- Multi-vCPU 환경에서 vCPU 1-3의 출력이 누락됨
- `hello.bin` 실행 시 vCPU 0만 출력되고 나머지는 출력 없음
- 메모리 접근은 정상이지만 데이터 세그먼트 접근 실패

**근본 원인**:
```c
// 이전 코드 (버그)
vcpu->sregs.ds.base = 0;  // vCPU 1-3도 base가 0
vcpu->sregs.ds.limit = 0xFFFFFFFF;

// CS는 제대로 설정됨
vcpu->sregs.cs.base = vcpu_id * mem_size;  // vCPU별로 다름
```

**해결 방법**:
```c
// 수정된 코드
vcpu->sregs.ds.base = vcpu_id * mem_size;  // CS base와 동일하게
vcpu->sregs.ds.limit = 0xFFFFFFFF;
vcpu->sregs.es.base = vcpu_id * mem_size;
vcpu->sregs.ss.base = vcpu_id * mem_size;
```

**영향**:
- Multi-vCPU 데모가 완전히 동작하게 됨
- 모든 vCPU의 출력이 정상적으로 표시됨
- Color-coded output이 제대로 작동

**커밋**: `a40a33c` - fix(vmm): Fix multi-vCPU output and improve usability

### 2. Fibonacci Guest 프로그램 수정 (11/28)

**문제 상황**:
- Fibonacci 결과값이 부정확
- Hypercall 호출 규약 불일치
- 출력 형식이 다른 guest와 달라 가독성 저하

**수정 내용**:
```assembly
# 이전: 레지스터 관리 불일치
movb $1, %al          # HC_PUTCHAR
movw $0x500, %dx
# EDX 값이 보존되지 않음

# 수정 후: 레지스터 올바르게 보존
pushl %edx
movb $1, %al
movw $0x500, %dx
outb %al, %dx
popl %edx
```

**개선 사항**:
- 올바른 피보나치 수열 출력 (0, 1, 1, 2, 3, 5, 8, 13, 21, 34...)
- 숫자 → ASCII 변환 로직 수정
- 줄바꿈 형식 통일

**커밋**: `360b3e9` - fix(guest): Fix fibonacci guest output

### 3. 키보드 입력 안정화: Interrupt Injection 제거 (11/28)

**문제 상황**:
- `KVM_INTERRUPT` ioctl 사용 시 triple fault 발생
- IRQCHIP 없이 interrupt injection 시도하여 guest 크래시
- Protected mode에서 간헐적 hang 발생

**근본 원인**:
```c
// 잘못된 코드
if (ioctl(vcpu->fd, KVM_INTERRUPT, &irq) < 0) {
    // IRQCHIP이 없는데 interrupt를 inject하려 함
}
```

**해결 방법**:
- IRQ injection 코드 완전 제거
- Hypercall 기반 polling 방식으로 통일
- stdin monitoring thread는 유지 (버퍼 채우기용)

**장점**:
- Triple fault 문제 해결
- 모든 게스트에서 안정적 동작
- 코드 복잡도 감소 (39줄 → 15줄)

**커밋**: `0f51f43` - fix(vmm): Enable stdin thread and remove interrupt injection

### 4. Arch Linux 빌드 문제 해결 (11/27)

**문제 상황**:
- Fedora에서 빌드한 `kernel.bin`은 정상 동작
- Arch Linux에서 빌드한 버전은 부팅 실패 (triple fault)
- 동일한 코드, 동일한 Makefile인데 결과가 다름

**원인 분석**:
```bash
# Fedora GCC: i686 기본값
$ gcc -m32 -march=native → i686 코드 생성

# Arch GCC: i386 기본값
$ gcc -m32 -march=native → i386 코드 생성 (일부 명령어 미지원)
```

**해결 방법**:
```makefile
# os-1k/Makefile에 명시적 아키텍처 지정
CFLAGS += -march=i686

# 추가 링커 플래그
LDFLAGS += --no-pie --build-id=none -z norelro
```

**결과**:
- Arch Linux에서도 정상 빌드 및 실행
- Binary 크기: 13K → 12K (더 최적화됨)
- 백업: `backups/working-fedora-build/` 디렉토리 생성

**문서화**: `docs/investigations/arch_vs_fedora_build_issue.md` 추가

**커밋**: `f1832b9` - fix(os-1k): Fix Arch Linux build to work on Zen 5 KVM

### 5. 사용성 개선: 확장자 제거 (11/28)

**변경 내용**:
- Guest 바이너리 파일에서 `.bin` 확장자 제거
- Tab completion이 더 편리하게 개선

**이전**:
```bash
./kvm-vmm guest/hello.bin guest/counter.bin guest/multiplication.bin
```

**개선 후**:
```bash
./kvm-vmm guest/hello guest/counter guest/multiplication
# Tab으로 자동완성 시 더 빠름
```

**추가 기능**:
- Multi-vCPU 실행 시 컬러 범례 자동 출력
- vCPU 0: Cyan, vCPU 1: Green, vCPU 2: Yellow, vCPU 3: Blue

**커밋**: `a40a33c` - fix(vmm): Fix multi-vCPU output and improve usability

### 6. 4KB Paging으로 전환 (11/27)

**변경 내용**:
- 기존 4MB PSE (Page Size Extension) 페이징에서 4KB 표준 페이징으로 변경
- x86 표준 2-level page table 구조 사용

**이유**:
- 4MB 페이지는 x86 표준이 아님
- 교육적 목적: 표준 페이징 메커니즘 학습
- 더 세밀한 메모리 관리 가능

**구현**:
```c
// Page Directory Entry (4KB 페이지 사용)
pde = page_table_addr | 0x07;  // Present, R/W, User (PSE 비트 없음)

// Page Table Entry
for (int i = 0; i < 1024; i++) {
    page_table[i] = (i * 4096) | 0x07;  // 4KB 단위 매핑
}
```

**커밋**: `f1832b9`의 일부

## 버그 수정 및 마이너 개선

### 7. 터미널 입출력 개선 (11/23)

**문제점들**:
1. Raw mode에서 줄바꿈이 제대로 출력되지 않음
2. 사용자 입력이 중복으로 echo됨
3. Shell 프롬프트가 바로 표시되지 않음 (버퍼링 문제)

**해결**:
```c
// 1. Raw mode에서 OPOST 유지 (줄바꿈 처리)
termios.c_oflag |= OPOST;  // 출력 후처리 유지

// 2. Echo 비활성화
termios.c_lflag &= ~(ECHO | ICANON);

// 3. flush_output() 함수 추가
void flush_output(void) {
    fflush(stdout);
}
```

**커밋들**: `b486d0f`, `e0e35fe`, `a911f08`, `6f9e072`

### 8. 코드 정리 및 리팩토링 (11/23)

**작업 내용**:
- 큰 함수들을 작은 함수로 분리
- 모든 마크다운 파일에서 이모지 제거 (지침 준수)
- `.gitignore` 업데이트
- Guest 빌드 스크립트 수정

**개선**:
- 코드 가독성 향상
- 유지보수 용이
- 빌드 시스템 안정화

**커밋들**: `bd3b57f`, `bb24e1f`, `84fce1c`

### 9. 문서 작업 (11/23 - 11/28)

**최종 문서 완성**:
1. `docs/최종보고서.md` - 프로젝트 전체 요약
2. `docs/벤치마크_결과.md` - 성능 측정 및 분석
3. `docs/기술평가.md` - 코드 품질 및 설계 결정
4. `docs/데모가이드.md` - 실행 방법 및 예제
5. `docs/1K_OS_설계.md` - 1K OS 아키텍처 설명

**문서 구조 개편**:
- 모든 문서를 `docs/` 폴더로 이동
- README 간소화
- 인덱스 페이지 추가 (`docs/INDEX.md`)

**커밋**: `cf5d47d`, `7dd2909`, `1d2e66c`

## Week 13 이후 작업 (11/30 - 12/02)

### 10. Long Mode (64-bit) 지원 추가 (12/01)

**동기**:
- 교수님이 Linux 부팅 가능성 언급
- Linux는 64-bit Long Mode 필요
- 교육 확장 과제로 시도

**구현 내용**:
```c
// 1. CR4.PAE 활성화
vcpu->sregs.cr4 |= X86_CR4_PAE;

// 2. IA32_EFER.LME 설정
struct kvm_msrs *msrs;
msrs->entries[0].index = MSR_EFER;
msrs->entries[0].data = EFER_LME | EFER_LMA;

// 3. 4-level page table 구조 생성
setup_long_mode_paging();

// 4. 64-bit GDT 설정
setup_long_mode_gdt();
```

**현재 상태**:
- 인프라 구현 완료
- `KVM_SET_SREGS` 오류 해결
- 64-bit guest 바이너리 로드 가능

**남은 작업**:
- Guest 64-bit 코드 테스트
- 실제 Linux 부팅 시도 (Phase 2)

**커밋들**: `59a9d93`, `26b37d0`, `a4dbb00`

### 11. Linux Boot Protocol 준비 (12/02)

**Phase 2 작업 시작**:
- Linux boot protocol 헤더 파싱
- 초기 RAM disk (initrd) 로딩 구조
- Zero page 설정

**새로운 파일들**:
- `kvm-vmm-x86/src/linux_boot.c` - Boot protocol 처리
- `kvm-vmm-x86/src/linux_boot.h` - 구조체 정의

**현재 상태**: 초기 인프라만 구현, 실제 부팅 미완성

**커밋들**: `b870a62`, `0054786`

### 12. 디버깅 인프라 추가 (12/01)

**목적**:
- Long Mode 및 Linux 부팅 디버깅 지원
- 레지스터 상태 덤프
- 메모리 내용 검사

**기능**:
```c
void dump_registers(struct kvm_vcpu *vcpu);
void dump_memory(void *guest_mem, uint64_t addr, size_t len);
void dump_segment(struct kvm_segment *seg, const char *name);
```

**커밋**: `8554e3b` - feat(debug): Add comprehensive debugging infrastructure

## 프로젝트 통계

### 최종 코드 규모
```
kvm-vmm-x86/src/main.c:       ~1,500 LOC
kvm-vmm-x86/src/debug.c:        ~200 LOC (새로 추가)
kvm-vmm-x86/src/paging_64.c:   ~150 LOC (새로 추가)
kvm-vmm-x86/src/linux_boot.c:  ~100 LOC (새로 추가)
os-1k/kernel.c:                 ~900 LOC
os-1k/shell.c:                  ~100 LOC
os-1k/common.c:                 ~200 LOC
guest/*.S:                      ~600 LOC (fibonacci 수정 포함)
────────────────────────────────────
Core (Week 13까지):           ~3,300 LOC
Extension (Long Mode):          ~450 LOC
────────────────────────────────────
Total:                         ~3,750 LOC
```

### Git 활동 통계
```bash
$ git log --since="2025-11-23" --until="2025-12-02" --oneline | wc -l
78

$ git log --since="2025-11-23" --until="2025-11-26" --oneline | wc -l
41  # 11/23-26: 프로젝트 완성 및 최종 정리

$ git log --since="2025-11-27" --until="2025-11-28" --oneline | wc -l
25  # 11/27-28: 버그 수정 및 발표 준비

$ git log --since="2025-11-30" --until="2025-12-02" --oneline | wc -l
7   # 12/01-02: Long Mode 확장 작업
```

### 주요 커밋들 (Week 13)

**11/23 (프로젝트 완성)**:
- `773f737` - chore: Final polish and cleanup for project completion
- `13c71fe` - Fix VMM hang issue and improve single/multi-vCPU output
- `630b688` - feat: Add readline function with echo and backspace support
- `08ab07d` - docs: Add comprehensive benchmark results and performance analysis
- `36b27c5` - docs: Add technical evaluation document

**11/24 (포스터 준비)**:
- `86b4342` - feat: Add final poster and supporting assets
- `e9dbffe` - chore: Remove poster creation guide files after submission

**11/27 (주요 버그 수정)**:
- `f1832b9` - fix(os-1k): Fix Arch Linux build to work on Zen 5 KVM
- `fb367a6` - Add triple fault investigation report and debug test files

**11/28 (사용성 개선)**:
- `a40a33c` - fix(vmm): Fix multi-vCPU output and improve usability
- `360b3e9` - fix(guest): Fix fibonacci guest output
- `0f51f43` - fix(vmm): Enable stdin thread and remove interrupt injection
- `cf5d47d` - docs: Reorganize and standardize documentation structure

**12/01-02 (확장 작업)**:
- `59a9d93` - feat(long-mode): Implement 64-bit Long Mode support infrastructure
- `26b37d0` - feat(long-mode): Integrate 64-bit Long Mode setup into main VMM
- `a4dbb00` - fix(long-mode): Resolve KVM_SET_SREGS issue for 64-bit Long Mode

## 기술적 결정 사항

### Interrupt Injection 포기 결정

**초기 계획 (Week 12)**:
- KVM_INTERRUPT ioctl로 키보드/타이머 인터럽트 전달
- Guest IDT handler로 처리

**실제 구현 (Week 13)**:
- Hypercall 기반 polling 방식 유지
- IRQ injection 코드 완전 제거

**변경 이유**:
1. **안정성**: Triple fault 문제 완전 해결
2. **단순성**: 코드 복잡도 대폭 감소 (39줄 → 15줄)
3. **충분성**: 교육용 목적에 적합한 수준
4. **이식성**: IRQCHIP 의존성 제거

### 4KB vs 4MB Paging

**교수님 피드백**:
- "4MB 페이지는 x86에서 안 쓴다"
- 문서 오타 지적

**최종 결정**:
- 4KB 표준 페이징으로 변경
- 2-level page table 구조
- 더 표준적이고 교육적인 구현

### Long Mode 확장 작업

**동기**:
- Linux 부팅 가능성 탐색
- 프로젝트 완성 후 개인 학습 목표

**접근 방식**:
- Phase 0: 디버깅 인프라 (12/01 완료)
- Phase 1: Long Mode 기본 (12/01 완료)
- Phase 2: Linux Boot Protocol (12/02 진행 중)
- Phase 3: Linux 실제 부팅 (미완성)

**현실적 평가**:
- APIC/ACPI 구현 필요 (상당한 작업량)
- Device tree 생성 및 로딩 필요
- 교육 과제 범위를 넘어섬

## 학습 내용

### 1. x86 Segmentation의 중요성

**발견**:
- CS base만 설정하고 DS base를 잊으면 데이터 접근 실패
- 모든 세그먼트 레지스터(CS, DS, ES, SS)의 base가 일치해야 함

**교훈**:
- x86 아키텍처의 역사적 복잡성
- Real mode에서 Protected mode 전환 시 주의사항
- KVM API의 저수준 특성

### 2. Toolchain 차이의 영향

**발견**:
- 동일한 소스 코드도 컴파일러 설정에 따라 다르게 동작
- Arch GCC vs Fedora GCC의 기본 타겟 차이

**교훈**:
- 크로스 플랫폼 빌드의 어려움
- 명시적 아키텍처 지정의 중요성
- 바이너리 호환성 문제

### 3. KVM API의 제약사항

**발견**:
- IRQCHIP 없이 `KVM_INTERRUPT` 사용 시 triple fault
- Long Mode 설정 시 MSR과 SREGS 순서 중요

**교훈**:
- Low-level virtualization API 사용의 복잡성
- 에러 메시지만으로 디버깅이 어려움
- 문서와 레퍼런스 구현 병행 필요

### 4. 점진적 개발의 중요성

**Week 13의 접근 방식**:
1. 버그 수정 → 안정화
2. 사용성 개선 → 완성도
3. 문서화 → 전달력
4. 확장 기능 → 학습 심화

**교훈**:
- 완성도 높은 작은 시스템이 불완전한 큰 시스템보다 가치 있음
- 각 단계에서 동작하는 버전 유지
- 실험적 기능은 별도 브랜치/플래그로 관리

## 남은 작업

### 프로젝트 완성 기준 (11/30까지)

- [x] 모든 핵심 기능 구현
- [x] 주요 버그 수정
- [x] 문서화 완료
- [x] 데모 및 확인 완료
- [x] 포스터 발표 자료 제출

### 확장 작업 (선택, 12월 이후)

- [x] Long Mode 인프라 (Phase 0-1)
- [x] 디버깅 도구
- [ ] Linux Boot Protocol 완성 (Phase 2)
- [ ] APIC/ACPI 구현 (Phase 3)
- [ ] 실제 Linux 커널 부팅 (Phase 4)

## 결론

**Week 13 성과**:
- 프로젝트 완성 확인
- 치명적 버그 수정 (Multi-vCPU DS segment)
- 크로스 플랫폼 빌드 문제 해결 (Arch/Fedora)
- 사용성 및 문서 개선
- 네이티브 수준 성능 달성 확인
- 확장 작업 시작 (Long Mode)

**프로젝트 상태**: 완성 (Feature Complete)

**향후 방향**:
- Linux 부팅 지원 (개인 학습 목표)
- 코드 리뷰 및 최적화
- 추가 guest 프로그램 개발
- 교육 자료로 활용
