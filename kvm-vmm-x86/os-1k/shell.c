/*
 * shell.c - Simple shell for 1K OS
 *
 * x86 32-bit port with menu system
 */

#include "user.h"

/* Menu system */
static void show_menu(void) {
    printf("\n=== 1K OS Menu ===\n\n");
    printf("  1. Multiplication (2x1 ~ 9x9)\n");
    printf("  2. Counter (0-9)\n");
    printf("  3. Echo (echo your input)\n");
    printf("  4. About 1K OS\n");
    printf("  0. Exit\n");
    printf("\nSelect: ");
}

static void multiplication_demo(void) {
    printf("\n=== Multiplication Table ===\n");
    for (int i = 2; i <= 9; i++) {
        for (int j = 1; j <= 9; j++) {
            printf("%d*%d=%d ", i, j, i * j);
        }
        printf("\n");
    }
}

static void counter_demo(void) {
    printf("\n=== Counter 0-9 ===\n");
    for (int i = 0; i <= 9; i++) {
        printf("%d ", i);
    }
    printf("\n");
}

static void echo_demo(void) {
    printf("\n=== Echo Program (type 'quit' to exit) ===\n");
    while (1) {
        printf("$ ");
        char line[64];
        for (int i = 0;; i++) {
            char ch = getchar();
            if (ch == '\n') {
                line[i] = '\0';
                printf("\n");
                break;
            }
            putchar(ch);
            line[i] = ch;
        }

        if (strcmp(line, "quit") == 0) {
            printf("Exiting echo program\n");
            break;
        } else {
            printf("Echo: %s\n", line);
        }
    }
}

static void about_demo(void) {
    printf("\n=== About 1K OS ===\n");
    printf("1K OS: Operating System in 1000 Lines\n");
    printf("Ported from RISC-V to x86 Protected Mode\n");
    printf("Features:\n");
    printf("  - Protected Mode with Paging\n");
    printf("  - Keyboard and Timer Interrupts\n");
    printf("  - Simple Shell\n");
    printf("  - File System (tar format)\n");
}

void main(void) {
    printf("\nWelcome to 1K OS Shell!\n");
    printf("Type '1-4' to run demos, '0' to exit\n");

    while (1) {
        show_menu();

        char choice = getchar();
        printf("%c\n", choice);

        if (choice == '0') {
            printf("Exiting shell...\n");
            exit();
        } else if (choice == '1') {
            multiplication_demo();
        } else if (choice == '2') {
            counter_demo();
        } else if (choice == '3') {
            echo_demo();
        } else if (choice == '4') {
            about_demo();
        } else {
            printf("Unknown option: %c\n", choice);
        }
    }
}
