#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/wait.h>

#define SYSCTL_PATH "/proc/sys/net/core/socket_limit_per_process"
#define NEW_LIMIT 100
#define MAIN_SOCKETS 99

struct thread_data {
    int *sockets;
    int socket_count;
    int test_result;
};

int read_socket_limit() {
    int fd, limit = -1;
    char buffer[32];
    
    fd = open(SYSCTL_PATH, O_RDONLY);
    if (fd < 0) {
        printf("   ERROR: Cannot open %s: %s\n", SYSCTL_PATH, strerror(errno));
        return -1;
    }
    
    if (read(fd, buffer, sizeof(buffer) - 1) > 0) {
        limit = atoi(buffer);
    }
    close(fd);
    return limit;
}

int write_socket_limit(int new_limit) {
    int fd;
    char buffer[32];
    
    fd = open(SYSCTL_PATH, O_WRONLY);
    if (fd < 0) {
        printf("   ERROR: Cannot open %s for writing: %s\n", SYSCTL_PATH, strerror(errno));
        return -1;
    }
    
    snprintf(buffer, sizeof(buffer), "%d", new_limit);
    if (write(fd, buffer, strlen(buffer)) < 0) {
        printf("   ERROR: Cannot write to %s: %s\n", SYSCTL_PATH, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

void* thread_function(void* arg) {
    struct thread_data *data = (struct thread_data*)arg;
    int result;
    
    printf("   Thread: Main thread has exited, now testing socket creation...\n");
    
    // 测试1: 创建第100个socket，应该成功（因为限制是100）
    printf("   Thread: Creating socket %d (should succeed):\n", MAIN_SOCKETS + 1);
    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result < 0) {
        printf("   Thread: FAIL - Socket creation should have succeeded but failed: %s\n", strerror(errno));
        data->test_result = 1;
        return NULL;
    }
    printf("   Thread: SUCCESS - Created socket (fd=%d)\n", result);
    data->sockets[0] = result;
    data->socket_count = 1;
    
    // 测试2: 尝试创建第101个socket，应该失败
    printf("   Thread: Creating socket %d (should fail):\n", MAIN_SOCKETS + 2);
    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result >= 0) {
        printf("   Thread: FAIL - Socket creation should have failed but succeeded (fd=%d)\n", result);
        close(result);
        data->test_result = 1;
        return NULL;
    } else {
        printf("   Thread: SUCCESS - Socket creation failed as expected (%s)\n", strerror(errno));
    }
    
    // 测试3: 释放一个socket，然后重新创建，应该成功
    printf("   Thread: Releasing one socket and creating a new one (should succeed):\n");
    close(data->sockets[0]);
    printf("   Thread: Released socket\n");
    
    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result < 0) {
        printf("   Thread: FAIL - Failed to create socket after releasing one: %s\n", strerror(errno));
        data->test_result = 1;
        return NULL;
    }
    printf("   Thread: SUCCESS - Created new socket (fd=%d) after releasing one\n", result);
    data->sockets[0] = result;
    
    data->test_result = 0;
    return NULL;
}

int main() {
    int sockets[MAIN_SOCKETS];
    int i, original_limit;
    pthread_t thread;
    struct thread_data tdata;
    int thread_sockets[2];
    
    printf("=== Socket Limit Thread Test ===\n");
    printf("Testing per-process socket limit sharing between threads\n\n");
    
    // 读取并设置限制
    original_limit = read_socket_limit();
    if (original_limit < 0) {
        return 1;
    }
    printf("Original socket limit: %d\n", original_limit);
    
    printf("1. Setting socket limit to %d:\n", NEW_LIMIT);
    if (write_socket_limit(NEW_LIMIT) < 0) {
        return 1;
    }
    printf("   SUCCESS: Socket limit set to %d\n\n", NEW_LIMIT);
    
    // 在主线程中创建99个socket
    printf("2. Main thread creating %d sockets:\n", MAIN_SOCKETS);
    for (i = 0; i < MAIN_SOCKETS; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sockets[i] < 0) {
            printf("   FAIL: Failed to create socket %d: %s\n", i + 1, strerror(errno));
            goto cleanup;
        }
        if ((i + 1) % 20 == 0) {
            printf("   Created %d sockets...\n", i + 1);
        }
    }
    printf("   SUCCESS: Main thread created %d sockets\n\n", MAIN_SOCKETS);
    
    // 启动子线程
    printf("3. Starting child thread to test shared socket limit:\n");
    tdata.sockets = thread_sockets;
    tdata.socket_count = 0;
    tdata.test_result = -1;
    
    if (pthread_create(&thread, NULL, thread_function, &tdata) != 0) {
        printf("   FAIL: Failed to create thread\n");
        goto cleanup;
    }
    
    // 等待子线程完成
    pthread_join(thread, NULL);
    
    if (tdata.test_result == 0) {
        printf("   SUCCESS: Thread tests passed - socket limit is shared per-process\n\n");
        printf("=== All tests passed! ===\n");
    } else {
        printf("   FAIL: Thread tests failed\n");
    }
    
    // 清理线程创建的socket
    for (i = 0; i < tdata.socket_count; i++) {
        if (tdata.sockets[i] >= 0) {
            close(tdata.sockets[i]);
        }
    }
    
cleanup:
    // 清理主线程的socket
    printf("Cleaning up main thread sockets...\n");
    for (i = 0; i < MAIN_SOCKETS; i++) {
        if (sockets[i] >= 0) {
            close(sockets[i]);
        }
    }
    
    // 恢复原始限制值
    printf("Restoring original socket limit to %d...\n", original_limit);
    write_socket_limit(original_limit);
    
    return (tdata.test_result == 0) ? 0 : 1;
} 