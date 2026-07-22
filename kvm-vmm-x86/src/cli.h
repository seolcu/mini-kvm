/*
 * cli.h - command line parsing
 *
 * Produces a fully-resolved vmm_config_t from argv so that main() never
 * inspects argv itself. Options may appear anywhere, before or after the
 * guest binaries, and accept both "--flag value" and "--flag=value".
 */

#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stdint.h>

#include "debug.h"

#define MAX_VCPUS 4
#define MAX_MODULES 8

/* Linux boot entry strategy (experimental; see linux_boot.h). */
typedef enum {
    LINUX_ENTRY_SETUP,
    LINUX_ENTRY_CODE32,
    LINUX_ENTRY_BOOT64,
} linux_entry_mode_t;

/* Which address Linux's RSI points at on entry. */
typedef enum {
    LINUX_RSI_BASE,
    LINUX_RSI_HDR,
} linux_rsi_mode_t;

typedef struct {
    /* Guest selection: one binary per vCPU, or a bzImage in Linux mode. */
    const char *guests[MAX_VCPUS];
    int num_guests;

    /* CPU mode */
    bool paging;            /* 32-bit protected mode with paging */
    bool long_mode;         /* 64-bit long mode (implies paging) */
    uint32_t entry_point;   /* guest EIP in paging mode */
    uint32_t load_offset;   /* where the binary lands in guest memory */

    /* Display */
    bool vga;               /* render the 0xB8000 text buffer */

    /* Diagnostics */
    bool verbose;
    debug_level_t debug_level;
    bool dump_regs;             /* dump registers on every VM exit */
    bool explain;               /* single-step so a fault can be explained */
    unsigned long explain_steps; /* step budget; 0 means unlimited */
    const char *dump_mem_path;  /* dump guest memory here on exit, or NULL */

    /* Multiboot modules (an initrd, usually) */
    const char *modules[MAX_MODULES];
    int num_modules;

    /* Linux boot (experimental) */
    bool linux_boot;
    const char *bzimage_path;
    const char *linux_cmdline;
    const char *initrd_path;
    linux_entry_mode_t linux_entry;
    linux_rsi_mode_t linux_rsi;
} vmm_config_t;

/*
 * Parse argv into *cfg.
 * Returns 0 on success, >0 if the process should exit with that status
 * (usage error, or --help). Diagnostics are printed here.
 */
int cli_parse(int argc, char **argv, vmm_config_t *cfg);

/* Print the usage message to the given stream. */
void cli_usage(const char *argv0, FILE *out);

#endif /* CLI_H */
