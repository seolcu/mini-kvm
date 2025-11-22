/*
 * shell.c - Simple shell for 1K OS
 *
 * x86 32-bit port with menu system
 */

#include "user.h"

/* Menu system */
static void show_menu(void) {
    printf("\n=== 1K OS Menu ===\n\n");
    printf("  1. Multiplication Table (2x1 ~ 9x9)\n");
    printf("  2. Counter (0-9)\n");
    printf("  3. Echo (interactive)\n");
    printf("  4. Fibonacci Sequence\n");
    printf("  5. Prime Numbers (up to 100)\n");
    printf("  6. Calculator\n");
    printf("  7. Factorial (0! ~ 12!)\n");
    printf("  8. GCD (Greatest Common Divisor)\n");
    printf("  9. About 1K OS\n");
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

static void fibonacci_demo(void) {
    printf("\n=== Fibonacci Sequence (first 15 numbers) ===\n");
    int a = 0, b = 1;
    printf("F(0) = %d\n", a);
    printf("F(1) = %d\n", b);
    
    for (int i = 2; i < 15; i++) {
        int next = a + b;
        printf("F(%d) = %d\n", i, next);
        a = b;
        b = next;
    }
}

static void prime_demo(void) {
    printf("\n=== Prime Numbers up to 100 ===\n");
    int count = 0;
    
    for (int n = 2; n <= 100; n++) {
        int is_prime = 1;
        for (int i = 2; i * i <= n; i++) {
            if (n % i == 0) {
                is_prime = 0;
                break;
            }
        }
        if (is_prime) {
            printf("%d ", n);
            count++;
            if (count % 10 == 0) {
                printf("\n");
            }
        }
    }
    if (count % 10 != 0) {
        printf("\n");
    }
    printf("Total: %d primes\n", count);
}

static void calculator_demo(void) {
    printf("\n=== Simple Calculator ===\n");
    printf("Enter two numbers and operator (+, -, *, /)\n");
    printf("Example: 12 + 5\n");
    printf("Type 'q' to quit\n\n");
    
    while (1) {
        printf("Calculate: ");
        
        // Read first number
        int num1 = 0;
        int negative1 = 0;
        char ch = getchar();
        if (ch == 'q') {
            printf("q\nExiting calculator\n");
            break;
        }
        if (ch == '-') {
            negative1 = 1;
            putchar(ch);
            ch = getchar();
        }
        while (ch >= '0' && ch <= '9') {
            putchar(ch);
            num1 = num1 * 10 + (ch - '0');
            ch = getchar();
        }
        if (negative1) num1 = -num1;
        
        // Skip spaces
        while (ch == ' ') {
            putchar(ch);
            ch = getchar();
        }
        
        // Read operator
        char op = ch;
        putchar(op);
        
        // Skip spaces
        ch = getchar();
        while (ch == ' ') {
            putchar(ch);
            ch = getchar();
        }
        
        // Read second number
        int num2 = 0;
        int negative2 = 0;
        if (ch == '-') {
            negative2 = 1;
            putchar(ch);
            ch = getchar();
        }
        while (ch >= '0' && ch <= '9') {
            putchar(ch);
            num2 = num2 * 10 + (ch - '0');
            ch = getchar();
        }
        if (negative2) num2 = -num2;
        
        // Skip to newline
        while (ch != '\n') {
            ch = getchar();
        }
        printf("\n");
        
        // Calculate
        int result = 0;
        int valid = 1;
        
        if (op == '+') {
            result = num1 + num2;
        } else if (op == '-') {
            result = num1 - num2;
        } else if (op == '*') {
            result = num1 * num2;
        } else if (op == '/') {
            if (num2 == 0) {
                printf("Error: Division by zero\n");
                valid = 0;
            } else {
                result = num1 / num2;
            }
        } else {
            printf("Error: Unknown operator '%c'\n", op);
            valid = 0;
        }
        
        if (valid) {
            printf("Result: %d\n", result);
        }
    }
}

static void factorial_demo(void) {
    printf("\n=== Factorial Calculator ===\n");
    printf("Calculating factorials for n = 0 to 12\n\n");
    
    int result = 1;
    printf("0! = %d\n", result);
    
    for (int n = 1; n <= 12; n++) {
        result *= n;
        printf("%d! = %d\n", n, result);
    }
    printf("\nNote: 13! = 6227020800 (overflow on 32-bit)\n");
}

static void gcd_demo(void) {
    printf("\n=== GCD (Greatest Common Divisor) ===\n");
    printf("Examples of Euclidean algorithm:\n\n");
    
    int pairs[][2] = {
        {48, 18},
        {100, 75},
        {123, 456},
        {17, 19},
        {1071, 462}
    };
    
    for (int i = 0; i < 5; i++) {
        int a = pairs[i][0];
        int b = pairs[i][1];
        int orig_a = a, orig_b = b;
        
        // Euclidean algorithm
        while (b != 0) {
            int temp = b;
            b = a % b;
            a = temp;
        }
        
        printf("GCD(%d, %d) = %d\n", orig_a, orig_b, a);
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
    printf("  - User Programs: 9 demos\n");
    printf("\nMini-KVM VMM Project\n");
    printf("Educational hypervisor using KVM API\n");
}

void main(void) {
    printf("\n======================================\n");
    printf("   Welcome to 1K OS Shell!\n");
    printf("   Mini-KVM Educational Hypervisor\n");
    printf("======================================\n");
    printf("\nType '1-9' to run demos, '0' to exit\n");

    while (1) {
        show_menu();

        char choice = getchar();
        printf("%c\n", choice);

        if (choice == '0') {
            printf("\nExiting shell...\n");
            printf("Thank you for using 1K OS!\n");
            exit();
        } else if (choice == '1') {
            multiplication_demo();
        } else if (choice == '2') {
            counter_demo();
        } else if (choice == '3') {
            echo_demo();
        } else if (choice == '4') {
            fibonacci_demo();
        } else if (choice == '5') {
            prime_demo();
        } else if (choice == '6') {
            calculator_demo();
        } else if (choice == '7') {
            factorial_demo();
        } else if (choice == '8') {
            gcd_demo();
        } else if (choice == '9') {
            about_demo();
        } else {
            printf("Unknown option: %c\n", choice);
            printf("Please select 0-9\n");
        }
    }
}
