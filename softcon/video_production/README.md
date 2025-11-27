# Mini-KVM 소프트콘 영상 제작 자료

이 폴더는 5분짜리 소프트콘 데모 영상 제작을 위한 모든 자료를 포함합니다.

## 📁 파일 구성

### 기획 문서
- **`README_production.md`**: 전체 제작 프로세스 요약 및 빠른 시작 가이드
- **`video_timeline.md`**: 5분 영상 세그먼트별 타임라인 (0:00-5:00)
- **`progress.md`**: 현재 진행 상황 체크리스트

### 제작 스크립트
- **`narration_script.md`**: 세그먼트별 내레이션 대본 (녹음용)
- **`presentation.md`**: Marp 슬라이드 소스 (14장)
- **`presentation.html`**: 슬라이드 HTML 버전 (OBS 브라우저 소스용)
- **`presentation_slides.pdf`**: 슬라이드 PDF 백업

### 데모 스크립트
- **`record_demos.sh`**: 터미널 데모 8개 자동 실행 스크립트
- **`quick_test_demos.sh`**: 빠른 데모 테스트용

### 제작 가이드
- **`obs_recording_guide.md`**: OBS Studio 설정 및 녹화 완전 가이드
- **`visual_assets_guide.md`**: 비주얼 자료 활용 계획

### 비주얼 자료 (차트/다이어그램/스크린샷)
- `chart_radar.png` - 성능 비교 레이더 차트 (메인)
- `architecture.png` - 시스템 아키텍처 다이어그램
- `qr.png` - GitHub QR 코드
- `202322071_설규원_소프트콘_썸네일.png` - 썸네일
- `screenshot_multi_vcpu.png` - 멀티 vCPU 실행 화면
- `screenshot_1k_os_menu.png` - 1K OS 메뉴
- `screenshot_1k_os_running.png` - 1K OS 실행 화면
- 기타 차트: `chart_speed.png`, `chart_init_time.png`, `chart_memory.png`, 등

---

## 🚀 빠른 시작

### 1단계: 슬라이드 확인
브라우저로 `presentation.html` 열기

### 2단계: 데모 테스트
```bash
./quick_test_demos.sh
```

### 3단계: OBS 설정
`obs_recording_guide.md` 참조하여 6개 씬 구성

### 4단계: 대본 연습
`narration_script.md` 읽으며 3-5회 연습

### 5단계: 녹화 시작
세그먼트별 개별 녹화 (7개)

### 6단계: 편집
Kdenlive로 타임라인 배치 및 렌더링

---

## 📊 영상 구성 (5분)

| 시간 | 세그먼트 | 내용 |
|------|---------|------|
| 0:00-0:20 | 인트로 + 훅 | 4 vCPU 병렬 실행 |
| 0:20-1:00 | 문제 + 아키텍처 | QEMU 비교, 시스템 구조 |
| 1:00-2:00 | Real Mode 데모 | 단일/멀티 vCPU 성능 |
| 2:00-3:30 | Protected Mode 데모 | 1K OS 4개 프로그램 |
| 3:30-4:20 | 성능 + 기술 | 레이더 차트, 혁신 |
| 4:20-4:50 | 응용 + 가치 | 실전 활용 사례 |
| 4:50-5:00 | 클로징 + CTA | GitHub 링크 |

---

## 🎬 제작 프로세스

1. **사전 준비** (30분): OBS 씬 구성, 터미널 설정, 대본 연습
2. **녹화** (1-2시간): 7개 세그먼트 개별 녹화
3. **편집** (1-2시간): Kdenlive에서 타임라인 배치, 전환 효과, 텍스트 오버레이
4. **최종화** (30분): 렌더링, 품질 확인, 업로드

상세 내용은 `README_production.md` 참조

---

## ✅ 체크리스트

- [ ] 슬라이드 미리보기 완료
- [ ] 터미널 데모 테스트 완료
- [ ] OBS 씬 6개 구성 완료
- [ ] 마이크 필터 설정 완료
- [ ] 대본 연습 3회 이상
- [ ] 세그먼트 1-7 녹화 완료
- [ ] 영상 편집 완료
- [ ] 최종 렌더링 완료
- [ ] 업로드 완료

---

**모든 자료 준비 완료! 성공적인 영상 제작을 기원합니다!** 🎉
