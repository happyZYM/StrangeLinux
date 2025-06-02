#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

// must run as root!!!
void get_physical_address(void *virtual_addr, const char* description) {
    unsigned long virt_page = (unsigned long)virtual_addr / sysconf(_SC_PAGESIZE);
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open pagemap");
        return;
    }
    
    unsigned long data;
    if (pread(fd, &data, sizeof(data), virt_page * sizeof(data)) != sizeof(data)) {
        perror("Failed to read from pagemap");
        close(fd);
        return;
    }
    
    close(fd);
    
    if (!(data & (1ULL << 63))) {
        printf("[%s] Page not present in memory\n", description);
        return;
    }
    
    unsigned long frame_num = data & ((1ULL << 55) - 1);
    unsigned long physical_addr = frame_num * sysconf(_SC_PAGESIZE) + 
                                 ((unsigned long)virtual_addr % sysconf(_SC_PAGESIZE));
    printf("[%s] Virtual: %p -> Physical: 0x%lx (Frame: %lu)\n", 
           description, virtual_addr, physical_addr, frame_num);
}

void create_memory_pressure() {
    printf("\n=== Creating memory pressure to force page reclamation ===\n");
    size_t pressure_size = 64 * 1024 * 1024;
    void *pressure_mem = mmap(NULL, pressure_size, 
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (pressure_mem != MAP_FAILED) {
        for (size_t i = 0; i < pressure_size; i += sysconf(_SC_PAGESIZE)) {
            *((char*)pressure_mem + i) = 1;
        }
        printf("Allocated and touched %zu MB of memory\n", pressure_size / 1024 / 1024);
        sleep(1);
        munmap(pressure_mem, pressure_size);
        printf("Released pressure memory\n");
    }
}

int main() {
    size_t page_size = sysconf(_SC_PAGESIZE);
    printf("=== Memory Mapping Physical Address Test ===\n");
    printf("System page size: %zu bytes\n", page_size);
    printf("Process PID: %d\n", getpid());
    
    if (access("/proc/self/pagemap", R_OK) != 0) {
        printf("WARNING: Cannot read /proc/self/pagemap. Run with sudo for physical addresses.\n");
    }
    
    printf("\n--- Step 1: Initial mapping ---\n");
    void *addr1 = mmap(
        NULL,
        page_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0
    );
    
    if (addr1 == MAP_FAILED) {
        perror("mmap first time failed");
        exit(1);
    }
    
    printf("First mapping at virtual address: %p\n", addr1);
    
    strcpy((char *)addr1, "Hello, first mapping!");
    printf("Content written: %s\n", (char *)addr1);
    
    get_physical_address(addr1, "FIRST_MAP");
    
    printf("\n--- Step 2: Unmapping first region ---\n");
    if (munmap(addr1, page_size) == -1) {
        perror("munmap failed");
        exit(1);
    }
    printf("First mapping unmapped\n");
    
    // create_memory_pressure();
    
    printf("\n--- Step 3: Remapping at same virtual address ---\n");
    void *addr2 = mmap(
        addr1,
        page_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1, 0
    );
    
    if (addr2 == MAP_FAILED) {
        perror("mmap second time failed");
        exit(1);
    }
    
    printf("Second mapping at virtual address: %p\n", addr2);
    
    if (addr1 == addr2) {
        printf("✓ Successfully remapped to the same virtual address\n");
    } else {
        printf("✗ Virtual addresses differ (unexpected)\n");
    }
    
    printf("Content after remapping (should be empty): '%s'\n", (char *)addr2);
    
    strcpy((char *)addr2, "Hello, second mapping!");
    printf("New content written: %s\n", (char *)addr2);
    
    get_physical_address(addr2, "SECOND_MAP");
    
    printf("\n--- Step 4: Comparison ---\n");
    printf("Both mappings used the same virtual address: %p\n", addr1);
    printf("If physical addresses differ, the test demonstrates virtual-to-physical remapping.\n");
    
    if (munmap(addr2, page_size) == -1) {
        perror("munmap failed");
        exit(1);
    }
    
    printf("\n=== Test completed successfully ===\n");
    return 0;
}