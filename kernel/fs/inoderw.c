#include <fs/inode.h>
#include <mmu.h>
#include <string.h>
#include <printk.h>
#include <file.h>
ssize_t inode_read_data(struct file *f, size_t off, size_t len, char *buf)
{
	(void)f; /* TODO: use flags */
	struct inode *ino = file_get_inode(f);
	size_t pageoff = off % arch_mm_page_size(0);
	size_t pagenum = off / arch_mm_page_size(0);
	size_t amount = 0;

	if(off + len > ino->length) {
		if(off >= ino->length)
			return 0;
		len = ino->length - off;
	}

	while(amount < len) {
		size_t thisread = arch_mm_page_size(0) - pageoff;
		if(thisread > (len - amount))
			thisread = len - amount;

		struct inodepage *page = inode_get_page(ino, pagenum);
		if(!page) {
			return amount;
		}
		memcpy(buf, (void *)(page->frame + PHYS_MAP_START + pageoff), thisread);
		inode_release_page(ino, page);
		buf += thisread;
		pageoff = 0;
		pagenum++;
		amount += thisread;
	}
	return amount;
}

ssize_t inode_write_data(struct file *f, size_t off, size_t len, const char *buf)
{
	(void)f; /* TODO: use flags */
	struct inode *ino = file_get_inode(f);
	size_t pageoff = off % arch_mm_page_size(0);
	size_t pagenum = off / arch_mm_page_size(0);
	size_t amount = 0;

	while(amount < len) {
		size_t thiswrite = arch_mm_page_size(0) - pageoff;
		if(thiswrite > (len - amount))
			thiswrite = len - amount;

		struct inodepage *page = inode_get_page(ino, pagenum);
		if(!page) {
			return amount;
		}
		memcpy((void *)(page->frame + PHYS_MAP_START + pageoff), buf, thiswrite);
		if(thiswrite + off + amount > ino->length) {
			ino->length = thiswrite + off + amount;
			inode_mark_dirty(ino);
		}
		inode_release_page(ino, page);
		buf += thiswrite;
		pageoff = 0;
		pagenum++;
		amount += thiswrite;
	}
	return amount;
}

