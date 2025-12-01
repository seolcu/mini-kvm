/*
 * Debug utilities for Mini-KVM VMM
 *
 * Provides comprehensive debugging infrastructure:
 * - Enhanced verbose logging for all VM exits
 * - Register and CPU state dumps
 * - Memory inspection and dumps
 * - Page table walking
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <linux/kvm.h>

// Debug verbosity levels
typedef enum {
    DEBUG_NONE = 0,     // No debug output
    DEBUG_BASIC = 1,    // Basic VM exits and hypercalls
    DEBUG_DETAILED = 2, // Detailed register states
    DEBUG_ALL = 3       // Everything including I/O port accesses
} debug_level_t;

// Global debug configuration
extern debug_level_t debug_level;

// Macros for conditional debug output
#define DEBUG_PRINT(level, fmt, ...) \
    do { \
        if (debug_level >= (level)) { \
            fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while(0)

#define DEBUG_VMEXIT(vcpu_id, fmt, ...) \
    DEBUG_PRINT(DEBUG_BASIC, "[vCPU %d] VM-EXIT: " fmt, vcpu_id, ##__VA_ARGS__)

#define DEBUG_HC(vcpu_id, fmt, ...) \
    DEBUG_PRINT(DEBUG_BASIC, "[vCPU %d] HYPERCALL: " fmt, vcpu_id, ##__VA_ARGS__)

#define DEBUG_IO(vcpu_id, fmt, ...) \
    DEBUG_PRINT(DEBUG_ALL, "[vCPU %d] I/O: " fmt, vcpu_id, ##__VA_ARGS__)

// Register dump functions
void dump_general_registers(int vcpu_fd, int vcpu_id);
void dump_special_registers(int vcpu_fd, int vcpu_id);
void dump_segment_registers(struct kvm_sregs *sregs, int vcpu_id);
void dump_control_registers(struct kvm_sregs *sregs, int vcpu_id);
void dump_all_registers(int vcpu_fd, int vcpu_id);

// Memory inspection functions
void dump_memory_region(void *mem, uint64_t guest_addr, size_t size, const char *label);
void dump_memory_to_file(void *mem, size_t size, const char *filename);
void dump_guest_memory_map(void *mem, size_t mem_size);

// Page table walking (for debugging paging issues)
void walk_page_tables_32bit(void *mem, uint32_t cr3, uint32_t virt_addr);
void walk_page_tables_pae(void *mem, uint32_t cr3, uint32_t virt_addr);
void walk_page_tables_64bit(void *mem, uint64_t cr3, uint64_t virt_addr);

// VM exit analysis
const char *get_exit_reason_string(uint32_t exit_reason);
void print_vm_exit_details(struct kvm_run *run, int vcpu_id);

// Stack trace helpers
void dump_guest_stack(void *mem, uint32_t esp, uint32_t ss_base, int count);

// Instruction disassembly helper (shows bytes around RIP)
void dump_instruction_context(void *mem, uint64_t rip, int bytes_before, int bytes_after);

#endif // DEBUG_H
