/*
 * kernel.c - 1K OS x86 port
 *
 * Ported from RISC-V RV32 to x86 Protected Mode (32-bit)
 */

#include "kernel.h"
#include "common.h"

/* Linker-provided symbols */
extern char __kernel_base[];
extern char __stack_top[];
extern char __bss[];
extern char __bss_end[];
extern char __free_ram[];
extern char __free_ram_end[];
extern char _binary_shell_bin_start[];
extern char _binary_shell_bin_size[];

/* Global process table */
struct process procs[PROCS_MAX];
struct process *current_proc;
struct process *idle_proc;

/*
 * Page allocator: simple bump allocator
 * Allocates pages from free RAM region
 * Returns VIRTUAL address (can be converted to physical by subtracting 0x80000000)
 */
paddr_t alloc_pages(uint32_t n) {
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t vaddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) vaddr, 0, n * PAGE_SIZE);
    return vaddr;  // Returns VIRTUAL address
}

/*
 * Map a virtual address to a physical address
 * Uses x86 32-bit 2-level paging (10-10-12 split)
 * 
 * page_table: Level 1 page directory (1024 entries)
 * vaddr: Virtual address to map
 * paddr: Physical address to map to
 * flags: Page flags (PAGE_P, PAGE_RW, PAGE_U)
 */
void map_page(uint32_t *page_table, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    /* Extract page directory index (bits 31:22) */
    uint32_t pd_index = (vaddr >> 22) & 0x3FF;
    
    /* Check if page table exists, create if not */
    if ((page_table[pd_index] & PAGE_P) == 0) {
        uint32_t pt_vaddr = alloc_pages(1);
        uint32_t pt_paddr = pt_vaddr - 0x80000000;  // Convert virtual to physical
        page_table[pd_index] = (pt_paddr & 0xFFFFF000) | PAGE_P | PAGE_RW | PAGE_U;
    }

    /* Extract page table index (bits 21:12) */
    uint32_t pt_index = (vaddr >> 12) & 0x3FF;
    
    /* Get page table address - convert physical to virtual (high-half mapping) */
    uint32_t pt_paddr = page_table[pd_index] & 0xFFFFF000;
    uint32_t *pt = (uint32_t *) (pt_paddr + 0x80000000);
    
    /* Map the page */
    pt[pt_index] = (paddr & 0xFFFFF000) | flags | PAGE_P;
}

/*
 * Hypercall interface for putchar
 * Uses port 0x500 with syscall number in AL
 */
void putchar(char ch) {
    __asm__ volatile(
        "movb %0, %%bl\n\t"
        "movb $1, %%al\n\t"
        "movw $0x500, %%dx\n\t"
        "outb %%al, %%dx"
        :
        : "r"((uint8_t)ch)
        : "al", "bl", "dx"
    );
}

/*
 * Hypercall interface for getchar
 * Uses port 0x500 with syscall number in AL
 * Returns character in AL
 * 
 * WORKAROUND: IN instructions don't trap in KVM with current setup.
 * Instead of using IN, we write result to a fixed memory location.
 * Physical address: 0x4000 (16KB, safely after kernel which ends at ~0x3118)
 * Virtual address: Use IDENTITY-MAPPED address 0x4000 directly
 */
#define HYPERCALL_RESULT_ADDR ((volatile int *)0x4000)

long getchar(void) {
    // Request GETCHAR via OUT
    // VMM will write result to memory before KVM_RUN returns
    __asm__ volatile(
        "movb $2, %%al\n\t"        // HC_GETCHAR
        "movw $0x500, %%dx\n\t"    // Port 0x500
        "outb %%al, %%dx\n\t"      // Trigger GETCHAR hypercall (OUT)
        :
        :
        : "al", "dx", "memory"     // memory clobber forces reload
    );
    
    // Invalidate TLB entry for the result address to ensure we see VMM's write
    // Use INVLPG instruction to invalidate specific page
    __asm__ volatile(
        "invlpg (%0)"
        :
        : "r" (HYPERCALL_RESULT_ADDR)
        : "memory"
    );
    
    // Result is now in memory, written by VMM during VM exit handling
    volatile int *addr = HYPERCALL_RESULT_ADDR;
    int result = *addr;
    
    return result;
}

/* Filesystem */
struct file files[FILES_MAX];
uint8_t disk[DISK_MAX_SIZE];

/*
 * Convert octal string to integer (for tar headers)
 */
int oct2int(char *oct, int len) {
    int dec = 0;
    for (int i = 0; i < len; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;

        dec = dec * 8 + (oct[i] - '0');
    }
    return dec;
}

/*
 * Flush filesystem to disk
 * Note: Since we don't have virtio-blk, this just updates in-memory disk
 */
void fs_flush(void) {
    memset(disk, 0, sizeof(disk));
    unsigned off = 0;
    
    for (int file_i = 0; file_i < FILES_MAX; file_i++) {
        struct file *file = &files[file_i];
        if (!file->in_use)
            continue;

        struct tar_header *header = (struct tar_header *) &disk[off];
        memset(header, 0, sizeof(*header));
        strcpy(header->name, file->name);
        strcpy(header->mode, "000644");
        strcpy(header->magic, "ustar");
        strcpy(header->version, "00");
        header->type = '0';

        int filesz = file->size;
        for (int i = sizeof(header->size); i > 0; i--) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++)
            checksum += (unsigned char) disk[off + i];

        for (int i = 5; i >= 0; i--) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }

    printf("wrote %d bytes to disk\n", sizeof(disk));
}

/*
 * Initialize filesystem from embedded tar archive
 */
void fs_init(void) {
    /* In-memory filesystem - no disk I/O */
    unsigned off = 0;
    
    for (int i = 0; i < FILES_MAX; i++) {
        struct tar_header *header = (struct tar_header *) &disk[off];
        if (header->name[0] == '\0')
            break;

        if (strcmp(header->magic, "ustar") != 0)
            PANIC("invalid tar header: magic=\"%s\"", header->magic);

        int filesz = oct2int(header->size, sizeof(header->size));
        struct file *file = &files[i];
        file->in_use = true;
        strcpy(file->name, header->name);
        memcpy(file->data, header->data, filesz);
        file->size = filesz;
        printf("file: %s, size=%d\n", file->name, file->size);

        off += align_up(sizeof(struct tar_header) + filesz, SECTOR_SIZE);
    }
}

/*
 * Lookup file by name
 */
struct file *fs_lookup(const char *filename) {
    for (int i = 0; i < FILES_MAX; i++) {
        struct file *file = &files[i];
        if (!strcmp(file->name, filename))
            return file;
    }

    return NULL;
}

/*
 * User entry point
 * This transfers control from kernel mode to user mode
 * by jumping to the user's code at USER_BASE
 */
__attribute__((naked)) void user_entry(void) {
    __asm__ volatile(
        // Jump to user code at USER_BASE (0x01000000)
        // The shell binary's entry point is at this address
        "movl $0x01000000, %%eax\n\t"
        "jmp *%%eax\n\t"
        :
        :
        : "eax"
    );
}

/*
 * Context switch between processes
 * Saves current context and loads next context
 * 
 * prev_sp: Pointer to save current ESP
 * next_sp: Pointer to load new ESP from
 */
__attribute__((naked)) void switch_context(uint32_t *prev_sp, uint32_t *next_sp) {
    __asm__ volatile(
        // Save callee-saved registers
        "pushl %%ebx\n\t"
        "pushl %%esi\n\t"
        "pushl %%edi\n\t"
        "pushl %%ebp\n\t"
        
        // Save current ESP to *prev_sp
        "movl 20(%%esp), %%eax\n\t"    // prev_sp (after 4 pushes)
        "movl %%esp, (%%eax)\n\t"
        
        // Load new ESP from *next_sp
        "movl 24(%%esp), %%eax\n\t"    // next_sp
        "movl (%%eax), %%esp\n\t"
        
        // Restore callee-saved registers
        "popl %%ebp\n\t"
        "popl %%edi\n\t"
        "popl %%esi\n\t"
        "popl %%ebx\n\t"
        
        "ret\n\t"
        :
        :
        : "memory"
    );
}

/*
 * Create a new process
 * Allocates page table, maps kernel and user pages, sets up stack
 */
struct process *create_process(const void *image, size_t image_size) {
    struct process *proc = NULL;
    int i;
    
    /* Find free process slot */
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        PANIC("no free process slots");

    /* Setup initial stack frame for context switch */
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // EBP
    *--sp = 0;                      // EDI
    *--sp = 0;                      // ESI
    *--sp = 0;                      // EBX
    *--sp = (uint32_t) user_entry;  // Return address (will jump to user_entry)

    /* Allocate page directory - alloc_pages returns VIRTUAL address */
    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    /* Map kernel pages (high half mapping)
     * Virtual 0x80000000+ maps to physical 0x0+
     * Kernel is loaded at physical 0x1000 = virtual 0x80001000
     * 
     * Map entire 4MB region for simplicity (matches VMM setup)
     */
    /* Map entire 4MB kernel region (0x80000000-0x80400000) */
    for (uint32_t vaddr = 0x80000000; vaddr < 0x80400000; vaddr += PAGE_SIZE) {
        paddr_t paddr = vaddr - 0x80000000;  // Convert virtual to physical
        map_page(page_table, vaddr, paddr, PAGE_RW);
    }
    
    /* Also create identity mapping for low 4MB (needed for GDT access during transitions) */
    for (uint32_t vaddr = 0; vaddr < 0x400000; vaddr += PAGE_SIZE) {
        map_page(page_table, vaddr, vaddr, PAGE_RW);
    }

    /* Map user pages for code/data */
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page_vaddr = alloc_pages(1);
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;
        memcpy((void *) page_vaddr, image + off, copy_size);
        /* Convert virtual to physical for mapping */
        paddr_t page_paddr = page_vaddr - 0x80000000;
        map_page(page_table, USER_BASE + off, page_paddr, PAGE_U | PAGE_RW);
    }
    
    /* Map additional pages for user stack 
     * Shell expects stack at 0x01003000, so map pages from code end to stack top
     * Allocate enough pages to cover from USER_BASE to 0x01004000 (16KB total)
     */
    uint32_t code_end = USER_BASE + ((image_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    uint32_t stack_top = 0x01004000;  // Shell uses stack starting at 0x01003000
    for (uint32_t vaddr = code_end; vaddr < stack_top; vaddr += PAGE_SIZE) {
        paddr_t page_vaddr = alloc_pages(1);
        paddr_t page_paddr = page_vaddr - 0x80000000;
        map_page(page_table, vaddr, page_paddr, PAGE_U | PAGE_RW);
    }

    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    /* Convert page table virtual address to physical for CR3 */
    proc->page_table = (uint32_t *) ((uint32_t) page_table - 0x80000000);
    
    return proc;
}

/*
 * Yield CPU to another process
 * Simple round-robin scheduler
 */
void yield(void) {
    struct process *next = idle_proc;
    
    /* Find next runnable process */
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    if (next == current_proc)
        return;

    struct process *prev = current_proc;
    current_proc = next;

    /* Switch page directory (CR3) */
    __asm__ volatile(
        "movl %0, %%cr3\n\t"
        :
        : "r" ((uint32_t) next->page_table)
        : "memory"
    );

    /* Switch context */
    switch_context(&prev->sp, &next->sp);
}

/*
 * Handle syscalls from user space
 * Syscall interface: hypercall via port 0x500
 */
void handle_syscall(struct trap_frame *f) {
    switch (f->eax) {
        case SYS_PUTCHAR:
            putchar(f->ebx);
            break;
            
        case SYS_GETCHAR:
            while (1) {
                long ch = getchar();
                if (ch >= 0) {
                    f->eax = ch;
                    break;
                }
                yield();
            }
            break;
            
        case SYS_EXIT:
            printf("process %d exited\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            yield();
            PANIC("unreachable");
            
        case SYS_READFILE:
        case SYS_WRITEFILE: {
            const char *filename = (const char *) f->ebx;
            char *buf = (char *) f->ecx;
            int len = f->edx;
            struct file *file = fs_lookup(filename);
            
            if (!file) {
                printf("file not found: %s\n", filename);
                f->eax = -1;
                break;
            }

            if (len > (int) sizeof(file->data))
                len = file->size;

            if (f->eax == SYS_WRITEFILE) {
                memcpy(file->data, buf, len);
                file->size = len;
                fs_flush();
            } else {
                memcpy(buf, file->data, len);
            }

            f->eax = len;
            break;
        }
        
        default:
            PANIC("unexpected syscall eax=%x\n", f->eax);
    }
}

/*
 * Trap handler - called from trap entry
 * Handles exceptions and syscalls
 */
void handle_trap(struct trap_frame *f) {
    /* For now, assume all traps are syscalls */
    handle_syscall(f);
}

/*
 * Kernel main entry point
 * Called from boot.S after basic setup
 */
void kernel_main(void) {
    /* Clear BSS */
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    
    printf("\n\n");
    printf("=== 1K OS x86 ===\n");
    printf("Booting in Protected Mode with Paging...\n\n");
    
    /* Initialize filesystem */
    fs_init();
    printf("Filesystem initialized\n");

    /* Create idle process */
    idle_proc = create_process(NULL, 0);
    idle_proc->pid = 0;
    printf("Created idle process (pid=0)\n");

    /* Create shell process */
    struct process *shell_proc = create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);
    printf("Created shell process (pid=%d)\n", shell_proc->pid);
    
    printf("\n=== Kernel Initialization Complete ===\n");
    printf("Starting shell process (PID %d)...\n\n", shell_proc->pid);
    
    /* Bootstrap into shell process
     * This is a special case - we're not doing a normal context switch,
     * but rather jumping from kernel initialization into the first user process.
     * We load the shell's page table, set ESP to shell's stack, and jump to user_entry.
     */
    current_proc = shell_proc;
    __asm__ volatile(
        "movl %0, %%cr3\n\t"        // Load shell's page table
        "movl %1, %%esp\n\t"        // Set stack to shell's stack
        "jmp user_entry\n\t"        // Jump to user_entry (which jumps to USER_BASE)
        :
        : "r" ((uint32_t) shell_proc->page_table),
          "r" (shell_proc->sp)
        : "memory"
    );
    
    /* Should never reach here */
    PANIC("Returned from shell process");
}
