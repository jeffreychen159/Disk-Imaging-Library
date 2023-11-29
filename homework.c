/*
 * file:        homework.c
 * description: skeleton file for CS 5600 system
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2023
 */

#define FUSE_USE_VERSION 30
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse3/fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include "fs5600.h"

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int blknum, int nblks);
extern int block_write(void *buf, int blknum, int nblks);

struct fs_super* superblock;
void* blk_map;
void* in_map;
/* how many buckets of size M do you need to hold N items? 
 */
int div_round_up(int n, int m) {
    return (n + m - 1) / m;
}

/* quick and dirty function to split an absolute path (i.e. begins with "/")
 * uses the same interface as the command line parser in Lab 1
 */
int split_path(const char *path, int argc_max, char **argv, char *buf, int buf_len)
{
    int i = 0, c = 1;
    char *end = buf + buf_len;

    if (*path++ != '/' || *path == 0)
        return 0;
        
    while (c != 0 && i < argc_max && buf < end) {
        argv[i++] = buf;
        while ((c = *path++) && (c != '/') && buf < end)
            *buf++ = c;
        *buf++ = 0;
    }
    return i;
}

/* I'll give you this function for free, to help 
 */
void inode_2_stat(struct stat *sb, struct fs_inode *in)
{
    memset(sb, 0, sizeof(*sb));
    sb->st_mode = in->mode;
    sb->st_nlink = 1;
    sb->st_uid = in->uid;
    sb->st_gid = in->gid;
    sb->st_size = in->size;
    sb->st_blocks = div_round_up(in->size, BLOCK_SIZE);
    sb->st_atime = sb->st_mtime = sb->st_ctime = in->mtime;
}

void* lab3_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    // initializes the superblock as a global variable
    superblock = (struct fs_super *)malloc(sizeof(struct fs_super));
    block_read(superblock, 0, 1);

    blk_map = malloc(BLOCK_SIZE * superblock->blk_map_len);
    block_read(blk_map, 1, superblock->blk_map_len);

    in_map = malloc(BLOCK_SIZE * superblock->in_map_len);
    block_read(in_map, 1 + superblock->blk_map_len, superblock->in_map_len);
    // for (int i = 0; i < BLOCK_SIZE; i++)
    // {
    //     if (bit_test(blk_map,i))
    //     {
    //         printf("block number: %d\tvalid\n",i);
    //     }
    // }
    // for (int i = 0; i < BLOCK_SIZE; i++)
    // {
    //     if (bit_test(in_map,i))
    //     {
    //         printf("inode number: %d\tvalid\n",i);
    //     }
    // }
    return NULL;
}

// static int lab3_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
//                          off_t offset, struct fuse_file_info *fi)
// {
//     (void)offset;
//     (void)fi;

//     if (strcmp(path, "/") != 0)
//         return -ENOENT;
    
//     block_read(buf, 1, 1);
//     filler(buf, ".", NULL, 0, 0);
//     filler(buf, "..", NULL, 0, 0);
//     filler(buf, path + 1, NULL, 0, 0); // Skip the leading '/' in hello_path

//     return 0;
// }
/* for read-only version you need to implement:
 * - lab3_init
 * - lab3_getattr
 * - lab3_readdir
 * - lab3_read
 *
 * for the full version you need to implement:
 * - lab3_create
 * - lab3_mkdir
 * - lab3_unlink
 * - lab3_rmdir
 * - lab3_rename
 * - lab3_chmod
 * - lab3_truncate
 * - lab3_write
 */

/* operations vector. Please don't rename it, or else you'll break things
 * uncomment fields as you implement them.
 */
struct fuse_operations fs_ops = {
    .init = lab3_init,
//    .getattr = lab3_getattr,
//    .readdir = lab3_readdir,
//    .read = lab3_read,

//    .create = lab3_create,
//    .mkdir = lab3_mkdir,
//    .unlink = lab3_unlink,
//    .rmdir = lab3_rmdir,
//    .rename = lab3_rename,
//    .chmod = lab3_chmod,
//    .truncate = lab3_truncate,
//    .write = lab3_write,
};

