/*
 * 64-bit paging setup for Mini-KVM
 *
 * Implements 4-level page tables (PML4 → PDPT → PD → PT)
 * for x86-64 Long Mode operation.
 */

#include "paging_64.h"
#include "long_mode.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

// Setup 4-level paging for 64-bit guest
// Maps first 'mem_size' bytes as identity mapping (VA = PA)
// Returns CR3 value (physical address of PML4)
uint64_t setup_page_tables_64bit(void *guest_mem, size_t mem_size) {
    DEBUG_PRINT(DEBUG_DETAILED, "Setting up 64-bit 4-level page tables");
    DEBUG_PRINT(DEBUG_DETAILED, "Guest memory size: %zu MB", mem_size / (1024*1024));
    
    // Clear page table regions
    memset((char *)guest_mem + GUEST_64_PML4_ADDR, 0, 0x4000); // Clear 16KB for all tables
    
    // Get pointers to each level
    pml4_t *pml4 = (pml4_t *)((char *)guest_mem + GUEST_64_PML4_ADDR);
    pdpt_t *pdpt = (pdpt_t *)((char *)guest_mem + GUEST_64_PDPT_ADDR);
    pd_t *pd = (pd_t *)((char *)guest_mem + GUEST_64_PD_ADDR);
    
    // PML4[0] → PDPT (covers 512GB of virtual address space)
    pml4->entries[0] = GUEST_64_PDPT_ADDR | PTE_PRESENT | PTE_WRITE | PTE_USER;
    DEBUG_PRINT(DEBUG_DETAILED, "PML4[0] = 0x%llx → PDPT at 0x%x", 
               (unsigned long long)pml4->entries[0], GUEST_64_PDPT_ADDR);
    
    // PDPT[0] → PD (covers 1GB of virtual address space)
    pdpt->entries[0] = GUEST_64_PD_ADDR | PTE_PRESENT | PTE_WRITE | PTE_USER;
    DEBUG_PRINT(DEBUG_DETAILED, "PDPT[0] = 0x%llx → PD at 0x%x", 
               (unsigned long long)pdpt->entries[0], GUEST_64_PD_ADDR);
    
    // PD entries: Use 2MB pages (PSE bit set)
    // Map enough 2MB pages to cover guest memory
    size_t num_pages_2mb = (mem_size + (2 << 20) - 1) / (2 << 20); // Round up
    
    DEBUG_PRINT(DEBUG_DETAILED, "Creating %zu PD entries (2MB pages)", num_pages_2mb);
    
    for (size_t i = 0; i < num_pages_2mb && i < PT_ENTRIES; i++) {
        uint64_t phys_addr = i * (2ULL << 20); // Each page is 2MB
        pd->entries[i] = phys_addr | PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_PSE;
        
        if (i < 4 || i == num_pages_2mb - 1) {
            DEBUG_PRINT(DEBUG_ALL, "PD[%zu] = 0x%llx (maps 0x%llx-0x%llx)", 
                       i, (unsigned long long)pd->entries[i],
                       (unsigned long long)phys_addr,
                       (unsigned long long)(phys_addr + (2 << 20) - 1));
        } else if (i == 4) {
            DEBUG_PRINT(DEBUG_ALL, "... (%zu more entries)", num_pages_2mb - 5);
        }
    }
    
    DEBUG_PRINT(DEBUG_BASIC, "64-bit page tables setup complete");
    DEBUG_PRINT(DEBUG_BASIC, "Identity mapping: 0x0 - 0x%lx", 
               (unsigned long)(num_pages_2mb * (2 << 20) - 1));
    
    return GUEST_64_PML4_ADDR;
}

// Setup 4-level paging with kernel/user split
// Identity maps lower memory, maps high memory (0xFFFFFFFF80000000+) to physical 0
uint64_t setup_page_tables_64bit_kernel(void *guest_mem, size_t mem_size,
                                       uint64_t kernel_virt_base) {
    DEBUG_PRINT(DEBUG_DETAILED, "Setting up 64-bit page tables with kernel mapping");
    DEBUG_PRINT(DEBUG_DETAILED, "Kernel virtual base: 0x%llx", 
               (unsigned long long)kernel_virt_base);
    
    // Clear page table regions
    memset((char *)guest_mem + GUEST_64_PML4_ADDR, 0, 0x4000);
    
    // Get pointers to each level
    pml4_t *pml4 = (pml4_t *)((char *)guest_mem + GUEST_64_PML4_ADDR);
    pdpt_t *pdpt_low = (pdpt_t *)((char *)guest_mem + GUEST_64_PDPT_ADDR);
    pd_t *pd = (pd_t *)((char *)guest_mem + GUEST_64_PD_ADDR);
    
    // Setup identity mapping for lower memory (PML4[0])
    pml4->entries[0] = GUEST_64_PDPT_ADDR | PTE_PRESENT | PTE_WRITE | PTE_USER;
    pdpt_low->entries[0] = GUEST_64_PD_ADDR | PTE_PRESENT | PTE_WRITE | PTE_USER;
    
    // Setup PD entries with 2MB pages for identity mapping
    size_t num_pages_2mb = (mem_size + (2 << 20) - 1) / (2 << 20);
    
    for (size_t i = 0; i < num_pages_2mb && i < PT_ENTRIES; i++) {
        uint64_t phys_addr = i * (2ULL << 20);
        pd->entries[i] = phys_addr | PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_PSE;
    }
    
    // Setup kernel mapping at high address
    // For simplicity, use the same PD for kernel space (mirrors low memory)
    uint64_t pml4_index = VA_PML4_INDEX(kernel_virt_base);
    uint64_t pdpt_index = VA_PDPT_INDEX(kernel_virt_base);
    
    DEBUG_PRINT(DEBUG_DETAILED, "Kernel mapping: PML4[%llu], PDPT[%llu]", 
               (unsigned long long)pml4_index, (unsigned long long)pdpt_index);
    
    // For now, use same PDPT and PD (simple mirror mapping)
    pml4->entries[pml4_index] = GUEST_64_PDPT_ADDR | PTE_PRESENT | PTE_WRITE;
    pdpt_low->entries[pdpt_index] = GUEST_64_PD_ADDR | PTE_PRESENT | PTE_WRITE;
    
    DEBUG_PRINT(DEBUG_BASIC, "64-bit kernel page tables setup complete");
    DEBUG_PRINT(DEBUG_BASIC, "Lower mapping: 0x0 - 0x%lx (identity)", 
               (unsigned long)(num_pages_2mb * (2 << 20) - 1));
    DEBUG_PRINT(DEBUG_BASIC, "Kernel mapping: 0x%llx+ → 0x0+ (mirror)", 
               (unsigned long long)kernel_virt_base);
    
    return GUEST_64_PML4_ADDR;
}

// Verify page table setup (for debugging)
void verify_page_tables_64bit(void *guest_mem, uint64_t test_va) {
    DEBUG_PRINT(DEBUG_DETAILED, "Verifying page tables for VA 0x%llx", 
               (unsigned long long)test_va);
    
    pml4_t *pml4 = (pml4_t *)((char *)guest_mem + GUEST_64_PML4_ADDR);
    
    uint64_t pml4_idx = VA_PML4_INDEX(test_va);
    uint64_t pdpt_idx = VA_PDPT_INDEX(test_va);
    uint64_t pd_idx = VA_PD_INDEX(test_va);
    uint64_t offset = test_va & 0x1FFFFF; // 2MB page offset
    
    DEBUG_PRINT(DEBUG_DETAILED, "Indices: PML4[%llu] PDPT[%llu] PD[%llu] offset=0x%llx",
               (unsigned long long)pml4_idx, (unsigned long long)pdpt_idx,
               (unsigned long long)pd_idx, (unsigned long long)offset);
    
    pml4e_t pml4e = pml4->entries[pml4_idx];
    if (!(pml4e & PTE_PRESENT)) {
        DEBUG_PRINT(DEBUG_BASIC, "PML4[%llu] not present!", (unsigned long long)pml4_idx);
        return;
    }
    
    pdpt_t *pdpt = (pdpt_t *)((char *)guest_mem + (pml4e & PTE_ADDR_MASK));
    pdpte_t pdpte = pdpt->entries[pdpt_idx];
    if (!(pdpte & PTE_PRESENT)) {
        DEBUG_PRINT(DEBUG_BASIC, "PDPT[%llu] not present!", (unsigned long long)pdpt_idx);
        return;
    }
    
    pd_t *pd = (pd_t *)((char *)guest_mem + (pdpte & PTE_ADDR_MASK));
    pde_t pde = pd->entries[pd_idx];
    if (!(pde & PTE_PRESENT)) {
        DEBUG_PRINT(DEBUG_BASIC, "PD[%llu] not present!", (unsigned long long)pd_idx);
        return;
    }
    
    if (pde & PTE_PSE) {
        // 2MB page
        uint64_t phys_addr = (pde & ~0x1FFFFF) | offset;
        DEBUG_PRINT(DEBUG_BASIC, "VA 0x%llx → PA 0x%llx (2MB page)", 
                   (unsigned long long)test_va, (unsigned long long)phys_addr);
    } else {
        DEBUG_PRINT(DEBUG_BASIC, "4KB pages not implemented in verification");
    }
}
