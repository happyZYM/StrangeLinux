/* SPDX-License-Identifier: GPL-2.0-or-later */
/* internal.h: ramfs internal definitions
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/mutex.h>

/* 持久化相关数据结构 - 每个实例独立 */
struct ramfs_persist_info {
	char *sync_dir;          /* 后端同步目录路径 */
	struct mutex sync_mutex; /* 同步操作互斥锁 */
	bool enabled;           /* 是否启用持久化 */
};

/* RAMfs挂载选项 */
struct ramfs_mount_opts {
	umode_t mode;
};

/* RAMfs文件系统信息 - 需要在所有文件中可见 */
struct ramfs_fs_info {
	struct ramfs_mount_opts mount_opts;
	struct ramfs_persist_info persist_info;  /* 每个实例的持久化信息 */
};

extern const struct inode_operations ramfs_file_inode_operations;

/* 持久化相关函数声明 */
int ramfs_bind_instance(struct ramfs_fs_info *fsi, const char *sync_dir);
int ramfs_persist_file(struct file *file);
