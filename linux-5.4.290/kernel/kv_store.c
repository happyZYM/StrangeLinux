#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>

SYSCALL_DEFINE2(write_kv, int, k, int, v) {
    return sizeof(int);
}

SYSCALL_DEFINE1(read_kv, int, k) {
    return 0;
}

int kv_store_initialize(struct task_struct *task) {
    int i;
    struct kv_store_struct *kv_store = kmalloc(sizeof(struct kv_store_struct), GFP_KERNEL);
    if (!kv_store) {
        return -ENOMEM;
    }
    for (i = 0; i < 1024; i++) {
        INIT_HLIST_HEAD(&kv_store->kv_store[i]);
        spin_lock_init(&kv_store->kv_store_lock[i]);
    }
    task->kv_store_ptr = kv_store;
    return 0;
}

void kv_store_release(struct task_struct *task) {
    pr_debug("not implemented");
    int i;
    struct kv_store_node *full_node_ptr;
    struct hlist_node *list_ptr;
    struct kv_store_struct *kv_store = task->kv_store_ptr;
    for (i = 0; i < 1024; i++) {
        spin_lock(&kv_store->kv_store_lock[i]);
        hlist_for_each_entry_safe(full_node_ptr, list_ptr, &kv_store->kv_store[i], node) {
            hlist_del(&full_node_ptr->node);
            kfree(full_node_ptr);
        }
        spin_unlock(&kv_store->kv_store_lock[i]);
    }
    kfree(task->kv_store_ptr);
    task->kv_store_ptr = NULL;
    return;
}

EXPORT_SYMBOL(kv_store_initialize);
EXPORT_SYMBOL(kv_store_release);