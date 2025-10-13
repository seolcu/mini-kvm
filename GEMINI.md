# Gemini Project Context: mini-kvm

## Directory Overview

This directory contains the research notes, meeting logs, and documentation for the "mini-kvm" project. It is a self-directed project for Ajou University, focused on developing a lightweight Virtual Machine Monitor (VMM) using the Linux KVM API. The entire documentation is in Korean.

The project's main goal is to run the `xv6` educational operating system on a custom-built VMM. The development process involves referencing established open-source emulators like `Bochs` and `QEMU`.

## Key Files & Structure

-   **`README.md`**: The primary entry point for the project. It outlines the 16-week project schedule, from initial research to final presentation, and contains a curated list of links to reference projects (Bochs, xv6, QEMU), documentation (KVM API), and tutorials.

-   **`meetings/`**: This directory contains markdown files (`week1.md`, `week2.md`, etc.) documenting the weekly meetings with the project advisor. These notes summarize key advice, decisions, and next steps. For example, the advice to use `xv6` instead of a full Linux kernel and to reference `Bochs`'s source code comes from these meetings.

-   **`research/`**: This directory holds the detailed weekly research and development logs. Each sub-directory (e.g., `week1/`, `week2/`) contains a `README.md` that chronicles the technical journey, including:
    -   **Challenges:** Detailed accounts of problems encountered, such as compiling the legacy x86 version of `xv6` with modern compilers and the significant difficulties in configuring and compiling an old version of `Bochs` (2.2.6).
    -   **Solutions:** The steps taken to resolve issues, like using `distrobox` to create an older Ubuntu environment for compilation, modifying Makefiles, and patching C++ source code to work with newer compilers.
    -   **Discoveries:** Learnings about build systems, compiler flags (`-Werror`), emulator configurations (`.bochsrc`), and debugging with GDB.

## Usage

This repository serves as a comprehensive log of the research and development process for the mini-kvm VMM. It is not a source code repository for the VMM itself, but rather a documentation of the learning, problem-solving, and decision-making that goes into building it.

When interacting with this project, refer to the `research/` directory for technical context on what has been attempted and the `meetings/` directory for the project's direction and guidance.
