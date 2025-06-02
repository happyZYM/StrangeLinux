/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/namei.h>
#include <linux/path.h>
#include "internal.h"

/* ramfs_mount_opts 和 ramfs_fs_info 已在 internal.h 中定义 */

#define RAMFS_DEFAULT_MODE	0755

static const struct super_operations ramfs_ops;
static const struct inode_operations ramfs_dir_inode_operations;

static const struct address_space_operations ramfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.set_page_dirty	= __set_page_dirty_no_writeback,
};

struct inode *ramfs_get_inode(struct super_block *sb,
				const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &ramfs_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &ramfs_file_inode_operations;
			inode->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &ramfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
ramfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = ramfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

static int ramfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int retval = ramfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return ramfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = ramfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = current_time(dir);
		} else
			iput(inode);
	}
	return error;
}

static const struct inode_operations ramfs_dir_inode_operations = {
	.create		= ramfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= ramfs_symlink,
	.mkdir		= ramfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= ramfs_mknod,
	.rename		= simple_rename,
};

/*
 * Display the mount options in /proc/mounts.
 */
static int ramfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct ramfs_fs_info *fsi = root->d_sb->s_fs_info;

	if (fsi->mount_opts.mode != RAMFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mount_opts.mode);
	return 0;
}

static const struct super_operations ramfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= ramfs_show_options,
};

enum ramfs_param {
	Opt_mode,
	Opt_persist_dir,
};

static const struct fs_parameter_spec ramfs_param_specs[] = {
	fsparam_u32oct("mode",	Opt_mode),
	fsparam_string("persist_dir", Opt_persist_dir),
	{}
};

const struct fs_parameter_description ramfs_fs_parameters = {
	.name		= "ramfs",
	.specs		= ramfs_param_specs,
};

static int ramfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct ramfs_fs_info *fsi = fc->s_fs_info;
	int opt;

	opt = fs_parse(fc, &ramfs_fs_parameters, param, &result);
	if (opt < 0) {
		/*
		 * We might like to report bad mount options here;
		 * but traditionally ramfs has ignored all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to ignore other mount options.
		 */
		if (opt == -ENOPARAM)
			opt = 0;
		return opt;
	}

	switch (opt) {
	case Opt_mode:
		fsi->mount_opts.mode = result.uint_32 & S_IALLUGO;
		break;
	case Opt_persist_dir:
		/* 设置持久化目录 */
		kfree(fsi->persist_info.sync_dir);
		fsi->persist_info.sync_dir = kstrdup(param->string, GFP_KERNEL);
		if (!fsi->persist_info.sync_dir)
			return -ENOMEM;
		fsi->persist_info.enabled = true;
		break;
	}

	return 0;
}

static int ramfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct ramfs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= RAMFS_MAGIC;
	sb->s_op		= &ramfs_ops;
	sb->s_time_gran		= 1;

	inode = ramfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static int ramfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, ramfs_fill_super);
}

static void ramfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations ramfs_context_ops = {
	.free		= ramfs_free_fc,
	.parse_param	= ramfs_parse_param,
	.get_tree	= ramfs_get_tree,
};

int ramfs_init_fs_context(struct fs_context *fc)
{
	struct ramfs_fs_info *fsi;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	fsi->mount_opts.mode = RAMFS_DEFAULT_MODE;
	
	/* 初始化持久化信息 */
	mutex_init(&fsi->persist_info.sync_mutex);
	fsi->persist_info.enabled = false;
	fsi->persist_info.sync_dir = NULL;
	
	fc->s_fs_info = fsi;
	fc->ops = &ramfs_context_ops;
	return 0;
}

static void ramfs_kill_sb(struct super_block *sb)
{
	struct ramfs_fs_info *fsi = sb->s_fs_info;
	
	if (fsi) {
		/* 清理持久化资源 */
		kfree(fsi->persist_info.sync_dir);
		kfree(fsi);
	}
	kill_litter_super(sb);
}

static struct file_system_type ramfs_fs_type = {
	.name		= "ramfs",
	.init_fs_context = ramfs_init_fs_context,
	.parameters	= &ramfs_fs_parameters,
	.kill_sb	= ramfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

/**
 * ramfs_bind_instance - 设置特定RAMfs实例的后端同步目录
 * @fsi: RAMfs实例信息
 * @sync_dir: 后端同步目录路径
 * 
 * 返回值: 成功返回0，失败返回负错误码
 */
int ramfs_bind_instance(struct ramfs_fs_info *fsi, const char *sync_dir)
{
	char *new_dir;
	struct path path;
	int ret;

	if (!fsi || !sync_dir)
		return -EINVAL;

	/* 检查目录是否存在 */
	ret = kern_path(sync_dir, LOOKUP_FOLLOW, &path);
	if (ret)
		return ret;
	path_put(&path);

	/* 分配新的目录字符串 */
	new_dir = kstrdup(sync_dir, GFP_KERNEL);
	if (!new_dir)
		return -ENOMEM;

	mutex_lock(&fsi->persist_info.sync_mutex);
	
	/* 释放旧的目录字符串 */
	kfree(fsi->persist_info.sync_dir);
	
	/* 设置新的同步目录 */
	fsi->persist_info.sync_dir = new_dir;
	fsi->persist_info.enabled = true;
	
	mutex_unlock(&fsi->persist_info.sync_mutex);

	printk(KERN_INFO "RAMfs: 实例后端同步目录设置为 %s\n", sync_dir);
	return 0;
}
EXPORT_SYMBOL(ramfs_bind_instance);

/* 
 * 注意: 多实例支持使用挂载参数而不是全局procfs接口
 * 用法: mount -t ramfs -o persist_dir=/tmp/backend none /mnt/ramfs
 */

static int __init init_ramfs_fs(void)
{
	return register_filesystem(&ramfs_fs_type);
}
fs_initcall(init_ramfs_fs);

/* RAMfs是内置的，不需要cleanup函数 */
