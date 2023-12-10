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

    struct fs_dirent dir[N_ENT];
    if (block_read(dir, in->ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }
    for (int i = 0; i < N_ENT; i++)
    {
        if (dir[i].valid == 1 && strcmp(name, dir[i].name) == 0)
        {
            return dir[i].inode; // return matched inode number
        }
    }
    return -ENOENT;
}
/*
loopup function for read/write functions? we need to buffer the parent directory, and the matched dirent
*/
int lookup_rw(const char *name, struct fs_inode *in, struct fs_dirent dir[], struct fs_dirent **dirent)
{ // kind of tricky to use double pointer, *dirent to modify dir, **dirent to keep change outside this func
    if (!S_ISDIR(in->mode))
    {
        return -ENOTDIR;
    }

    if (block_read(dir, in->ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }
    for (int i = 0; i < N_ENT; i++)
    {
        if (dir[i].valid == 1 && strcmp(name, dir[i].name) == 0)
        {
            *dirent = &dir[i];
            return dir[i].inode; // return matched inode number
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
// do we need a path_to_parent?
int path_to_parent(const char *path, char *name)
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
    int i = 0;
    for (i; i < argc - 1; i++)
    {
        inum = lookup(argv[i], &in_table[inum]);
        if (inum < 0)
            return inum;
    }
    strcpy(name, argv[i]);
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
int block_idx_to_num(int idx, struct fs_inode *in)
{
    int blk_num;
    if (idx < N_DIRECT)
    {
        blk_num = in->ptrs[idx] ? in->ptrs[idx] : alloc_block();
    }
    else if (idx < N_DIRECT + N_INDIRECT)
    {
        int indir_ptrs[N_INDIRECT];
        block_read(indir_ptrs, in->indir_1, 1);
        if (indir_ptrs[idx - N_DIRECT] == 0)
        {
            indir_ptrs[idx - N_DIRECT] = alloc_block();
            block_write(indir_ptrs, in->indir_1, 1);
        }
        blk_num = indir_ptrs[idx - N_DIRECT];
    }
    else if (idx < N_DIRECT + N_INDIRECT + N_INDIRECT * N_INDIRECT)
    {
        int indir_ptrs_1[N_INDIRECT];
        int indir_ptrs_2[N_INDIRECT];
        int node_idx = (idx - N_DIRECT - N_INDIRECT) / N_INDIRECT;
        int leaf_idx = (idx - N_DIRECT - N_INDIRECT) % N_INDIRECT;
        block_read(indir_ptrs_1, in->indir_2, 1);
        block_read(indir_ptrs_2, indir_ptrs_1[node_idx], 1);
        if (indir_ptrs_2[leaf_idx] == 0)
        {
            indir_ptrs_2[leaf_idx] = alloc_block();
            block_write(indir_ptrs_2, indir_ptrs_1[node_idx], 1);
        }
        blk_num = indir_ptrs_2[leaf_idx];
    }
    else
    {
        return -EINVAL;
    }
    return blk_num;
}

// int append_block(struct fs_inode *in)
// {
//     int n_blk = div_round_up(in->size, BLOCK_SIZE);
//     int blk_num = alloc_block();
//     if (blk_num < 0)
//     {
//         return blk_num;
//     }

//     if (n_blk < N_DIRECT)
//     {
//         in->ptrs[n_blk]=blk_num;
//     }
//     else if (n_blk < N_DIRECT + N_INDIRECT)
//     {
//         int indir_ptrs[N_INDIRECT];
//         block_read(indir_ptrs, in->indir_1, 1);
//         indir_ptrs[n_blk - N_DIRECT] = blk_num;
//         block_write(indir_ptrs, in->indir_1, 1);// but I think we should write back last
//     }
//     else if (n_blk < N_DIRECT + N_INDIRECT + N_INDIRECT * N_INDIRECT)
//     {
//         int indir_ptrs_1[N_INDIRECT];
//         int indir_ptrs_2[N_INDIRECT];
//         block_read(indir_ptrs_1, in->indir_2, 1);
//         int indir_ptr_1 = indir_ptrs_1[(n_blk - N_DIRECT - N_INDIRECT) / N_INDIRECT];
//         block_read(indir_ptrs_2, indir_ptr_1, 1);
//         return indir_ptrs_2[(n_blk - N_DIRECT - N_INDIRECT) % N_INDIRECT];
//     }
//     else
//     {
//         return -EINVAL;
//     }
//     return blk_num;
// }

// int extend_file(struct fs_inode* in, int n_blk, int blk_arr[])
// {
//     int n_blocks = div_round_up(in->size, BLOCK_SIZE);
//     // [0,6,6+256,6+256+256^2]
//     // 3+2+1=6 possibilities

//     int indir_1_ptrs[N_INDIRECT] = {};
//     int indir_2_ptrs[N_INDIRECT][N_INDIRECT] = {};
//     int i = 0; // allocated blocks count
//     for (int i = 0; i < n_blk; i++)
//     {
//         int blk_num=alloc_block();
//         if (blk_num<0)
//         {
//             break;
//         }
//         if (n_blocks < N_DIRECT)
//         {
//             in->ptrs[n_blocks] = blk_num;
//         }
//         else if (n_blocks < N_DIRECT + N_INDIRECT)
//         {
//             block_read(indir_1_ptrs, in->indir_1, 1);
//             indir_1_ptrs[n_blocks-N_DIRECT]=blk_num;
//         }
//         else if (n_blocks < N_DIRECT + N_INDIRECT + N_INDIRECT * N_INDIRECT)
//         {
//             int node_idx = (n_blocks - N_DIRECT - N_INDIRECT) / N_INDIRECT;
//             int leaf_idx = (n_blocks - N_DIRECT - N_INDIRECT) % N_INDIRECT;
//             int buffer[N_INDIRECT];
//             block_read(buffer, in->indir_2, 1);
//             block_read(indir_2_ptrs[node_idx], buffer[node_idx], 1);
//             indir_2_ptrs[node_idx][leaf_idx]=blk_num;
//         }
//         n_blocks++;
//         blk_arr[i]=blk_num;
//     }

//     for (i; old_n_blk + i < n_blk; i++)
//     {
//         int blk_num = alloc_block();
//         if (blk_num < 0)
//         {
//             return blk_num;
//         }
//         if (old_n_blk + i < N_DIRECT)
//         {
//             in->ptrs[old_n_blk + i] = blk_num;
//         }
//         else if (old_n_blk + i < N_DIRECT + N_INDIRECT)
//         {
//             indir_1_ptrs[old_n_blk + i - N_DIRECT] = blk_num;
//         }
//         else if (old_n_blk + i < N_DIRECT + N_INDIRECT + N_INDIRECT * N_INDIRECT)
//         {
//             int node_num = (old_n_blk + i - N_DIRECT - N_INDIRECT) / N_INDIRECT;
//             int leaf_num = (old_n_blk + i - N_DIRECT - N_INDIRECT) % N_INDIRECT;
//             indir_2_ptrs[node_num][leaf_num] = blk_num;
//         }
//     }
//     block_write(indir_1_ptrs, in->indir_1, 1);
//     return i;
// }
int alloc_block()
{
    for (int i = 0; i < superblock->disk_size; i++)
    {
        if (!bit_test(blk_map, i)) // i_th block is free
        {
            bit_set(blk_map, i);
            block_write(blk_map, 1, superblock->blk_map_len);
            // initialize
            char block[BLOCK_SIZE];
            memset(block, 0, sizeof(block));
            block_write(block, i, 1);
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
            struct fs_inode *inode = &in_table[i];
            memset(inode, 0, sizeof(struct fs_inode));
            return i;
        }
    }
    return -ENOSPC; // out of inodes
}

void *lab3_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    // initializes the superblock as a global variable
    superblock = (struct fs_super *)malloc(sizeof(struct fs_super));
    if (block_read(superblock, 0, 1) == -EIO)
    {
        fprintf(stderr, "Superblock read error: %s", strerror(errno));
        exit(EIO);
    }

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

    struct fs_dirent dir[N_ENT];
    if (block_read(dir, inode.ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }
    for (int i = 0; i < N_ENT; i++)
    { // valid entries are not contiguous, go through the whole block
        if (dir[i].valid == 1)
        {
            filler(ptr, dir[i].name, NULL, 0, 0);
        }
    }
    return 0;
}

/*
You can use cat to test this method
*/
int lab3_read(const char *path, char *buf, size_t len, off_t offset, struct fuse_file_info *fi)
{
    if (path == NULL || path[0] != '/')
    {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf_path[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf_path, sizeof(buf_path));

    struct fs_inode inode = in_table[1]; // root directory is inode 1
    // Traverse the directory structure to find the inode
    for (int i = 0; i < argc; i++)
    {
        inode = in_table[lookup(argv[i], &inode)];
    }

    // Checks to see if it is a file
    if (!S_ISREG(inode.mode))
    {
        return -EISDIR;
    }

    // Check if the offset is within the file size
    if (offset >= inode.size) {
        return 0;
    }

    // Calculate the remaining bytes to read
    size_t remaining_bytes = inode.size - offset;
    size_t bytes_to_read = len < remaining_bytes ? len : remaining_bytes;

    // Calculate the block number and offset within the block
    int block_size = BLOCK_SIZE;
    int block_num = offset / block_size;
    int block_offset = offset % block_size;

    // Read data from the file block by block, handling indirect and double indirect blocks
    while (bytes_to_read > 0) {
        char block[BLOCK_SIZE];

        if (block_num < N_DIRECT) {
            // Direct block
            if (block_read(block, inode.ptrs[block_num], 1) == -EIO) {
                return -EIO;
            }
        } else if (block_num < N_DIRECT + (BLOCK_SIZE / sizeof(int32_t))) {
            // Single indirect block
            int32_t indirect_block[BLOCK_SIZE / sizeof(int32_t)];
            if (block_read(indirect_block, inode.indir_1, 1) == -EIO) {
                return -EIO;
            }
            int indirect_index = block_num - N_DIRECT;
            if (indirect_block[indirect_index] == 0) {
                // Block not allocated, consider as zeros
                memset(block, 0, BLOCK_SIZE);
            } else {
                // Read data from the indirect block
                if (block_read(block, indirect_block[indirect_index], 1) == -EIO) {
                    return -EIO;
                }
            }
        } else {
            // Double indirect block
            int32_t double_indirect_block[BLOCK_SIZE / sizeof(int32_t)];
            if (block_read(double_indirect_block, inode.indir_2, 1) == -EIO) {
                return -EIO;
            }
            int double_indirect_index = (block_num - N_DIRECT - (BLOCK_SIZE / sizeof(int32_t))) / (BLOCK_SIZE / sizeof(int32_t));
            int32_t indirect_block[BLOCK_SIZE / sizeof(int32_t)];
            if (block_read(indirect_block, double_indirect_block[double_indirect_index], 1) == -EIO) {
                return -EIO;
            }
            int indirect_index = (block_num - N_DIRECT - (BLOCK_SIZE / sizeof(int32_t))) % (BLOCK_SIZE / sizeof(int32_t));
            if (indirect_block[indirect_index] == 0) {
                // Block not allocated, consider as zeros
                memset(block, 0, BLOCK_SIZE);
            } else {
                // Read data from the indirect block
                if (block_read(block, indirect_block[indirect_index], 1) == -EIO) {
                    return -EIO;
                }
            }
        }

        // Calculate the number of bytes to copy from this block
        size_t bytes_from_block = bytes_to_read < (block_size - block_offset) ? bytes_to_read : (block_size - block_offset);

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

/*
This is used to test lab3_create using touch
*/
int lab3_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi)
{
    return 0;
}

/*
Creates a file in the disk
You can use touch, cat > to test these functions
*/
int lab3_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    if (path == NULL || path[0] != '/')
    {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf_path[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf_path, sizeof(buf_path));

    struct fs_inode parent_inode = in_table[1]; // root directory is inode 1

    // Traverse the directory structure to find the parent inode
    for (int i = 0; i < argc - 1; i++)
    {
        parent_inode = in_table[lookup(argv[i], &parent_inode)];
    }

    // Check if the parent inode represents a directory
    if (!S_ISDIR(parent_inode.mode))
    {
        return -ENOTDIR;
    }

    // Check if the file already exists
    int existing_inode = lookup(argv[argc - 1], &parent_inode);
    if (existing_inode != -ENOENT)
    {
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
    memset(dirents, 0, BLOCK_SIZE);
    block_read(dirents, parent_inode.ptrs[0], 1);

    // Find a free directory entry
    int dirent_idx;
    for (dirent_idx = 0; dirent_idx < N_ENT; dirent_idx++)
    {
        if (!dirents[dirent_idx].valid)
        {
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

/*
Removes a file from the disk
You can use rm to test this function after creating a file
*/
int lab3_unlink(const char *path)
{
    if (path == NULL || path[0] != '/')
    {
        return -ENOENT;
    }

    char *argv[MAX_DEPTH];
    char buf_path[256];
    int argc = split_path(path, MAX_DEPTH, argv, buf_path, sizeof(buf_path));

    struct fs_inode parent_inode = in_table[1]; // root directory is inode 1

    // Find the parent node
    for (int i = 0; i < argc - 1; i++)
    {
        parent_inode = in_table[lookup(argv[i], &parent_inode)];
    }

    if (!S_ISDIR(parent_inode.mode))
    {
        return -ENOTDIR;
    }

    // Check if the file is in parent directory
    int file_inode_num = lookup(argv[argc - 1], &parent_inode);
    if (file_inode_num < 0)
    {
        return file_inode_num; // File does not exist
    }

    struct fs_inode file_inode = in_table[file_inode_num];

    // Check if the file is a directory
    if (S_ISDIR(file_inode.mode))
    {
        return -EISDIR; // Cannot unlink a directory
    }

    // Clear the directory entry in the parent directory
    struct fs_dirent dirents[N_ENT];
    if (block_read(dirents, parent_inode.ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }

    int dirent_idx;
    for (dirent_idx = 0; dirent_idx < N_ENT; dirent_idx++)
    {
        if (dirents[dirent_idx].valid && strcmp(dirents[dirent_idx].name, argv[argc - 1]) == 0)
        {
            break;
        }
    }

    if (dirent_idx < N_ENT)
    {
        dirents[dirent_idx].valid = 0;
    }

    // Write the updated directory entries back to the block
    if (block_write(dirents, parent_inode.ptrs[0], 1) == -EIO)
    {
        return -EIO;
    }

    // Free the bits of inode and block
    bit_clear(in_map, file_inode_num);
    bit_clear(blk_map, file_inode.ptrs[0]);

    // Update the bitmaps in the disk
    block_write(in_map, 1 + superblock->blk_map_len, superblock->in_map_len);
    block_write(blk_map, 1, superblock->blk_map_len);

    return 0;
}

int lab3_mkdir(const char *path, mode_t mode)
{
    // parent path
    char name[28];
    int parent_inum = path_to_parent(path, name);
    if (parent_inum < 0)
        return parent_inum;

    // dir itself
    struct fs_inode *parent_inode = &in_table[parent_inum];
    struct fs_dirent parent_dir[N_ENT];
    struct fs_dirent *parent_dirent;
    int inum = lookup_rw(name, parent_inode, parent_dir, &parent_dirent);
    if (inum > 0) // path already exists
    {
        return -EEXIST;
    }
    else if (inum == -ENOENT) // able to create the new directory
    {
        // allocate block, inode
        int blk_num = alloc_block();
        if (blk_num < 0)
            return blk_num;
        int in_num = alloc_inode();
        if (in_num < 0)
            return in_num;

        // write directory entry of the new dir in parent data block
        int c = 0;
        while (parent_dir[c].valid) // TODO: handle out of space for a new entry
            c++;
        struct fs_dirent *parent_dirent = &parent_dir[c];
        parent_dirent->valid = 1;
        parent_dirent->inode = in_num;
        strcpy(parent_dirent->name, name);
        int parent_blknum = in_table[parent_inum].ptrs[0];
        block_write(parent_dir, parent_blknum, 1);

        // write inode table
        struct fs_inode *inode = &in_table[in_num]; // use a pointer to modify inode table itself
        memset(inode, 0, sizeof(struct fs_inode));
        inode->mode = mode | S_IFDIR;
        inode->size = BLOCK_SIZE;
        inode->mtime = time(NULL);
        inode->ptrs[0] = blk_num;

        int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
        int in_block_offset = in_num / N_INODE;
        block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1);

        // write to new dir data block(an empty file)
        struct fs_dirent new_dir[N_ENT];
        memset(new_dir, 0, BLOCK_SIZE);
        block_write(new_dir, blk_num, 1);
    }
    else // other errors
    {
        return inum;
    }
    return 0;
}
/*
only remove empty dirs
*/
int lab3_rmdir(const char *path)
{
    // make sure parent path is valid
    char name[28];
    int parent_inum = path_to_parent(path, name);
    if (parent_inum < 0)
        return parent_inum;

    // lookup name in parent
    struct fs_inode *parent_inode = &in_table[parent_inum];
    struct fs_dirent parent_dir[N_ENT];
    struct fs_dirent *parent_dirent;
    int inum = lookup_rw(name, parent_inode, parent_dir, &parent_dirent);
    if (inum < 0)
        return inum;
    // struct fs_inode *parent_inode = &in_table[parent_inum];
    // if (!S_ISDIR(parent_inode->mode))
    // {
    //     return -ENOTDIR;
    // }

    // struct fs_dirent parent_dir[N_ENT];
    // if (block_read(parent_dir, parent_inode->ptrs[0], 1) == -EIO)
    // {
    //     return -EIO;
    // }

    // int inum = -ENOENT;
    // struct fs_dirent* parent_dirent;
    // for (int i = 0; i < N_ENT; i++)
    // {
    //     if (parent_dir[i].valid == 1 && strcmp(name, parent_dir[i].name) == 0)
    //     {
    //         parent_dirent = &parent_dir[i];// need to modify parent directory later
    //         inum = parent_dirent->inode;
    //     }
    // }
    // if (inum == -ENOENT)
    //     return inum;

    // check if target is a directory
    struct fs_inode inode = in_table[inum];
    if (!S_ISDIR(inode.mode))
    {
        return -ENOTDIR;
    }

    // check if the directory is empty
    struct fs_dirent dir[N_ENT];
    if (block_read(dir, inode.ptrs[0], 1) == -EIO)
        return -EIO;
    for (int i = 0; i < N_ENT; i++)
    {
        if (dir[i].valid)
            return -ENOTEMPTY;
    }

    // remove dirent in parent
    parent_dirent->valid = 0;
    block_write(parent_dir, parent_inode->ptrs[0], 1);

    // update bitmaps
    bit_clear(blk_map, inode.ptrs[0]);
    block_write(blk_map, 1, 1);
    bit_clear(in_map, inum);
    block_write(in_map, 1 + superblock->blk_map_len, 1);

    return 0;
}

/*
use `mv` to test.
rename:
    $mv /path/to/src /path/to/dest
move:
    $mv /path/to src /path/to/dest/
with slash, dest is treated as a directory
only implemented the first functionality
*/
int lab3_rename(const char *oldpath, const char *newpath, unsigned int flags)
{
    // check either parent is valid
    char old_name[28];
    int old_parent_inum = path_to_parent(oldpath, old_name);
    if (old_parent_inum < 0)
        return old_parent_inum;
    char new_name[28];
    int new_parent_inum = path_to_parent(newpath, new_name);
    if (new_parent_inum < 0)
        return new_parent_inum;
    if (old_parent_inum != new_parent_inum)
    {
        return -EINVAL; // unable to rename across directories
    }

    // lookup oldname in parent, check if valid
    struct fs_inode *parent_inode = &in_table[old_parent_inum];
    struct fs_dirent parent_dir[N_ENT];
    struct fs_dirent *parent_dirent;
    int old_inum = lookup_rw(old_name, parent_inode, parent_dir, &parent_dirent);
    if (old_inum < 0)
        return old_inum;
    // lookup newname in parent, make sure it DOESN'T exist
    int new_inum = lookup(new_name, parent_inode);
    if (new_inum > 0) // destination already exists
        return -EEXIST;
    else if (new_inum == -ENOENT) // able to rename
    {
        // update dirent in parent
        strcpy(parent_dirent->name, new_name);
        block_write(parent_dir, parent_inode->ptrs[0], 1);
        // update inode
        in_table[old_inum].mtime = time(NULL);
        return 0;
    }
    else
    {
        return new_inum;
    }
}

int lab3_chmod(const char *path, mode_t new_mode, struct fuse_file_info *fi)
{
    // Check if the path is valid
    int inum = path_to_inode(path);
    if (inum < 0)
        return inum;

    // Get the corresponding inode
    struct fs_inode *inode = &in_table[inum];

    // Update the mode of the inode
    // inode->mode = (old_mode & S_IFMT) | new_mode;

    // Update modification time
    inode->mtime = time(NULL);

    // Write the updated inode back to the disk
    int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
    int in_block_offset = inum / N_INODE;
    if (block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1) == -EIO)
    {
        return -EIO;
    }

    return 0;
}

/*
Truncates the function to 0 bytes
Test with this function, piazza post allowed implementing truncate only working with file length 0
truncate -s 0 [filename]
*/
int lab3_truncate(const char *path, off_t new_len, struct fuse_file_info *fi)
{
    // Check if the path is valid
    int inum = path_to_inode(path);
    if (inum < 0)
        return inum;
        
    // Get the corresponding inode
    struct fs_inode *inode = &in_table[inum];

    // Check if the file is a regular file
    if (!S_ISREG(inode->mode))
        return -EINVAL; // Invalid argument

    // Case where truncate only truncates to 0
    if (new_len != 0)
        return -EINVAL; // Invalid argument

    // Free all existing blocks
    for (int i = 0; i < N_INODE; i++)
    {
        if (inode->ptrs[i] != 0)
        {
            // Free the block in the block bitmap
            bit_clear(blk_map, inode->ptrs[i]);
            inode->ptrs[i] = 0;
        }
    }

    // Updating inode properties
    inode->size = new_len;
    inode->mtime = time(NULL);

    // Write the updated inode back to the disk
    int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
    int in_block_offset = inum / N_INODE;
    if (block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1) == -EIO)
    {
        return -EIO;
    }

    // Update the block bitmap on the disk
    block_write(blk_map, 1, superblock->blk_map_len);

    return 0;
}

/*
To test:
echo "text" > file

cat > file
hello, world!
^C

cat >> file
Add a new line
^C
for file with multiple lines, the size is correct but can not print correctly
*/
int lab3_write(const char *path, const char *buf, size_t len, off_t offset, struct fuse_file_info *)

{
    // parse path to inode
    int inum = path_to_inode(path);
    if (inum < 0)
        return inum;
    // check if is a file
    struct fs_inode *inode = &in_table[inum];
    if (!S_ISREG(inode->mode))
    {
        return -EISDIR;
    }
    // check offset
    if (offset > inode->size)
    {
        return -EINVAL;
    }

    int start_block = offset / BLOCK_SIZE;
    int end_block = (offset + len) / BLOCK_SIZE;
    int n_blk_done = start_block;

    // assemble the write buffer
    char write_buf[(end_block - start_block + 1) * BLOCK_SIZE];
    memset(write_buf, 0, sizeof(write_buf));
    char *bufptr = write_buf;
    // copy the first block
    int start_blknum = block_idx_to_num(start_block, inode);
    block_read(bufptr, start_blknum, 1);
    // copy the last block
    int end_blknum = block_idx_to_num(end_block, inode);
    if (end_blknum > start_blknum) // if throw an error, there's no space
    {
        block_read(bufptr + (end_block - start_block) * BLOCK_SIZE, end_blknum, 1);
    }
    // copy input buffer
    memcpy(write_buf + offset % BLOCK_SIZE, buf, len);

    // write to data blocks
    int write_cursor = offset;
    int bytes_to_write = len;
    int bytes_written = 0;

    for (n_blk_done; n_blk_done < end_block + 1; n_blk_done++)
    {
        int block_offset = write_cursor % BLOCK_SIZE;
        int bytes = len > (BLOCK_SIZE - block_offset) ? (BLOCK_SIZE - block_offset) : len;
        int blk_num = block_idx_to_num(n_blk_done, inode);
        if (blk_num <= 0)
            break;
        block_write(bufptr, blk_num, 1);
        bufptr += BLOCK_SIZE;
        bytes_written += bytes;
        write_cursor += bytes;
        bytes_to_write -= bytes;
    }

    // update metadata
    if (inode->size < (offset + len))
    { // if write beyond old end of file
        inode->size = inode->size + bytes_written - offset;
    }
    inode->mtime=time(NULL);
    int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
    int in_block_offset = inum / N_INODE;
    block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1);    
    
    return bytes_written;
}
    
    
int lab3_chmod(const char *path, mode_t new_mode, struct fuse_file_info *fi) 
{
    // Check if the path is valid
    int inum = path_to_inode(path);
    if (inum < 0)
        return inum;

    // Get the corresponding inode
    struct fs_inode *inode = &in_table[inum];

    // Update the mode of the inode
    mode_t old_mode = inode->mode;
    inode->mode = (old_mode & S_IFMT) | new_mode;

    // Update modification time
    inode->mtime = time(NULL);

    // Write the updated inode back to the disk
    int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
    int in_block_offset = inum / N_INODE;
    if (block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1) == -EIO)
    {
        return -EIO;
    }

    return 0;
}


/*
Truncates the function to 0 bytes
Test with this function, piazza post allowed implementing truncate only working with file length 0
truncate -s 0 [filename]
*/
int lab3_truncate(const char *path, off_t new_len, struct fuse_file_info *fi) 
{
    // Check if the path is valid
    int inum = path_to_inode(path);
    if (inum < 0)
        return inum;

    // Get the corresponding inode
    struct fs_inode *inode = &in_table[inum];

    // Check if the file is a regular file
    if (!S_ISREG(inode->mode))
        return -EINVAL;  // Invalid argument

    // Case where truncate only truncates to 0
    if (new_len != 0)
        return -EINVAL;  // Invalid argument

    
    // Free all existing blocks
    for (int i = 0; i < N_INODE; i++)
    {
        if (inode->ptrs[i] != 0)
        {
            // Free the block in the block bitmap
            bit_clear(blk_map, inode->ptrs[i]);
            inode->ptrs[i] = 0;
        }
    }

    // Updating inode properties
    inode->size = new_len;
    inode->mtime = time(NULL);

    // Write the updated inode back to the disk
    int in_block_start = 1 + superblock->blk_map_len + superblock->in_map_len;
    int in_block_offset = inum / N_INODE;
    if (block_write(in_table + in_block_offset * N_INODE, in_block_start + in_block_offset, 1) == -EIO)
    {
        return -EIO;
    }

    // Update the block bitmap on the disk
    block_write(blk_map, 1, superblock->blk_map_len);

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

    .utimens = lab3_utimens,
    .create = lab3_create,
    .mkdir = lab3_mkdir,
    .unlink = lab3_unlink,
    .rmdir = lab3_rmdir,
    .rename = lab3_rename,
    .chmod = lab3_chmod,
    .truncate = lab3_truncate,
    .write = lab3_write,
};
