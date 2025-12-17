# 16주차 연구노트 (마지막 주)

목표: Mini-KVM 위에서 Linux(bzImage) 부팅 로그를 **UART(COM1)** 로 출력하고, initramfs까지 연결해 “부팅이 진행된다”는 데모를 만든다.

저번주(week14-15) 방향:

- 범용 하드웨어 지원은 포기하고 타겟을 극단적으로 축소한다.
- initramfs “구현”을 피하고, 가능한 한 정적 포함/최소 장치로 간다.
- 타이머/인터럽트 기반(IRQCHIP)부터 안정화한다.

관련 계획 문서:

- `research/week16/linux_boot_plan.md`

---

## 이번 주에 실제로 한 일(구현)

### 1) Linux 부팅 모드 확장

- Linux 모드에서 게스트 메모리를 256MB로 확장 (`kvm-vmm-x86/src/main.c`)
- Linux 모드에서 `KVM_CREATE_IRQCHIP` 강제 활성화 (IRQ 라우팅/PIT 에뮬 기대)
- UART COM1(0x3f8~0x3ff) I/O 처리 추가
  - OUT(THR)로 나오는 바이트를 stdout으로 출력
  - IN(LSR)은 항상 “송신 가능(THRE|TEMT)” 반환

### 2) Linux boot protocol 파라미터 처리 보강

- bzImage에서 파싱한 setup header(`boot_params->hdr`)를 `setup_linux_boot_params()`가 덮어쓰지 않도록 수정
  - 기존에는 `memset(boot_params, 0, ...)` 후 header가 날아가서 `code32_start`가 0으로 보이는 문제가 있었음
- `--initrd <file>` 옵션 추가:
  - initrd를 게스트 메모리 0x04000000(64MB)에 적재
  - `boot_params->hdr.ramdisk_image/ramdisk_size/initrd_addr_max` 갱신

### 3) 디버깅을 위한 VM exit 핸들링 추가

- Linux 경로에서 보이던 `KVM_EXIT_MMIO`를 최소 처리(읽기는 0으로 채움, 쓰기는 무시)
- 일부 exit reason 추가 처리(무조건 실패하지 않도록)

---

## 실험 기록(재현 가능한 커맨드)

### 준비: 호스트 커널/이니트램프를 프로젝트 폴더로 복사

권한 문제(`/boot`의 initramfs는 root-only)가 있어서 파일을 프로젝트 쪽으로 복사해서 사용했다.

```bash
cd kvm-vmm-x86
sudo cp /boot/vmlinuz-6.17.11-300.fc43.x86_64 ./bzImage
sudo cp /boot/initramfs-6.17.11-300.fc43.x86_64.img ./initrd.img
sudo chmod 644 ./initrd.img
```

### 실행(부팅 시도)

```bash
cd kvm-vmm-x86
make vmm
./kvm-vmm --debug 2 --linux ./bzImage --initrd ./initrd.img --cmdline \
  "console=ttyS0 earlycon=uart,io,0x3f8,115200 rd.shell rd.debug loglevel=8 root=/dev/ram0 acpi=off noapic nolapic pci=off nokaslr panic=-1"
```

관찰:

- KVM/VM 생성 및 bzImage 로드, boot params/E820 세팅, initrd 적재까지는 정상 로그가 출력됨.
- 실행 직후에는 아래 두 가지 케이스를 관찰했다.
  1) (초기 실모드 진입 시도) 의미 없는 포트 I/O(예: `port=0xfff1` 등)가 반복되며 진행이 멈춘 듯 보임(earlycon 출력 없음).
  2) (코드32 진입 실험 시도) 대량의 `KVM_EXIT_MMIO`가 발생하고 최종적으로 `KVM_EXIT_INTERNAL_ERROR(suberror=0x3)`로 종료.

---

## 문제 분석(현재 막힌 지점)

### A) 실모드 진입 경로에서 “BIOS 의존/레거시 포트” 가능성

- bzImage의 setup 코드는 일부 레거시 초기화(A20, 8042, CMOS 등)를 기대할 수 있다.
- 현재 VMM은 UART 외 포트 I/O를 대부분 무시하며, **IN에 대한 기본값도 제한적**이라 부팅 코드가 polling에서 멈출 가능성이 있다.

추가 관찰:

- `objdump`로 bzImage setup 일부를 확인해보니 실제로 `sti` 이후 `int 0x0` 같은 인터럽트 호출이 들어있었다.
- 이는 IVT(0x0000)와 BIOS 인터럽트 핸들러가 존재한다는 전제를 깔고 동작할 가능성이 커서, “펌웨어/BIOS 없이 KVM만으로 setup 코드를 그대로 실행”하는 접근은 리스크가 크다.

### B) 코드32 진입 경로에서 “부팅 상태/boot_params 전달” 불일치 가능성

- 코드32로 바로 점프하면 Linux가 기대하는 레지스터/세그먼트/boot_params 전달 방식이 정확해야 한다.
- boot_params 포인터/내용이 어긋나면 커널이 잘못된 물리주소로 접근해 MMIO 폭주로 이어질 수 있다(관찰된 0xbdfe... 대 물리주소 write).

---

## 다음 계획(개선안) + 바로 할 일

1) **포트 I/O 기본값 보강**
   - 알 수 없는 IN은 0으로 채워서 “대기 루프가 풀릴 여지”를 만든다.
   - A20(0x92), 8042(0x60/0x64), PIC(0x20/0x21/0xa0/0xa1), POST(0x80), CMOS(0x70/0x71) 최소 에뮬을 추가한다.
2) **MMIO/IO 로깅 제한**
   - 현재는 MMIO가 폭주하면 로그가 과도해 디버깅 자체가 어려워진다(상한선을 둔다).
3) **실모드(setup 0x90200) 경로 우선 안정화**
   - “setup 코드가 code32_start로 넘어가서 earlycon을 찍는 단계”까지를 1차 목표로 한다.
4) (플랜 B) 코드32 직접 진입 재시도
   - 위가 막히면, boot protocol 문서 기준으로 보호모드 진입 상태를 더 정확히 맞춰서 재시도한다.

---

## 추가 진행(업데이트)

### 1) 메모리 레이아웃/boot_params 처리 수정

- bzImage setup 코드를 `0x90000`이 아니라 `0x10000`으로 적재하도록 변경
  - 기존에는 `boot_params`를 `0x90000`에 통째로 `memcpy` 하면서 setup 코드 영역을 덮어써서, 실모드/코드32 둘 다 불안정해질 가능성이 높았음.
- `boot_params`는 guest RAM의 `0x90000`(zero page) 위치에 직접 구성하도록 변경(호스트 malloc → 게스트 memcpy 제거).
- 로컬 테스트용 `bzImage/initrd.img`는 **커밋 대상에서 제외**되도록 `.gitignore` 추가.

### 2) Linux 진입 전략을 실험할 수 있도록 옵션 추가

- `--linux-entry setup|code32` 추가
  - `code32`: 보호모드(페이징 OFF)에서 `code32_start`로 직접 진입(기본값).
- `--linux-rsi base|hdr` 추가
  - Linux 초기 진입 코드가 `ESI`를 어떻게 해석하는지 실험하기 위한 옵션.
  - 관찰상 `base`는 즉시 MMIO 폭주(`0xbdfdf...`) 패턴으로 이어지는 경우가 많았고,
    `hdr`는 단일 스텝 디버그에서 더 “멀리” 진행(대신 특정 지점에서 triple fault).

### 3) 디버그/관찰 개선

- `--debug 3`에서 KVM single-step 디버그를 개선:
  - 보호모드에서는 `RIP` 전체로 선형주소를 계산하도록 수정(기존 real-mode 가정으로 잘못된 주소를 찍던 문제 수정).
  - SHUTDOWN(triple fault) 시점에 “마지막 single-step 상태”를 함께 출력하도록 개선.

### 4) 최신 관찰(가장 최근 실패 지점)

- `--linux-entry code32 --linux-rsi hdr --debug 3` 조합에서,
  Linux decompressor가 진행되다가 `rep stosd`(`bytes=f3 ab ...`) 수행 중 triple fault로 리셋되는 패턴을 관찰.
- 마지막 single-step 상태 예시(요지):
  - `RIP=0x1000a9`, `RCX=0x1800`, `RDI=0xfdf000`, `IDT.base=0x11e4280`, `IDT.limit=0xff`
  - 즉, 커널이 자체적으로 IDT를 구성/설정한 뒤, 메모리 초기화 루틴(추정) 진행 중 예외로 리셋되는 형태.

---

## 다음 계획(개선)

1) **“왜 triple fault가 나는지” 원인 식별을 최우선으로**
   - 단일 스텝에서 마지막 상태만으로는 예외 번호를 확정하기 어려움.
   - 커널이 설정한 IDT(예: `IDT.base=... limit=...`)가 실제로 유효한지(엔트리/핸들러 주소) 확인하는 로깅을 추가한다.
2) **Linux entry 조건을 boot protocol에 더 엄밀히 맞추기**
   - `ESI` 의미(boot_params vs setup_header), stack/segment 초기 상태를 문서 기준으로 다시 대조.
   - 필요하면 `--linux-rsi` 외에 관련 레지스터/세그먼트 값을 로그로 고정 출력.
3) **테스트 커널을 바꿔서 변수 줄이기**
   - 호스트 최신 배포판 커널 대신, (가능하면) 부팅 경로가 단순한 “작은 bzImage”로 재현을 시도해 원인 범위를 좁힌다.

---

## 최종 업데이트(부팅 성공)

### 1) root cause: boot_params(zero page) 레이아웃이 틀려서 커널이 헤더 필드를 잘못 읽고 있었음

- 기존 `struct boot_params`가 실제 Linux boot protocol의 오프셋(`hdr@0x1f1`, `e820_map@0x2d0`)과 달라서,
  커널이 `boot_params->hdr.init_size` 같은 필드를 **깨진 값으로 해석**하고 비정상 주소 접근/트리플폴트로 이어졌음.
- `kvm-vmm-x86/src/linux_boot.h`에서 zero page를 4KB로 고정하고, 오프셋을 `_Static_assert`로 검증하도록 수정.

### 2) initrd 적재 주소를 “고정 64MB”에서 “상단 메모리”로 변경

- Fedora 커널의 `init_size`가 큰 편이라, initrd를 64MB에 고정 적재하면 **커널 footprint와 겹쳐 initrd가 덮어써짐**.
- `kvm-vmm-x86/src/linux_boot.c`에서 initrd를 `mem_size` 상단 쪽(4KB align-down)으로 배치하고,
  `KERNEL_LOAD_ADDR + init_size` 이후에만 놓이도록 체크를 추가.

### 3) userspace 콘솔 출력/입력 지원: COM1 IRQ4 최소 동작 추가

- 커널 printk는 polling으로도 출력되지만, userspace(`/init`/쉘)는 8250 드라이버의 **IRQ 기반 TX/RX**에 의존해 출력이 막힐 수 있었음.
- `kvm-vmm-x86/src/main.c`에서
  - UART RX: stdin 입력을 버퍼링하고 `IRQ4`를 pulse
  - UART TX: `IER.THRE` 활성화 시점/THR write 시점에 `IRQ4`를 pulse
  - `IIR/LSR`에 RX-ready/THRE 상태를 최소 반영
  을 추가해서 `/bin/sh` 프롬프트까지 출력됨을 확인.

### 4) initramfs 빌드 스크립트 추가(커밋에 바이너리 포함 X)

- `/boot`의 initramfs는 권한(0600)으로 읽을 수 없는 경우가 있어, 프로젝트 내부에서 재현 가능한 최소 initramfs를 생성하도록 함.
- `kvm-vmm-x86/tools/mkinitramfs.sh`가
  - `kvm-vmm-x86/initramfs/init.c`를 빌드하여 `/init`로 넣고
  - 호스트의 `bash` + 필요한 공유 라이브러리를 포함한 `initramfs.cpio`를 생성한다.

### 재현 커맨드(현재 성공 조합)

```bash
cd kvm-vmm-x86
make vmm
./tools/mkinitramfs.sh initramfs.cpio
./kvm-vmm --linux ./bzImage --initrd ./initramfs.cpio --linux-entry code32 --linux-rsi base --cmdline \
  "console=ttyS0 earlycon=uart,io,0x3f8,115200 loglevel=4 nokaslr"
```

관찰(요지):

- 커널 부팅 로그 출력 후, userspace에서 아래 메시지와 함께 쉘 프롬프트 진입:
  - `[mini-kvm] userspace init started`
  - `sh-5.3#`

### 남은 한계(범위 밖으로 둔 것)

- 디스크/virtio/PCI/ACPI 테이블 없음 → “완전한 배포판 부팅”은 목표 범위 밖.
- initramfs는 최소 구성(현재는 `bash` 기반)이라 일반 유틸리티(`uname` 등)는 포함되지 않음(필요하면 바이너리+라이브러리 추가 가능).
