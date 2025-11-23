#pragma once
#include "common.h"

void putchar(char ch);
int getchar(void);
int readfile(const char *filename, char *buf, int len);
int writefile(const char *filename, const char *buf, int len);
__attribute__((noreturn)) void exit(void);

// Flush output buffer (ensures prompts are displayed immediately)
void flush_output(void);

// Read a line of input with echo and backspace support
// Returns length of input (excluding null terminator)
// Buffer must have space for at least bufsz characters + null terminator
int readline(char *buf, int bufsz);
