// Test 1: High concurrency read/write stress test
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define THREAD_NUM  20
#define ITERATION_NUM 1000
#define TEST_FILE "/tmp/ramfs/concurrent_test.txt"

void *write_thread(void *arg) {
    FILE *fp;
    char buf[32];
    for(int i=0; i<ITERATION_NUM; ++i) {
        fp = fopen(TEST_FILE, "w");
        if(fp == NULL) {
            perror("fopen failed");
            continue;
        }
        sprintf(buf, "Thread write %d\n", i);
        fwrite(buf, 1, strlen(buf), fp);
        fclose(fp);
    }
    return NULL;
}

void *read_thread(void *arg) {
    char buf[64];
    FILE *fp;
    for(int i=0; i<ITERATION_NUM; ++i) {
        fp = fopen(TEST_FILE, "r");
        if(fp == NULL) {
            perror("fopen failed");
            continue;
        }
        fread(buf, 1, 63, fp);
        fclose(fp);
    }
    return NULL;
}

int main() {
    pthread_t tid[THREAD_NUM];
    for(int i=0; i<THREAD_NUM/2; ++i)
        pthread_create(&tid[i], NULL, write_thread, NULL);
    for(int i=THREAD_NUM/2; i<THREAD_NUM; ++i)
        pthread_create(&tid[i], NULL, read_thread, NULL);
    for(int i=0; i<THREAD_NUM; ++i)
        pthread_join(tid[i], NULL);
    printf("Concurrent read/write test complete.\n");
    return 0;
}

