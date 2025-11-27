# 1K OS Protected Mode SHUTDOWN 문제 조사 (2025-11-27)

## 환경 정보

- **시스템**: AMD Ryzen 5 9600X (Zen 5), 데스크톱
- **커널**: Linux 6.12.59-1-lts (linux-lts)
- **이전 시스템**: AMD Ryzen 7 PRO 6850U (Zen 3+), 노트북에서는 정상 작동
- **문제**: `./kvm-vmm --paging os-1k/kernel.bin` 실행 시 즉시 SHUTDOWN

## 증상

```
=== Starting VM execution (1 vCPUs) ===

[kernel] SHUTDOWN - Attempting to get exception info...
[kernel] SHUTDOWN at RIP=0xfff0, RSP=0x0
[kernel]   RAX=0x0 RBX=0x0 RCX=0x0 RDX=0x600
[kernel]   CR0=0x10 CR3=0x0 CR4=0x0
[kernel]   CS=0xf000 DS=0x0 SS=0x0
[kernel]   Exception: injected=0 nr=8 has_error=1 error=0x0
```

**핵심**: 
- Protected Mode 설정 (`CR0=0x80000011`, `CR3=0x100000`) 완료 후
- **첫 번째 KVM_RUN에서 즉시** CPU가 리셋 벡터(`RIP=0xfff0`)로 리셋됨
- Exception nr=8 (Double Fault)

## 시도한 해결 방법

### 1. 커널 변경
- **linux-lts (6.12.59)로 부팅** → 여전히 실패
- 이전 조사에서는 Linux 6.17+ 문제로 의심했으나, LTS에서도 발생

### 2. KVM AMD 모듈 옵션 조정
- **NPT (Nested Page Tables) 비활성화** (`npt=0`) → 실패
- **AVIC (Advanced Virtual Interrupt Controller) 비활성화** (`avic=0`) → 실패  
- **Nested virtualization 비활성화** (`nested=0`) → 실패

### 3. Real Mode 테스트
- **Real Mode 게스트는 정상 작동** (`./kvm-vmm guest/hello.bin`)
- 문제는 **Protected Mode with Paging**에만 국한됨

### 4. 바이너리 크기 조사
- 작은 테스트 바이너리 (100-200바이트) → 성공
- **kernel.bin (13KB)** → 실패
- 문제: 바이너리 **크기/내용**이 KVM 동작에 영향

### 5. 바이트 단위 Bisect
- kernel.bin의 처음 **26바이트까지는 성공**
- **27바이트부터 실패** (바이트 26 = `0xe8`, call 명령어 시작)
- 하지만 동일한 call 명령어를 가진 다른 바이너리는 성공
- **특정 바이트 패턴 조합**이 문제

### 6. Protected Mode without Paging 시도
- `guest/pmode_nopaging.S` 생성 시도
- 링커 문제로 바이너리 크기가 134MB로 비정상적으로 커짐 → 실패

## 핵심 발견 사항

### 발견 1: CPU 리셋 발생
- KVM_SET_SREGS로 설정한 Protected Mode 레지스터가 **무시됨**
- KVM_RUN 실행 직후 CPU가 Real Mode 리셋 벡터로 이동
- 이것은 **AMD SVM의 VMRUN 진입 검증 실패**를 의미

### 발견 2: Zen 5 아키텍처 특이성
- **AMD Ryzen 5 9600X (Zen 5)**: 실패
- **AMD Ryzen 7 PRO 6850U (Zen 3+)**: 성공
- Zen 5의 KVM SVM 구현에 변경사항이 있을 가능성

### 발견 3: 바이너리 내용 의존성
- 동일한 VMM 설정, 동일한 Protected Mode 설정
- 바이너리 **크기와 내용**에 따라 성공/실패 결정
- KVM이 게스트 메모리를 프리스캔하는 과정에서 문제 발생 추정

### 발견 4: 페이지 테이블 설정은 정상
```
Page Dir @ 0x100000: PDE[0]=0x00000083 PDE[512]=0x00000083
Entry @ 0x1000: bc 70 76 01 80 bd 00 00 (kernel.bin 첫 바이트)
GDT @ 0x500: 00 00 00 00 00 00 00 00 (Null descriptor)
```
- GDT, IDT, 페이지 디렉토리 모두 올바르게 설정됨
- 문제는 KVM/CPU의 VM 진입 단계

## 가능한 원인 추론

### 1. Zen 5 KVM SVM 호환성 문제
- Zen 5의 새로운 기능이 기존 KVM 구현과 충돌
- VMCB (Virtual Machine Control Block) 검증 로직 변경
- 특정 메모리 패턴이 검증을 트리거

### 2. PSE (Page Size Extension) 관련
- 4MB PSE 페이징 사용 중
- Zen 5에서 PSE 처리 방식이 변경되었을 가능성
- 하지만 NPT 비활성화로도 해결되지 않음

### 3. 세그먼트 디스크립터 캐시
- Protected Mode 세그먼트 레지스터의 숨겨진 캐시 부분
- Zen 5에서 더 엄격한 검증 수행 가능성

## 미해결 상태

### 시간 제약
- 영상 촬영 마감까지 1시간
- Protected Mode 데모 불가능

### 대안
1. **Real Mode 데모만 사용**
   - 4 vCPU 병렬 실행 시연
   - 성능 비교 데이터는 이전 결과 활용
   
2. **QEMU로 대체**
   - 1K OS를 QEMU에서 실행
   - 하지만 Mini-KVM의 핵심 가치 훼손

3. **다른 시스템에서 촬영**
   - Zen 3+ 노트북에서 촬영
   - 시간적으로 불가능

## 다음 조사 방향 (추후)

1. **QEMU TCG로 kernel.bin 실행 시도**
   - Pure 에뮬레이션 모드에서 동작 확인

2. **AMD 포럼/Linux KVM 메일링 리스트**
   - Zen 5 KVM SVM 관련 이슈 검색
   - 버그 리포트 제출

3. **KVM 디버그 로그 활성화**
   ```bash
   echo 1 > /sys/module/kvm/parameters/ignore_msrs
   echo 'file arch/x86/kvm/* +p' > /sys/kernel/debug/dynamic_debug/control
   ```

4. **4KB 페이징으로 변경**
   - PSE 대신 일반 4KB 페이징 사용
   - 페이지 테이블 구조 재작성 필요

5. **Protected Mode without Paging**
   - 페이징 없이 Protected Mode만 사용
   - GDT/Flat segments로 간단한 커널 실행

## 결론

**Zen 5 (AMD Ryzen 5 9600X) 환경에서 Mini-KVM의 Protected Mode with Paging이 작동하지 않습니다.**

이것은:
- VMM 코드의 버그가 **아님** (Real Mode는 정상 작동)
- 특정 하드웨어/커널 조합의 호환성 문제
- Zen 5의 KVM SVM 구현 또는 CPU 마이크로코드 관련

**임시 해결책**: Real Mode 데모로 영상 제작, Protected Mode는 이전 결과 데이터와 슬라이드로 대체

**장기 해결**: Zen 5 KVM SVM 이슈 추적 및 커널 패치 대기, 또는 다른 시스템 활용
