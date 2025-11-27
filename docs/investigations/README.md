# Mini-KVM Troubleshooting 기록

이 폴더는 개발 과정에서 발생한 주요 버그와 이슈의 조사 및 해결 과정을 기록합니다.

---

## 조사 문서 목록

### ✅ 해결된 이슈

#### 1. [Arch vs Fedora Build Issue](arch_vs_fedora_build_issue.md)
- **문제**: Arch Linux에서 빌드한 1K OS 커널이 즉시 Double Fault로 크래시
- **원인**: GCC 기본 타겟 아키텍처 차이 (Arch: i386, Fedora: i686)
- **해결**: `-march=i686` 플래그 추가
- **날짜**: 2024-11-27 ~ 2025-11-27
- **상태**: ✅ **해결됨**
- **영향**: 1K OS가 Arch Linux에서 정상 동작

---

### ⚠️ 미해결 이슈

#### 2. [1K OS Protected Mode SHUTDOWN](INVESTIGATION_1K_OS_SHUTDOWN.md)
- **문제**: AMD Zen 5 CPU에서 1K OS 실행 시 종료 시점에 SHUTDOWN 발생
- **증상**: 프로그램은 정상 실행되나 메뉴 복귀 시 크래시
- **환경**: AMD Ryzen 5 9600X (Zen 5), Arch Linux
- **날짜**: 2025-11-27
- **상태**: ⚠️ **미해결** (CPU 특정 이슈로 추정)
- **영향**: 제한적 (기능은 동작하나 깔끔한 종료 불가)
- **Workaround**: 프로그램 실행 후 강제 종료 허용

---

## 조사 방법론

각 문서는 다음 구조를 따릅니다:

1. **Executive Summary**: 문제 요약, 원인, 해결 방법
2. **Timeline**: 발생 및 해결 일정
3. **Problem Description**: 증상과 환경 정보
4. **Investigation Process**: 시도한 디버깅 방법
5. **Solution** (해결된 경우): 수정 사항과 결과
6. **Lessons Learned**: 얻은 교훈과 권장사항

---

## 주요 디버깅 도구

프로젝트에서 자주 사용한 디버깅 도구:

- **`--verbose` 플래그**: VMM의 VM exit, I/O, hypercall 로그 출력
- **Binary 비교**: `hexdump`, `cmp -l`, `objdump -d`, `readelf -S`
- **GCC 분석**: `gcc -v`, `nm -S`, `objdump -h`
- **KVM 디버깅**: `KVM_RUN` exit reason, exception 정보
- **Test binaries**: 문제 격리를 위한 최소 재현 바이너리

---

## 미래 조사자를 위한 팁

1. **항상 working binary를 백업하세요**
   - `backups/` 폴더에 동작하는 바이너리 저장
   - 바이너리 비교는 문제 해결의 핵심

2. **Test binary로 문제를 격리하세요**
   - 최소 재현 가능한 프로그램 작성
   - 한 번에 한 가지 변경사항만 테스트

3. **GCC 설정 차이를 확인하세요**
   - `gcc -v`로 빌드 설정 비교
   - 크로스 플랫폼 개발 시 특히 중요

4. **섹션 레이아웃을 확인하세요**
   - `readelf -S kernel.elf`
   - 메모리 레이아웃 문제는 페이지 경계와 관련

5. **CPU 특정 이슈를 염두에 두세요**
   - 일부 문제는 특정 CPU 아키텍처에서만 발생
   - 다른 시스템에서 테스트해보기

---

## 관련 문서

- [데모가이드](../데모가이드.md): 정상 동작하는 VMM 사용법
- [1K OS 설계](../1K_OS_설계.md): 1K OS 아키텍처 설계 문서
- [최종보고서](../최종보고서.md): 프로젝트 전체 요약

---

**마지막 업데이트**: 2025-11-28
