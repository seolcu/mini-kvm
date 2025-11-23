This project is a minimal, educational x86 hypervisor (VMM) built using the Linux KVM API. It's written primarily in C, with some x86 assembly for guest code.

### Project Overview

The hypervisor, `mini-kvm`, can run multiple vCPUs simultaneously, supporting both simple 16-bit real-mode guests and a more complex 32-bit protected-mode guest OS named "1K OS". The project demonstrates core virtualization concepts, including memory management, vCPU creation, I/O emulation through a UART serial port, and guest-host communication via hypercalls.

The project is structured into three main parts:

1.  **`hypervisor`**: A Rust-based hypervisor.
2.  **`kvm-vmm-x86`**: The C-based VMM, which is the main focus of the `README.md`.
3.  **`HLeOs`**: A small OS written in Rust.

This document will focus on the C-based VMM in `kvm-vmm-x86` as it appears to be the most complete and documented part of the project.

### Building and Running

The project uses `make` for building the VMM and guest operating systems.

**Prerequisites:**

*   GCC, Make, Binutils
*   QEMU/KVM
*   Rust (for the Rust-based components)

**Build Commands:**

*   **Build the VMM (C version):**
    ```bash
    cd kvm-vmm-x86
    make vmm
    ```

*   **Build the real-mode guests:**
    ```bash
    cd kvm-vmm-x86/guest
    ./build.sh
    ```

*   **Build the 1K OS (protected-mode guest):**
    ```bash
    cd kvm-vmm-x86/os-1k
    make
    ```

*   **Build the Rust-based hypervisor:**
    ```bash
    cd hypervisor
    cargo build
    ```

*   **Build HLeOs:**
    ```bash
    cd HLeOs/HLeOs
    make
    ```

**Running the VMM:**

The C-based VMM is run from the command line, with the guest binary as an argument.

*   **Run a real-mode guest:**
    ```bash
    ./kvm-vmm-x86/kvm-vmm ./kvm-vmm-x86/guest/hello.bin
    ```

*   **Run multiple real-mode guests on multiple vCPUs:**
    ```bash
    ./kvm-vmm-x86/kvm-vmm ./kvm-vmm-x86/guest/multiplication.bin ./kvm-vmm-x86/guest/counter.bin
    ```

*   **Run the 1K OS (protected-mode):**
    ```bash
    printf "1\n0\n" | ./kvm-vmm-x86/kvm-vmm --paging ./kvm-vmm-x86/os-1k/kernel.bin
    ```

### Development Conventions

*   **Code Style:** The C code follows the K&R style with 4-space indentation. Functions are named using `snake_case`, and macros are in `UPPER_CASE`. The assembly code uses Intel syntax and lowercase mnemonics.
*   **Comments:** Comments are used to explain the "why" behind the code, not just the "what".
*   **Testing:** The project includes several test cases that can be run via `make` targets (e.g., `make test`, `make test-hello`).

### Key Files

*   `kvm-vmm-x86/src/main.c`: The core of the C-based VMM. It handles VM and vCPU creation, memory management, and the main VM exit loop.
*   `kvm-vmm-x86/Makefile`: The main `Makefile` for the C-based VMM.
*   `kvm-vmm-x86/guest/`: Contains the source code for the simple real-mode guests.
*   `kvm-vmm-x86/os-1k/`: Contains the source code for the 1K OS, the protected-mode guest.
*   `hypervisor/src/main.rs`: The entry point for the Rust-based hypervisor.
*   `HLeOs/HLeOs/src/main.rs`: The main source file for the HLeOs operating system.
*   `README.md`: The main documentation for the project.
