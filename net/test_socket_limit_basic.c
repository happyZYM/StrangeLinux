#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

#define DEFAULT_LIMIT 1024

int main() {
    int sockets[DEFAULT_LIMIT + 1];
    int i, result;
    
    printf("=== Socket Limit Basic Test ===\n");
    printf("Testing default limit of %d sockets per process\n\n", DEFAULT_LIMIT);
    
    // 测试1: 创建1024个socket，应该成功
    printf("1. Creating %d sockets (should succeed):\n", DEFAULT_LIMIT);
    for (i = 0; i < DEFAULT_LIMIT; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sockets[i] < 0) {
            printf("   FAIL: Failed to create socket %d: %s\n", i + 1, strerror(errno));
            return 1;
        }
        if ((i + 1) % 100 == 0) {
            printf("   Created %d sockets...\n", i + 1);
        }
    }
    printf("   SUCCESS: Created all %d sockets\n\n", DEFAULT_LIMIT);
    
    // 测试2: 尝试创建第1025个socket，应该失败
    printf("2. Creating socket %d (should fail):\n", DEFAULT_LIMIT + 1);
    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result >= 0) {
        printf("   FAIL: Socket creation should have failed but succeeded (fd=%d)\n", result);
        close(result);
        goto cleanup;
    } else {
        printf("   SUCCESS: Socket creation failed as expected (%s)\n\n", strerror(errno));
    }
    
    // 测试3: 释放一个socket，然后重新创建，应该成功
    printf("3. Releasing one socket and creating a new one (should succeed):\n");
    close(sockets[0]);
    printf("   Released socket 1\n");
    
    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result < 0) {
        printf("   FAIL: Failed to create socket after releasing one: %s\n", strerror(errno));
        goto cleanup;
    }
    printf("   SUCCESS: Created new socket (fd=%d) after releasing one\n\n", result);
    sockets[0] = result;
    
    printf("=== All tests passed! ===\n");
    
cleanup:
    // 清理所有socket
    printf("Cleaning up sockets...\n");
    for (i = 0; i < DEFAULT_LIMIT; i++) {
        if (sockets[i] >= 0) {
            close(sockets[i]);
        }
    }
    
    return 0;
} 