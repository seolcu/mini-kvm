# mini-kvm

아주대학교 자기주도프로젝트: 리눅스 KVM API를 이용한 초소형 가상 머신 모니터 (VMM) 개발

## Current Research Plan

| 주차 | 날짜          | 연구 수행 내용                                         | 결과물                                |
| ---- | ------------- | ------------------------------------------------------ | ------------------------------------- |
| 1주  | 9월 1일       | 문서 수집 및 학습 (KVM API, CPU, 리눅스 커널, QEMU 등) | 학습 기록서, 핵심 문서 모음           |
| 2주  | 9월 8일       | 구조 설계, 개발 환경 구축, Git 저장소 생성             | 설계도, 환경 정보, Git 원격 저장소    |
| 3주  | 9월 15일      | VM 생성 및 메모리 할당 코드 작성                       | 코드 및 설명                          |
| 4주  | 9월 22일      | 계획 점검, 오픈소스 코드 리뷰, 추가 학습               | 수정된 계획서, 학습 기록서            |
| 5주  | 9월 29일      | vCPU 생성, 레지스터 제어, 실행 코드 작성               | 코드 및 설명                          |
| 6주  | 10월 6일      | 메모리 입출력, VM 종료 코드 작성, 9월 성과 정리        | 코드 및 설명, 9월 성과보고서          |
| 7주  | 10월 13일     | 계획/설계 수정, 오픈소스 리뷰, 학습                    | 수정된 계획서/설계도, 학습 기록서     |
| 8주  | 10월 20일     | 메모리 입출력 및 디버깅 코드 작성                      | 코드 및 설명                          |
| 중간 | **10월 24일** | 중간보고서 작성 및 제출                                | **중간보고서**                        |
| 9주  | 10월 27일     | 실행 가능한 바이너리 테스트 및 디버깅                  | 코드 및 설명, 테스트 결과 기록서      |
| 10주 | 11월 3일      | 리눅스 커널/부트 프로세스 학습, 10월 성과 정리         | 학습 기록서, 10월 성과보고서          |
| 11주 | 11월 10일     | 부트로더 실행 코드 작성 및 디버깅                      | 코드 및 설명                          |
| 12주 | 11월 17일     | 리눅스 커널 실행 코드 작성 및 디버깅                   | 코드 및 설명                          |
| 13주 | 11월 24일     | 리눅스 커널 실행 안정화 테스트                         | 코드 및 설명                          |
| 14주 | 12월 1일      | 최종 테스트, 시연 영상 녹화, 성과 정리                 | Git 저장소, 시연 영상, 성과보고서     |
| 15주 | 12월 8일      | 문서화 및 프로젝트 마무리                              | 최종 문서, Git 원격 저장소            |
| 16주 | 12월 15일     | 문서화 및 프로젝트 마무리                              | 최종 문서, Git 원격 저장소            |
| 기말 | **12월 19일** | 결과 보고서, 연구 노트, 기타 증빙 제출                 | **결과 보고서, 연구 노트, 기타 증빙** |

## Links

### Other Repos

- [Fork of xv6-public](https://github.com/seolcu/xv6-public)

### Reference Projects

- [Bochs Repository](https://github.com/bochs-emu/Bochs)
- [xv6 Repository](https://github.com/mit-pdos/xv6-public)
- [QEMU Repository](https://gitlab.com/qemu-project/qemu)

### Docs

- [KVM Documentation](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [Bochs Documentation](https://bochs.sourceforge.io/cgi-bin/topper.pl?name=New+Bochs+Documentation&url=https://bochs.sourceforge.io/doc/docbook/)
- [Slides - Virtualization without direct execution - designing a portable VM](https://bochs.sourceforge.io/VirtNoJit.pdf)
- [Paper - Virtualization Without Direct Execution or Jitting: Designing a Portable Virtual Machine Infrastructure](https://bochs.sourceforge.io/Virtualization_Without_Hardware_Final.pdf)
- [4.3. The configuration file bochsrc](https://bochs.sourceforge.io/doc/docbook/user/bochsrc.html)

### Tutorials

- [How to compile xv6 on Linux](https://www.youtube.com/watch?v=TLiV_sK77jg)
- [Bochs 설정법](https://yohda.tistory.com/entry/BOCHS-%EC%9E%91%EC%84%B1%EC%A4%91)

### Etc.

- [Bochs Releases](https://sourceforge.net/projects/bochs/files/bochs/)
- [LMDE 6](https://linuxmint.com/edition.php?id=308)
- [dot-bochsrc by Thomas Nguyen](https://gitlab07.cs.washington.edu/tomn/lvisor-18wi/-/blob/master/tests/xv6-src/dot-bochsrc)
