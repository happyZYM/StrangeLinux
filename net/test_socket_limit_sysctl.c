#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define SYSCTL_PATH "/proc/sys/net/core/socket_limit_per_process"
#define NEW_LIMIT 100

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

int main() {
    int sockets[NEW_LIMIT + 1];
    int i, result, original_limit;
    
    printf("=== Socket Limit Sysctl Test ===\n");
    printf("Testing dynamic limit modification via sysctl\n\n");
    
    // 读取原始限制值
    original_limit = read_socket_limit();
    if (original_limit < 0) {
        return 1;
    }
    printf("Original socket limit: %d\n", original_limit);
    
    // 修改限制为100
    printf("1. Setting socket limit to %d:\n", NEW_LIMIT);
    if (write_socket_limit(NEW_LIMIT) < 0) {
        return 1;
    }
    
    // 验证修改是否成功
    result = read_socket_limit();
    if (result != NEW_LIMIT) {
        printf("   FAIL: Expected limit %d, but got %d\n", NEW_LIMIT, result);
        goto restore;
    }
    printf("   SUCCESS: Socket limit changed to %d\n\n", NEW_LIMIT);
    
    // 测试新的限制：创建100个socket，应该成功
    printf("2. Creating %d sockets (should succeed):\n", NEW_LIMIT);
    for (i = 0; i < NEW_LIMIT; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sockets[i] < 0) {
            printf("   FAIL: Failed to create socket %d: %s\n", i + 1, strerror(errno));
            goto cleanup;
        }
        if ((i + 1) % 20 == 0) {
            printf("   Created %d sockets...\n", i + 1);
        }
    }
    printf("   SUCCESS: Created all %d sockets\n\n", NEW_LIMIT);
    
    // 测试：尝试创建第101个socket，应该失败
    printf("3. Creating socket %d (should fail):\n", NEW_LIMIT + 1);
    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result >= 0) {
        printf("   FAIL: Socket creation should have failed but succeeded (fd=%d)\n", result);
        close(result);
        goto cleanup;
    } else {
        printf("   SUCCESS: Socket creation failed as expected (%s)\n\n", strerror(errno));
    }
    
    // 测试：释放一个socket，然后重新创建，应该成功
    printf("4. Releasing one socket and creating a new one (should succeed):\n");
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
    for (i = 0; i < NEW_LIMIT; i++) {
        if (sockets[i] >= 0) {
            close(sockets[i]);
        }
    }
    
restore:
    // 恢复原始限制值
    printf("Restoring original socket limit to %d...\n", original_limit);
    write_socket_limit(original_limit);
    
    return 0;
} 