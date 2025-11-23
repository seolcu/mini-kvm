# 3주차 상담내용 (09/16)

- `./configure` 옵션을 바꿔보자. 아래와 같은 옵션을 추가해보자.
  - `--disable-vga`
  - `--with-term`
  - `--with-nogui`
- 기존 `./configure` 옵션에서, 아래와 같은 옵션은 빼도 될 것 같다.
  - `--enable-smp`: 싱글코어로 할 것이다.
  - `--enable-all-optimizations --enable-4meg-pages --enable-global-pages`: 지금은 부가적인 기능은 빼는 게 좋다.
- 코드 부분에서는, `config.cc`를 전부 읽기보다는 필요한 것만 남기고 최대한 다 빼는 게 좋다.
  - ata: ata0 master 하나만 있어도 될 것 같음.
  - floppy: 제거
  - vga: 제거
  - serial mode: `term` 또는 `file`로
  - usb: 제거
  - mouse: 제거
  - 기타 디스플레이: 제거
  - 네트워크: 제거
  - 클럭: 제거
  - pci 디바이스: 제거
