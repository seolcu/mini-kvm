# 3주차 연구내용

목표: VM 생성 및 메모리 할당 코드 작성

저번주 todo:

- Bochs 코드 리뷰
- 프로젝트 코드 구조 설계
- (KVM) VM 생성 및 메모리 할당 코드 작성

## 연구 내용

### Bochs 2.2.6 컴파일

#### Configure

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

이후 같은 옵션으로 configure가 정상적으로 진행되었습니다. (X window 관련 문제가 발생하지 않는 걸 보니, term을 켜면 vga는 저절로 꺼지는 것 같습니다.) 따라서 바로 `make`를 진행해봤습니다. 그랬더니 수많은 warning과 함께, 아래와 같은 에러가 발생했습니다.

![alt text](image.png)

아무래도 GCC 버전이 달라 C++ 표준이 달라져서 생긴 에러인 것 같습니다.

#### GCC 버전 낮추기

따라서 시스템의 GCC 버전을 더 낮추기로 했습니다. 그래서 더 찾아보던 중, [C++과 GCC에 관한 블로그 글](https://dulidungsil.tistory.com/entry/GCC-%EB%B2%84%EC%A0%84%EA%B3%BC-C-%EB%B2%84%EC%A0%84-%EB%A7%A4%EC%B9%AD)을 하나 발견했습니다. 이 글에는 이렇게 쓰여있습니다:

> 1998년에 첫 번째 표준인 C++ 98이 공개된 후 오랜 기간동안 정체기를 거치다가 2011년이 되어서야 새로운 개념들이 추가된 버전이 공개되었습니다. C++ 11부터 Modern C++ 이라고 부릅니다.
>
> - trusty: Ubuntu 14.04, xenial: Ubuntu 16.04, bionic: Ubuntu 18.04
> - focal: Ubuntu 20.04,
> - jammy: Ubuntu 22.04, kinetic: Ubuntu 22.10
> - lunar: Ubuntu 23.04

우선 현재 사용중이던 Ubuntu 22.04에는 gcc 4.8 미만을 설치할 수 없었습니다.

따라서 distrobox에서 제공하는 가장 낮은 Ubuntu 버전인 Ubuntu 16.04를 설치하고 build-essential을 설치해봤습니다.

```bash
distrobox create -i quay.io/toolbx/ubuntu-toolbox:16.04
```

```bash
sudo apt install build-essential
```

이후 gcc 버전을 확인해보았습니다.

```
gcc (Ubuntu 5.4.0-6ubuntu1~16.04.12) 5.4.0 20160609
Copyright (C) 2015 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

기본 설치된 gcc 버전은 5.4.0이었습니다. 따라서 apt로 gcc-4.7을 직접 설치해보았습니다.

```bash
sudo apt install gcc-4.7 gcc-4.7-multilib g++-4.7 g++-4.7-multilib
```

그리고 gcc 4.7을 기본 컴파일러로 사용하기 위해, [update-alternatives와 관련된 블로그 글](https://blog.koriel.kr/gcc-g-dareun-beojeon-cugahago-paekiji-gwanrihagi/)을 통해 gcc 4.7을 기본으로 설정했습니다.

```bash
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.7 10
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.7 10
```

이제 gcc 4.7이 설치되었습니다:

```
$ gcc --version
gcc (Ubuntu/Linaro 4.7.4-3ubuntu12) 4.7.4
Copyright (C) 2012 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

따라서 바로 configure 후 make 해보았습니다:

![alt text](image-1.png)

여전히 같은 문제로 컴파일에 실패했습니다.

## 다음주 todo
