/* SPDX-License-Identifier: GPL-2.0-or-later */
/* internal.h: ramfs internal definitions
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/mutex.h>

struct ramfs_persist_info {
	char *sync_dir;
	struct mutex sync_mutex;
	bool enabled;
};

struct ramfs_mount_opts {
	umode_t mode;
};

struct ramfs_fs_info {
	struct ramfs_mount_opts mount_opts;
	struct ramfs_persist_info persist_info;
};

extern const struct inode_operations ramfs_file_inode_operations;

int ramfs_bind_instance(struct ramfs_fs_info *fsi, const char *sync_dir);
int ramfs_persist_file(struct file *file);
