# Mini-KVM 소프트콘 영상 제작 - 최종 요약

## 📋 생성된 파일 목록

### 1. 기획 문서
- **`video_timeline.md`**: 5분 영상 세그먼트별 상세 타임라인 및 구성
- **`visual_assets_guide.md`**: 사용 가능한 비주얼 자료 정리 및 활용 계획

### 2. 제작 자료
- **`narration_script.md`**: 세그먼트별 내레이션 대본 (연속 버전 포함)
- **`presentation.md`**: Marp 슬라이드 (14장)
- **`record_demos.sh`**: 터미널 데모 자동 녹화 스크립트 (8개 데모)

### 3. 녹화 가이드
- **`obs_recording_guide.md`**: OBS Studio 설정 및 녹화 워크플로우 완전 가이드

---

## 🎬 제작 프로세스 요약

### Phase 1: 사전 준비 (30분)
1. **Marp 슬라이드 HTML 생성**
   ```bash
   cd /home/seolcu/문서/코드/mini-kvm/softcon
   npx @marp-team/marp-cli presentation.md -o presentation.html
   ```

2. **터미널 설정**
   - 폰트 크기: 24pt 이상
   - 배경: 불투명 검정
   - 색상: xterm-256color

3. **OBS Studio 씬 구성**
   - 6개 씬 생성 (타이틀, 슬라이드, 터미널, 차트, 아키텍처, 클로징)
   - 전환 효과 설정 (Fade, Cut)
   - 마이크 필터 적용 (노이즈 억제, 게이트, 컴프레서)

### Phase 2: 녹화 (1-2시간)
1. **세그먼트별 개별 녹화** (7개 세그먼트)
   - `segment_01_intro.mp4` (20초)
   - `segment_02_problem_arch.mp4` (40초)
   - `segment_03_real_mode.mp4` (60초)
   - `segment_04_protected_mode.mp4` (90초)
   - `segment_05_performance.mp4` (50초)
   - `segment_06_applications.mp4` (30초)
   - `segment_07_closing.mp4` (10초)

2. **녹화 도우미 스크립트 사용**
   ```bash
   cd /home/seolcu/문서/코드/mini-kvm/softcon
   ./record_demos.sh
   ```

### Phase 3: 편집 (1-2시간)
1. **Kdenlive (또는 DaVinci Resolve) 사용**
   - 세그먼트 클립 import
   - 타임라인 배치 및 트리밍
   - 전환 효과 추가
   - 텍스트 오버레이 추가 (성능 수치, 프로그램 이름 등)
   - 배경 음악 추가 (선택, 저작권 없는 음악)
   - 오디오 레벨 노멀라이즈

2. **렌더링**
   - 형식: MP4 (H.264)
   - 해상도: 1920x1080
   - 비트레이트: 8000 Kbps
   - 오디오: AAC, 192 Kbps
   - 예상 파일 크기: 50-150MB

### Phase 4: 최종화 (30분)
1. 영상 품질 확인
2. 오디오 동기화 확인
3. 필요시 재인코딩 (파일 크기 최적화)
4. 웹 업로드

---

## 📊 타임라인 한눈에 보기

```
0:00 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ 5:00

0:00-0:20 │ 인트로 + 훅 (4 vCPU 병렬)
          │ 타이틀 → 터미널
          
0:20-1:00 │ 문제 정의 + 아키텍처
          │ 슬라이드 (QEMU 비교, 시스템 구조)
          
1:00-2:00 │ Real Mode 데모
          │ 터미널 (단일 vCPU, 2 vCPU, 4 vCPU)
          
2:00-3:30 │ Protected Mode - 1K OS 데모
          │ 터미널 (곱셈표, 피보나치, 에코, 소수)
          
3:30-4:20 │ 성능 비교 + 기술 디테일
          │ 레이더 차트 + 슬라이드 (코드 스니펫)
          
4:20-4:50 │ 응용 분야 + 가치 제안
          │ 슬라이드 (교육, 임베디드, 연구, 테스트)
          
4:50-5:00 │ 클로징 + CTA
          │ 터미널 + QR 코드 + GitHub 링크
```

---

## 🎯 핵심 메시지

1. **1,500줄로 완전한 하이퍼바이저 구현** (코드 간결성)
2. **50-100배 빠른 실행 속도** (성능 우위)
3. **4개 vCPU 병렬 실행** (실시간 병렬성)
4. **Real Mode + Protected Mode 지원** (완전한 기능)
5. **교육적 가치** (읽기 쉬운 코드, 명확한 설계)

---

## ✅ 최종 체크리스트

### 사전 준비
- [ ] Marp 슬라이드 HTML 생성 확인
- [ ] 터미널 설정 (폰트 24pt+, 색상)
- [ ] OBS Studio 씬 6개 구성
- [ ] 마이크 테스트 및 필터 적용
- [ ] 대본 연습 3회 이상
- [ ] `record_demos.sh` 테스트 실행

### 녹화
- [ ] 7개 세그먼트 개별 녹화 완료
- [ ] 각 세그먼트 파일 확인 (화질, 음질)
- [ ] 실수 부분 재녹화 완료

### 편집
- [ ] Kdenlive 프로젝트 생성 (1920x1080)
- [ ] 세그먼트 클립 import 및 배치
- [ ] 트리밍 및 전환 효과 추가
- [ ] 텍스트 오버레이 추가
- [ ] 배경 음악 추가 (선택)
- [ ] 오디오 레벨 조정 (-14 LUFS)

### 최종화
- [ ] 전체 미리보기 확인
- [ ] 렌더링 (MP4, 1080p, 8000 Kbps)
- [ ] 파일 크기 확인 (50-150MB)
- [ ] 오디오 동기화 확인
- [ ] 화질 최종 점검
- [ ] 웹 업로드 준비

---

## 🚀 빠른 시작 가이드

### 1단계: Marp 슬라이드 생성
```bash
cd /home/seolcu/문서/코드/mini-kvm/softcon
npx @marp-team/marp-cli presentation.md -o presentation.html
```

### 2단계: 터미널 데모 스크립트 테스트
```bash
./record_demos.sh
# 메뉴에서 각 데모 테스트
```

### 3단계: OBS Studio 열기 및 씬 구성
- `obs_recording_guide.md` 참조하여 6개 씬 생성
- 마이크 테스트 및 필터 적용

### 4단계: 대본 연습
- `narration_script.md` 읽으며 3-5회 연습
- 숫자 발음 연습 (1,500줄, 50배, 90% 등)

### 5단계: 녹화 시작
- 세그먼트별 개별 녹화 (7개)
- 실수 시 해당 부분만 재녹화

### 6단계: 편집
- Kdenlive 열기
- 세그먼트 import 및 타임라인 배치
- 전환, 텍스트, 오디오 편집

### 7단계: 렌더링 및 업로드
- MP4로 렌더링 (1080p)
- 최종 확인 후 웹 업로드

---

## 📞 문제 해결 Quick Reference

| 문제 | 해결 방법 |
|------|-----------|
| 터미널이 검은 화면 | "화면 캡처" 대신 사용 |
| 음성이 작음 | 마이크 게인 +5dB, 컴프레서 적용 |
| 영상이 끊김 | 인코더 사전 설정 "빠르게", 다른 프로그램 종료 |
| 파일 크기 과대 | 비트레이트 낮추기 (8000→5000 Kbps) |
| 슬라이드 흐릿함 | 브라우저 소스 해상도 1920x1080 확인 |
| 대본 실수 | 즉시 멈추고 해당 문장 처음부터 재녹음 |

---

## 🎨 비주얼 자료 위치

| 자료 | 경로 | 용도 |
|------|------|------|
| 레이더 차트 ⭐ | `chart_radar.png` | 성능 비교 (3:30-3:50) |
| 아키텍처 | `architecture.png` | 시스템 구조 (0:35-1:00) |
| QR 코드 | `qr.png` | GitHub 링크 (4:55-5:00) |
| 썸네일 | `202322071_설규원_소프트콘_썸네일.png` | 타이틀 (0:00-0:05) |
| 멀티 vCPU | `screenshot_multi_vcpu.png` | Real Mode 보조 |
| 1K OS 메뉴 | `screenshot_1k_os_menu.png` | Protected Mode 인트로 |

---

## 💡 Pro Tips

1. **세그먼트별 녹화**: 한 번에 전체보다 세그먼트별로 나눠서 녹화 (실수 대응 용이)
2. **터미널 스크립트 활용**: `record_demos.sh`로 일관된 데모 실행
3. **오디오 우선**: 화질보다 음질이 더 중요 - 마이크 필터 반드시 적용
4. **여유 시간**: 각 클립 앞뒤 2초 여유 (편집 용이)
5. **백업**: 녹화 파일은 즉시 백업 (재녹화 방지)

---

## 📁 파일 구조

```
softcon/
├── video_timeline.md           # 타임라인 및 구성
├── narration_script.md         # 내레이션 대본
├── presentation.md             # Marp 슬라이드 소스
├── presentation.html           # 슬라이드 HTML (생성 필요)
├── record_demos.sh             # 터미널 데모 스크립트
├── obs_recording_guide.md      # OBS 녹화 가이드
├── visual_assets_guide.md      # 비주얼 자료 가이드
├── README_production.md        # 본 파일 (최종 요약)
│
├── chart_radar.png             # 레이더 차트 ⭐
├── architecture.png            # 아키텍처 다이어그램
├── qr.png                      # GitHub QR 코드
├── 202322071_설규원_소프트콘_썸네일.png  # 썸네일
├── screenshot_multi_vcpu.png   # 멀티 vCPU 스크린샷
├── screenshot_1k_os_menu.png   # 1K OS 메뉴
└── [기타 차트 및 스크린샷]
```

---

## 🏁 다음 단계

1. **지금 바로**: Marp 슬라이드 HTML 생성
2. **오늘**: OBS Studio 씬 구성 및 테스트 녹화
3. **내일**: 전체 세그먼트 녹화 완료
4. **내일 모레**: 편집 및 렌더링
5. **마감 전**: 최종 확인 및 업로드

**모든 준비가 완료되었습니다. 성공적인 영상 제작을 기원합니다!** 🎉
