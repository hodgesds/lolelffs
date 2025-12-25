/*
 * aligned.ld - Page-aligned linker script for lolelffs
 *
 * This linker script ensures the .lolfs.super section is page-aligned (4096 bytes).
 * Page alignment is important for:
 * - Efficient mmap() operations
 * - Direct kernel access
 * - Memory management optimization
 *
 * Usage: gcc -T aligned.ld example.c fs.o -o example
 */

SECTIONS
{
    /*
     * Align to page boundary (4096 bytes) before the filesystem section.
     * This ensures the filesystem data starts at a page boundary.
     */
    . = ALIGN(4096);

    .lolfs.super : ALIGN(4096) {
        /* Mark start of filesystem (page-aligned) */
        PROVIDE(__lolfs_start = .);

        /* Include filesystem data */
        *(.lolfs.super)

        /* Align end to page boundary as well */
        . = ALIGN(4096);

        /* Mark end of filesystem (page-aligned) */
        PROVIDE(__lolfs_end = .);
    }
}

/*
 * Insert after .data section.
 * The page alignment ensures efficient memory mapping.
 */
INSERT AFTER .data;
