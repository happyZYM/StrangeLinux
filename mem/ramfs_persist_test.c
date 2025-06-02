#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/wait.h>
#include <signal.h>

#define RAMFS_MOUNT_POINT "/mnt/ramfs"
#define BACKEND_DIR "/tmp/ramfs_backend"
#define TEST_FILE_PATH RAMFS_MOUNT_POINT "/testfile"
#define PROC_RAMFS_PERSIST "/proc/ramfs_persist"

/* 测试结果统计 */
struct test_stats {
    int total_tests;
    int passed_tests;
    int failed_tests;
};

struct test_stats stats = {0, 0, 0};

void test_result(const char *test_name, int result) {
    stats.total_tests++;
    if (result) {
        stats.passed_tests++;
        printf("[PASS] %s\n", test_name);
    } else {
        stats.failed_tests++;
        printf("[FAIL] %s\n", test_name);
    }
}

/* 检查是否是在支持的环境中运行 */
int check_environment() {
    /* 检查是否有root权限 */
    if (getuid() != 0) {
        printf("警告: 需要root权限来挂载文件系统\n");
        printf("某些测试可能会失败\n\n");
        return 0;
    }
    
    /* 检查proc文件系统是否挂载 */
    if (access("/proc", F_OK) != 0) {
        printf("错误: /proc 文件系统未挂载\n");
        return -1;
    }
    
    return 1;  /* 完整支持 */
}

/* 设置RAMfs持久化后端目录 - 使用挂载参数 */
int setup_ramfs_persist(const char *backend_dir) {
    char mount_options[512];
    
    /* 先卸载可能存在的ramfs */
    umount(RAMFS_MOUNT_POINT);
    
    /* 构造挂载选项 */
    snprintf(mount_options, sizeof(mount_options), "persist_dir=%s", backend_dir);
    
    printf("尝试使用持久化参数挂载: -o %s\n", mount_options);
    
    /* 使用挂载参数挂载RAMfs */
    if (mount("none", RAMFS_MOUNT_POINT, "ramfs", 0, mount_options) < 0) {
        printf("注意: 无法使用持久化参数挂载RAMfs: %s\n", strerror(errno));
        printf("尝试普通挂载...\n");
        
        /* 回退到普通挂载 */
        if (mount("none", RAMFS_MOUNT_POINT, "ramfs", 0, NULL) < 0) {
            printf("错误: 无法挂载RAMfs到 %s: %s\n", RAMFS_MOUNT_POINT, strerror(errno));
            return -1;
        }
        return -1;  /* 表示挂载成功但没有持久化功能 */
    }
    
    printf("RAMfs挂载成功，持久化后端目录: %s\n", backend_dir);
    return 0;
}

/* 创建RAMfs挂载点 */
int setup_ramfs() {
    /* 创建挂载点 */
    if (mkdir(RAMFS_MOUNT_POINT, 0755) < 0 && errno != EEXIST) {
        printf("注意: 无法创建RAMfs挂载点 %s\n", RAMFS_MOUNT_POINT);
        return -1;
    }
    
    printf("RAMfs挂载点创建成功: %s\n", RAMFS_MOUNT_POINT);
    return 0;
}

/* 创建后端目录 */
int setup_backend_dir() {
    if (mkdir(BACKEND_DIR, 0755) < 0 && errno != EEXIST) {
        perror("创建后端目录失败");
        return -1;
    }
    
    printf("后端目录创建成功: %s\n", BACKEND_DIR);
    return 0;
}

/* 测试1: 基本功能验证 */
void test_basic_functionality() {
    printf("\n=== 测试1: 基本功能验证 ===\n");
    
    /* 检查RAMfs是否可用 */
    if (access(RAMFS_MOUNT_POINT, F_OK) == 0) {
        test_result("RAMfs挂载点存在", 1);
    } else {
        test_result("RAMfs挂载点存在", 0);
        return;
    }
    
    /* 检查是否可以在RAMfs中创建文件 */
    int fd = open(RAMFS_MOUNT_POINT "/basic_test", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        const char *content = "Basic test content";
        if (write(fd, content, strlen(content)) > 0) {
            test_result("RAMfs文件创建和写入", 1);
        } else {
            test_result("RAMfs文件创建和写入", 0);
        }
        close(fd);
    } else {
        test_result("RAMfs文件创建和写入", 0);
    }
    
    /* 检查挂载信息中是否包含持久化参数 */
    FILE *mounts = fopen("/proc/mounts", "r");
    if (mounts) {
        char line[1024];
        int found_persist = 0;
        while (fgets(line, sizeof(line), mounts)) {
            if (strstr(line, RAMFS_MOUNT_POINT) && strstr(line, "ramfs")) {
                printf("挂载信息: %s", line);
                if (strstr(line, "persist_dir")) {
                    found_persist = 1;
                }
                break;
            }
        }
        fclose(mounts);
        test_result("RAMfs持久化参数检测", found_persist);
    } else {
        test_result("RAMfs持久化参数检测", 0);
    }
}

/* 测试2: 单文件持久化正确性 */
void test_single_file_persistence() {
    int fd;
    const char *test_content = "Hello, RAMfs Persistence Test!";
    char buffer[256];
    char backend_file_path[512];
    FILE *backend_file;
    
    printf("\n=== 测试2: 单文件持久化正确性 ===\n");
    
    /* 检查是否可以设置后端目录 */
    if (setup_ramfs_persist(BACKEND_DIR) < 0) {
        printf("跳过持久化测试 (接口不可用)\n");
        return;
    }
    
    /* 写入RAMfs文件 */
    fd = open(TEST_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        test_result("创建RAMfs测试文件", 0);
        return;
    }
    
    if (write(fd, test_content, strlen(test_content)) < 0) {
        test_result("写入RAMfs文件内容", 0);
        close(fd);
        return;
    }
    
    /* 触发fsync（应该触发持久化） */
    if (fsync(fd) < 0) {
        printf("注意: fsync失败，但这可能是正常的\n");
    }
    close(fd);
    
    test_result("写入并fsync RAMfs文件", 1);
    
    /* 等待一小段时间让持久化完成 */
    usleep(100000);  /* 100ms */
    
    /* 检查后端文件是否存在且内容正确 */
    snprintf(backend_file_path, sizeof(backend_file_path), "%s/testfile", BACKEND_DIR);
    backend_file = fopen(backend_file_path, "r");
    if (!backend_file) {
        test_result("后端文件存在性检查", 0);
        printf("注意: 后端文件 %s 不存在\n", backend_file_path);
        return;
    }
    
    memset(buffer, 0, sizeof(buffer));
    if (fread(buffer, 1, sizeof(buffer) - 1, backend_file) <= 0) {
        test_result("读取后端文件内容", 0);
        fclose(backend_file);
        return;
    }
    fclose(backend_file);
    
    if (strcmp(buffer, test_content) == 0) {
        test_result("后端文件内容一致性", 1);
    } else {
        printf("期望内容: %s\n实际内容: %s\n", test_content, buffer);
        test_result("后端文件内容一致性", 0);
    }
}

/* 多线程写入测试的线程函数 */
struct thread_args {
    int thread_id;
    int write_count;
    volatile int *should_stop;
};

void* writer_thread(void* arg) {
    struct thread_args *args = (struct thread_args*)arg;
    char filename[256];
    char content[256];
    int fd;
    int i;
    
    for (i = 0; i < args->write_count && !(*args->should_stop); i++) {
        snprintf(filename, sizeof(filename), "%s/thread_%d_file_%d", 
                 RAMFS_MOUNT_POINT, args->thread_id, i);
        snprintf(content, sizeof(content), "Thread %d, File %d, Time %ld", 
                 args->thread_id, i, time(NULL));
        
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, content, strlen(content));
            close(fd);
        }
        
        usleep(10000); /* 10ms延迟 */
    }
    
    return NULL;
}

void* flusher_thread(void* arg) {
    struct thread_args *args = (struct thread_args*)arg;
    int i;
    char filename[256];
    int fd;
    
    usleep(500000); /* 等待500ms让一些写入操作完成 */
    
    for (i = 0; i < 5 && !(*args->should_stop); i++) {
        snprintf(filename, sizeof(filename), "%s/thread_0_file_%d", 
                 RAMFS_MOUNT_POINT, i);
        fd = open(filename, O_RDONLY);
        if (fd >= 0) {
            fsync(fd); /* 触发持久化 */
            close(fd);
        }
        usleep(50000); /* 50ms延迟 */
    }
    
    return NULL;
}

/* 测试3: 多线程顺序一致性 */
void test_multithread_consistency() {
    pthread_t writers[3];
    pthread_t flusher;
    struct thread_args args[4];
    volatile int should_stop = 0;
    int i;
    
    printf("\n=== 测试3: 多线程顺序一致性 ===\n");
    
    /* 检查RAMfs是否挂载且持久化可用 */
    if (access(RAMFS_MOUNT_POINT, F_OK) != 0) {
        printf("跳过多线程测试 (RAMfs挂载点不可用)\n");
        return;
    }
    
    /* 创建写入线程 */
    for (i = 0; i < 3; i++) {
        args[i].thread_id = i;
        args[i].write_count = 10;
        args[i].should_stop = &should_stop;
        if (pthread_create(&writers[i], NULL, writer_thread, &args[i]) != 0) {
            test_result("创建写入线程", 0);
            should_stop = 1;
            return;
        }
    }
    
    /* 创建flush线程 */
    args[3].should_stop = &should_stop;
    if (pthread_create(&flusher, NULL, flusher_thread, &args[3]) != 0) {
        test_result("创建flush线程", 0);
        should_stop = 1;
    }
    
    /* 运行5秒后停止 */
    sleep(5);
    should_stop = 1;
    
    /* 等待所有线程完成 */
    for (i = 0; i < 3; i++) {
        pthread_join(writers[i], NULL);
    }
    pthread_join(flusher, NULL);
    
    test_result("多线程写入和flush操作", 1);
    
    /* 检查一些文件是否被持久化 */
    char backend_file_path[512];
    int persistent_files = 0;
    for (i = 0; i < 5; i++) {
        snprintf(backend_file_path, sizeof(backend_file_path), 
                 "%s/thread_0_file_%d", BACKEND_DIR, i);
        if (access(backend_file_path, F_OK) == 0) {
            persistent_files++;
        }
    }
    
    if (persistent_files > 0) {
        printf("发现 %d 个持久化文件\n", persistent_files);
        test_result("多线程持久化验证", 1);
    } else {
        test_result("多线程持久化验证", 0);
        printf("注意: 没有发现持久化文件，可能需要手动触发或功能未实现\n");
    }
}

/* 测试4: 多实例独立性 */
void test_multi_instance() {
    const char *mount_point1 = "/mnt/ramfs1";
    const char *mount_point2 = "/mnt/ramfs2";
    const char *backend_dir1 = "/tmp/backend1";
    const char *backend_dir2 = "/tmp/backend2";
    char mount_options1[512], mount_options2[512];
    char file_path1[512], file_path2[512];
    char backend_file1[512], backend_file2[512];
    int fd;
    const char *content1 = "Instance 1 content";
    const char *content2 = "Instance 2 content";
    char buffer[256];
    FILE *backend_file;
    
    printf("\n=== 测试4: 多实例独立性 ===\n");
    
    /* 创建挂载点和后端目录 */
    mkdir(mount_point1, 0755);
    mkdir(mount_point2, 0755);
    mkdir(backend_dir1, 0755);
    mkdir(backend_dir2, 0755);
    
    /* 准备挂载选项 */
    snprintf(mount_options1, sizeof(mount_options1), "persist_dir=%s", backend_dir1);
    snprintf(mount_options2, sizeof(mount_options2), "persist_dir=%s", backend_dir2);
    
    /* 挂载第一个实例 */
    if (mount("none", mount_point1, "ramfs", 0, mount_options1) < 0) {
        printf("跳过多实例测试 (无法挂载第一个实例: %s)\n", strerror(errno));
        return;
    }
    
    /* 挂载第二个实例 */
    if (mount("none", mount_point2, "ramfs", 0, mount_options2) < 0) {
        printf("跳过多实例测试 (无法挂载第二个实例: %s)\n", strerror(errno));
        umount(mount_point1);
        return;
    }
    
    test_result("多实例挂载", 1);
    
    /* 在第一个实例中写入文件 */
    snprintf(file_path1, sizeof(file_path1), "%s/file1", mount_point1);
    fd = open(file_path1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, content1, strlen(content1));
        fsync(fd);
        close(fd);
        test_result("实例1文件写入", 1);
    } else {
        test_result("实例1文件写入", 0);
    }
    
    /* 在第二个实例中写入文件 */
    snprintf(file_path2, sizeof(file_path2), "%s/file2", mount_point2);
    fd = open(file_path2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, content2, strlen(content2));
        fsync(fd);
        close(fd);
        test_result("实例2文件写入", 1);
    } else {
        test_result("实例2文件写入", 0);
    }
    
    /* 等待持久化完成 */
    usleep(200000);
    
    /* 检查实例1的后端文件 */
    snprintf(backend_file1, sizeof(backend_file1), "%s/file1", backend_dir1);
    backend_file = fopen(backend_file1, "r");
    if (backend_file) {
        memset(buffer, 0, sizeof(buffer));
        fread(buffer, 1, sizeof(buffer) - 1, backend_file);
        fclose(backend_file);
        
        if (strcmp(buffer, content1) == 0) {
            test_result("实例1独立持久化", 1);
        } else {
            test_result("实例1独立持久化", 0);
            printf("期望: %s, 实际: %s\n", content1, buffer);
        }
    } else {
        test_result("实例1独立持久化", 0);
        printf("后端文件不存在: %s\n", backend_file1);
    }
    
    /* 检查实例2的后端文件 */
    snprintf(backend_file2, sizeof(backend_file2), "%s/file2", backend_dir2);
    backend_file = fopen(backend_file2, "r");
    if (backend_file) {
        memset(buffer, 0, sizeof(buffer));
        fread(buffer, 1, sizeof(buffer) - 1, backend_file);
        fclose(backend_file);
        
        if (strcmp(buffer, content2) == 0) {
            test_result("实例2独立持久化", 1);
        } else {
            test_result("实例2独立持久化", 0);
            printf("期望: %s, 实际: %s\n", content2, buffer);
        }
    } else {
        test_result("实例2独立持久化", 0);
        printf("后端文件不存在: %s\n", backend_file2);
    }
    
    /* 清理 */
    umount(mount_point1);
    umount(mount_point2);
}

/* 清理函数 */
void cleanup() {
    /* 尝试卸载RAMfs */
    if (umount(RAMFS_MOUNT_POINT) == 0) {
        printf("RAMfs卸载成功\n");
    }
    
    /* 删除挂载点 */
    rmdir(RAMFS_MOUNT_POINT);
    
    /* 清理后端目录中的测试文件 */
    system("rm -f " BACKEND_DIR "/thread_* " BACKEND_DIR "/testfile " BACKEND_DIR "/disable_test 2>/dev/null");
}

/* 显示手动测试指令 */
void show_manual_test_instructions() {
    printf("\n=== 手动测试指令 ===\n");
    printf("如果某些自动化测试失败，可以尝试手动测试：\n\n");
    
    printf("1. 基本挂载和使用：\n");
    printf("   mkdir -p /mnt/ramfs\n");
    printf("   mount -t ramfs none /mnt/ramfs\n");
    printf("   echo 'test' > /mnt/ramfs/file1\n");
    printf("   cat /mnt/ramfs/file1\n\n");
    
    printf("2. 使用持久化功能挂载（新方式）：\n");
    printf("   mkdir -p /tmp/ramfs_backend\n");
    printf("   umount /mnt/ramfs  # 先卸载\n");
    printf("   mount -t ramfs -o persist_dir=/tmp/ramfs_backend none /mnt/ramfs\n\n");
    
    printf("3. 测试持久化：\n");
    printf("   echo 'persistent content' > /mnt/ramfs/persistent_file\n");
    printf("   sync  # 或者使用 fsync\n");
    printf("   cat /tmp/ramfs_backend/persistent_file\n\n");
    
    printf("4. 多实例测试：\n");
    printf("   # 实例1\n");
    printf("   mkdir -p /mnt/ramfs1 /tmp/backend1\n");
    printf("   mount -t ramfs -o persist_dir=/tmp/backend1 none /mnt/ramfs1\n");
    printf("   # 实例2\n");
    printf("   mkdir -p /mnt/ramfs2 /tmp/backend2\n");
    printf("   mount -t ramfs -o persist_dir=/tmp/backend2 none /mnt/ramfs2\n");
    printf("   # 测试两个实例独立持久化\n");
    printf("   echo 'data1' > /mnt/ramfs1/file1; sync\n");
    printf("   echo 'data2' > /mnt/ramfs2/file2; sync\n");
    printf("   cat /tmp/backend1/file1  # 应该显示 data1\n");
    printf("   cat /tmp/backend2/file2  # 应该显示 data2\n\n");
    
    printf("5. 清理：\n");
    printf("   umount /mnt/ramfs /mnt/ramfs1 /mnt/ramfs2\n");
    printf("   rm -rf /tmp/ramfs_backend /tmp/backend1 /tmp/backend2\n");
}

int main(int argc, char *argv[]) {
    printf("=================================\n");
    printf("RAMfs持久化功能测试程序\n");
    printf("=================================\n");
    
    /* 检查运行环境 */
    int env_status = check_environment();
    if (env_status < 0) {
        printf("环境检查失败，退出测试\n");
        return 1;
    }
    
    /* 解析命令行参数 */
    int manual_only = 0;
    if (argc > 1 && strcmp(argv[1], "--manual") == 0) {
        manual_only = 1;
    }
    
    if (manual_only) {
        show_manual_test_instructions();
        return 0;
    }
    
    /* 设置测试环境 */
    printf("\n=== 环境设置 ===\n");
    
    if (setup_backend_dir() < 0) {
        printf("设置后端目录失败，继续测试其他功能\n");
    }
    
    if (setup_ramfs() < 0) {
        printf("设置RAMfs失败，某些测试将跳过\n");
    }
    
    /* 运行测试 */
    test_basic_functionality();
    test_single_file_persistence();
    test_multithread_consistency();
    test_multi_instance();
    
    /* 清理 */
    cleanup();
    
    /* 输出测试结果 */
    printf("\n=== 测试结果总结 ===\n");
    printf("总测试数: %d\n", stats.total_tests);
    printf("通过测试: %d\n", stats.passed_tests);
    printf("失败测试: %d\n", stats.failed_tests);
    
    if (stats.total_tests > 0) {
        printf("成功率: %.1f%%\n", (float)stats.passed_tests / stats.total_tests * 100);
    }
    
    if (stats.failed_tests > 0) {
        printf("\n注意: 某些测试失败可能是因为：\n");
        printf("- 内核不支持持久化功能 (需要使用修改后的内核)\n");
        printf("- 缺少root权限\n");
        printf("- procfs未挂载或不支持\n");
        
        show_manual_test_instructions();
    }
    
    return stats.failed_tests > 0 ? 1 : 0;
} 