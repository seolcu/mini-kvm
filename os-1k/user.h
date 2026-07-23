#pragma once
#include "common.h"

void putchar(char ch);

// Blocking read of one character. Returns -1 at end of input.
int getchar(void);

__attribute__((noreturn)) void exit(void);

// Read a line of input with echo and backspace support.
// Returns the length of the input, excluding the null terminator.
// At end of input this terminates the process rather than returning.
int readline(char *buf, int bufsz);
