#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#ifndef TEST_DIR
#define TEST_DIR "test_files"
#endif
#define SMALL_FILE_SIZE (4 * 1024)      // 4KB
#define LARGE_FILE_SIZE (1024 * 1024 * 1024)   // 1GB

int mmapped_file_write(const char* filename, size_t offset, const char* data, size_t data_size) {
    int fd = open(filename, O_RDWR);
    if (fd == -1) {
        perror("mmapped_file_write: open");
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("mmapped_file_write: fstat");
        close(fd);
        return -1;
    }
    size_t original_size = st.st_size;
    size_t required_size = offset + data_size;
    size_t mmap_size = original_size;
    
    if (original_size == 0) {
        mmap_size = (required_size < sysconf(_SC_PAGESIZE)) ? sysconf(_SC_PAGESIZE) : required_size;
        if (ftruncate(fd, mmap_size) == -1) {
            perror("mmapped_file_write: extend empty file");
            close(fd);
            return -1;
        }
    } else if (required_size > original_size) {
        mmap_size = required_size;
        if (ftruncate(fd, mmap_size) == -1) {
            perror("mmapped_file_write: extend file");
            close(fd);
            return -1;
        }
    }
    
    void* mapped = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmapped_file_write: mmap");
        close(fd);
        return -1;
    }
    
    if (offset + data_size <= mmap_size) {
        memcpy((char*)mapped + offset, data, data_size);
    } else {
        printf("Error: Write would exceed mapped region\n");
        munmap(mapped, mmap_size);
        close(fd);
        return -1;
    }
    
    if (msync(mapped, mmap_size, MS_SYNC) == -1) {
        perror("mmapped_file_write: msync");
        munmap(mapped, mmap_size);
        close(fd);
        return -1;
    }
    
    munmap(mapped, mmap_size);
    close(fd);
    
    return 0;
}

char* mmapped_file_read(const char* filename, size_t* file_size) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return NULL;
    }
    *file_size = st.st_size;
    if (*file_size == 0) {
        close(fd);
        return NULL;
    }
    
    void* mapped = mmap(NULL, *file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    
    char* content = malloc(*file_size + 1);
    memcpy(content, mapped, *file_size);
    content[*file_size] = '\0';
    
    munmap(mapped, *file_size);
    close(fd);
    
    return content;
}


// 测试结果结构
typedef struct {
    char* test_name;
    double bandwidth_mbps;
    int success;
    char* error_msg;
} test_result_t;

// 获取当前时间（微秒精度）
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// 创建测试目录
void create_test_directory() {
    if (mkdir(TEST_DIR, 0755) == -1 && errno != EEXIST) {
        perror("mkdir");
        exit(1);
    }
}

// 创建测试文件
void create_test_files() {
    char filepath[256];
    
    // 1. 空文件
    snprintf(filepath, sizeof(filepath), "%s/empty.dat", TEST_DIR);
    int fd = open(filepath, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd == -1) {
        perror("create empty file");
        exit(1);
    }
    close(fd);
    printf("Created empty file: %s\n", filepath);
    
    // 2. 4KB小文件
    snprintf(filepath, sizeof(filepath), "%s/small.dat", TEST_DIR);
    fd = open(filepath, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd == -1) {
        perror("create small file");
        exit(1);
    }
    
    // 填充测试数据
    char buffer[1024];
    for (int i = 0; i < 1024; i++) {
        buffer[i] = 'A' + (i % 26);
    }
    
    for (int i = 0; i < SMALL_FILE_SIZE / 1024; i++) {
        if (write(fd, buffer, 1024) != 1024) {
            perror("write small file");
            exit(1);
        }
    }
    close(fd);
    printf("Created small file: %s (%d bytes)\n", filepath, SMALL_FILE_SIZE);
    
    // 3. 1GB大文件
    snprintf(filepath, sizeof(filepath), "%s/large.dat", TEST_DIR);
    fd = open(filepath, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd == -1) {
        perror("create large file");
        exit(1);
    }
    
    // 填充更复杂的测试数据
    for (int i = 0; i < LARGE_FILE_SIZE / 1024; i++) {
        for (int j = 0; j < 1024; j++) {
            buffer[j] = (char)((i * 1024 + j) & 0xFF);
        }
        if (write(fd, buffer, 1024) != 1024) {
            perror("write large file");
            exit(1);
        }
    }
    close(fd);
    printf("Created large file: %s (%d bytes)\n", filepath, LARGE_FILE_SIZE);
}

// 获取文件信息
void print_file_info(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) == -1) {
        perror("stat");
        return;
    }
    
    printf("File: %s\n", filepath);
    printf("  Size: %ld bytes\n", st.st_size);
    printf("  Mode: %o\n", st.st_mode & 0777);
    printf("  Inode: %ld\n", st.st_ino);
}

// 验证文件内容
int verify_file_content(const char* filepath, const char* expected_content, size_t size) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        perror("verify_file_content: open");
        return 0;
    }
    
    char* buffer = malloc(size + 1);
    if (read(fd, buffer, size) != (ssize_t)size) {
        perror("verify_file_content: read");
        free(buffer);
        close(fd);
        return 0;
    }
    buffer[size] = '\0';
    
    int result = (memcmp(buffer, expected_content, size) == 0);
    
    free(buffer);
    close(fd);
    return result;
}

// 生成hexdump命令进行验证
void hexdump_verify(const char* filepath) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "hexdump -C %s | head -10", filepath);
    printf("\nHexdump of %s:\n", filepath);
    system(cmd);
}

// 测试1：读后写操作
test_result_t test_read_then_write(const char* filepath) {
    test_result_t result = {0};
    result.test_name = "Read-Then-Write";
    
    double start_time = get_time();
    
    // 先读取文件内容（如果文件非空）
    size_t file_size = 0;
    char* original_content = mmapped_file_read(filepath, &file_size);
    
    // 修改内容：在文件开头添加前缀
    const char* new_prefix = "MODIFIED:";
    size_t prefix_len = strlen(new_prefix);
    
    // 使用mmapped_file_write进行覆盖写入
    if (mmapped_file_write(filepath, 0, new_prefix, prefix_len) == 0) {
        // 验证写入内容：重新读取文件并检查前缀
        size_t new_file_size = 0;
        char* new_content = mmapped_file_read(filepath, &new_file_size);
        
        if (new_content && new_file_size >= prefix_len) {
            if (memcmp(new_content, new_prefix, prefix_len) == 0) {
                result.success = 1;
                printf("  ✓ Content verification: Prefix correctly written to file\n");
            } else {
                result.error_msg = "Content verification failed: Prefix not found in file";
            }
        } else {
            result.error_msg = "Content verification failed: Could not read file after write";
        }
        
        if (new_content) {
            free(new_content);
        }
        
        double end_time = get_time();
        double duration = end_time - start_time;
        
        // 计算带宽：读取原内容 + 写入新内容 + 验证读取
        size_t total_bytes = file_size + prefix_len + new_file_size;
        result.bandwidth_mbps = total_bytes / (1024.0 * 1024.0) / duration;
    } else {
        result.error_msg = "Failed to write via mmap";
    }
    
    if (original_content) {
        free(original_content);
    }
    
    return result;
}

// 测试2：跨页写操作
test_result_t test_cross_page_write(const char* filepath) {
    test_result_t result = {0};
    result.test_name = "Cross-Page-Write";
    
    double start_time = get_time();
    
    // 跨页写入：从第一页末尾写到第二页开头
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t write_start = page_size - 16;  // 距离页边界16字节
    const char* cross_page_data = "CROSS_PAGE_WRITE_TEST_DATA_ABCDEFGH";
    size_t write_len = strlen(cross_page_data);
    
    // 使用mmapped_file_write进行跨页写入
    // 函数会自动扩展文件以确保能容纳这个写入操作
    if (mmapped_file_write(filepath, write_start, cross_page_data, write_len) == 0) {
        // 验证跨页写入内容：读取文件并检查指定位置的数据
        size_t file_size = 0;
        char* file_content = mmapped_file_read(filepath, &file_size);
        
        if (file_content && file_size >= write_start + write_len) {
            if (memcmp(file_content + write_start, cross_page_data, write_len) == 0) {
                result.success = 1;
                printf("  ✓ Content verification: Cross-page data correctly written at offset %zu\n", write_start);
                printf("  ✓ Page boundary verification: Data spans from page 1 (offset %zu) to page 2 (offset %zu)\n", 
                       write_start, write_start + write_len - 1);
            } else {
                result.error_msg = "Content verification failed: Cross-page data not found at expected position";
            }
        } else {
            result.error_msg = "Content verification failed: File too small or could not read after write";
        }
        
        if (file_content) {
            free(file_content);
        }
        
        double end_time = get_time();
        double duration = end_time - start_time;
        result.bandwidth_mbps = (write_len + file_size) / (1024.0 * 1024.0) / duration;
    } else {
        result.error_msg = "Failed to perform cross-page write via mmap";
    }
    
    return result;
}

// 测试3：追加写操作
test_result_t test_append_write(const char* filepath) {
    test_result_t result = {0};
    result.test_name = "Append-Write";
    
    double start_time = get_time();
    
    // 获取当前文件大小
    struct stat st;
    size_t original_size = 0;
    if (stat(filepath, &st) == 0) {
        original_size = st.st_size;
    }
    
    // 要追加的数据
    const char* append_data = "\nAPPENDED_DATA: This data was appended via mmap!\n";
    size_t append_len = strlen(append_data);
    
    // 使用mmapped_file_write进行追加写入
    // 写入位置设为文件末尾，函数会自动扩展文件
    if (mmapped_file_write(filepath, original_size, append_data, append_len) == 0) {
        // 验证追加内容：读取文件并检查末尾的数据
        size_t new_file_size = 0;
        char* file_content = mmapped_file_read(filepath, &new_file_size);
        
        if (file_content && new_file_size >= original_size + append_len) {
            if (memcmp(file_content + original_size, append_data, append_len) == 0) {
                result.success = 1;
                printf("  ✓ Content verification: Data correctly appended at offset %zu\n", original_size);
                printf("  ✓ Size verification: File expanded from %zu to %zu bytes\n", 
                       original_size, new_file_size);
            } else {
                result.error_msg = "Content verification failed: Appended data not found at expected position";
            }
        } else {
            result.error_msg = "Content verification failed: File size incorrect or could not read after append";
        }
        
        if (file_content) {
            free(file_content);
        }
        
        double end_time = get_time();
        double duration = end_time - start_time;
        result.bandwidth_mbps = (append_len + new_file_size) / (1024.0 * 1024.0) / duration;
    } else {
        result.error_msg = "Failed to append via mmap";
    }
    
    return result;
}

// 运行所有测试
void run_tests() {
    char test_files[3][256];
    snprintf(test_files[0], sizeof(test_files[0]), "%s/empty.dat", TEST_DIR);
    snprintf(test_files[1], sizeof(test_files[1]), "%s/small.dat", TEST_DIR);
    snprintf(test_files[2], sizeof(test_files[2]), "%s/large.dat", TEST_DIR);
    
    const char* test_file_ptrs[] = {
        test_files[0],
        test_files[1], 
        test_files[2]
    };
    
    test_result_t (*test_functions[])(const char*) = {
        test_read_then_write,
        test_cross_page_write,
        test_append_write
    };
    
    printf("\n=== Running File Mapping Tests ===\n");
    
    for (int file_idx = 0; file_idx < 3; file_idx++) {
        printf("\n--- Testing file: %s ---\n", test_file_ptrs[file_idx]);
        print_file_info(test_file_ptrs[file_idx]);
        
        for (int test_idx = 0; test_idx < 3; test_idx++) {
            printf("\nRunning %s test...\n", 
                   test_idx == 0 ? "Read-Then-Write" :
                   test_idx == 1 ? "Cross-Page-Write" : "Append-Write");
            
            test_result_t result = test_functions[test_idx](test_file_ptrs[file_idx]);
            
            if (result.success) {
                printf("✓ %s: SUCCESS (%.2f MB/s)\n", 
                       result.test_name, result.bandwidth_mbps);
            } else {
                printf("✗ %s: FAILED - %s\n", 
                       result.test_name, result.error_msg ? result.error_msg : "Unknown error");
            }
        }
        
        // 显示修改后的文件内容
        hexdump_verify(test_file_ptrs[file_idx]);
    }
}

int main() {
    printf("=== File Memory Mapping Test Suite ===\n");
    printf("This program tests mmap-based file operations\n");
    printf("Required system calls: open, stat, mmap, munmap, msync\n\n");
    
    // 创建测试环境
    create_test_directory();
    create_test_files();
    
    // 运行测试
    run_tests();
    
    printf("\n=== Test Summary ===\n");
    printf("All tests completed. Check the output above for results.\n");
    printf("Use 'hexdump -C %s/*.dat' to verify file contents.\n", TEST_DIR);
    
    return 0;
}
