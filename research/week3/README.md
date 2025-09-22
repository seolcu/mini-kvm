# 3주차 연구내용

목표: VM 생성 및 메모리 할당 코드 작성

저번주 todo:

- Bochs 코드 리뷰
- 프로젝트 코드 구조 설계
- (KVM) VM 생성 및 메모리 할당 코드 작성

## 연구 내용

### Bochs 2.2.6 컴파일

우선 저번주에 이어서 Bochs 2.2.6 컴파일을 이어서 해봤습니다.

저번주에 사용한 configure 명령은 아래와 같았습니다. (xv6 레포의 Notes 파일에서 발견된 명령어입니다.)

```bash
./configure --enable-smp --enable-disasm --enable-debugger --enable-all-optimizations --enable-4meg-pages --enable-global-pages --enable-pae --disable-reset-on-triple-fault
```

그리고 이를 컴파일하면 다음와 같은 에러가 발생했었습니다.

```
ERROR: X windows gui was selected, but X windows libraries were not found.
```

저번주에는 xorg를 설치해 해결하려 했으나, 교수님과 상담 후 `--with-term` 옵션을 주기로 결정했습니다. 또한, 멀티코어를 활성화하는 `--enable-smp` 옵션과, 최적화 및 페이징과 관련된 옵션들인 `--enable-all-optimizations --enable-4meg-pages --enable-global-pages --enable-pae`도 모두 삭제하기로 했습니다.

```bash
./configure --enable-disasm --enable-debugger --disable-reset-on-triple-fault --with-term
```

그랬더니, 다음과 같은 에러가 발생했습니다.

```
Curses library not found: tried curses, ncurses, termlib and pdcurses.
```

Curses 관련 라이브러리가 없다고 떠서, 아래 명령어로 설치해줬습니다. (Ubuntu 22.04 기준)

```bash
sudo apt install libncurses-dev
```

이후 같은 옵션으로 configure가 정상적으로 진행되었습니다. 따라서 바로 `make`를 진행해봤습니다.

## 다음주 todo
