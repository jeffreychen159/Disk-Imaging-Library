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
#include "homework.h"

struct fs_super* superblock;

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int blknum, int nblks);
extern int block_write(void *buf, int blknum, int nblks);

struct fs_super* superblock;
void* blk_map;
void* in_map;
struct fs_inode* in_table;
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

/*
search name in directory with inode 'in', and convert it to the subdir inode number
*/
int lookup(const char *name, struct fs_inode *in)
{
    if (!S_ISDIR(in->mode))
    {
        return -ENOTDIR;
    }

    struct fs_dirent dirents[N_ENT];
    if (block_read(dirents, in->ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }
    for (int i = 0; i < N_ENT; i++)
    {
        if (dirents[i].valid == 1 && strcmp(name, dirents[i].name) == 0)
        {
            return dirents[i].inode;
        }
    }
    return -ENOENT;
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

void *lab3_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    // initializes the superblock as a global variable
    superblock = (struct fs_super *)malloc(sizeof(struct fs_super));
    block_read(superblock, 0, 1);

    int blk_map_start = 1;
    int in_map_start = blk_map_start + superblock->blk_map_len;
    int in_table_start = in_map_start + superblock->in_map_len;
    // read bitmaps and inode table
    blk_map = malloc(BLOCK_SIZE * superblock->blk_map_len);
    if (block_read(blk_map, blk_map_start, superblock->blk_map_len)==-EIO)
    {
        fprintf(stderr, "Block bitmap read error: %s", strerror(errno));
        exit(EIO);
    }
    in_map = malloc(BLOCK_SIZE * superblock->in_map_len);
    if (block_read(in_map, in_map_start, superblock->in_map_len)==-EIO)
    {
        fprintf(stderr, "Inode bitmap read error: %s", strerror(errno));
        exit(EIO);
    }
    in_table = malloc(BLOCK_SIZE * superblock->inodes_len);
    if (block_read(in_table, in_table_start, superblock->inodes_len)==-EIO)
    {
        fprintf(stderr, "Inode table read error: %s", strerror(errno));
        exit(EIO);
    }
    // struct fs_dirent buffer[32];
    // block_read(buffer,in_table[1].ptrs[0],1);
    return NULL;
}

/* use stat to test:
$stat tmp
$stat tmp/dir
$stat tmp/file.1
*/
int lab3_getattr(const char *path, struct stat *sb, struct fuse_file_info *fi)
{
    if (path == NULL || path[0] != '/') {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf, sizeof(buf));

    struct fs_inode inode = in_table[1]; // the rootdir is with inode1
    // get inode from the path
    for (int i = 0; i < argc; i++)
    {
        inode = in_table[lookup(argv[i], &inode)];
    }
    inode_2_stat(sb, &inode);

    return 0;
}


int lab3_readdir(const char *path, void *ptr, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    if (path == NULL || path[0] != '/') {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf, sizeof(buf));

    struct fs_inode inode = in_table[1]; // the rootdir is with inode1
    // get inode from the path
    for (int i = 0; i < argc; i++)
    {
        inode = in_table[lookup(argv[i], &inode)];
    }
    // check if is a directory
    if (!S_ISDIR(inode.mode))
    {
        return -ENOTDIR;
    }

    // filler(buf, ".", NULL, 0, 0);
    // filler(buf, "..", NULL, 0, 0);

    struct fs_dirent dirents[N_ENT];
    if (block_read(dirents, inode.ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }
    for (int i = 0; i < N_ENT; i++)
    {// valid entries are not contiguous, go through the whole block
        if (dirents[i].valid == 1)
        {
            filler(ptr, dirents[i].name, NULL, 0, 0);
        }
    }
    return 0;
}

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
    .getattr = lab3_getattr,
    .readdir = lab3_readdir,
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

