/*
 * Copyright (c) 2005-2007, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "prex.h"
#include <sys/param.h>
#include "list.h"
#include <sys/stat.h>
#include "vnode.h"
#include "mount.h"
#include "file.h"

#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#define open __open_variadic
#include <fcntl.h>
#undef open

#include "vfs.h"

#ifdef DEBUG_VFS
int	vfs_debug = VFSDB_FLAGS;
#endif

struct task *main_task;	/* we only have a single process */

int open(const char *pathname, int flags, mode_t mode)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	file_t fp;
	int fd, error;
	int acc;

	/* Find empty slot for file descriptor. */
	if ((fd = task_newfd(t)) == -1) {
		errno = EMFILE;
		return -1;
	}

	acc = 0;
	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		acc = VREAD;
		break;
	case O_WRONLY:
		acc = VWRITE;
		break;
	case O_RDWR:
		acc = VREAD | VWRITE;
		break;
	}

	if ((error = task_conv(t, pathname, acc, path)) != 0)
		goto out_errno;

	if ((error = sys_open(path, flags, mode, &fp)) != 0)
		goto out_errno;

	t->t_ofile[fd] = fp;
	t->t_nopens++;
	return fd;
out_errno:
	errno = error;
	return -1;
}

int open64(const char *pathname, int flags, mode_t mode) __attribute__((alias("open")));

int creat(const char *pathname, mode_t mode)
{
	return open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

int close(int fd)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	error = EBADF;

	if (fd >= OPEN_MAX)
		goto out_errno;
	fp = t->t_ofile[fd];
	if (fp == NULL)
		goto out_errno;

	if ((error = sys_close(fp)) != 0)
		goto out_errno;

	t->t_ofile[fd] = NULL;
	t->t_nopens--;
	return 0;
out_errno:
	errno = error;
	return -1;
}

int mknod(const char *pathname, mode_t mode, dev_t dev)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
		goto out_errno;

	error = sys_mknod(path, mode);
	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}

off_t lseek(int fd, off_t offset, int whence)
{
	struct task *t = main_task;
	file_t fp;
	off_t org;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_lseek(fp, offset, whence, &org);
	if (error)
		goto out_errno;
	return org;
out_errno:
	errno = error;
	return -1;
}

ssize_t read(int fd, void *buf, size_t count)
{
	struct task *t = main_task;
	file_t fp;
	size_t bytes;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_read(fp, buf, count, &bytes);
	if (error)
		goto out_errno;

	return bytes;
out_errno:
	errno = error;
	return -1;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	struct task *t = main_task;
	file_t fp;
	size_t bytes;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_write(fp, buf, count, &bytes);
	if (error)
		goto out_errno;
	return bytes;
out_errno:
	errno = error;
	return -1;
}

#if 0
static int
fs_ioctl(struct task *t, struct ioctl_msg *msg)
{
	struct task *t = main_task;
	file_t fp;

	if ((fp = task_getfp(t, msg->fd)) == NULL)
		return EBADF;

	return sys_ioctl(fp, msg->request, msg->buf);
}

static int
fs_fsync(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;

	return sys_fsync(fp);
}
#endif

int __fxstat(int ver, int fd, struct stat *st)
{
	struct task *t = main_task;
	file_t fp;
	int error;

	errno = ENOSYS;
	if (ver != 1)
		goto out_errno;

	error = EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_fstat(fp, st);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

struct stat64;
// FIXME: assumes stat == stat64, may be incorrect for 32-bit port
int __fxstat64(int ver, int fd, struct stat64 *st)
    __attribute__((alias("__fxstat")));

#if 0
static int
fs_opendir(struct task *t, struct open_msg *msg)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	file_t fp;
	int fd, error;

	/* Find empty slot for file descriptor. */
	if ((fd = task_newfd(t)) == -1)
		return EMFILE;

	/* Get the mounted file system and node */
	if ((error = task_conv(t, msg->path, VREAD, path)) != 0)
		return error;

	if ((error = sys_opendir(path, &fp)) != 0)
		return error;
	t->t_ofile[fd] = fp;
	msg->fd = fd;
	return 0;
}

static int
fs_closedir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	int fd, error;

	fd = msg->data[0];
	if (fd >= OPEN_MAX)
		return EBADF;
	fp = t->t_ofile[fd];
	if (fp == NULL)
		return EBADF;

	if ((error = sys_closedir(fp)) != 0)
		return error;
	t->t_ofile[fd] = NULL;
	return 0;
}
#endif

int
ll_readdir(int fd, struct dirent *d)
{
	struct task *t = main_task;
	int error;
	file_t fp;

	error = -EBADF;
	if ((fp = task_getfp(t, fd)) == NULL)
		goto out_errno;

	error = sys_readdir(fp, d);
	if (error)
		goto out_errno;
	return 0;
out_errno:
	errno = error;
	return -1;
}

#if 0
static int
fs_rewinddir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;

	return sys_rewinddir(fp);
}

static int
fs_seekdir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	long loc;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;
	loc = msg->data[1];

	return sys_seekdir(fp, loc);
}

static int
fs_telldir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	long loc;
	int error;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;
	loc = msg->data[1];

	if ((error = sys_telldir(fp, &loc)) != 0)
		return error;
	msg->data[0] = loc;
	return 0;
}
#endif

int
mkdir(const char *pathname, mode_t mode)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	if ((error = task_conv(t, pathname, VWRITE, path)) != 0)
		goto out_errno;

	error = sys_mkdir(path, mode);
	if (error)
		goto out_errno;

	return 0;
out_errno:
	errno = error;
	return -1;
}

#if 0
static int
fs_rmdir(struct task *t, struct path_msg *msg)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	if (msg->path == NULL)
		return ENOENT;
	if ((error = task_conv(t, msg->path, VWRITE, path)) != 0)
		return error;

	return sys_rmdir(path);
}

static int
fs_rename(struct task *t, struct path_msg *msg)
{
	struct task *t = main_task;
	char src[PATH_MAX];
	char dest[PATH_MAX];
	int error;

	if (msg->path == NULL || msg->path2 == NULL)
		return ENOENT;

	if ((error = task_conv(t, msg->path, VREAD, src)) != 0)
		return error;

	if ((error = task_conv(t, msg->path2, VWRITE, dest)) != 0)
		return error;

	return sys_rename(src, dest);
}

static int
fs_chdir(struct task *t, struct path_msg *msg)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	file_t fp;
	int error;

	if (msg->path == NULL)
		return ENOENT;
	if ((error = task_conv(t, msg->path, VREAD, path)) != 0)
		return error;

	/* Check if directory exits */
	if ((error = sys_opendir(path, &fp)) != 0)
		return error;
	if (t->t_cwdfp)
		sys_closedir(t->t_cwdfp);
	t->t_cwdfp = fp;
	strlcpy(t->t_cwd, path, sizeof(t->t_cwd));
 	return 0;
}

static int
fs_fchdir(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	int fd;

	fd = msg->data[0];
	if ((fp = task_getfp(t, fd)) == NULL)
		return EBADF;

	if (t->t_cwdfp)
		sys_closedir(t->t_cwdfp);
	t->t_cwdfp = fp;
	return sys_fchdir(fp, t->t_cwd);
}

static int
fs_link(struct task *t, struct msg *msg)
{
	/* XXX */
	return EPERM;
}

static int
fs_unlink(struct task *t, struct path_msg *msg)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	if (msg->path == NULL)
		return ENOENT;
	if ((error = task_conv(t, msg->path, VWRITE, path)) != 0)
		return error;

	return sys_unlink(path);
}
#endif

int __xstat(int ver, const char *pathname, struct stat *st)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	errno = ENOSYS;
	if (ver != 1)
		goto out_errno;

	error = task_conv(t, pathname, 0, path);
	if (error)
		goto out_errno;

	error = sys_stat(path, st);
	if (error)
		goto out_errno;
	return 0;

out_errno:
	errno = error;
	return -1;
}

struct stat64;
// FIXME: assumes stat == stat64, may be incorrect for 32-bit port
int __xstat64(int ver, const char *pathname, struct stat64 *st)
    __attribute__((alias("__xstat")));

char *getcwd(char *path, size_t size)
{
	struct task *t = main_task;

	strlcpy(path, t->t_cwd, size);
	return 0;
}

/*
 * Duplicate a file descriptor
 */
int dup(int oldfd)
{
	struct task *t = main_task;
	file_t fp;
	int newfd;
	int error;

	error = EBADF;
	if ((fp = task_getfp(t, oldfd)) == NULL)
		goto out_errno;

	/* Find smallest empty slot as new fd. */
	error = EMFILE;
	if ((newfd = task_newfd(t)) == -1)
		goto out_errno;

	t->t_ofile[newfd] = fp;

	/* Increment file reference */
	vref(fp->f_vnode);
	fp->f_count++;

	return newfd;
out_errno:
	errno = error;
	return -1;
}

/*
 * Duplicate a file descriptor to a particular value.
 */
int dup2(int oldfd, int newfd)
{
	struct task *t = main_task;
	file_t fp, org;
	int error;

	error = EBADF;
	if (oldfd >= OPEN_MAX || newfd >= OPEN_MAX)
		goto out_errno;
	fp = t->t_ofile[oldfd];
	if (fp == NULL)
		goto out_errno;
	org = t->t_ofile[newfd];
	if (org != NULL) {
		/* Close previous file if it's opened. */
		error = sys_close(org);
	}
	t->t_ofile[newfd] = fp;

	/* Increment file reference */
	vref(fp->f_vnode);
	fp->f_count++;
	return newfd;
out_errno:
	errno = error;
	return -1;
}

#if 0
/*
 * The file control system call.
 */
static int
fs_fcntl(struct task *t, struct fcntl_msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	int arg, new_fd;

	if ((fp = task_getfp(t, msg->fd)) == NULL)
		return EBADF;

	arg = msg->arg;
	switch (msg->cmd) {
	case F_DUPFD:
		if (arg >= OPEN_MAX)
			return EINVAL;
		/* Find smallest empty slot as new fd. */
		if ((new_fd = task_newfd(t)) == -1)
			return EMFILE;
		t->t_ofile[new_fd] = fp;

		/* Increment file reference */
		vref(fp->f_vnode);
		fp->f_count++;
		msg->arg = new_fd;
		break;
	case F_GETFD:
		msg->arg = fp->f_flags & FD_CLOEXEC;
		break;
	case F_SETFD:
		fp->f_flags = (fp->f_flags & ~FD_CLOEXEC) |
			(msg->arg & FD_CLOEXEC);
		msg->arg = 0;
		break;
	case F_GETFL:
	case F_SETFL:
		msg->arg = -1;
		break;
	default:
		msg->arg = -1;
		break;
	}
	return 0;
}

/*
 * Check permission for file access
 */
static int
fs_access(struct task *t, struct path_msg *msg)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int acc, mode, error = 0;

	mode = msg->data[0];
	acc = 0;
	if (mode & R_OK)
		acc |= VREAD;
	if (mode & W_OK)
		acc |= VWRITE;

	if ((error = task_conv(t, msg->path, acc, path)) != 0)
		return error;

	return sys_access(path, mode);
}

static int
fs_pipe(struct task *t, struct msg *msg)
{
#ifdef CONFIG_FIFOFS
	char path[PATH_MAX];
	file_t rfp, wfp;
	int error, rfd, wfd;

	DPRINTF(VFSDB_CORE, ("fs_pipe\n"));

	if ((rfd = task_newfd(t)) == -1)
		return EMFILE;
	t->t_ofile[rfd] = (file_t)1; /* temp */

	if ((wfd = task_newfd(t)) == -1) {
		t->t_ofile[rfd] = NULL;
		return EMFILE;
	}
	sprintf(path, "/mnt/fifo/pipe-%x-%d", (u_int)t->t_taskid, rfd);

	if ((error = sys_mknod(path, S_IFIFO)) != 0)
		goto out;
	if ((error = sys_open(path, O_RDONLY | O_NONBLOCK, 0, &rfp)) != 0) {
		goto out;
	}
	if ((error = sys_open(path, O_WRONLY | O_NONBLOCK, 0, &wfp)) != 0) {
		goto out;
	}
	t->t_ofile[rfd] = rfp;
	t->t_ofile[wfd] = wfp;
	t->t_nopens += 2;
	msg->data[0] = rfd;
	msg->data[1] = wfd;
	return 0;
 out:
	t->t_ofile[rfd] = NULL;
	t->t_ofile[wfd] = NULL;
	return error;
#else
	return ENOSYS;
#endif
}

/*
 * Return if specified file is a tty
 */
static int
fs_isatty(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;
	int istty = 0;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;

	if (fp->f_vnode->v_flags & VISTTY)
		istty = 1;
	msg->data[0] = istty;
	return 0;
}

static int
fs_truncate(struct task *t, struct path_msg *msg)
{
	struct task *t = main_task;
	char path[PATH_MAX];
	int error;

	if (msg->path == NULL)
		return ENOENT;
	if ((error = task_conv(t, msg->path, VWRITE, path)) != 0)
		return error;

	return sys_truncate(path, msg->data[0]);
}

static int
fs_ftruncate(struct task *t, struct msg *msg)
{
	struct task *t = main_task;
	file_t fp;

	if ((fp = task_getfp(t, msg->data[0])) == NULL)
		return EBADF;

	return sys_ftruncate(fp, msg->data[1]);
}
#endif

int
fs_noop(void)
{
	return 0;
}

#ifdef DEBUG_VFS
/*
 * Dump internal data.
 */
static int
fs_debug(struct task *t, struct msg *msg)
{

	dprintf("<File System Server>\n");
	vnode_dump();
	mount_dump();
	return 0;
}
#endif

struct bootfs_metadata {
	uint64_t size;
	uint64_t offset;
	char name[112];
};

extern char bootfs_start;

void unpack_bootfs(void)
{
	struct bootfs_metadata *md = (struct bootfs_metadata *)&bootfs_start;
	int fd, i;
	const char *dirs[] = {	// XXX: derive from bootfs contents
		"/usr",
		"/usr/lib",
		"/usr/lib/jvm",
		"/usr/lib/jvm/jre",
		"/usr/lib/jvm/jre/lib",
		"/usr/lib/jvm/jre/lib/amd64",
		"/usr/lib/jvm/jre/lib/amd64/server",
		NULL,
	};

	for (i = 0; dirs[i] != NULL; i++) {
		printf("creating %s", dirs[i]);

		if (mkdir(dirs[i], 0666) < 0) {
			perror("mkdir");
			sys_panic("foo");
		}
	}

	for (i = 0; md[i].name[0]; i++) {
		int ret;

		printf("unpacking %s", md[i].name);

		fd = creat(md[i].name, 0666);
		if (fd < 0) {
			printf("couldn't create %s: %d\n",
				md[i].name, errno);
			sys_panic("foo");
		}

		ret = write(fd, &bootfs_start + md[i].offset, md[i].size);
		if (ret != md[i].size) {
			printf("write failed, ret = %d, errno = %d\n",
				ret, errno);
			sys_panic("foo");
		}

		close(fd);
	}
}

void mount_rootfs(void)
{
	int ret;

	ret = sys_mount("", "/", "ramfs", 0, NULL);
	if (ret)
		printf("failed to mount rootfs, error = %d\n", ret);
	else
		printf("mounted rootfs\n");

	if (mkdir("/dev", 0755) < 0)
		printf("failed to create /dev, error = %d\n", errno);

	sys_mount("", "/dev", "devfs", 0, NULL);
	if (ret)
		printf("failed to mount devfs, error = %d\n", ret);
	else
		printf("mounted devfs\n");

}

int console_init(void);

void
vfs_init(void)
{
	const struct vfssw *fs;

	vnode_init();
	task_alloc(&main_task);
	console_init();

	/*
	 * Initialize each file system.
	 */
	for (fs = vfssw; fs->vs_name; fs++) {
		DPRINTF(VFSDB_CORE, ("VFS: initializing %s\n",
				     fs->vs_name));
		fs->vs_init();
	}

	mount_rootfs();
	unpack_bootfs();

	if (open("/dev/console", O_RDWR, 0) != 0)
		printf("failed to open console, error = %d\n", errno);
	if (dup(0) != 1)
		printf("failed to dup console (1)\n");
	if (dup(0) != 2)
		printf("failed to dup console (2)\n");
}

void sys_panic(const char *str)
{
	printf(str);
	while (1)
		;
}

