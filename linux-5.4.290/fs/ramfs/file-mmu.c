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
#include <linux/fcntl.h>

#include "internal.h"

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

static int dev_mkdir(const char *name, umode_t mode)
{
	struct dentry *dentry;
	struct path path;
	int err;

	dentry = kern_path_create(AT_FDCWD, name, &path, LOOKUP_DIRECTORY);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		/* If the directory already exists, that's fine */
		if (err == -EEXIST) {
			struct path existing_path;
			err = kern_path(name, LOOKUP_DIRECTORY, &existing_path);
			if (!err) {
				path_put(&existing_path);
				return 0; /* Directory exists, success */
			}
		}
		return err;
	}

	err = vfs_mkdir(d_inode(path.dentry), dentry, mode);
	done_path_create(&path, dentry);
	return err;
}

static int mkdir_p(const char *nodepath, umode_t mode)
{
	char *path;
	char *s;
	int err = 0;

	if (!nodepath || !*nodepath)
		return 0;

	/* parent directories do not exist, create them */
	path = kstrdup(nodepath, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	s = path;
	/* Skip leading slash */
	if (*s == '/')
		s++;
	
	for (;;) {
		s = strchr(s, '/');
		if (!s)
			break;
		s[0] = '\0';
		if (strlen(path) > 0) { /* Don't try to create empty path */
			err = dev_mkdir(path, mode);
			if (err && err != -EEXIST)
				break;
		}
		s[0] = '/';
		s++;
	}
	
	/* Create the final directory */
	if (!err && strlen(path) > 0) {
		err = dev_mkdir(path, mode);
		if (err == -EEXIST)
			err = 0;
	}
	
	kfree(path);
	return err;
}

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

	persist_info = get_persist_info(file);
	if (!persist_info || !persist_info->enabled || !persist_info->sync_dir) {
		return 0;
	}

	if (!dentry || !inode) {
		printk(KERN_DEBUG "RAMfs: persist_file 参数无效\n");
		return -EINVAL;
	}

	mutex_lock(&persist_info->sync_mutex);

	printk(KERN_DEBUG "RAMfs: 开始持久化文件 %s 到 %s\n",  dentry->d_name.name, persist_info->sync_dir);

	full_path = kmalloc(PATH_MAX, GFP_KERNEL);
	temp_path = kmalloc(PATH_MAX, GFP_KERNEL);
	buffer = kmalloc(buffer_size, GFP_KERNEL);
	
	if (!full_path || !temp_path || !buffer) {
		ret = -ENOMEM;
		goto cleanup;
	}

	{
		char *rel_path = NULL;
		struct dentry *parent;
		struct dentry *root_dentry;
		char *dir_path = NULL;
		
		rel_path = kmalloc(PATH_MAX, GFP_KERNEL);
		if (!rel_path) {
			ret = -ENOMEM;
			goto cleanup;
		}
		rel_path[0] = '\0';
		
		root_dentry = file->f_path.mnt->mnt_root;
		
		parent = dentry->d_parent;
		while (parent && parent != root_dentry) {
			char *tmp_path;
			int name_len = strlen(parent->d_name.name);
			
			if (name_len == 0 || (name_len == 1 && parent->d_name.name[0] == '/')) {
				parent = parent->d_parent;
				continue;
			}
			
			tmp_path = kmalloc(PATH_MAX, GFP_KERNEL);
			if (!tmp_path) {
				ret = -ENOMEM;
				kfree(rel_path);
				goto cleanup;
			}
			
			if (rel_path[0] == '\0') {
				strcpy(tmp_path, parent->d_name.name);
			} else {
				snprintf(tmp_path, PATH_MAX, "%s/%s", parent->d_name.name, rel_path);
			}
			
			strcpy(rel_path, tmp_path);
			kfree(tmp_path);
			parent = parent->d_parent;
		}
		
		if (rel_path[0] == '\0') {
			snprintf(full_path, PATH_MAX, "%s/%s", persist_info->sync_dir, dentry->d_name.name);
		} else {
			dir_path = kmalloc(PATH_MAX, GFP_KERNEL);
			if (!dir_path) {
				ret = -ENOMEM;
				kfree(rel_path);
				goto cleanup;
			}
			
			snprintf(dir_path, PATH_MAX, "%s/%s", persist_info->sync_dir, rel_path);
			
			printk(KERN_DEBUG "RAMfs: 创建目标目录: %s\n", dir_path);
			ret = mkdir_p(dir_path, 0755);
			if (ret < 0 && ret != -EEXIST) {
				printk(KERN_ERR "RAMfs: 创建目录 %s 失败: %d\n", dir_path, ret);
				kfree(dir_path);
				kfree(rel_path);
				goto cleanup;
			}
			ret = 0; /* Reset ret as directory creation is successful */
			
			snprintf(full_path, PATH_MAX, "%s/%s", dir_path, dentry->d_name.name);
			kfree(dir_path);
		}
		
		kfree(rel_path);
	}

	snprintf(temp_path, PATH_MAX, "%s.tmp.%ld", full_path, (long)current->pid);
	
	printk(KERN_DEBUG "RAMfs: 临时文件路径: %s\n", temp_path);
	printk(KERN_DEBUG "RAMfs: 目标文件路径: %s\n", full_path);

	temp_file = filp_open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (IS_ERR(temp_file)) {
		ret = PTR_ERR(temp_file);
		temp_file = NULL;
		printk(KERN_ERR "RAMfs: 无法创建临时文件 %s: %d\n", temp_path, ret);
		goto cleanup;
	}

	printk(KERN_DEBUG "RAMfs: 文件大小: %lld bytes\n", inode->i_size);

	pos = 0;
	while (pos < inode->i_size) {
		struct page *page;
		unsigned long index = pos >> PAGE_SHIFT;
		unsigned long offset = pos & (PAGE_SIZE - 1);
		size_t copy_len = min((size_t)(inode->i_size - pos), 
				      min(buffer_size, PAGE_SIZE - offset));
		void *page_addr;

		page = find_get_page(inode->i_mapping, index);
		if (!page) {
			printk(KERN_ERR "RAMfs: 无法获取页面 %lu\n", index);
			ret = -EIO;
			break;
		}

		page_addr = kmap(page);
		memcpy(buffer, page_addr + offset, copy_len);
		kunmap(page);
		put_page(page);

		printk(KERN_DEBUG "RAMfs: 从页面 %lu 复制了 %zu bytes\n", index, copy_len);

		bytes_written = kernel_write(temp_file, buffer, copy_len,  &temp_file->f_pos);
		if (bytes_written != copy_len) {
			ret = bytes_written < 0 ? bytes_written : -EIO;
			printk(KERN_ERR "RAMfs: 写入临时文件失败: %d\n", ret);
			goto cleanup;
		}

		pos += copy_len;
	}

	printk(KERN_DEBUG "RAMfs: 文件内容复制完成，共 %lld bytes\n", pos);

	filp_close(temp_file, NULL);
	temp_file = NULL;

	{
		struct path temp_rename_path;
		struct dentry *temp_dentry, *target_dentry;
		struct inode *dir_inode;
		int rename_ret;
		
		ret = kern_path(temp_path, 0, &temp_rename_path);
		if (ret) {
			printk(KERN_ERR "RAMfs: 无法获取临时文件路径: %d\n", ret);
			goto cleanup;
		}
		
		temp_dentry = temp_rename_path.dentry;
		dir_inode = d_inode(temp_dentry->d_parent);
		
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
		
		rename_ret = vfs_rename(dir_inode, temp_dentry, 
					dir_inode, target_dentry, 
					NULL, 0);
		
		if (rename_ret) {
			ret = rename_ret;
			printk(KERN_ERR "RAMfs: 重命名失败: %d\n", ret);
		} else {
			printk(KERN_DEBUG "RAMfs: 原子性重命名成功: %s -> %s\n", temp_path, full_path);
		}
		
		dput(target_dentry);
		inode_unlock(dir_inode);
		path_put(&temp_rename_path);
	}

	if (ret == 0) {
		printk(KERN_INFO "RAMfs: 文件 %s 持久化成功\n", dentry->d_name.name);
	}

cleanup:
	if (temp_file) filp_close(temp_file, NULL);
	
	if (ret < 0 && temp_path) {
		struct path temp_cleanup_path;
		if (kern_path(temp_path, 0, &temp_cleanup_path) == 0) {
			vfs_unlink(d_inode(temp_cleanup_path.dentry->d_parent), temp_cleanup_path.dentry, NULL);
			path_put(&temp_cleanup_path);
		}
	}
	
	kfree(full_path);
	kfree(temp_path);
	kfree(buffer);
	
	mutex_unlock(&persist_info->sync_mutex);
	return ret;
}

static int ramfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	// just a wrapper
	return ramfs_persist_file(file);
}

const struct file_operations ramfs_file_operations = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= ramfs_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
	.get_unmapped_area	= ramfs_mmu_get_unmapped_area,
};

const struct inode_operations ramfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};
