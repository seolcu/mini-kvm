# 9주차 연구내용

목표: KVM 기반 하이퍼바이저로 전환

## 연구 내용

저번주에 교수님과의 상담을 통해 x86 기반 하이퍼바이저 개발에서, RISC-V Linux 위에서 KVM API를 사용하는 방식으로 변경하기로 했습니다.

### 새로운 아키텍처

```
┌─────────────────────────────────────┐
│  Guest OS (Toy OS / xv6-riscv)      │  ← hypercall 전송
├─────────────────────────────────────┤
│  하이퍼바이저 (Rust)                   │  ← 개발 대상
├─────────────────────────────────────┤
│  RISC-V Linux + KVM 모듈             │  ← /dev/kvm 인터페이스 사용
├─────────────────────────────────────┤
│  QEMU (RISC-V 에뮬레이션)             │  ← H-extension
├─────────────────────────────────────┤
│  x86 호스트                          │  ← 개발 머신
└─────────────────────────────────────┘
```

하이퍼바이저는 다음과 같은 역할을 합니다:

- Guest OS로부터 hypercall을 받음
- KVM ioctl 호출로 변환 (VCPU 생성/실행, 메모리 설정 등)
- RISC-V Linux의 `/dev/kvm` 인터페이스 사용

### 환경 설정

#### RISC-V 크로스컴파일러 설치

QEMU는 이미 full 버전으로 설치되어 있습니다.

RISC-V Linux 커널을 빌드하려면 크로스컴파일러가 필요하기에, 다음 패키지들을 설치했습니다:

```bash
sudo pacman -S riscv64-linux-gnu-gcc riscv64-linux-gnu-binutils riscv64-linux-gnu-glibc
```

```bash
riscv64-linux-gnu-gcc --version
```

```
riscv64-linux-gnu-gcc (GCC) 15.1.0
Copyright (C) 2025 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

정상적으로 설치되었습니다.

#### RISC-V Linux 부팅 테스트

RISC-V Linux 커널을 직접 빌드하면 시간이 오래 걸리므로, 우선 pre-built 이미지로 RISC-V Linux가 정상적으로 부팅되는지 확인해보기로 했습니다.

[Debian RISC-V 위키](https://wiki.debian.org/RISC-V)를 찾아보니 [Debian installer 이미지](wget https://deb.debian.org/debian/dists/sid/main/installer-riscv64/current/images/netboot/debian-installer/riscv64/)가 제공되고 있었습니다.

따라서 Linux과 initrd를 받아서 바로 QEMU로 부팅을 시도했습니다. 이전 bare-metal 하이퍼바이저 프로젝트(1000hv 튜토리얼)에서 QEMU를 사용할 때 `-cpu rv64,h=true` 옵션을 사용했던 것이 기억났습니다. 검색해보니 `h=true`는 RISC-V H-extension (Hypervisor extension)을 활성화하는 옵션이었습니다. KVM도 H-extension을 요구하기에 다음과 같이 옵션을 작성해 부팅해보았습니다.

```bash
qemu-system-riscv64 -machine virt -cpu rv64,h=true -m 2G \
  -kernel linux -initrd initrd.gz -nographic \
  -append "console=ttyS0"
```

![alt text](image-1.png)

성공적으로 인스톨러가 부팅되었습니다.

이후 QEMU 옵션을 조정하며 네트워크 등의 장치를 설정해 정상 설치했으나, 결과적으로 `/dev/kvm`을 찾을 수 없었습니다. 알고보니, 커널에 KVM 모듈이 포함되어 있지 않았습니다. 우분투와 도커도 시도해보았으나, 같은 문제가 발생했습니다.

#### 결론 및 방향 설정

pre-built 이미지들로는 KVM을 테스트할 수 없었습니다:

따라서 직접 Linux 커널을 빌드하기로 결정했습니다.
