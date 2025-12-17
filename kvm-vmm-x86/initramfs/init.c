#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_all(int fd, const char *s)
{
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0) {
            return;
        }
        s += (size_t)n;
        len -= (size_t)n;
    }
}

static void log_console(const char *s)
{
    write_all(STDOUT_FILENO, s);
}

static void ensure_dir(const char *path)
{
    if (mkdir(path, 0755) < 0) {
        if (errno == EEXIST) {
            return;
        }
    }
}

static void mount_early_fs(void)
{
    ensure_dir("/dev");
    ensure_dir("/proc");
    ensure_dir("/sys");

    // Ignore failures: kernel configs vary.
    (void)mount("devtmpfs", "/dev", "devtmpfs", 0, "");
    (void)mount("proc", "/proc", "proc", 0, "");
    (void)mount("sysfs", "/sys", "sysfs", 0, "");
}

static void setup_console_stdio(void)
{
    int fd = open("/dev/console", O_RDWR);
    if (fd < 0) {
        return;
    }

    (void)dup2(fd, STDIN_FILENO);
    (void)dup2(fd, STDOUT_FILENO);
    (void)dup2(fd, STDERR_FILENO);

    if (fd > STDERR_FILENO) {
        close(fd);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    mount_early_fs();
    setup_console_stdio();

    log_console("\n[mini-kvm] userspace init started\n");
    log_console("[mini-kvm] exec /bin/sh -i (serial input supported)\n\n");

    char *const sh_argv[] = {"/bin/sh", "-i", NULL};
    execv("/bin/sh", sh_argv);

    log_console("[mini-kvm] execv(/bin/sh) failed: ");
    log_console(strerror(errno));
    log_console("\n");

    for (;;) {
        sleep(1);
    }
}
