/* file-mmu.c: ramfs MMU-based file operations
 *
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
#include <linux/mm.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>

#include "internal.h"

/* 获取RAMfs实例的持久化信息 */
static struct ramfs_persist_info *get_persist_info(struct file *file)
{
	struct ramfs_fs_info *fsi = file->f_path.dentry->d_sb->s_fs_info;
	return fsi ? &fsi->persist_info : NULL;
}

static unsigned long ramfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

/**
 * ramfs_persist_file - 将RAMfs文件持久化到后端存储
 * @file: 要持久化的文件
 * 
 * 返回值: 成功返回0，失败返回负错误码
 */
int ramfs_persist_file(struct file *file)
{
	struct ramfs_persist_info *persist_info;
	struct inode *inode = file_inode(file);
	struct dentry *dentry = file->f_path.dentry;
	char *full_path = NULL;
	char *temp_path = NULL;
	struct file *backend_file = NULL;
	struct file *temp_file = NULL;
	loff_t pos = 0;
	ssize_t bytes_written;
	char *buffer = NULL;
	int ret = 0;
	const size_t buffer_size = PAGE_SIZE;

	/* 获取持久化信息 */
	persist_info = get_persist_info(file);
	if (!persist_info || !persist_info->enabled || !persist_info->sync_dir) {
		return 0;  /* 未启用持久化，直接返回成功 */
	}

	if (!dentry || !inode) {
		printk(KERN_DEBUG "RAMfs: persist_file 参数无效\n");
		return -EINVAL;
	}

	mutex_lock(&persist_info->sync_mutex);

	printk(KERN_DEBUG "RAMfs: 开始持久化文件 %s 到 %s\n", 
	       dentry->d_name.name, persist_info->sync_dir);

	/* 分配路径缓冲区 */
	full_path = kmalloc(PATH_MAX, GFP_KERNEL);
	temp_path = kmalloc(PATH_MAX, GFP_KERNEL);
	buffer = kmalloc(buffer_size, GFP_KERNEL);
	
	if (!full_path || !temp_path || !buffer) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/* 构造目标文件路径 */
	snprintf(full_path, PATH_MAX, "%s/%s", persist_info->sync_dir, dentry->d_name.name);
	snprintf(temp_path, PATH_MAX, "%s/.%s.tmp.%ld", persist_info->sync_dir, 
		 dentry->d_name.name, (long)current->pid);

	printk(KERN_DEBUG "RAMfs: 临时文件路径: %s\n", temp_path);
	printk(KERN_DEBUG "RAMfs: 目标文件路径: %s\n", full_path);

	/* 创建临时文件 */
	temp_file = filp_open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(temp_file)) {
		ret = PTR_ERR(temp_file);
		temp_file = NULL;
		printk(KERN_ERR "RAMfs: 无法创建临时文件 %s: %d\n", temp_path, ret);
		goto cleanup;
	}

	printk(KERN_DEBUG "RAMfs: 文件大小: %lld bytes\n", inode->i_size);

	/* 通过inode地址空间直接读取文件内容 */
	pos = 0;
	while (pos < inode->i_size) {
		struct page *page;
		unsigned long index = pos >> PAGE_SHIFT;
		unsigned long offset = pos & (PAGE_SIZE - 1);
		size_t copy_len = min((size_t)(inode->i_size - pos), 
				      min(buffer_size, PAGE_SIZE - offset));
		void *page_addr;

		/* 获取页面 */
		page = find_get_page(inode->i_mapping, index);
		if (!page) {
			printk(KERN_ERR "RAMfs: 无法获取页面 %lu\n", index);
			ret = -EIO;
			break;
		}

		/* 映射页面并复制数据 */
		page_addr = kmap(page);
		memcpy(buffer, page_addr + offset, copy_len);
		kunmap(page);
		put_page(page);

		printk(KERN_DEBUG "RAMfs: 从页面 %lu 复制了 %zu bytes\n", index, copy_len);

		/* 写入临时文件 */
		bytes_written = kernel_write(temp_file, buffer, copy_len, 
					     &temp_file->f_pos);
		if (bytes_written != copy_len) {
			ret = bytes_written < 0 ? bytes_written : -EIO;
			printk(KERN_ERR "RAMfs: 写入临时文件失败: %d\n", ret);
			goto cleanup;
		}

		pos += copy_len;
	}

	printk(KERN_DEBUG "RAMfs: 文件内容复制完成，共 %lld bytes\n", pos);

	/* 关闭临时文件 */
	filp_close(temp_file, NULL);
	temp_file = NULL;

	/* 简化的原子性重命名：直接创建最终文件 */
	backend_file = filp_open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(backend_file)) {
		ret = PTR_ERR(backend_file);
		backend_file = NULL;
		printk(KERN_ERR "RAMfs: 无法创建后端文件 %s: %d\n", full_path, ret);
		goto cleanup;
	}

	/* 重新读取临时文件内容写入最终文件 */
	temp_file = filp_open(temp_path, O_RDONLY, 0);
	if (IS_ERR(temp_file)) {
		ret = PTR_ERR(temp_file);
		temp_file = NULL;
		printk(KERN_ERR "RAMfs: 无法重新打开临时文件: %d\n", ret);
		goto cleanup;
	}

	pos = 0;
	while (1) {
		ssize_t bytes_read = kernel_read(temp_file, buffer, buffer_size, &pos);
		if (bytes_read <= 0) {
			if (bytes_read < 0)
				ret = bytes_read;
			break;
		}

		bytes_written = kernel_write(backend_file, buffer, bytes_read, 
					     &backend_file->f_pos);
		if (bytes_written != bytes_read) {
			ret = bytes_written < 0 ? bytes_written : -EIO;
			printk(KERN_ERR "RAMfs: 写入后端文件失败: %d\n", ret);
			goto cleanup;
		}
	}

	/* 关闭后端文件 */
	filp_close(backend_file, NULL);
	backend_file = NULL;

	/* 关闭临时文件 */
	filp_close(temp_file, NULL);
	temp_file = NULL;

	/* 删除临时文件 */
	{
		struct path temp_unlink_path;
		if (kern_path(temp_path, 0, &temp_unlink_path) == 0) {
			vfs_unlink(d_inode(temp_unlink_path.dentry->d_parent), 
				   temp_unlink_path.dentry, NULL);
			path_put(&temp_unlink_path);
		}
	}

	if (ret == 0) {
		printk(KERN_INFO "RAMfs: 文件 %s 持久化成功\n", dentry->d_name.name);
	}

cleanup:
	if (temp_file)
		filp_close(temp_file, NULL);
	if (backend_file)
		filp_close(backend_file, NULL);
	
	/* 如果失败，清理临时文件 */
	if (ret < 0 && temp_path) {
		struct path temp_cleanup_path;
		if (kern_path(temp_path, 0, &temp_cleanup_path) == 0) {
			vfs_unlink(d_inode(temp_cleanup_path.dentry->d_parent), 
				   temp_cleanup_path.dentry, NULL);
			path_put(&temp_cleanup_path);
		}
	}
	
	kfree(full_path);
	kfree(temp_path);
	kfree(buffer);
	
	mutex_unlock(&persist_info->sync_mutex);
	return ret;
}

/**
 * ramfs_fsync - RAMfs fsync操作，触发持久化
 */
static int ramfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	return ramfs_persist_file(file);
}

const struct file_operations ramfs_file_operations = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= ramfs_fsync,  /* 替换 noop_fsync */
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
	.get_unmapped_area	= ramfs_mmu_get_unmapped_area,
};

const struct inode_operations ramfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};
