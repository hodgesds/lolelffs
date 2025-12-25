/*
 * lolfs.ld - Basic linker script for embedding lolelffs filesystem
 *
 * This linker script defines the .lolfs.super section and provides
 * symbols for accessing it from C code.
 *
 * IMPORTANT: This uses a simple approach that appends the filesystem
 * section to .rodata, ensuring it's in a loadable segment.
 *
 * Usage: gcc -Wl,-T,lolfs.ld example.c fs.o -o example
 */

SECTIONS
{
    /*
     * Define the .lolfs_data section containing the filesystem.
     * This is a separate loadable section.
     */
    .lolfs_data : {
        PROVIDE(__lolfs_start = .);
        *(.lolfs.super)
        PROVIDE(__lolfs_end = .);
    }
}

/*
 * Insert after .data section.
 * This creates a new loadable section in the binary.
 */
INSERT AFTER .data;
