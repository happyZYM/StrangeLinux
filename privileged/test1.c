#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SYS_write_kv 436
#define SYS_read_kv  437

#define NUM_THREADS 100
#define NUM_ITERATIONS 1000
#define MAX_KEY 2048

pthread_mutex_t mutex;
int expected_values[MAX_KEY];

void* thread_function(void* arg) {
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        int k = rand() % MAX_KEY;
        int v = rand();
        int operation = rand() % 2; 

        if (operation) {
            if (syscall(SYS_write_kv, k, v) != -1) {
                pthread_mutex_lock(&mutex);
                expected_values[k] = v;
                pthread_mutex_unlock(&mutex);
            } else {
                printf("Error writing key %d\n", k);
            }
        } else {
            int read_value = syscall(SYS_read_kv, k);
            pthread_mutex_lock(&mutex);
            int expected_value = expected_values[k];
            pthread_mutex_unlock(&mutex);
            
            if (read_value != -1 && read_value != expected_value) {
                printf("Data inconsistency detected for key %d: expected %d, got %d\n", k, expected_value, read_value);
            }
        }
    }
    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    srand(time(NULL));

    pthread_mutex_init(&mutex, NULL);
    for (int i = 0; i < MAX_KEY; ++i) {
        expected_values[i] = -1; 
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        if (pthread_create(&threads[i], NULL, thread_function, NULL) != 0) {
            printf("Error creating thread %d\n", i);
            return -1;
        }
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&mutex);
    printf("Data consistency test completed.\n");
    return 0;
}