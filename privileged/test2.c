#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <sys/wait.h>

// 系统调用号，从syscall_64.tbl中获取
#define SYS_write_kv 436 // int write_kv(int k, int v)
#define SYS_read_kv  437 // int read_kv(int k)

int main() {
    pid_t pid;
    int value;
    long ret;
    int key = 100;  // 使用整数键
    int parent_value = 12345;  // 父进程的值
    int child_value = 67890;   // 子进程的值
    
    // 父进程写入一个键值对
    printf("Parent: Writing key=%d, value=%d\n", key, parent_value);
    ret = syscall(SYS_write_kv, key, parent_value);
    if (ret < 0) {
        perror("Parent: write_kv failed");
        return 1;
    }
    
    // 读取并验证
    value = syscall(SYS_read_kv, key);
    if (ret < 0) {
        perror("Parent: read_kv failed");
        return 1;
    }
    printf("Parent: Read key=%d, value=%d\n", key, value);
    
    // 创建子进程
    pid = fork();
    
    if (pid < 0) {
        perror("Fork failed");
        return 1;
    } else if (pid == 0) {
        // 子进程
        printf("Child: I'm the child process\n");
        
        // 先读取父进程设置的键值
        value = syscall(SYS_read_kv, key);
        if (ret < 0) {
            perror("Child: read_kv failed");
            exit(1);
        }
        printf("Child: Read key=%d, value=%d\n", key, value);
        
        // 修改这个键值
        printf("Child: Modifying key=%d, value=%d\n", key, child_value);
        ret = syscall(SYS_write_kv, key, child_value);
        if (ret < 0) {
            perror("Child: write_kv failed");
            exit(1);
        }
        
        // 读取并验证修改后的值
        value = syscall(SYS_read_kv, key);
        if (ret < 0) {
            perror("Child: read_kv failed");
            exit(1);
        }
        printf("Child: After modification key=%d, value=%d\n", key, value);
        
        exit(0);
    } else {
        // 父进程等待子进程完成
        wait(NULL);
        printf("Parent: Child process finished\n");
        
        // 检查子进程的修改是否影响了父进程
        value = syscall(SYS_read_kv, key);
        if (ret < 0) {
            perror("Parent: read_kv failed");
            return 1;
        }
        printf("Parent: After child modification key=%d, value=%d\n", key, value);
        
        // 如果值仍然是父进程设置的值，则说明子进程获得了独立的kv_store拷贝
        if (value == parent_value) {
            printf("TEST PASSED: Child process has an independent copy of kv_store\n");
        } else {
            printf("TEST FAILED: Child process modifications affected parent's kv_store (value=%d)\n", value);
        }
    }
    
    return 0;
}

