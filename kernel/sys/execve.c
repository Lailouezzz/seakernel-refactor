#include <fs/sys.h>
#include <lib/elf.h>
#include <string.h>
#include <thread.h>
#include <process.h>
#include <printk.h>
#include <errno.h>
#include <sys.h>
#include <file.h>
#include <fs/stat.h>

static void write_data(uintptr_t *end, void *data, size_t len)
{
	*end -= len;
	memcpy((void *) *end, data, len);
}

static void write_aux(uintptr_t *end, long key, long value)
{
	write_data(end, &value, sizeof(long));
	write_data(end, &key, sizeof(long));
}

sysret_t sys_execve(const char *path, char **arg, char **env)
{
	(void)arg;
	(void)env;
	int err = 0;
	int fd = sys_open(path, O_RDONLY, 0);
	if(fd < 0)
		return fd;

	struct elf_header header;
	if(sys_pread(fd, &header, sizeof(header), 0) != sizeof(header)) {
		err = -ENOEXEC;
		goto out_close;
	}

	if(memcmp(header.ident, "\177ELF", 3)) {
		err = -ENOEXEC;
		goto out_close;
	}

	/* set-id */
	struct file *file = process_get_file(fd);
	struct inode *node = file_get_inode(file);
	kobj_putref(file);

	if(node->mode & S_ISUID) {
		current_thread->process->suid = current_thread->process->euid;
		current_thread->process->euid = node->uid;
	}
	if(node->mode & S_ISGID) {
		current_thread->process->sgid = current_thread->process->egid;
		current_thread->process->egid = node->gid;
	}

	kobj_putref(node);

	/* other tests... */

	process_remove_mappings(current_thread->process, false);
	uintptr_t max, phdrs=0;
	if(elf_parse_executable(&header, fd, &max, &phdrs) < 0) {
		sys_exit(-ENOEXEC);
		/* ...die */
	}

	/* TODO: mark executable as "busy" */

	process_close_files(current_thread->process, false);

	uintptr_t aux = (uintptr_t)current_thread->user_tls_base + USER_TLS_SIZE;

	write_aux(&aux, AT_NULL, AT_NULL);
	write_aux(&aux, AT_PAGESZ, arch_mm_page_size(0));
	write_aux(&aux, AT_EXECFD, fd);
	if(phdrs) {
		write_aux(&aux, AT_PHDR, phdrs);
		write_aux(&aux, AT_PHENT, header.phsize);
		write_aux(&aux, AT_PHNUM, header.phnum);
	}
	write_aux(&aux, AT_ENTRY, header.entry);
	/* TODO: aux UID, etc */

	
	long nil = 0;
	write_data(&aux, &nil, sizeof(nil));
	write_data(&aux, &nil, sizeof(nil));

	long argc = 0;
	write_data(&aux, &argc, sizeof(long));
	arch_thread_usermode_jump(header.entry, aux);

out_close:
	sys_close(fd);
	return err;
}
