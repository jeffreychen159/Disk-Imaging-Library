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
#include <time.h>

#include "fs5600.h"
#include "homework.h"

struct fs_super *superblock;

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int blknum, int nblks);
extern int block_write(void *buf, int blknum, int nblks);

struct fs_super *superblock;
void *blk_map;
void *in_map;
struct fs_inode *in_table;
/* how many buckets of size M do you need to hold N items?
 */
int div_round_up(int n, int m)
{
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

    while (c != 0 && i < argc_max && buf < end)
    {
        argv[i++] = buf;
        while ((c = *path++) && (c != '/') && buf < end)
            *buf++ = c;
        *buf++ = 0;
    }
    return i;
}

/*
search name in directory with inode 'in'
on success return subdir inode number, otherwise error code
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
            return dirents[i].inode; // return matched inode number
        }
    }
    return -ENOENT;
}

/*
convert path to inode number,
on success return inode number, otherwise return error code
*/
int path_to_inode(const char *path)
{
    if (path == NULL || path[0] != '/')
    {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf, sizeof(buf));

    int inum = 1; // the rootdir is with inode1
    // get inode from the path
    for (int i = 0; i < argc; i++)
    {
        inum = lookup(argv[i], &in_table[inum]);
        if (inum < 0)
            return inum;
    }
    return inum;
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
/*
helper functions for allocation
*/
int alloc_block()
{
    for (int i = 0; i < superblock->disk_size; i++)
    {
        if (!bit_test(blk_map, i)) // i_th block is free
        {
            bit_set(blk_map, i);
            block_write(blk_map, 1, superblock->blk_map_len);
            return i;
        }
    }
    return -ENOSPC; // out of blocks
}

int alloc_inode()
{
    for (int i = 0; i < superblock->inodes_len * N_INODE; i++)
    {
        if (!bit_test(in_map, i)) // i_th inode is free
        {
            bit_set(in_map, i);
            block_write(in_map, 1 + superblock->blk_map_len, superblock->in_map_len);
            return i;
        }
    }
    return -ENOSPC; // out of inodes
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
    if (block_read(blk_map, blk_map_start, superblock->blk_map_len) == -EIO)
    {
        fprintf(stderr, "Block bitmap read error: %s", strerror(errno));
        exit(EIO);
    }
    in_map = malloc(BLOCK_SIZE * superblock->in_map_len);
    if (block_read(in_map, in_map_start, superblock->in_map_len) == -EIO)
    {
        fprintf(stderr, "Inode bitmap read error: %s", strerror(errno));
        exit(EIO);
    }
    in_table = malloc(BLOCK_SIZE * superblock->inodes_len);
    if (block_read(in_table, in_table_start, superblock->inodes_len) == -EIO)
    {
        fprintf(stderr, "Inode table read error: %s", strerror(errno));
        exit(EIO);
    }
    // struct fs_dirent buffer[32];
    // block_read(buffer,in_table[1].ptrs[0],1);
    return NULL;
}

/*
use stat to test:
$stat tmp
$stat tmp/dir
$stat tmp/file.1
*/
int lab3_getattr(const char *path, struct stat *sb, struct fuse_file_info *fi)
{
    int inum = path_to_inode(path);
    if (inum < 0)
        return inum;
    inode_2_stat(sb, &in_table[inum]);

    return 0;
}

int lab3_readdir(const char *path, void *ptr, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    int inum = path_to_inode(path);
    if (inum < 0)
        return inum;

    struct fs_inode inode = in_table[inum];
    // check if is a directory
    if (!S_ISDIR(inode.mode))
    {
        return -ENOTDIR;
    }

    struct fs_dirent dirents[N_ENT];
    if (block_read(dirents, inode.ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }
    for (int i = 0; i < N_ENT; i++)
    { // valid entries are not contiguous, go through the whole block
        if (dirents[i].valid == 1)
        {
            filler(ptr, dirents[i].name, NULL, 0, 0);
        }
    }
    return 0;
}

/*
You can use cat to test this method
*/
int lab3_read(const char *path, char *buf, size_t len, off_t offset, struct fuse_file_info *fi) 
{
    if (path == NULL || path[0] != '/') {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf_path[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf_path, sizeof(buf_path));

    struct fs_inode inode = in_table[1]; // root directory is inode 1
    // Traverse the directory structure to find the inode
    for (int i = 0; i < argc; i++) {
        inode = in_table[lookup(argv[i], &inode)];
    }

    // Checks to see if it is a file
    if (!S_ISREG(inode.mode)) {
        return -EISDIR;
    }

    // Check if the offset is within the file size
    if (offset >= inode.size)
    {
        return 0;
    }

    // Calculate the remaining bytes to read
    size_t remaining_bytes = inode.size - offset;
    size_t bytes_to_read = len < remaining_bytes ? len : remaining_bytes;

    // Calculate the block number and offset within the block
    int block_num = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;

    // Read data from the file block by block
    while (bytes_to_read > 0)
    {
        char block[BLOCK_SIZE];
        // Checks if there is an error reading the block
        if (block_read(block, inode.ptrs[block_num], 1) == -EIO)
        {
            return -EIO;
        }

        // Calculate the number of bytes to copy from this block
        size_t bytes_from_block = bytes_to_read < (BLOCK_SIZE - block_offset) ? bytes_to_read : (BLOCK_SIZE - block_offset);

        // Copy the data from the block to the buffer
        memcpy(buf, block + block_offset, bytes_from_block);

        // Update pointers and counters
        buf += bytes_from_block;
        bytes_to_read -= bytes_from_block;
        block_offset = 0;
        block_num++;
    }

    return len - bytes_to_read;
}

int lab3_create(const char *path, mode_t mode, struct fuse_file_info *fi) 
{
    if (path == NULL || path[0] != '/') {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf_path[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf_path, sizeof(buf_path));

    struct fs_inode parent_inode = in_table[1]; // root directory is inode 1

    // Traverse the directory structure to find the parent inode
    for (int i = 0; i < argc - 1; i++) {
        parent_inode = in_table[lookup(argv[i], &parent_inode)];
    }

    // Check if the parent inode represents a directory
    if (!S_ISDIR(parent_inode.mode)) {
        return -ENOTDIR;
    }

    // Check if the file already exists
    int existing_inode = lookup(argv[argc - 1], &parent_inode);
    if (existing_inode != -ENOENT) {
        return -EEXIST;
    }

    // Find a free inode in the inode table
    int inode_num = alloc_inode();
    if (inode_num < 0)
        return inode_num;

    // Initialize the new inode
    struct fs_inode *new_inode = &in_table[inode_num];
    new_inode->mode = S_IFREG | mode;
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    new_inode->size = 0;
    new_inode->mtime = time(NULL);

    // Find a free block in the block bitmap for the file data
    int block_num = alloc_block();

    // Update the new inode with the block pointer
    new_inode->ptrs[0] = block_num;

    int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
    int in_block_offset = inode_num / N_INODE;
    block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1);

    // Update the parent directory entry with the new file information
    struct fs_dirent dirents[N_ENT];
    block_read(dirents, parent_inode.ptrs[0], 1);

    // Find a free directory entry
    int dirent_idx;
    for (dirent_idx = 0; dirent_idx < N_ENT; dirent_idx++) {
        if (!dirents[dirent_idx].valid) {
            break;
        }
    }

    // Update the directory entry with the new file information
    dirents[dirent_idx].valid = 1;
    dirents[dirent_idx].inode = inode_num;
    strncpy(dirents[dirent_idx].name, argv[argc - 1], sizeof(dirents[dirent_idx].name));

    // Write the updated directory entries back to the block
    block_write(dirents, parent_inode.ptrs[0], 1);
    
    return 0;
}


int lab3_mkdir(const char *path, mode_t mode)
{
    if (path == NULL || path[0] != '/')
    {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf, sizeof(buf));

    int inum = 1; // the rootdir is with inode1
    // make sure parent path is valid
    int i = 0;
    for (i; i < argc - 1; i++)
    {
        inum = lookup(argv[i], &in_table[inum]); // inode number of parent
        if (inum < 0)
            return inum;
    }
    // path itself
    int new_inum = lookup(argv[i], &in_table[inum]);
    if (new_inum > 0) // path already exists
    {
        return -EEXIST;
    }
    else if (new_inum == -ENOENT) // able to create the new directory
    {
        // allocate block, inode
        int blk_num = alloc_block();
        if (blk_num < 0)
            return blk_num;
        int in_num = alloc_inode();
        if (in_num < 0)
            return in_num;

        // write inode table
        struct fs_inode *inode = &in_table[in_num];
        inode->ptrs[0] = blk_num;
        inode->size = BLOCK_SIZE;
        inode->mode = mode | S_IFDIR;
        inode->mtime = time(NULL);

        int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
        int in_block_offset = in_num / N_INODE;
        block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1);

        // write directory entry of the new dir in parent data block
        struct fs_dirent dirents[N_ENT];
        int parent_blknum = in_table[inum].ptrs[0];
        block_read(dirents, parent_blknum, 1);
        int c = 0;
        while (dirents[c].valid) // TODO: handle out of space for a new entry
            c++;
        struct fs_dirent *dirent = &dirents[c];
        dirent->valid = 1;
        dirent->inode = in_num;
        strcpy(dirent->name, argv[i]);

        block_write(dirents, parent_blknum, 1);

        // write to new dir data block(an empty file)
        struct fs_dirent new_dirents[N_ENT];
        memset(new_dirents, 0, BLOCK_SIZE);
        block_write(new_dirents, blk_num, 1);
    }
    else // other errors
    {
        return new_inum;
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
    .read = lab3_read,

   .create = lab3_create,
   .mkdir = lab3_mkdir,
//    .unlink = lab3_unlink,
//    .rmdir = lab3_rmdir,
//    .rename = lab3_rename,
//    .chmod = lab3_chmod,
//    .truncate = lab3_truncate,
//    .write = lab3_write,
};
