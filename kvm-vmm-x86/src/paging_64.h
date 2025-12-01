/*
 * 64-bit paging setup header for Mini-KVM
 */

#ifndef PAGING_64_H
#define PAGING_64_H

#include <stdint.h>
#include <stddef.h>

// Setup identity-mapped 4-level page tables
// Returns CR3 value (physical address of PML4)
uint64_t setup_page_tables_64bit(void *guest_mem, size_t mem_size);

// Setup 4-level page tables with kernel/user split
// Maps high memory (kernel_virt_base+) to physical 0
uint64_t setup_page_tables_64bit_kernel(void *guest_mem, size_t mem_size,
                                        uint64_t kernel_virt_base);

// Verify page table setup (debugging)
void verify_page_tables_64bit(void *guest_mem, uint64_t test_va);

#endif // PAGING_64_H
