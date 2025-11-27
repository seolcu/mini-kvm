# Mini-KVM 소프트웨어 콘테스트 제출물

이 폴더는 2025 소프트웨어 콘테스트 제출을 위한 자료를 포함합니다.

---

## 최종 제출물

### 발표 자료
- **설규원_MiniKVM_발표자료.pdf** (프로젝트 루트에 위치)
  - 최종 발표용 슬라이드 (여자친구 제작)

### 포스터
- **poster.pdf** - 프로젝트 포스터 (인쇄용)
- **poster.pptx** - 포스터 편집 가능 원본

### 발표 영상
- **presentation_slides.pdf** - 영상 제작용 슬라이드 (발표 대본 포함)

---

## 자료 생성 도구

### 차트 생성
- **generate_charts.py** - 성능 벤치마크 차트 자동 생성 스크립트
  - 실행: `python generate_charts.py`
  - 생성 위치: `assets/charts/`

### 템플릿
- **templates/** - 포스터 및 영상 제작용 템플릿
  - `poster_template.pptx` - 포스터 기본 템플릿
  - `demo_video_template.pptx` - 영상 프레젠테이션 템플릿

---

## 생성된 자료

### assets/
이미지 및 그래픽 자료가 저장된 폴더

#### assets/charts/
성능 벤치마크 차트:
- `chart_code_size.png` - 코드 크기 비교
- `chart_init_time.png` - 초기화 시간
- `chart_memory.png` - 메모리 사용량
- `chart_radar.png` - 종합 성능 레이더 차트
- `chart_scalability.png` - Multi-vCPU 확장성
- `chart_speed.png` - 실행 속도 비교
- `performance_chart.png` - 전체 성능 요약

#### assets/screenshots/
실행 화면 캡처:
- `screenshot_1k_os_menu.png` - 1K OS 메뉴 화면
- `screenshot_1k_os_running.png` - 1K OS 실행 화면
- `screenshot_multi_vcpu.png` - Multi-vCPU 병렬 실행

#### assets/
기타 자료:
- `architecture.png` - 시스템 아키텍처 다이어그램
- `qr.png` - GitHub 저장소 QR 코드
- `202322071_설규원_소프트콘_썸네일.png` - 콘테스트 제출용 썸네일

---

## 참고 문서

### 프로젝트 문서
- **메인 문서**: `/docs/INDEX.md` - 전체 문서 가이드
- **데모 가이드**: `/docs/데모가이드.md` - 시연 방법
- **최종 보고서**: `/docs/최종보고서.md` - 프로젝트 종합 보고서
- **벤치마크**: `/docs/벤치마크_결과.md` - 성능 측정 결과

### 발표 대본
구글 독스로 이동됨 (파일 삭제됨)

---

## 폴더 구조

```
softcon/
├── README.md                              (이 문서)
├── poster.pdf                             최종 포스터
├── poster.pptx                            포스터 편집본
├── presentation_slides.pdf                발표 영상용 슬라이드
├── generate_charts.py                     차트 생성 스크립트
├── templates/                             제작 템플릿
│   ├── demo_video_template.pptx
│   └── poster_template.pptx
└── assets/                                이미지 자료
    ├── charts/                            성능 차트 (7개)
    ├── screenshots/                       실행 화면 (3개)
    ├── architecture.png
    ├── qr.png
    └── 202322071_설규원_소프트콘_썸네일.png
```

---

**제출 학번**: 202322071  
**제출자**: 설규원  
**제출일**: 2025년 11월 28일
