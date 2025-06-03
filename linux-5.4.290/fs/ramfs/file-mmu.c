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
#include <linux/mount.h>

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

static int create_single_dir(const char *path, umode_t mode)
{
	struct path parent;
	int error;
	
	error = kern_path(path, LOOKUP_DIRECTORY, &parent);
	if (!error) {
		/* 目录已存在 */
		path_put(&parent);
		return 0;
	}
	
	/* 获取父目录和目录名 */
	{
		char *tmp_path = kstrdup(path, GFP_KERNEL);
		char *name, *parent_path;
		
		if (!tmp_path)
			return -ENOMEM;
		
		name = strrchr(tmp_path, '/');
		if (!name) {
			kfree(tmp_path);
			return -EINVAL;
		}
		
		*name = '\0';
		name++;
		parent_path = tmp_path;
		
		/* 如果父路径为空，使用根目录 */
		if (parent_path[0] == '\0')
			parent_path = "/";
		
		/* 获取父目录 */
		error = kern_path(parent_path, LOOKUP_DIRECTORY, &parent);
		if (error) {
			kfree(tmp_path);
			return error;
		}
		
		/* 创建目录 */
		{
			struct dentry *child;
			struct inode *dir = d_inode(parent.dentry);
			
			inode_lock(dir);
			child = lookup_one_len(name, parent.dentry, strlen(name));
			if (IS_ERR(child)) {
				error = PTR_ERR(child);
			} else {
				error = vfs_mkdir(dir, child, mode);
				dput(child);
			}
			inode_unlock(dir);
		}
		
		if (error == -EEXIST)
			error = 0;
		
		path_put(&parent);
		kfree(tmp_path);
	}
	
	return error;
}

/* 递归创建目录，类似于mkdir -p */
static int mkdir_p(const char *path, umode_t mode)
{
	char *tmp_path, *p;
	int error = 0;
	
	/* 如果路径为空或只有根目录，直接返回成功 */
	if (!path || !*path || (path[0] == '/' && !path[1]))
		return 0;
	
	/* 复制路径以便修改 */
	tmp_path = kstrdup(path, GFP_KERNEL);
	if (!tmp_path)
		return -ENOMEM;
	
	/* 跳过开头的斜杠 */
	p = tmp_path;
	if (*p == '/')
		p++;
	
	/* 逐级创建目录 */
	while ((p = strchr(p, '/'))) {
		*p = '\0';
		error = create_single_dir(tmp_path, mode);
		if (error && error != -EEXIST) {
			printk(KERN_ERR "RAMfs: 创建目录 %s 失败: %d\n", tmp_path, error);
			break;
		}
		*p = '/';
		p++;
	}
	
	/* 创建最后一级目录 */
	if (!error)
		error = create_single_dir(tmp_path, mode);
	
	kfree(tmp_path);
	return error;
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

	/* 构造目标文件路径 - 保留目录结构 */
	{
		char *rel_path = NULL;
		struct dentry *parent;
		struct dentry *root_dentry;
		char *dir_path = NULL;
		
		/* 分配相对路径缓冲区 */
		rel_path = kmalloc(PATH_MAX, GFP_KERNEL);
		if (!rel_path) {
			ret = -ENOMEM;
			goto cleanup;
		}
		rel_path[0] = '\0';
		
		/* 获取挂载点根目录 */
		root_dentry = file->f_path.mnt->mnt_root;
		
		/* 从文件dentry开始，向上构建相对路径，直到挂载点根目录 */
		parent = dentry->d_parent;  /* 从父目录开始，不包括文件自身 */
		while (parent && parent != root_dentry) {
			char *tmp_path;
			int name_len = strlen(parent->d_name.name);
			
			/* 跳过根目录 */
			if (name_len == 0 || (name_len == 1 && parent->d_name.name[0] == '/')) {
				parent = parent->d_parent;
				continue;
			}
			
			/* 动态分配临时路径缓冲区 */
			tmp_path = kmalloc(PATH_MAX, GFP_KERNEL);
			if (!tmp_path) {
				ret = -ENOMEM;
				kfree(rel_path);
				goto cleanup;
			}
			
			/* 构造新的相对路径 */
			if (rel_path[0] == '\0') {
				strcpy(tmp_path, parent->d_name.name);
			} else {
				snprintf(tmp_path, PATH_MAX, "%s/%s", parent->d_name.name, rel_path);
			}
			
			strcpy(rel_path, tmp_path);
			kfree(tmp_path);
			parent = parent->d_parent;
		}
		
		/* 构造最终路径 */
		if (rel_path[0] == '\0') {
			/* 如果是挂载点根目录下的文件 */
			snprintf(full_path, PATH_MAX, "%s/%s", persist_info->sync_dir, dentry->d_name.name);
		} else {
			/* 如果在子目录中 */
			dir_path = kmalloc(PATH_MAX, GFP_KERNEL);
			if (!dir_path) {
				ret = -ENOMEM;
				kfree(rel_path);
				goto cleanup;
			}
			
			/* 构造目录路径 */
			snprintf(dir_path, PATH_MAX, "%s/%s", persist_info->sync_dir, rel_path);
			
			/* 确保目录存在 */
			printk(KERN_DEBUG "RAMfs: 创建目标目录: %s\n", dir_path);
			ret = mkdir_p(dir_path, 0755);
			if (ret < 0) {
				printk(KERN_ERR "RAMfs: 创建目录 %s 失败: %d\n", dir_path, ret);
				kfree(dir_path);
				kfree(rel_path);
				goto cleanup;
			}
			
			/* 构造文件完整路径 */
			snprintf(full_path, PATH_MAX, "%s/%s", dir_path, dentry->d_name.name);
			kfree(dir_path);
		}
		
		kfree(rel_path);
	}

	/* 构造临时文件路径 */
	snprintf(temp_path, PATH_MAX, "%s.tmp.%ld", full_path, (long)current->pid);
	
	printk(KERN_DEBUG "RAMfs: 临时文件路径: %s\n", temp_path);
	printk(KERN_DEBUG "RAMfs: 目标文件路径: %s\n", full_path);

	/* 创建临时文件 */
	temp_file = filp_open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
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

	/* 原子性重命名：将临时文件重命名为最终文件 */
	{
		struct path temp_rename_path;
		struct dentry *temp_dentry, *target_dentry;
		struct inode *dir_inode;
		int rename_ret;
		
		/* 获取临时文件路径 */
		ret = kern_path(temp_path, 0, &temp_rename_path);
		if (ret) {
			printk(KERN_ERR "RAMfs: 无法获取临时文件路径: %d\n", ret);
			goto cleanup;
		}
		
		temp_dentry = temp_rename_path.dentry;
		dir_inode = d_inode(temp_dentry->d_parent);
		
		/* 在同一目录下查找/创建目标文件的dentry */
		inode_lock(dir_inode);
		target_dentry = lookup_one_len(strrchr(full_path, '/') + 1, 
					       temp_dentry->d_parent, 
					       strlen(strrchr(full_path, '/') + 1));
		
		if (IS_ERR(target_dentry)) {
			ret = PTR_ERR(target_dentry);
			inode_unlock(dir_inode);
			path_put(&temp_rename_path);
			printk(KERN_ERR "RAMfs: 无法查找目标文件dentry: %d\n", ret);
			goto cleanup;
		}
		
		/* 执行重命名操作 */
		rename_ret = vfs_rename(dir_inode, temp_dentry, 
					dir_inode, target_dentry, 
					NULL, 0);
		
		if (rename_ret) {
			ret = rename_ret;
			printk(KERN_ERR "RAMfs: 重命名失败: %d\n", ret);
		} else {
			printk(KERN_DEBUG "RAMfs: 原子性重命名成功: %s -> %s\n", 
			       temp_path, full_path);
		}
		
		dput(target_dentry);
		inode_unlock(dir_inode);
		path_put(&temp_rename_path);
	}

	if (ret == 0) {
		printk(KERN_INFO "RAMfs: 文件 %s 持久化成功\n", dentry->d_name.name);
	}

cleanup:
	if (temp_file)
		filp_close(temp_file, NULL);
	
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
