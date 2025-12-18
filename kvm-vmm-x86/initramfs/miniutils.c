#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static int cmd_uname(int argc, char **argv)
{
    struct utsname u;
    if (uname(&u) < 0) {
        perror("uname");
        return 1;
    }

    bool all = false;
    if (argc >= 2 && strcmp(argv[1], "-a") == 0) {
        all = true;
    }

    if (all) {
        printf("%s %s %s %s %s\n", u.sysname, u.nodename, u.release, u.version, u.machine);
    } else {
        printf("%s\n", u.sysname);
    }
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: cat <file>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "cat: %s: %s\n", path, strerror(errno));
            continue;
        }

        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            (void)fwrite(buf, 1, n, stdout);
        }
        fclose(f);
    }
    return 0;
}

static int list_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        printf("%s\n", ent->d_name);
    }
    closedir(d);
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    if (argc < 2) {
        return list_dir(".");
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
        if (argc > 2) {
            printf("%s:\n", path);
        }
        if (list_dir(path) != 0) {
            rc = 1;
        }
        if (argc > 2 && i + 1 < argc) {
            printf("\n");
        }
    }
    return rc;
}

static int cmd_halt(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    sync();
    if (reboot(RB_POWER_OFF) < 0) {
        perror("reboot(RB_POWER_OFF)");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];

    if (strcmp(name, "uname") == 0) {
        return cmd_uname(argc, argv);
    }
    if (strcmp(name, "ls") == 0) {
        return cmd_ls(argc, argv);
    }
    if (strcmp(name, "cat") == 0) {
        return cmd_cat(argc, argv);
    }
    if (strcmp(name, "halt") == 0 || strcmp(name, "poweroff") == 0) {
        return cmd_halt(argc, argv);
    }

    fprintf(stderr, "miniutils: unknown applet '%s'\n", name);
    return 127;
}

