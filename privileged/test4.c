#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <errno.h>

struct task_info {
    pid_t pid;
    void *task_struct_ptr;
};

int get_task_struct_info(struct task_info *info) {
    memset(info, 0, sizeof(struct task_info));
    
    void *handle = dlopen("linux-vdso.so.1", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Failed to open vDSO: %s\n", dlerror());
        return -1;
    }
    
    int (*vdso_get_task_info)(struct task_info *) = dlsym(handle, "get_task_struct_info");
    if (!vdso_get_task_info) {
        fprintf(stderr, "Failed to find get_task_struct_info: %s\n", dlerror());
        dlclose(handle);
        return -1;
    }
    
    int ret = vdso_get_task_info(info);
    
    dlclose(handle);
    return ret;
}

int validate_task_struct_ptr(void *ptr) {
    unsigned long addr = (unsigned long)ptr;
    
    if (addr >= 0x7F0000000000UL && addr < 0x800000000000UL) {
        printf("Info: task_struct accessible at user virtual address %p\n", ptr);
        return 1;
    } else if (addr >= 0xFFFF800000000000UL) {
        printf("Info: task_struct at kernel virtual address %p\n", ptr);
        return 1;
    } else {
        fprintf(stderr, "Warning: task_struct pointer %p is in unexpected address range\n", ptr);
        return 0;
    }
}

pid_t get_pid_from_proc_stat() {
    FILE *stat_file = fopen("/proc/self/stat", "r");
    if (stat_file == NULL) {
        fprintf(stderr, "Failed to open /proc/self/stat: %s\n", strerror(errno));
        return -1;
    }
    
    pid_t pid;
    if (fscanf(stat_file, "%d", &pid) != 1) {
        fprintf(stderr, "Failed to read PID from /proc/self/stat\n");
        fclose(stat_file);
        return -1;
    }
    
    fclose(stat_file);
    return pid;
}

int test_single_process(const char *process_name) {
    struct task_info info;
    
    if (get_task_struct_info(&info) != 0) {
        fprintf(stderr, "%s: Failed to get task struct info\n", process_name);
        return 1;
    }
    
    pid_t pid_vdso = info.pid;
    
    pid_t pid_proc = get_pid_from_proc_stat();
    if (pid_proc == -1) {
        fprintf(stderr, "%s: Failed to get PID from /proc/self/stat\n", process_name);
        return 1;
    }
    
    validate_task_struct_ptr(info.task_struct_ptr);
    
    if (pid_proc != pid_vdso) {
        fprintf(stderr, "%s: PID mismatch - /proc: %d, vDSO: %d, task_struct: %p\n", 
                process_name, pid_proc, pid_vdso, info.task_struct_ptr);
        return 1;
    } else {
        printf("%s: SUCCESS - PID: %d, task_struct: %p\n", 
               process_name, pid_proc, info.task_struct_ptr);
        return 0;
    }
}

int main() {
    printf("=== vDSO task_struct_info Test ===\n");
    
    printf("\n--- Testing Parent Process ---\n");
    if (test_single_process("Parent") != 0) {
        return 1;
    }
    
    printf("\n--- Testing Fork Stability ---\n");
    const int num_forks = 5;
    int failed_children = 0;
    
    for (int i = 0; i < num_forks; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Failed to fork child %d: %s\n", i, strerror(errno));
            return 1;
        } else if (pid == 0) {
            char child_name[32];
            snprintf(child_name, sizeof(child_name), "Child-%d", i);
            
            int result = test_single_process(child_name);
            exit(result);
        } else {
            int status;
            pid_t waited_pid = waitpid(pid, &status, 0);
            if (waited_pid == -1) {
                fprintf(stderr, "Failed to wait for child %d: %s\n", i, strerror(errno));
                failed_children++;
            } else if (WEXITSTATUS(status) != 0) {
                failed_children++;
            }
        }
    }
    
    printf("\n--- Final Parent Process Test ---\n");
    if (test_single_process("Parent-Final") != 0) {
        return 1;
    }
    
    printf("\n=== Test Results ===\n");
    if (failed_children == 0) {
        printf("SUCCESS: All tests passed (%d children + parent)\n", num_forks);
        return 0;
    } else {
        printf("FAILURE: %d out of %d children failed\n", failed_children, num_forks);
        return 1;
    }
}
