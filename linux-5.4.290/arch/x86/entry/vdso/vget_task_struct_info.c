#include <linux/kernel.h>
// #include <asm/processor.h>
#include <linux/types.h>
#include <linux/sched.h>
// #define CS_BASES 2
#include <vdso/datapage.h>
#include <asm/vvar.h>


// vvar 区域大小，通常为 4 * PAGE_SIZE
#define VVAR_SIZE   (4 * PAGE_SIZE)
#define VTASK_SIZE  (ALIGN(sizeof(struct task_struct), PAGE_SIZE) + PAGE_SIZE)

extern char vvar_page;

int __vdso_get_task_struct_info(struct task_info *info)
{
    if (!info)
        return -1;
    
    // vtask 区域在 vvar 区域前面，紧邻
    struct task_info_view *view;
    view = (struct task_info_view *)(&vvar_page - VTASK_SIZE);
    
    // 从视图中读取信息
    info->pid = view->pid;
    
    // 返回用户空间能访问的 task_struct 映射地址
    info->kaddr = (struct task_struct *)((char *)view + PAGE_SIZE);
    
    return 0;
}

// 这是用户空间调用的包装函数
int get_task_struct_info(struct task_info *info)
{
    return __vdso_get_task_struct_info(info);
}
