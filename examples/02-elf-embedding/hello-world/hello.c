/*
 * hello.c - Simple program with embedded lolelffs filesystem
 *
 * This program demonstrates the basic concept of a dual-purpose binary:
 * it's both an executable program AND a mountable filesystem.
 *
 * The filesystem is embedded in the .lolfs.super ELF section.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    printf("╔═══════════════════════════════════════════════╗\n");
    printf("║   Hello from lolelffs embedded binary!       ║\n");
    printf("╚═══════════════════════════════════════════════╝\n");
    printf("\n");

    printf("This binary contains an embedded lolelffs filesystem!\n");
    printf("\n");
    printf("You can access it in several ways:\n");
    printf("\n");

    printf("1. Mount it (requires root):\n");
    printf("   sudo mkdir -p /mnt/hello\n");
    printf("   sudo mount -t lolelffs -o loop %s /mnt/hello\n", argv[0]);
    printf("   cat /mnt/hello/config.txt\n");
    printf("   sudo umount /mnt/hello\n");
    printf("\n");

    printf("2. Access via CLI tools:\n");
    printf("   lolelffs ls -i %s /\n", argv[0]);
    printf("   lolelffs cat -i %s /config.txt\n", argv[0]);
    printf("\n");

    printf("3. Check the ELF structure:\n");
    printf("   readelf -S %s | grep lolfs\n", argv[0]);
    printf("\n");

    printf("The embedded filesystem contains:\n");
    printf("  • /config.txt - Configuration file\n");
    printf("  • /message.txt - Welcome message\n");
    printf("  • /data/ - Data directory\n");
    printf("\n");

    printf("This demonstrates lolelffs's unique capability:\n");
    printf("Creating files that are BOTH executables AND filesystems!\n");
    printf("\n");

    return 0;
}
