/*
 * vm.c - VM lifecycle: /dev/kvm, the VM instance, and guest memory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>

#include "vm.h"
#include "debug.h"

/* Guest memory sizes per mode. */
#define MEM_SIZE_REAL   (256 * 1024)            /* fits 4 vCPUs under 1MB */
#define MEM_SIZE_PAGING (4 * 1024 * 1024)
#define MEM_SIZE_KERNEL (128 * 1024 * 1024)     /* ELF/Multiboot guests */
#define MEM_SIZE_LINUX  (256 * 1024 * 1024)

/* TSS scratch area, well clear of the kernel and page tables. */
#define TSS_ADDR 0x200000

static int kvm_fd = -1;
static int vm_fd = -1;

int vm_kvm_fd(void) { return kvm_fd; }
int vm_get_fd(void) { return vm_fd; }

int vm_init(bool need_irqchip)
{
    kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm_fd < 0) {
        perror("open /dev/kvm");
        fprintf(stderr, "Is KVM enabled, and are you in the 'kvm' group?\n");
        return -1;
    }

    int api_version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (api_version < 0) {
        perror("KVM_GET_API_VERSION");
        return -1;
    }
    if (api_version != KVM_API_VERSION) {
        fprintf(stderr, "KVM API version mismatch: expected %d, got %d\n",
                KVM_API_VERSION, api_version);
        return -1;
    }
    if (verbose_enabled()) {
        printf("KVM API version: %d\n", api_version);
    }

    vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) {
        perror("KVM_CREATE_VM");
        return -1;
    }
    if (verbose_enabled()) {
        printf("Created VM (fd=%d)\n", vm_fd);
    }

    /* Required on Intel before any vCPU exists; harmlessly unsupported on AMD. */
    if (ioctl(vm_fd, KVM_SET_TSS_ADDR, (unsigned long)TSS_ADDR) < 0) {
        if (verbose_enabled()) {
            perror("KVM_SET_TSS_ADDR (expected to fail on AMD)");
        }
    } else if (verbose_enabled()) {
        printf("Set TSS address to 0x%x\n", TSS_ADDR);
    }

    /*
     * Only interactive/Linux guests get an interrupt controller. Real-mode
     * guests must not have one: an unwanted IRQ0 hangs a guest that
     * terminates with HLT.
     */
    if (need_irqchip) {
        if (ioctl(vm_fd, KVM_CREATE_IRQCHIP) < 0) {
            perror("KVM_CREATE_IRQCHIP");
            fprintf(stderr, "Warning: interrupts will be unavailable.\n");
        } else if (verbose_enabled()) {
            printf("Created interrupt controller (8259 PIC, IOAPIC, LAPIC)\n");
        }

        /*
         * The 8254 PIT drives IRQ0. KVM emulates it in-kernel, so a guest's
         * timer ticks without any userspace involvement and without a VM exit
         * per tick.
         */
        struct kvm_pit_config pit = { .flags = 0 };
        if (ioctl(vm_fd, KVM_CREATE_PIT2, &pit) < 0) {
            perror("KVM_CREATE_PIT2");
            fprintf(stderr, "Warning: no timer interrupt.\n");
        } else if (verbose_enabled()) {
            printf("Created programmable interval timer (8254 PIT)\n");
        }
    }

    return 0;
}

void vm_shutdown(void)
{
    if (vm_fd >= 0) {
        close(vm_fd);
        vm_fd = -1;
    }
    if (kvm_fd >= 0) {
        close(kvm_fd);
        kvm_fd = -1;
    }
}

void vm_set_irq_level(uint32_t irq, int level)
{
    if (vm_fd < 0) {
        return;
    }

    struct kvm_irq_level req = { .irq = irq, .level = (uint32_t)level };
    (void)ioctl(vm_fd, KVM_IRQ_LINE, &req);
}

void vm_pulse_irq(uint32_t irq)
{
    vm_set_irq_level(irq, 1);
    vm_set_irq_level(irq, 0);
}

int vm_map_vcpu_memory(vcpu_context_t *ctx)
{
    if (ctx->linux_guest) {
        ctx->mem_size = MEM_SIZE_LINUX;
    } else if (ctx->image.format == GUEST_ELF ||
               ctx->image.format == GUEST_MULTIBOOT ||
               ctx->image.format == GUEST_MULTIBOOT2) {
        /* These load at 1MB and expect to allocate above it. 4MB would leave
         * a kernel almost no usable memory. */
        ctx->mem_size = MEM_SIZE_KERNEL;
    } else if (ctx->use_flat32) {
        /* Enough for the VGA text buffer and whatever the kernel allocates;
         * a real-mode-sized 256KB would not even reach 0xB8000. */
        ctx->mem_size = MEM_SIZE_KERNEL;
    } else if (ctx->use_paging) {
        ctx->mem_size = MEM_SIZE_PAGING;
    } else {
        ctx->mem_size = MEM_SIZE_REAL;
    }

    ctx->guest_mem = mmap(NULL, ctx->mem_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ctx->guest_mem == MAP_FAILED) {
        perror("mmap guest memory");
        return -1;
    }

    /*
     * One slot per vCPU, each at its own guest physical base. This is what
     * keeps concurrently running guests from seeing each other's memory.
     */
    struct kvm_userspace_memory_region region = {
        .slot = (uint32_t)ctx->vcpu_id,
        .flags = 0,
        .guest_phys_addr = (uint64_t)ctx->vcpu_id * ctx->mem_size,
        .memory_size = ctx->mem_size,
        .userspace_addr = (uint64_t)(uintptr_t)ctx->guest_mem,
    };

    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
    }

    if (verbose_enabled()) {
        vcpu_printf(ctx, "Guest memory: %zu KB, GPA 0x%llx -> HVA %p (slot %d)\n",
                    ctx->mem_size / 1024,
                    (unsigned long long)region.guest_phys_addr,
                    ctx->guest_mem, ctx->vcpu_id);
    }

    return 0;
}

int vm_create_vcpu(vcpu_context_t *ctx)
{
    ctx->vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, ctx->vcpu_id);
    if (ctx->vcpu_fd < 0) {
        perror("KVM_CREATE_VCPU");
        return -1;
    }

    int mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        return -1;
    }
    ctx->kvm_run_mmap_size = (size_t)mmap_size;

    ctx->kvm_run = mmap(NULL, ctx->kvm_run_mmap_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ctx->vcpu_fd, 0);
    if (ctx->kvm_run == MAP_FAILED) {
        perror("mmap kvm_run");
        return -1;
    }

    if (verbose_enabled()) {
        vcpu_printf(ctx, "Created vCPU (fd=%d), kvm_run %zu bytes\n",
                    ctx->vcpu_fd, ctx->kvm_run_mmap_size);
    }

    return 0;
}
