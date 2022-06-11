#include <errno.h>
#include <fcntl.h>
#include <linux/elf.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lolelffs.h"

struct superblock {
    struct lolelffs_sb_info info;
    char padding[4064]; /* Padding to match block size */
};

/* Returns ceil(a/b) */
static inline uint32_t idiv_ceil(uint32_t a, uint32_t b)
{
    uint32_t ret = a / b;
    if (a % b)
        return ret + 1;
    return ret;
}

static struct superblock *write_superblock(int fd, struct stat *fstats)
{
    struct superblock *sb = malloc(sizeof(struct superblock));
    if (!sb)
        return NULL;



    uint32_t nr_blocks = fstats->st_size / LOLELFFS_BLOCK_SIZE;
    uint32_t nr_inodes = nr_blocks;
    uint32_t mod = nr_inodes % LOLELFFS_INODES_PER_BLOCK;
    if (mod)
        nr_inodes += LOLELFFS_INODES_PER_BLOCK - mod;
    uint32_t nr_istore_blocks = idiv_ceil(nr_inodes, LOLELFFS_INODES_PER_BLOCK);
    uint32_t nr_ifree_blocks = idiv_ceil(nr_inodes, LOLELFFS_BLOCK_SIZE * 8);
    uint32_t nr_bfree_blocks = idiv_ceil(nr_blocks, LOLELFFS_BLOCK_SIZE * 8);
    uint32_t nr_data_blocks =
        nr_blocks - nr_istore_blocks - nr_ifree_blocks - nr_bfree_blocks;

    memset(sb, 0, sizeof(struct superblock));
    sb->info = (struct lolelffs_sb_info){
        .magic = htole32(LOLELFFS_MAGIC),
        .nr_blocks = htole32(nr_blocks),
        .nr_inodes = htole32(nr_inodes),
        .nr_istore_blocks = htole32(nr_istore_blocks),
        .nr_ifree_blocks = htole32(nr_ifree_blocks),
        .nr_bfree_blocks = htole32(nr_bfree_blocks),
        .nr_free_inodes = htole32(nr_inodes - 1),
        .nr_free_blocks = htole32(nr_data_blocks - 1),
    };

    int ret = write(fd, sb, sizeof(struct superblock));
    if (ret != sizeof(struct superblock)) {
        free(sb);
        return NULL;
    }

    printf(
        "Superblock: (%ld)\n"
        "\tmagic=%#x\n"
        "\tnr_blocks=%u\n"
        "\tnr_inodes=%u (istore=%u blocks)\n"
        "\tnr_ifree_blocks=%u\n"
        "\tnr_bfree_blocks=%u\n"
        "\tnr_free_inodes=%u\n"
        "\tnr_free_blocks=%u\n",
        sizeof(struct superblock), sb->info.magic, sb->info.nr_blocks,
        sb->info.nr_inodes, sb->info.nr_istore_blocks, sb->info.nr_ifree_blocks,
        sb->info.nr_bfree_blocks, sb->info.nr_free_inodes,
        sb->info.nr_free_blocks);

    return sb;
}

static int write_inode_store(int fd, struct superblock *sb)
{
    /* Allocate a zeroed block for inode store */
    char *block = malloc(LOLELFFS_BLOCK_SIZE);
    if (!block)
        return -1;

    memset(block, 0, LOLELFFS_BLOCK_SIZE);

    /* Root inode (inode 0) */
    struct lolelffs_inode *inode = (struct lolelffs_inode *) block;
    uint32_t first_data_block = 1 + le32toh(sb->info.nr_bfree_blocks) +
                                le32toh(sb->info.nr_ifree_blocks) +
                                le32toh(sb->info.nr_istore_blocks);
    inode->i_mode = htole32(S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR |
                            S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH);
    inode->i_uid = 0;
    inode->i_gid = 0;
    inode->i_size = htole32(LOLELFFS_BLOCK_SIZE);
    inode->i_ctime = inode->i_atime = inode->i_mtime = htole32(0);
    inode->i_blocks = htole32(1);
    inode->i_nlink = htole32(2);
    inode->ei_block = htole32(first_data_block);

    int ret = write(fd, block, LOLELFFS_BLOCK_SIZE);
    if (ret != LOLELFFS_BLOCK_SIZE) {
        ret = -1;
        goto end;
    }

    /* Reset inode store blocks to zero */
    memset(block, 0, LOLELFFS_BLOCK_SIZE);
    uint32_t i;
    for (i = 1; i < sb->info.nr_istore_blocks; i++) {
        ret = write(fd, block, LOLELFFS_BLOCK_SIZE);
        if (ret != LOLELFFS_BLOCK_SIZE) {
            ret = -1;
            goto end;
        }
    }
    ret = 0;

    printf(
        "Inode store: wrote %d blocks\n"
        "\tinode size = %ld B\n",
        i, sizeof(struct lolelffs_inode));

end:
    free(block);
    return ret;
}

static int write_ifree_blocks(int fd, struct superblock *sb)
{
    char *block = malloc(LOLELFFS_BLOCK_SIZE);
    if (!block)
        return -1;

    uint64_t *ifree = (uint64_t *) block;

    /* Set all bits to 1 */
    memset(ifree, 0xff, LOLELFFS_BLOCK_SIZE);

    /* First ifree block, containing first used inode */
    ifree[0] = htole64(0xfffffffffffffffe);
    int ret = write(fd, ifree, LOLELFFS_BLOCK_SIZE);
    if (ret != LOLELFFS_BLOCK_SIZE) {
        ret = -1;
        goto end;
    }

    /* All ifree blocks except the one containing 2 first inodes */
    ifree[0] = 0xffffffffffffffff;
    uint32_t i;
    for (i = 1; i < le32toh(sb->info.nr_ifree_blocks); i++) {
        ret = write(fd, ifree, LOLELFFS_BLOCK_SIZE);
        if (ret != LOLELFFS_BLOCK_SIZE) {
            ret = -1;
            goto end;
        }
    }
    ret = 0;

    printf("Ifree blocks: wrote %d blocks\n", i);

end:
    free(block);

    return ret;
}

static int write_bfree_blocks(int fd, struct superblock *sb)
{
    uint32_t nr_used = le32toh(sb->info.nr_istore_blocks) +
                       le32toh(sb->info.nr_ifree_blocks) +
                       le32toh(sb->info.nr_bfree_blocks) + 2;

    char *block = malloc(LOLELFFS_BLOCK_SIZE);
    if (!block)
        return -1;
    uint64_t *bfree = (uint64_t *) block;

    /*
     * First blocks (incl. sb + istore + ifree + bfree + 1 used block)
     * we suppose it won't go further than the first block
     */
    memset(bfree, 0xff, LOLELFFS_BLOCK_SIZE);
    uint32_t i = 0;
    while (nr_used) {
        uint64_t line = 0xffffffffffffffff;
        for (uint64_t mask = 0x1; mask; mask <<= 1) {
            line &= ~mask;
            nr_used--;
            if (!nr_used)
                break;
        }
        bfree[i] = htole64(line);
        i++;
    }
    int ret = write(fd, bfree, LOLELFFS_BLOCK_SIZE);
    if (ret != LOLELFFS_BLOCK_SIZE) {
        ret = -1;
        goto end;
    }

    /* other blocks */
    memset(bfree, 0xff, LOLELFFS_BLOCK_SIZE);
    for (i = 1; i < le32toh(sb->info.nr_bfree_blocks); i++) {
        ret = write(fd, bfree, LOLELFFS_BLOCK_SIZE);
        if (ret != LOLELFFS_BLOCK_SIZE) {
            ret = -1;
            goto end;
        }
    }
    ret = 0;

    printf("Bfree blocks: wrote %d blocks\n", i);
end:
    free(block);

    return ret;
}

static int write_data_blocks(int fd, struct superblock *sb)
{
    /* FIXME: unimplemented */
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s disk\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Open disk image */
    int fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("open():");
        return EXIT_FAILURE;
    }
    FILE *fp;
    if (NULL == (fp = fdopen(fd, "w+"))) {
      perror("fdopen failed");
      goto fclose;
   }

    /* Get image size */
    struct stat stat_buf;
    int ret = fstat(fd, &stat_buf);
    if (ret) {
        perror("fstat():");
        ret = EXIT_FAILURE;
        goto fclose;
    }

    /* Read elf header */
    //unsigned char e_ident[EI_NIDENT];
    //if (fread(e_ident, sizeof(e_ident), 1, fp) != 1) {
    //    perror("not an elf:");
    //    ret = EXIT_FAILURE;
    //    goto fclose;
    //}
    //fprintf(stdout, "file magic: %c%c%c\n", e_ident[EI_MAG1], e_ident[EI_MAG2], e_ident[EI_MAG3]);
    //if (e_ident[EI_CLASS] == ELFCLASS32) {
    //    perror("too lazy for 32 bit support");
    //    ret = EXIT_FAILURE;
    //    goto fclose;
    //}

    struct elf64_hdr hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        perror("not an elf:");
        ret = EXIT_FAILURE;
        goto fclose;
    }
    fprintf(stdout, "file magic: %c%c%c\n",
	hdr.e_ident[EI_MAG1], hdr.e_ident[EI_MAG2], hdr.e_ident[EI_MAG3]);
    fprintf(stdout, "section offset: %lld\n", hdr.e_shoff);


    /* Read program header */
    //Elf64_Phdr *phdr;
    //if (fread(phdr, sizeof(phdr), 1, fp) != 1) {
    //    perror("failed to read elf program header:");
    //    ret = EXIT_FAILURE;
    //    goto fclose;
    //}
    //fprintf(stdout, "program header: %c\n", phdr);
    //for (unsigned int n1 = 0; n1 < phdr.e_phnum; n1++) {
    //}

    // see:
    // https://stackoverflow.com/questions/12159595/how-to-get-a-pointer-to-a-specific-executable-files-section-of-a-program-from-w

    /* Read section headers and search for superblock */

    fseek(fp, hdr.e_shoff, SEEK_SET); //going to the offset of the section
    Elf64_Shdr sectHdr;
    if (fread(&sectHdr, sizeof(sectHdr), 1, fp) != 1) {
        perror("failed to read elf section header:");
        ret = EXIT_FAILURE;
        goto fclose;
    } //reading the size of section
    fprintf(stdout, "section header:\n\tname: %c size: %lld offset: %lld\n",
	sectHdr.sh_name, sectHdr.sh_size, sectHdr.sh_offset);

    char* SectNames = NULL;
    SectNames = malloc(sectHdr.sh_size); //variable for section names (like "Magic", "Data" etc.)
    // fseek(fp, sectHdr.sh_offset, SEEK_SET); //going to the offset of the section
    if (fread(SectNames, 1, sectHdr.sh_size, fp) != 1) {
        perror("failed to read elf section header:");
        ret = EXIT_FAILURE;
        goto fclose;
    } //reading the size of section
    for(int i=0; i<sectHdr.sh_size; i++)
    {
      char *name1 = "";
      fseek(fp, hdr.e_shoff + i*sizeof(sectHdr), SEEK_SET);
      if (fread(&sectHdr, 1, sizeof(sectHdr), fp) != 1) {
        perror("failed to elf section");
        ret = EXIT_FAILURE;
        goto fclose;
      }
      name1 = SectNames + sectHdr.sh_name;
      printf("section: %s \n", name1);
    }


    /* Get block device size */
    if ((stat_buf.st_mode & S_IFMT) == S_IFBLK) {
        long int blk_size = 0;
        ret = ioctl(fd, BLKGETSIZE64, &blk_size);
        if (ret != 0) {
            perror("BLKGETSIZE64:");
            ret = EXIT_FAILURE;
            goto fclose;
        }
        stat_buf.st_size = blk_size;
    }

    /* Check if image is large enough */
    long int min_size = 100 * LOLELFFS_BLOCK_SIZE;
    if (stat_buf.st_size <= min_size) {
        fprintf(stderr, "File is not large enough (size=%ld, min size=%ld)\n",
                stat_buf.st_size, min_size);
        ret = EXIT_FAILURE;
        goto fclose;
    }

    /* Write superblock (block 0) */
    struct superblock *sb = write_superblock(fd, &stat_buf);
    if (!sb) {
        perror("write_superblock():");
        ret = EXIT_FAILURE;
        goto fclose;
    }

    /* Write inode store blocks (from block 1) */
    ret = write_inode_store(fd, sb);
    if (ret) {
        perror("write_inode_store():");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

    /* Write inode free bitmap blocks */
    ret = write_ifree_blocks(fd, sb);
    if (ret) {
        perror("write_ifree_blocks()");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

    /* Write block free bitmap blocks */
    ret = write_bfree_blocks(fd, sb);
    if (ret) {
        perror("write_bfree_blocks()");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

    /* Write data blocks */
    ret = write_data_blocks(fd, sb);
    if (ret) {
        perror("write_data_blocks():");
        ret = EXIT_FAILURE;
        goto free_sb;
    }

free_sb:
    free(sb);
fclose:
    close(fd);

    return ret;
}
#include <errno.h>
