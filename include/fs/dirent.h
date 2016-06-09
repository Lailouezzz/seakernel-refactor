#pragma once

#include <slab.h>
#include <fs/inode.h>

#define DIRENT_UNLINK 1

struct dirent {
	struct kobj_header _header;

	_Atomic int flags;
	struct inode_id ino;
	struct inode_id parent;
	size_t namelen;
	char name[256];
};

#define DIRENT_ID_LEN (sizeof(char) * 256 + sizeof(size_t) + sizeof(struct inode_id))

struct kobj kobj_dirent;
struct inode *dirent_get_inode(struct dirent *);

struct gd_dirent {
	int64_t        d_ino;    /* 64-bit inode number */
	int64_t        d_off;    /* 64-bit offset to next structure */
	unsigned short d_reclen; /* Size of this dirent */
	unsigned char  d_type;   /* File type */
	char           d_name[]; /* Filename (null-terminated) */
};


