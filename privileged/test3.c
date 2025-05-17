#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>

// 系统调用号，从syscall_64.tbl中获取
#define SYS_write_kv 436 // int write_kv(int k, int v)
#define SYS_read_kv  437 // int read_kv(int k)

// 创建一个子程序，用于被execve调用
int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "child") == 0) {
        // 这是被execve创建的子进程
        printf("Child process (execve): Starting\n");
        
        // 尝试读取父进程设置的键值
        int key = 100;
        int value;
        value = syscall(SYS_read_kv, key);
        
        if (value < 0) {
            printf("Child process (execve): read_kv failed with error %d (%s)\n", 
                   errno, strerror(errno));
            printf("TEST PASSED: execve'd process has an empty kv_store\n");
        } else {
            printf("Child process (execve): Read key=%d, value=%d\n", key, value);
            printf("TEST FAILED: execve'd process inherited kv_store\n");
        }
        
        return 0;
    } else {
        // 这是父进程
        int key = 100;
        int value = 12345;
        long ret;
        
        // 父进程写入一个键值对
        printf("Parent: Writing key=%d, value=%d\n", key, value);
        ret = syscall(SYS_write_kv, key, value);
        if (ret < 0) {
            perror("Parent: write_kv failed");
            return 1;
        }
        
        // 读取并验证
        value = syscall(SYS_read_kv, key);
        if (value < 0) {
            perror("Parent: read_kv failed");
            return 1;
        }
        printf("Parent: Read key=%d, value=%d\n", key, value);
        
        // 创建子进程并执行execve
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("Fork failed");
            return 1;
        } else if (pid == 0) {
            // 子进程
            printf("Child: I'm the child process before execve\n");
            
            // 先读取父进程设置的键值，确认fork后能正常读取
            value = syscall(SYS_read_kv, key);
            if (value < 0) {
                perror("Child: read_kv failed");
                exit(1);
            }
            printf("Child: Read key=%d, value=%d before execve\n", key, value);
            
            // 执行execve，重新运行自己，但带上"child"参数
            char *args[] = {argv[0], "child", NULL};
            execve(argv[0], args, NULL);
            
            // 如果execve失败
            perror("Child: execve failed");
            exit(1);
        } else {
            // 父进程等待子进程完成
            int status;
            wait(&status);
            printf("Parent: Child process finished with status %d\n", 
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        }
        
        return 0;
    }
}

