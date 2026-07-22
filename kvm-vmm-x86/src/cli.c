/*
 * cli.c - command line parsing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "cli.h"

void cli_usage(const char *argv0, FILE *out)
{
    fprintf(out,
        "Mini-KVM - a minimal x86 hypervisor built on the Linux KVM API\n"
        "\n"
        "Usage:\n"
        "  %s [options] <guest> [guest ...]   run 1-%d guests, one per vCPU\n"
        "  %s [options] --linux <bzImage>     boot a Linux kernel (experimental)\n"
        "\n"
        "CPU mode:\n"
        "  --paging              32-bit protected mode with paging (for the 1K OS)\n"
        "  --long-mode           64-bit long mode (implies --paging)\n"
        "  --entry ADDR          guest entry point in paging mode (default 0x80001000)\n"
        "  --load OFFSET         where to load the binary (default 0x1000 with paging)\n"
        "\n"
        "Display:\n"
        "  --vga                 render the VGA text buffer at 0xB8000 (needs --paging)\n"
        "\n"
        "Diagnostics:\n"
        "  -v, --verbose         log VM exits, hypercalls, and setup detail\n"
        "  --debug LEVEL         verbosity 0-3 (0=none 1=basic 2=detailed 3=all)\n"
        "  --dump-regs           dump registers on every VM exit\n"
        "  --dump-mem FILE       write guest memory to FILE when the guest exits\n"
        "  -h, --help            this message\n"
        "\n"
        "Linux boot (experimental, incomplete):\n"
        "  --linux <bzImage>     kernel image to boot\n"
        "  --linux-entry MODE    setup | code32 | boot64   (default code32)\n"
        "  --linux-rsi MODE      base | hdr                (default base)\n"
        "  --cmdline \"...\"       kernel command line\n"
        "  --initrd FILE         initrd image\n"
        "\n"
        "Options accept either \"--flag value\" or \"--flag=value\", and may appear\n"
        "before or after the guest binaries.\n"
        "\n"
        "Examples:\n"
        "  %s guest/hello\n"
        "  %s guest/counter guest/hello guest/multiplication\n"
        "  %s --paging os-1k/kernel\n"
        "  %s --long-mode guest/hello_64\n",
        argv0, MAX_VCPUS, argv0, argv0, argv0, argv0, argv0);
}

/*
 * Resolve the value for an option that takes one.
 *
 * Handles "--flag=value" (value embedded, *inlined set) and "--flag value"
 * (value in the next argv slot). Returns NULL and reports the error if no
 * value is available.
 */
static const char *option_value(const char *arg, const char *name,
                                int argc, char **argv, int *i, bool *inlined)
{
    size_t namelen = strlen(name);
    if (arg[namelen] == '=') {
        *inlined = true;
        const char *value = arg + namelen + 1;
        if (*value == '\0') {
            fprintf(stderr, "Error: %s= requires a value\n", name);
            return NULL;
        }
        return value;
    }

    *inlined = false;
    if (*i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires a value\n", name);
        return NULL;
    }
    (*i)++;
    return argv[*i];
}

/* True if arg is exactly `name` or begins with `name=`. */
static bool is_option(const char *arg, const char *name)
{
    size_t namelen = strlen(name);
    return strncmp(arg, name, namelen) == 0 &&
           (arg[namelen] == '\0' || arg[namelen] == '=');
}

/* Parse an unsigned integer, accepting 0x/0 prefixes. */
static int parse_u32(const char *text, const char *what, uint32_t *out)
{
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);

    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "Error: %s expects a number, got '%s'\n", what, text);
        return -1;
    }
#if ULONG_MAX > 0xFFFFFFFFUL
    if (value > 0xFFFFFFFFUL) {
        fprintf(stderr, "Error: %s value 0x%lx does not fit in 32 bits\n", what, value);
        return -1;
    }
#endif
    *out = (uint32_t)value;
    return 0;
}

int cli_parse(int argc, char **argv, vmm_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    /* Defaults matching os-1k/kernel.ld, which links at 0x80001000. */
    cfg->entry_point = 0x80001000;
    cfg->load_offset = 0x1000;
    cfg->debug_level = DEBUG_NONE;
    cfg->linux_entry = LINUX_ENTRY_CODE32;
    cfg->linux_rsi = LINUX_RSI_BASE;

    if (argc < 2) {
        cli_usage(argv[0], stderr);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        bool inlined = false;
        const char *value = NULL;

        /* Not an option: it is a guest binary. A lone "-" is treated the
         * same way rather than being mistaken for a flag. */
        if (arg[0] != '-' || arg[1] == '\0') {
            if (cfg->num_guests >= MAX_VCPUS) {
                fprintf(stderr, "Error: too many guests (max %d)\n", MAX_VCPUS);
                return 1;
            }
            cfg->guests[cfg->num_guests++] = arg;
            continue;
        }

        if (is_option(arg, "--help") || strcmp(arg, "-h") == 0) {
            cli_usage(argv[0], stdout);
            return 2;   /* caller exits 0; distinct from a usage error */
        }
        else if (is_option(arg, "--paging")) {
            cfg->paging = true;
        }
        else if (is_option(arg, "--vga")) {
            cfg->vga = true;
        }
        else if (is_option(arg, "--long-mode")) {
            cfg->long_mode = true;
            cfg->paging = true;     /* long mode requires paging */
        }
        else if (is_option(arg, "--verbose") || strcmp(arg, "-v") == 0) {
            cfg->verbose = true;
            if (cfg->debug_level < DEBUG_BASIC) {
                cfg->debug_level = DEBUG_BASIC;
            }
        }
        else if (is_option(arg, "--dump-regs")) {
            cfg->dump_regs = true;
        }
        else if (is_option(arg, "--entry")) {
            if (!(value = option_value(arg, "--entry", argc, argv, &i, &inlined))) return 1;
            if (parse_u32(value, "--entry", &cfg->entry_point) < 0) return 1;
        }
        else if (is_option(arg, "--load")) {
            if (!(value = option_value(arg, "--load", argc, argv, &i, &inlined))) return 1;
            if (parse_u32(value, "--load", &cfg->load_offset) < 0) return 1;
        }
        else if (is_option(arg, "--debug")) {
            if (!(value = option_value(arg, "--debug", argc, argv, &i, &inlined))) return 1;
            uint32_t level;
            if (parse_u32(value, "--debug", &level) < 0) return 1;
            if (level > DEBUG_ALL) {
                fprintf(stderr, "Error: --debug level must be 0-%d\n", DEBUG_ALL);
                return 1;
            }
            cfg->debug_level = (debug_level_t)level;
            cfg->verbose = (level > DEBUG_NONE);
        }
        else if (is_option(arg, "--dump-mem")) {
            if (!(value = option_value(arg, "--dump-mem", argc, argv, &i, &inlined))) return 1;
            cfg->dump_mem_path = value;
        }
        else if (is_option(arg, "--linux")) {
            if (!(value = option_value(arg, "--linux", argc, argv, &i, &inlined))) return 1;
            cfg->bzimage_path = value;
            cfg->linux_boot = true;
        }
        else if (is_option(arg, "--linux-entry")) {
            if (!(value = option_value(arg, "--linux-entry", argc, argv, &i, &inlined))) return 1;
            if (strcmp(value, "setup") == 0)       cfg->linux_entry = LINUX_ENTRY_SETUP;
            else if (strcmp(value, "code32") == 0) cfg->linux_entry = LINUX_ENTRY_CODE32;
            else if (strcmp(value, "boot64") == 0) cfg->linux_entry = LINUX_ENTRY_BOOT64;
            else {
                fprintf(stderr, "Error: invalid --linux-entry '%s' (expected setup|code32|boot64)\n", value);
                return 1;
            }
        }
        else if (is_option(arg, "--linux-rsi")) {
            if (!(value = option_value(arg, "--linux-rsi", argc, argv, &i, &inlined))) return 1;
            if (strcmp(value, "base") == 0)     cfg->linux_rsi = LINUX_RSI_BASE;
            else if (strcmp(value, "hdr") == 0) cfg->linux_rsi = LINUX_RSI_HDR;
            else {
                fprintf(stderr, "Error: invalid --linux-rsi '%s' (expected base|hdr)\n", value);
                return 1;
            }
        }
        else if (is_option(arg, "--cmdline")) {
            if (!(value = option_value(arg, "--cmdline", argc, argv, &i, &inlined))) return 1;
            cfg->linux_cmdline = value;
        }
        else if (is_option(arg, "--initrd")) {
            if (!(value = option_value(arg, "--initrd", argc, argv, &i, &inlined))) return 1;
            cfg->initrd_path = value;
        }
        else {
            fprintf(stderr, "Error: unknown option '%s'\n", arg);
            fprintf(stderr, "Try '%s --help'.\n", argv[0]);
            return 1;
        }
    }

    /* --- Consistency checks --------------------------------------------- */

    if (cfg->linux_boot) {
        if (cfg->num_guests > 0) {
            fprintf(stderr, "Error: --linux takes the kernel image itself; "
                            "do not also pass a guest binary\n");
            return 1;
        }
        /* Linux boot is single-vCPU. */
        cfg->num_guests = 1;
        return 0;
    }

    if (cfg->num_guests == 0) {
        fprintf(stderr, "Error: no guest binary specified\n");
        fprintf(stderr, "Try '%s --help'.\n", argv[0]);
        return 1;
    }

    /* Without paging the binary is loaded at physical 0 and entered in real
     * mode, so a load offset would just misplace it. */
    if (!cfg->paging) {
        cfg->load_offset = 0;
    }

    return 0;
}
