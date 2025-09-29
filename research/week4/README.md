# 4주차 연구내용

목표: 계획 점검, 오픈소스 코드 리뷰, 추가 학습

저번주 todo:

- 전체 점검

## 연구내용

### SMP 관련 문제 해결하기

저번주에 Bochs 2.2.6을 컴파일하고 xv6를 디버거 없이 부팅해보았을때, 다음과 같은 에러가 발생했습니다.

![alt text](image.png)

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

저번에 디버거 붙였을 때랑 같은 에러가 발생했습니다.

### bochs에 gdb 붙여보기
