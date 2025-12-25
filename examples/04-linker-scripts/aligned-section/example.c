/*
 * example.c - Demonstrate page-aligned filesystem section
 */

#include <stdio.h>
#include <stdint.h>

extern char __lolfs_start[];
extern char __lolfs_end[];

#define PAGE_SIZE 4096

int main() {
    printf("Page-Aligned Filesystem Section Example\n");
    printf("========================================\n\n");

    uintptr_t start_addr = (uintptr_t)__lolfs_start;
    uintptr_t end_addr = (uintptr_t)__lolfs_end;
    size_t size = end_addr - start_addr;

    printf("Filesystem section:\n");
    printf("  Start:  0x%016lx\n", start_addr);
    printf("  End:    0x%016lx\n", end_addr);
    printf("  Size:   %zu bytes\n\n", size);

    /* Check alignment */
    printf("Alignment verification:\n");
    if (start_addr % PAGE_SIZE == 0) {
        printf("  ✓ Start is page-aligned (4096 bytes)\n");
    } else {
        printf("  ✗ Start is NOT page-aligned (offset: %lu bytes)\n",
               start_addr % PAGE_SIZE);
    }

    if (end_addr % PAGE_SIZE == 0) {
        printf("  ✓ End is page-aligned (4096 bytes)\n");
    } else {
        printf("  ✗ End is NOT page-aligned (offset: %lu bytes)\n",
               end_addr % PAGE_SIZE);
    }

    printf("\nBenefits of page alignment:\n");
    printf("  • Efficient mmap() operations\n");
    printf("  • Direct kernel page mapping\n");
    printf("  • Reduced memory fragmentation\n");
    printf("  • Better cache performance\n");

    return 0;
}
