#ifndef RAMFS_UTILS_H
#define RAMFS_UTILS_H

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

// RAMfs file flush function
// This function ensures data is written to the RAMfs and properly synchronized
static inline int ramfs_file_flush(FILE *fp) {
    if (fp == NULL) {
        return -1;
    }
    
    // First flush the stdio buffer
    if (fflush(fp) != 0) {
        return -1;
    }
    
    // Then sync the file descriptor to ensure data reaches the filesystem
    int fd = fileno(fp);
    if (fd < 0) {
        return -1;
    }
    
    // Use fsync to ensure data is written to storage
    if (fsync(fd) != 0) {
        return -1;
    }
    
    return 0;
}

#endif // RAMFS_UTILS_H 