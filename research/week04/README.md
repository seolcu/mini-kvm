# 4주차 연구내용

목표: 계획 점검, 오픈소스 코드 리뷰, 추가 학습

저번주 todo:

- 전체 점검

## 연구내용

### Bochs에서 SMP 지원 해제하기

저번주에 Bochs 2.2.6을 컴파일하고 xv6를 디버거 없이 부팅해보았을때, 다음과 같은 에러가 발생했습니다.

![alt text](image.png)

```
Booting from Hard Disk...
lapicid 0: panic: Expect to run on an SMP
 801031a1 80102e80 0 0 0 0 0 0 0
```

교수님께서 xv6가 듀얼코어를 요구할 것 같지는 않다고 하셨기에, .bochsrc 수정 없이 bochs 2.2.6 컴파일 옵션만 수정해보기로 했습니다.

따라서 `--enable-smp` 옵션을 주어 컴파일해보았습니다.

```bash
./configure --enable-smp --enable-disasm --disable-reset-on-triple-fault --with-term
```

컴파일 이후 `make bochs` xv6를 부팅해보니, 되는듯 하더니 다음과 같은 에러가 발생했습니다.

```
========================================================================
                       Bochs x86 Emulator 2.2.6
              Build from CVS snapshot on January 29, 2006
========================================================================
00000000000i[     ] reading configuration from .bochsrc
00000000000i[     ] installing term module as the Bochs GUI
00000000000i[     ] using log file bochsout.txt
========================================================================
Bochs is exiting with the following message:
[CPU0 ] exception(): 3rd (14) exception with no resolution
========================================================================
Makefile:210: recipe for target 'bochs' failed
make: *** [bochs] Error 1
```

xv6가 아닌 bochs 에러로 보여 bochs 소스 코드에서 `exception with no resolution`를 검색해보니, `cpu/exception.cc` 파일에서 다음 코드를 발견할 수 있었습니다:

```C++
#if BX_RESET_ON_TRIPLE_FAULT
    BX_ERROR(("exception(): 3rd (%d) exception with no resolution, shutdown status is %02xh, resetting", vector, DEV_cmos_get_reg(0x0f)));
    debug(BX_CPU_THIS_PTR prev_eip);
    bx_pc_system.Reset(BX_RESET_SOFTWARE);
#else
    BX_PANIC(("exception(): 3rd (%d) exception with no resolution", vector));
    BX_ERROR(("WARNING: Any simulation after this point is completely bogus."));
#endif
#if BX_DEBUGGER
    bx_guard.special_unwind_stack = true;
#endif
    longjmp(BX_CPU_THIS_PTR jmp_buf_env, 1); // go back to main decode loop
  }
```

`BX_RESET_ON_TRIPLE_FAULT` 이라는 상수값에 따라 명령이 분기되는데, 이 옵션은 bochs configure 과정에서 사용한 옵션인 `--disable-reset-on-triple-fault` 옵션과 관련이 있어 보였습니다. 뭔가 CPU에 문제가 발생하는 상황을 가정한 것 같은데, 이 옵션을 넣으면 무조건 else문으로 빠져 작동을 멈추도록 설계된 것 같습니다.

그래서 우선 configure에서 해당 플래그를 삭제하고 다시 컴파일해보았습니다.

```bash
./configure --enable-smp --enable-disasm --with-term
```

컴파일 이후 xv6를 부팅해보니, 부팅 과정에서 막혔습니다.

![alt text](image-1.png)

무슨 상황인가 싶어 configure 옵션에 디버거를 추가해 컴파일하고 xv6를 부팅해보았습니다.

```bash
./configure --enable-smp --enable-disasm --with-term --enable-debugger
```

![alt text](image-2.png)

저번에 디버거 붙였을 때랑 같은 에러가 발생했습니다. 아무래도 configure 옵션을 더 살펴봐야겠습니다.

### Bochs gdb stub 활성화하기

`./configure --help`로 `--enable-debugger`의 설명을 읽어보았습니다.

```
  --enable-debugger                 compile in support for Bochs internal debugger
```

Bochs의 내부 디버거를 활용한다고 되어있습니다. 저번에 교수님께서 디버거 붙일 때 다른 프로세스에서 gdb 붙이는 방식을 제안하셨으므로, 다른 옵션을 써야할 것 같았습니다. 따라서 검색 중, bochs 문서에서 [GDB Stub 관련 항목](https://bochs.sourceforge.io/doc/docbook/user/debugging-with-gdb.html)을 발견했습니다.

해당 문서에 따르면, configure에서 `--enable-gdb-stub` 옵션을 주고 bochsrc에 [gdbstub 관련 옵션](https://bochs.sourceforge.io/doc/docbook/user/bochsrc.html#BOCHSOPT-GDBSTUB)을 주면 설정한 포트로 gdb를 대기한다고 합니다. 따라서 바로 시도해보았습니다.

```bash
./configure --enable-smp --enable-disasm --with-term --enable-gdb-stub
```

이후 make를 했더니, 다음과 같은 에러가 발생했습니다.

```
$ make
cd iodev && \
make  libiodev.a
make[1]: Entering directory '/home/seolcu/문서/코드/bochs-2.2.6/iodev'
g++ -c  -I.. -I./.. -I../instrument/stubs -I./../instrument/stubs -g -O2 -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES   devices.cc -o devices.o
In file included from iodev.h:32:0,
                 from devices.cc:30:
../bochs.h:381:2: error: #error GDB stub was written for single processor support. If multiprocessor support is added, then we can remove this check.
 #error GDB stub was written for single processor support.  If multiprocessor support is added, then we can remove this check.
  ^
In file included from iodev.h:32:0,
                 from devices.cc:30:
../bochs.h: In member function ‘char* iofunctions::getaction(int)’:
../bochs.h:307:64: warning: deprecated conversion from string constant to ‘char*’ [-Wwrite-strings]
     static char *name[] = { "ignore", "report", "ask", "fatal" };
                                                                ^
../bochs.h:307:64: warning: deprecated conversion from string constant to ‘char*’ [-Wwrite-strings]
../bochs.h:307:64: warning: deprecated conversion from string constant to ‘char*’ [-Wwrite-strings]
../bochs.h:307:64: warning: deprecated conversion from string constant to ‘char*’ [-Wwrite-strings]
devices.cc: In constructor ‘bx_devices_c::bx_devices_c()’:
devices.cc:50:12: warning: deprecated conversion from string constant to ‘char*’ [-Wwrite-strings]
   put("DEV");
            ^
Makefile:120: recipe for target 'devices.o' failed
make[1]: *** [devices.o] Error 1
make[1]: Leaving directory '/home/seolcu/문서/코드/bochs-2.2.6/iodev'
Makefile:259: recipe for target 'iodev/libiodev.a' failed
make: *** [iodev/libiodev.a] Error 2
```

멀티코어에서는 GDB stub이 작동하지 않는 것 같습니다. 따라서 SMP를 꺼야 할 것 같습니다. SMP를 껐을 때 또 xv6에서 에러가 발생할까봐 걱정되지만, 우선 SMP를 다시 끄고 컴파일하기로 했습니다.

```bash
./configure --enable-disasm --with-term --enable-gdb-stub
```

역시 SMP를 빼고 나니 정상적으로 컴파일되었습니다. 이제 bochsrc에 다음과 같은 gdbstub 옵션을 넣어보았습니다.

```
gdbstub: enabled=1, port=1234, text_base=0, data_base=0, bss_base=0
```

이후 바로 `make bochs`로 xv6를 부팅해보았습니다. 다음과 같이 뜨며 1234 포트에서 gdb 연결 대기가 시작되었습니다.

```
Waiting for gdb connection on localhost:1234
```

다른 터미널에서 바로 gdb를 켜서 `target remote localhost:1234` 명령어로 연결해보았습니다.

```bash
(gdb) target remote localhost:1234
Remote debugging using localhost:1234
warning: unrecognized item "ENN" in "qSupported" response
qTStatus: Target returns error code 'NN'.
0x0000fff0 in ?? ()
qTStatus: Target returns error code 'NN'.
```

뭔가 경고가 뜨긴 하지만 되는 것 같습니다. `c` 명령어로 계속 실행하도록 했습니다.

![alt text](image-3.png)

```
Booting from Hard Disk...
lapicid 0: panic: Expect to run on an SMP
 801031a1 80102e80 0 0 0 0 0 0 0
```

여전히 xv6에서 SMP 관련 에러가 발생합니다. 아무래도 xv6에서 SMP 요구 자체를 끌 필요가 있을 것 같습니다.

### 멀티코어 요구 제거를 위한 xv6 코드 수정

main.c 파일에서 멀티코어와 관련되어보이는 함수 `mpinit()`와 `startothers()` 등의 함수를 비활성화하면서 멀티코어 처리를 끄려고 해 보았으나, bochs에서 잘 작동하지 않았습니다. 따라서 코드는 다시 되돌렸습니다.

### qemu에서 gdb 붙여보기

qemu 기준으로 xv6를 다시 보도록 했습니다.

xv6 부팅에서 qemu는 다음과 같은 옵션을 사용합니다.

```makefile
ifndef CPUS
CPUS := 2
endif
QEMUOPTS = -drive file=fs.img,index=1,media=disk,format=raw -drive file=xv6.img,index=0,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)
```

CPUS로 CPU 개수를 설정한 후, qemu 옵션으로 바로 넣는 모습입니다. 여기서 사용된 CPUS 변수는 qemu에서만 사용됩니다. 여기서 우선 CPUS를 1로 바꾸고 돌려보았습니다.

![alt text](image-4.png)

아무 문제 없이 부팅되었습니다.

`make qemu-gdb` 옵션도 있어서, 시도해보았습니다.

```bash
$ make qemu-gdb
sed "s/localhost:1234/localhost:26000/" < .gdbinit.tmpl > .gdbinit
*** Now run 'gdb'.
qemu-system-i386 -serial mon:stdio -drive file=fs.img,index=1,media=disk,format=raw -drive file=xv6.img,index=0,media=disk,format=raw -smp 1 -m 512  -S -gdb tcp::26000
```

포트 26000을 열어주어서, gdb로 attach 해보았습니다.

![alt text](image-5.png)

바로 gdb가 붙었습니다. `c`를 입력하니 정상적으로 작동합니다.

gdb에 편의성 기능들을 추가해주는 `pwndbg`를 사용하니 gdbinit을 자동으로 읽어 attach 해주고 symbol도 가져와줬습니다.

Bochs에서 안 돼서 아쉽지만, 우선 이 방식이라도 [xv6-public 레포](https://github.com/seolcu/xv6-public)의 README.md에 정리해뒀습니다.

### 프로젝트 되돌아보기

프로젝트에서 너무 문제 해결에 몰두하다 보면, 원래의 목적에서 벗어나 문제 해결에만 몰두하는, 주객이 전도되는 상황이 일어나곤 합니다. 이를 방지하기 위해, 4주차에는 프로젝트를 되돌아보는 시간을 가졌습니다.

#### 프로젝트의 목표는 무엇이었을까

이 프로젝트의 최종 목표는 `리눅스 KVM API를 이용한 초소형 가상 머신 모니터 (VMM) 개발` 입니다.

#### OS

프로젝트를 위해선 VMM 위에서 돌아갈 OS가 필요했습니다. 처음에는 리눅스를 생각했지만, 리눅스는 디바이스 드라이버 요구사항이 너무 많아 조금 더 원시적인 OS가 필요했습니다.

따라서 교육용 OS인 `xv6`를 선택했습니다. 현재 `xv6`는 RISC-V로 개발중이기에, x86 기반으로 개발된 이전 버전을 사용하기로 했습니다.

바로 로컬 환경에서 `xv6`를 컴파일해보았는데, 컴파일러 버전이 너무 높아 문제가 발생했습니다. 따라서 distrobox로 Ubuntu 16.04 환경을 만들어 컴파일에 성공했습니다.

#### VMM

아무런 레퍼런스 없이 VMM을 스스로 만들기는 어렵기에, 다른 오픈소스 VMM을 참고하기로 했습니다.

대표적인 VMM으로는 QEMU와 Bochs가 있었습니다. 여기에서 QEMU는 너무 방대하기에, Bochs를 틀로 사용하기로 했습니다. 다만 Bochs는 KVM 등의 하드웨어 지원을 사용하지 않고, 명령어를 하나하나 번역하는 인터프리터 방식이었기에 KVM과 관련된 부분은 QEMU의 코드를 참고하기로 했습니다.

그러나 Ubuntu 16.04의 Bochs 버전이 너무 높아, xv6가 잘 작동하지 않았습니다. 따라서 xv6 메모에 명시된 대로 Bochs 2.2.6를 직접 컴파일해 쓰기로 했습니다. 이 과정에서 SMP를 활성화하면 GDB를 못 붙이고, SMP를 비활성화하면 xv6가 안 돌아가는 진퇴양난의 상황을 마주하게 됩니다.

반면, QEMU는 코어 설정과 상관없이 잘 실행되었고, GDB도 잘 붙었습니다.

### 앞으로 뭘 하면 좋을까?

교수님과 상담을 통해, 앞으로의 계획을 더 탄탄히 하고 싶습니다.
