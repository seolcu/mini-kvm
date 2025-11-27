# 영상 제작 진행 상황

## ✅ 완료된 작업

### 1. Marp 슬라이드 생성
- **HTML 버전**: `presentation.html` (132KB) ✅
- **PDF 버전**: `presentation_slides.pdf` (590KB) ✅
- 총 14장의 슬라이드 생성 완료

### 2. 데모 환경 확인
- VMM 빌드 확인: ✅
- Real Mode 게스트 (6개): ✅
  - `hello.bin`, `counter.bin`, `minimal.bin`, `multiplication.bin`, `fibonacci.bin`, `matrix.bin`
- Protected Mode 1K OS: ✅
  - `kernel.bin` (13KB)

### 3. 빠른 데모 테스트 완료
테스트 결과:
- ✅ Hello World (단일 vCPU) - 정상 동작
- ✅ 4 vCPU 병렬 실행 - 출력 섞임 확인
- ⚠️ 1K OS 곱셈표 - SHUTDOWN 발생 (stdin 입력 문제로 추정)

---

## 🎬 다음 단계: OBS Studio 설정

### 준비 사항
1. **슬라이드 미리보기**
   ```bash
   # 브라우저로 열기
   firefox /home/seolcu/문서/코드/mini-kvm/softcon/presentation.html
   # 또는
   google-chrome /home/seolcu/문서/코드/mini-kvm/softcon/presentation.html
   ```

2. **터미널 설정**
   - 폰트 크기: 24pt 이상으로 증가
   - 프로파일: "녹화용" 프로파일 생성 권장
   - 배경: 불투명 검정 (#000000)
   - 색상 스킴: 기본 유지 (xterm-256color)

3. **OBS Studio 실행**
   ```bash
   obs
   ```

### OBS 씬 구성 순서
`obs_recording_guide.md` 참조하여:
1. 씬 1: 타이틀 카드 (`202322071_설규원_소프트콘_썸네일.png`)
2. 씬 2: 슬라이드 (브라우저 소스 → `presentation.html`)
3. 씬 3: 터미널 풀스크린 (화면 캡처)
4. 씬 4: 레이더 차트 (`chart_radar.png`)
5. 씬 5: 아키텍처 (`architecture.png`)
6. 씬 6: 클로징 (터미널 + QR + 텍스트)

### 마이크 설정
- 노이즈 억제 필터 추가
- 노이즈 게이트 (-30dB)
- 컴프레서 (3:1)
- 게인 조정 (+5dB 정도)

---

## ⚠️ 발견된 문제

### 1K OS stdin 입력 문제
1K OS 데모에서 stdin 입력 시 즉시 SHUTDOWN 발생. 두 가지 해결 방법:

**방법 A**: 대화형 실행 (수동 입력)
```bash
./kvm-vmm --paging os-1k/kernel.bin
# 터미널에서 직접 입력
```

**방법 B**: 입력 시뮬레이션 개선
- `record_demos.sh`의 1K OS 데모는 대화형으로 변경
- 녹화 중 실시간 타이핑

---

## 📝 권장 작업 흐름

### 오늘 (Day 1)
1. ✅ 슬라이드 생성 완료
2. ✅ 데모 테스트 완료
3. ⏳ 슬라이드 미리보기 (브라우저)
4. ⏳ 터미널 녹화용 프로파일 설정
5. ⏳ OBS Studio 씬 구성 (6개)
6. ⏳ 마이크 테스트 및 필터 설정
7. ⏳ 대본 연습 (3회 이상)

### 내일 (Day 2)
- 아침: 세그먼트 1-3 녹화 (인트로, 문제+아키텍처, Real Mode)
- 오후: 세그먼트 4-7 녹화 (Protected Mode, 성능, 응용, 클로징)
- 저녁: 편집 시작 (Kdenlive)

### 모레 (Day 3)
- 아침: 편집 완료 (전환, 텍스트, 오디오)
- 오후: 렌더링 및 최종 확인
- 저녁: 업로드

---

## 🚀 빠른 명령어 참조

### 슬라이드 미리보기
```bash
firefox /home/seolcu/문서/코드/mini-kvm/softcon/presentation.html &
```

### 터미널 데모 스크립트 실행
```bash
cd /home/seolcu/문서/코드/mini-kvm/softcon
./record_demos.sh
```

### 빠른 테스트
```bash
./quick_test_demos.sh
```

### OBS Studio 실행
```bash
obs &
```

---

## 📊 파일 상태 점검

| 파일 | 상태 | 크기 |
|------|------|------|
| `presentation.html` | ✅ | 132KB |
| `presentation_slides.pdf` | ✅ | 590KB |
| `presentation.md` | ✅ | - |
| `narration_script.md` | ✅ | - |
| `record_demos.sh` | ✅ | 실행 가능 |
| `quick_test_demos.sh` | ✅ | 실행 가능 |
| `obs_recording_guide.md` | ✅ | - |
| `chart_radar.png` | ✅ | - |
| `architecture.png` | ✅ | - |
| `qr.png` | ✅ | - |
| `202322071_설규원_소프트콘_썸네일.png` | ✅ | - |

모든 필수 파일 준비 완료! 🎉
