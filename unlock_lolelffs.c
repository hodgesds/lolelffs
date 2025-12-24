/*
 * unlock_lolelffs - Unlock an encrypted lolelffs filesystem
 *
 * Usage: unlock_lolelffs <mount_point> <password>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

/* Include the ioctl definitions from lolelffs.h */
#define LOLELFFS_IOC_MAGIC 'L'

struct lolelffs_ioctl_unlock {
    char password[256];
    unsigned int password_len;
};

#define LOLELFFS_IOC_UNLOCK _IOW(LOLELFFS_IOC_MAGIC, 1, struct lolelffs_ioctl_unlock)

struct lolelffs_ioctl_enc_status {
    unsigned int enc_enabled;
    unsigned int enc_unlocked;
    unsigned int enc_algorithm;
};

#define LOLELFFS_IOC_ENC_STATUS _IOR(LOLELFFS_IOC_MAGIC, 2, struct lolelffs_ioctl_enc_status)

static void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <mount_point> <password>\n", progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Unlock an encrypted lolelffs filesystem.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  sudo %s /mnt/lolelffs MyPassword123\n", progname);
    exit(1);
}

static int check_status(int fd)
{
    struct lolelffs_ioctl_enc_status status;
    int ret;

    ret = ioctl(fd, LOLELFFS_IOC_ENC_STATUS, &status);
    if (ret < 0) {
        perror("Failed to get encryption status");
        return -1;
    }

    printf("Encryption status:\n");
    printf("  Enabled: %s\n", status.enc_enabled ? "yes" : "no");
    printf("  Algorithm: %u\n", status.enc_algorithm);
    printf("  Unlocked: %s\n", status.enc_unlocked ? "yes" : "no");

    return status.enc_unlocked;
}

int main(int argc, char **argv)
{
    int fd, ret;
    struct lolelffs_ioctl_unlock req;

    if (argc != 3) {
        usage(argv[0]);
    }

    /* Open the mount point (any file in the filesystem will do) */
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("Failed to open mount point");
        fprintf(stderr, "Make sure the filesystem is mounted and you have permission to access it.\n");
        return 1;
    }

    /* Check current encryption status */
    printf("Checking encryption status...\n");
    ret = check_status(fd);
    if (ret < 0) {
        close(fd);
        return 1;
    }

    if (ret == 1) {
        printf("\nFilesystem is already unlocked!\n");
        close(fd);
        return 0;
    }

    /* Prepare unlock request */
    memset(&req, 0, sizeof(req));
    strncpy(req.password, argv[2], sizeof(req.password) - 1);
    req.password_len = strnlen(req.password, sizeof(req.password));

    /* Send unlock ioctl */
    printf("\nUnlocking filesystem...\n");
    ret = ioctl(fd, LOLELFFS_IOC_UNLOCK, &req);

    /* Zero password from memory */
    memset(&req, 0, sizeof(req));

    if (ret < 0) {
        perror("Failed to unlock filesystem");
        fprintf(stderr, "\nPossible reasons:\n");
        fprintf(stderr, "  - Incorrect password\n");
        fprintf(stderr, "  - Filesystem is not encrypted\n");
        fprintf(stderr, "  - Permission denied (try with sudo)\n");
        close(fd);
        return 1;
    }

    printf("Filesystem unlocked successfully!\n");
    printf("\nVerifying unlock status...\n");
    check_status(fd);

    close(fd);
    return 0;
}
