#include <fs/filesystem.h>
#include <fs/inode.h>
#include <system.h>
#include <lib/hash.h>
#include <string.h>
#include <errno.h>
#include <file.h>
static struct kobj_idmap active_filesystems;

static struct hash drivers;

__initializer static void _init_fs(void)
{
	kobj_idmap_create(&active_filesystems, sizeof(uint64_t));
	hash_create(&drivers, 0, 32);
}

static _Atomic uint64_t __next_id = 0;

static void _filesystem_init(void *obj)
{
	struct filesystem *fs = obj;
	fs->driver = NULL;
	fs->id = ++__next_id;
	kobj_idmap_insert(&active_filesystems, fs, &fs->id);
}

static void _filesystem_create(void *obj)
{
	struct filesystem *fs = obj;
	mutex_create(&fs->lock);
	_filesystem_init(obj);
}

/* TODO: mounting, unmounting */
static void _filesystem_put(void *obj)
{
	(void)obj;
}

static void _filesystem_destroy(void *obj)
{
	(void)obj;
}

struct kobj kobj_filesystem = {
	.initialized = false,
	.size = sizeof(struct filesystem),
	.name = "filesystem",
	.create = _filesystem_create,
	.init = _filesystem_init,
	.put = _filesystem_put,
	.destroy = _filesystem_destroy,
};

struct file_calls fs_fops = {
	.read = inode_read_data,
	.write = inode_write_data,
	.create = 0, .destroy = 0, .ioctl = 0, .select = 0, .open = 0, .close = 0,
};

int fs_load_inode(uint64_t fsid, uint64_t inoid, struct inode *node)
{
	if(fsid == 0)
		return 0;
	struct filesystem *fs = kobj_idmap_lookup(&active_filesystems, &fsid);
	if(!fs) {
		return -EIO;
	}
	node->fs = fs;
	mutex_acquire(&fs->lock); //TODO: do we need this?
	int ret = fs->driver->fs_ops->load_inode(fs, inoid, node);
	mutex_release(&fs->lock);
	return ret;
}

void fs_update_inode(struct inode *node)
{
	mutex_acquire(&node->fs->lock);
	node->fs->driver->fs_ops->update_inode(node->fs, node);
	mutex_release(&node->fs->lock);
}

int filesystem_register(struct fsdriver *driver)
{
	return hash_insert(&drivers, driver->name, strlen(driver->name), &driver->elem, driver);
}

void filesystem_deregister(struct fsdriver *driver)
{
	hash_delete(&drivers, driver->name, strlen(driver->name));
}
