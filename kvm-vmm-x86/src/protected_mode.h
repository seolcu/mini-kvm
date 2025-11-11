#ifndef PROTECTED_MODE_H
#define PROTECTED_MODE_H

#include <stdint.h>

// GDT 관련 정의
#define GDT_SIZE 5              // Null, code, data, user code, user data
#define GDT_ENTRY_SIZE 8        // 각 GDT entry는 8바이트
#define GDT_TOTAL_SIZE (GDT_SIZE * GDT_ENTRY_SIZE)
#define GDT_ADDR 0x1000        // GDT를 게스트 메모리 0x1000에 배치

// Segment selectors (index * 8)
#define SEL_NULL   0x00        // Null descriptor
#define SEL_KCODE  0x08        // Kernel code segment
#define SEL_KDATA  0x10        // Kernel data segment
#define SEL_UCODE  0x18        // User code segment
#define SEL_UDATA  0x20        // User data segment

// GDT descriptor 구조 (8바이트)
typedef struct {
    uint16_t limit_low;       // Limit[15:0]
    uint16_t base_low;        // Base[15:0]
    uint8_t  base_mid;        // Base[23:16]
    uint8_t  access;          // Access byte
    uint8_t  limit_granular;  // Limit[19:16] + Granularity
    uint8_t  base_high;       // Base[31:24]
} __attribute__((packed)) gdt_entry_t;

// GDTR (GDT Register)
typedef struct {
    uint16_t limit;           // GDT 크기 - 1
    uint32_t base;            // GDT 주소
} __attribute__((packed)) gdtr_t;

// IDT entry (Protected Mode에서 필요함)
typedef struct {
    uint16_t offset_low;      // Offset[15:0]
    uint16_t selector;        // Code segment selector
    uint8_t  reserved;        // Always 0
    uint8_t  flags;           // Type and attributes
    uint16_t offset_high;     // Offset[31:16]
} __attribute__((packed)) idt_entry_t;

// IDTR (IDT Register)
typedef struct {
    uint16_t limit;           // IDT 크기 - 1
    uint32_t base;            // IDT 주소
} __attribute__((packed)) idtr_t;

// Access byte 값들 (descriptor type + attributes)
#define ACCESS_CODE_R  0x9A    // Code segment, readable, present, ring 0
#define ACCESS_DATA_W  0x92    // Data segment, writable, present, ring 0

// Limit + Granularity byte
#define LIMIT_GRAN     0xC0    // Granularity = 1 (4KB), DB = 1 (32-bit)

#endif // PROTECTED_MODE_H
