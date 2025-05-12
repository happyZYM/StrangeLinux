#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    const char *filepath = "test_file.txt";
    const char *attr_name = "user.comment";
    const char *attr_value = "This file is generated for xattr test";
    char buffer[1024];
    ssize_t size;
    int fd;
    
    // create a test file
    fd = open(filepath, O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        perror("unable to create file");
        return 1;
    }
    write(fd, "This file is generated for xattr test", strlen("This file is generated for xattr test"));
    close(fd);
    
    printf("test file created: %s\n", filepath);
    
    // 1. set xattr
    if (setxattr(filepath, attr_name, attr_value, strlen(attr_value), 0) == -1) {
        perror("failed to set xattr");
        return 1;
    }
    printf("xattr set successfully: %s = %s\n", attr_name, attr_value);
    
    // 2. get xattr
    size = getxattr(filepath, attr_name, buffer, sizeof(buffer));
    if (size == -1) {
        perror("failed to get xattr");
        return 1;
    }
    buffer[size] = '\0';
    printf("xattr get successfully: %s = %s\n", attr_name, buffer);
    
    // 3. list all xattrs
    size = listxattr(filepath, buffer, sizeof(buffer));
    if (size == -1) {
        perror("failed to list xattr");
        return 1;
    }
    
    printf("all xattrs of the file:\n");
    char *name = buffer;
    while (name < buffer + size) {
        char value[1024];
        ssize_t value_size = getxattr(filepath, name, value, sizeof(value));
        if (value_size != -1) {
            value[value_size] = '\0';
            printf("  %s = %s\n", name, value);
        }
        name += strlen(name) + 1;
    }
    
    // // 4. remove xattr
    // if (removexattr(filepath, attr_name) == -1) {
    //     perror("failed to remove xattr");
    //     return 1;
    // }
    // printf("xattr removed successfully: %s\n", attr_name);
    
    // // verify xattr has been removed
    // if (getxattr(filepath, attr_name, buffer, sizeof(buffer)) == -1) {
    //     printf("verification successful: xattr %s has been removed\n", attr_name);
    // }
    
    // // clean up
    // unlink(filepath);
    // printf("test file removed\n");
    
    return 0;
}
