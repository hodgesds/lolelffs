/*
 * example.c - Demonstrate embedded filesystem in ELF binary
 *
 * This program contains an embedded lolelffs filesystem in the .lolfs.super section.
 * The section is added after compilation using objcopy.
 */

#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("╔═══════════════════════════════════════════════╗\n");
    printf("║  Embedded Filesystem Example                 ║\n");
    printf("╚═══════════════════════════════════════════════╝\n\n");

    printf("This binary contains an embedded lolelffs filesystem!\n\n");

    printf("The filesystem was embedded using:\n");
    printf("  objcopy --add-section .lolfs.super=fs.img \\\n");
    printf("          --set-section-flags .lolfs.super=alloc,load,readonly,data \\\n");
    printf("          program program-with-fs\n\n");

    printf("Access the embedded filesystem:\n\n");

    printf("1. Extract section, then use CLI tools:\n");
    printf("   objcopy --dump-section .lolfs.super=fs.img %s\n", argv[0]);
    printf("   lolelffs ls -i fs.img /\n");
    printf("   lolelffs cat -i fs.img /info.txt\n\n");

    printf("2. Check ELF structure:\n");
    printf("   readelf -S %s | grep lolfs\n\n", argv[0]);

    printf("3. Mount directly (requires root + kernel module):\n");
    printf("   sudo mount -t lolelffs -o loop %s /mnt/point\n");
    printf("   ls /mnt/point\n");
    printf("   sudo umount /mnt/point\n\n", argv[0]);

    printf("The .lolfs.super section contains the complete filesystem,\n");
    printf("making this binary both executable AND mountable!\n\n");

    return 0;
}
