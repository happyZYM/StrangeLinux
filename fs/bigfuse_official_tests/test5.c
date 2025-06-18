// Test 5: Large file read/write consistency test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TESTFILE "/tmp/ramfs/large_test.dat"
#define SIZE (1024*1024*100) // 100 MB

int main() {
    char *write_buf = malloc(SIZE);
    char *read_buf = malloc(SIZE);

    if (!write_buf || !read_buf) {
        perror("malloc failed");
        exit(1);
    }

    memset(write_buf, 'A', SIZE);

    FILE *fp = fopen(TESTFILE, "w");
    if(!fp) {
        perror("fopen write failed");
        exit(1);
    }
    fwrite(write_buf, 1, SIZE, fp);
    fclose(fp);
    printf("Write 100MB completed.\n");

    fp = fopen(TESTFILE, "r");
    if(!fp) {
        perror("fopen read failed");
        exit(1);
    }
    fread(read_buf, 1, SIZE, fp);
    fclose(fp);
    printf("Read 100MB completed.\n");

    if(memcmp(write_buf, read_buf, SIZE) == 0) {
        printf("Large file read/write consistency PASSED.\n");
    } else {
        printf("Large file read/write consistency FAILED.\n");
    }

    free(write_buf);
    free(read_buf);
    return 0;
}

