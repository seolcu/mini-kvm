#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>

int main() {
    // Mount essential filesystems
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

    printf("\n");
    printf("========================================\n");
    printf(" RISC-V Linux with KVM - Init Started\n");
    printf("========================================\n");
    printf("\n");

    // Check if /dev/kvm exists
    struct stat st;
    if (stat("/dev/kvm", &st) == 0) {
        printf("[SUCCESS] /dev/kvm exists!\n");
        printf("          Device type: %s\n",
               S_ISCHR(st.st_mode) ? "Character device" : "Other");
        printf("          Major: %d, Minor: %d\n",
               major(st.st_rdev), minor(st.st_rdev));

        // Try to open /dev/kvm
        int fd = open("/dev/kvm", O_RDWR);
        if (fd >= 0) {
            printf("[SUCCESS] /dev/kvm is accessible!\n");
            printf("          File descriptor: %d\n", fd);
            close(fd);
        } else {
            printf("[WARNING] /dev/kvm exists but cannot be opened\n");
        }
    } else {
        printf("[ERROR] /dev/kvm does not exist!\n");
        printf("        KVM is not available.\n");
    }

    printf("\n");
    printf("Environment setup complete!\n");
    printf("========================================\n");
    printf("\n");

    // Sleep to allow viewing output
    printf("System will halt in 5 seconds...\n");
    sleep(5);

    // Halt the system
    printf("Halting system...\n");
    sync();
    reboot(0x4321fedc); // LINUX_REBOOT_CMD_POWER_OFF

    return 0;
}
