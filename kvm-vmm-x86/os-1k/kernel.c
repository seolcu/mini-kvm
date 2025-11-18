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
 */
paddr_t alloc_pages(uint32_t n) {
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
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
        uint32_t pt_paddr = alloc_pages(1);
        page_table[pd_index] = (pt_paddr & 0xFFFFF000) | PAGE_P | PAGE_RW | PAGE_U;
    }

    /* Extract page table index (bits 21:12) */
    uint32_t pt_index = (vaddr >> 12) & 0x3FF;
    
    /* Get page table address */
    uint32_t *pt = (uint32_t *) (page_table[pd_index] & 0xFFFFF000);
    
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
 */
long getchar(void) {
    long ch;
    __asm__ volatile(
        "movb $2, %%al\n\t"
        "movw $0x500, %%dx\n\t"
        "outb %%al, %%dx\n\t"
        "inb %%dx, %%al\n\t"
        "movsbl %%al, %0"
        : "=r"(ch)
        :
        : "al", "dx"
    );
    return ch;
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
 * User entry point - jump to user space
 * Sets up user mode registers and executes sysret/iret
 */
__attribute__((naked)) void user_entry(void) {
    __asm__ volatile(
        "movl $0x23, %%eax\n\t"      // User data segment (GDT index 4, RPL=3)
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        
        "movl $0x01000000, %%eax\n\t"  // USER_BASE
        "pushl $0x23\n\t"              // SS
        "pushl %%eax\n\t"              // ESP (USER_BASE as stack)
        "pushfl\n\t"                   // EFLAGS
        "orl $0x200, (%%esp)\n\t"      // Set IF (interrupts enabled)
        "pushl $0x1B\n\t"              // CS (user code segment, RPL=3)
        "pushl %%eax\n\t"              // EIP (USER_BASE as entry)
        "iretl\n\t"
        :
        :
        : "memory"
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

    /* Allocate page directory */
    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    /* Map kernel pages (identity mapping) */
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_RW);

    /* Map user pages */
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;
        memcpy((void *) page, image + off, copy_size);
        map_page(page_table, USER_BASE + off, page, PAGE_U | PAGE_RW);
    }

    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    proc->page_table = page_table;
    
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
    
    /* Initialize filesystem */
    fs_init();

    /* Create idle process */
    idle_proc = create_process(NULL, 0);
    idle_proc->pid = 0;
    current_proc = idle_proc;

    /* Create shell process */
    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);
    
    /* Start multitasking */
    yield();

    PANIC("switched to idle process");
}
