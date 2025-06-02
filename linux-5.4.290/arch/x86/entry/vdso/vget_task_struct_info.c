#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <vdso/datapage.h>
#include <asm/vvar.h>


#define VTASK_SIZE (ALIGN(sizeof(struct task_struct), PAGE_SIZE) + PAGE_SIZE)

extern char vvar_page;

int __vdso_get_task_struct_info(struct task_info *info)
{
    if (!info) return -1;
    struct task_info_view *view;
    view = (struct task_info_view *)(&vvar_page - VTASK_SIZE);
    info->pid = view->pid;
    info->task_struct_ptr = (struct task_struct *)((char *)view + PAGE_SIZE + view->task_struct_inpage_offset);
    return 0;
}

int get_task_struct_info(struct task_info *info)
    __attribute__((weak, alias("__vdso_get_task_struct_info")));