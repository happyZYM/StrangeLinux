#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

// 系统调用号，从syscall_64.tbl中获取
#define SYS_write_kv 436
#define SYS_read_kv  437

int main(int argc, char *argv[]) {
    printf("KV Store Test Program\n");
    
    // 测试写入键值对
    int key1 = 100;
    int value1 = 42;
    printf("Writing key=%d, value=%d\n", key1, value1);
    long result = syscall(SYS_write_kv, key1, value1);
    if (result < 0) {
        printf("Error writing key-value: %d\n", errno);
        return 1;
    }
    
    // 测试读取刚刚写入的键值对
    printf("Reading key=%d\n", key1);
    int read_value = syscall(SYS_read_kv, key1);
    printf("Read value: %d\n", read_value);
    if (read_value != value1) {
        printf("Error: Read value doesn't match written value!\n");
    } else {
        printf("Success: Read value matches written value.\n");
    }
    
    // 测试读取不存在的键
    int key2 = 200;
    printf("\nReading non-existent key=%d\n", key2);
    read_value = syscall(SYS_read_kv, key2);
    printf("Read value for non-existent key: %d\n", read_value);
    
    // 测试更新现有键的值
    int value1_updated = 99;
    printf("\nUpdating key=%d with value=%d\n", key1, value1_updated);
    result = syscall(SYS_write_kv, key1, value1_updated);
    if (result < 0) {
        printf("Error updating key-value: %d\n", errno);
        return 1;
    }
    
    // 测试读取更新后的值
    printf("Reading updated key=%d\n", key1);
    read_value = syscall(SYS_read_kv, key1);
    printf("Read updated value: %d\n", read_value);
    if (read_value != value1_updated) {
        printf("Error: Read value doesn't match updated value!\n");
    } else {
        printf("Success: Read value matches updated value.\n");
    }
    
    // 测试多个键值对
    printf("\nTesting multiple key-value pairs:\n");
    for (int i = 1; i <= 5; i++) {
        int key = 1000 + i;
        int value = i * 10;
        printf("Writing key=%d, value=%d\n", key, value);
        result = syscall(SYS_write_kv, key, value);
        if (result < 0) {
            printf("Error writing key-value: %d\n", errno);
            continue;
        }
    }
    
    printf("\nReading multiple key-value pairs:\n");
    for (int i = 1; i <= 5; i++) {
        int key = 1000 + i;
        int expected = i * 10;
        read_value = syscall(SYS_read_kv, key);
        printf("Key=%d, Value=%d, Expected=%d\n", key, read_value, expected);
    }
    
    printf("\nKV Store test completed.\n");
    return 0;
}