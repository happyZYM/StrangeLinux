#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>

SYSCALL_DEFINE2(write_kv, int, k, int, v) {
    if (current->kv_store_ptr == NULL) {
        kv_store_creation(current);
    }
    
    struct kv_store_struct *kv_store = current->kv_store_ptr;
    if (!kv_store) {
        return -ENOMEM;
    }
    
    unsigned int hash = (unsigned int)k % 1024;
    struct kv_store_node *node_ptr;
    struct kv_store_node *new_node = NULL;
    int found = 0;
    
    spin_lock(&kv_store->kv_store_lock[hash]);
    
    // Try to find existing key
    hlist_for_each_entry(node_ptr, &kv_store->kv_store[hash], node) {
        if (node_ptr->key == k) {
            node_ptr->value = v;
            found = 1;
            break;
        }
    }
    
    // If key not found, create new node
    if (!found) {
        new_node = kmalloc(sizeof(struct kv_store_node), GFP_KERNEL);
        if (!new_node) {
            spin_unlock(&kv_store->kv_store_lock[hash]);
            return -ENOMEM;
        }
        
        new_node->key = k;
        new_node->value = v;
        INIT_HLIST_NODE(&new_node->node);
        hlist_add_head(&new_node->node, &kv_store->kv_store[hash]);
    }
    
    spin_unlock(&kv_store->kv_store_lock[hash]);
    return sizeof(int);
}

SYSCALL_DEFINE1(read_kv, int, k) {
    if(current->kv_store_ptr == NULL) {
        kv_store_creation(current);
    }
    
    struct kv_store_struct *kv_store = current->kv_store_ptr;
    if (!kv_store) {
        return 0;
    }
    
    unsigned int hash = (unsigned int)k % 1024;
    struct kv_store_node *node_ptr;
    int value = 0;
    
    spin_lock(&kv_store->kv_store_lock[hash]);
    
    // Search for the key
    hlist_for_each_entry(node_ptr, &kv_store->kv_store[hash], node) {
        if (node_ptr->key == k) {
            value = node_ptr->value;
            break;
        }
    }
    
    spin_unlock(&kv_store->kv_store_lock[hash]);
    return value;
}

int kv_store_creation(struct task_struct *task) {
    if(task->kv_store_ptr != NULL) {
        kv_store_dereference(task);
    }
    
    struct kv_store_struct *kv_store = kmalloc(sizeof(struct kv_store_struct), GFP_KERNEL);
    if (!kv_store) {
        return -ENOMEM;
    }
    
    int i;
    for (i = 0; i < 1024; i++) {
        INIT_HLIST_HEAD(&kv_store->kv_store[i]);
        spin_lock_init(&kv_store->kv_store_lock[i]);
    }
    
    atomic_set(&kv_store->refcount, 1);
    spin_lock_init(&kv_store->kv_store_global_lock);
    task->kv_store_ptr = kv_store;
    return 0;
}

void kv_store_dereference(struct task_struct *task) {
    if (task->kv_store_ptr == NULL) {
        return;
    }
    
    struct kv_store_struct *kv_store = task->kv_store_ptr;
    
    if (atomic_dec_and_test(&kv_store->refcount)) {
        int i;
        struct kv_store_node *full_node_ptr;
        struct hlist_node *list_ptr;
        
        for (i = 0; i < 1024; i++) {
            spin_lock(&kv_store->kv_store_lock[i]);
            hlist_for_each_entry_safe(full_node_ptr, list_ptr, &kv_store->kv_store[i], node) {
                hlist_del(&full_node_ptr->node);
                kfree(full_node_ptr);
            }
            spin_unlock(&kv_store->kv_store_lock[i]);
        }
        
        kfree(kv_store);
    }
    
    task->kv_store_ptr = NULL;
    return;
}

void kv_store_copy(struct task_struct *new_task, struct task_struct *old_task) {
    // If old_task has no kv_store, set new_task's kv_store to NULL and return
    if (old_task->kv_store_ptr == NULL) {
        new_task->kv_store_ptr = NULL;
        return;
    }
    
    // Allocate new kv_store
    struct kv_store_struct *old_kv_store = old_task->kv_store_ptr;
    struct kv_store_struct *new_kv_store = kmalloc(sizeof(struct kv_store_struct), GFP_KERNEL);
    if (!new_kv_store) {
        new_task->kv_store_ptr = NULL;
        return;
    }
    
    // Initialize the new kv_store
    int i;
    for (i = 0; i < 1024; i++) {
        INIT_HLIST_HEAD(&new_kv_store->kv_store[i]);
        spin_lock_init(&new_kv_store->kv_store_lock[i]);
    }
    
    atomic_set(&new_kv_store->refcount, 1);
    spin_lock_init(&new_kv_store->kv_store_global_lock);
    
    // Copy all key-value pairs
    struct kv_store_node *old_node_ptr;
    struct kv_store_node *new_node_ptr;
    
    for (i = 0; i < 1024; i++) {
        // Use trylock to avoid deadlocks during system initialization
        // if (!spin_trylock(&old_kv_store->kv_store_lock[i])) {
        //     // If we can't get the lock, just skip this bucket
        //     continue;
        // }
        spin_lock(&old_kv_store->kv_store_lock[i]);
        
        hlist_for_each_entry(old_node_ptr, &old_kv_store->kv_store[i], node) {
            new_node_ptr = kmalloc(sizeof(struct kv_store_node), GFP_ATOMIC);
            if (!new_node_ptr) {
                spin_unlock(&old_kv_store->kv_store_lock[i]);
                goto cleanup;
            }
            
            new_node_ptr->key = old_node_ptr->key;
            new_node_ptr->value = old_node_ptr->value;
            INIT_HLIST_NODE(&new_node_ptr->node);
            hlist_add_head(&new_node_ptr->node, &new_kv_store->kv_store[i]);
        }
        
        spin_unlock(&old_kv_store->kv_store_lock[i]);
    }
    
    // Set the new kv_store
    new_task->kv_store_ptr = new_kv_store;
    return;

cleanup:
    // Clean up the partially created kv_store
    for (i = 0; i < 1024; i++) {
        struct kv_store_node *node_ptr;
        struct hlist_node *tmp;
        
        hlist_for_each_entry_safe(node_ptr, tmp, &new_kv_store->kv_store[i], node) {
            hlist_del(&node_ptr->node);
            kfree(node_ptr);
        }
    }
    
    kfree(new_kv_store);
    new_task->kv_store_ptr = NULL;
}

void kv_store_reference(struct task_struct *new_task, struct task_struct *old_task) {
    if (old_task->kv_store_ptr == NULL) {
        new_task->kv_store_ptr = NULL;
        return;
    }
    
    struct kv_store_struct *kv_store = old_task->kv_store_ptr;
    atomic_inc(&kv_store->refcount);
    new_task->kv_store_ptr = kv_store;
}

EXPORT_SYMBOL(kv_store_creation);
EXPORT_SYMBOL(kv_store_dereference);
EXPORT_SYMBOL(kv_store_copy);
EXPORT_SYMBOL(kv_store_reference);