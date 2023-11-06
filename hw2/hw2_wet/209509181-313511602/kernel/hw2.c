#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>

asmlinkage long sys_hello(void)
{
    printk("Hello, World!\n");
    return 0;
}

asmlinkage long sys_set_weight(int weight)
{
    if (weight < 0) {
        return -EINVAL;
    }
    current->weight = weight;
    return 0;
}

asmlinkage long sys_get_weight(void)
{
    return current->weight;
}

static void get_child_leaf_sum(struct task_struct* root, long* sum)
{
    struct task_struct* child;
    struct list_head* children_list;
    // Check if we in a leaf
    if (list_empty(&root->children)) {
        *sum += root->weight;
        return;
    }
    list_for_each(children_list, &root->children)
    {
        child = list_entry(children_list, struct task_struct, sibling);
        get_child_leaf_sum(child, sum);
    }
}

asmlinkage long sys_get_leaf_children_sum(void)
{
    long sum = 0;
    if (list_empty(&current->children)) {
        return -ECHILD;
    }
    get_child_leaf_sum(current, &sum);
    return sum;
}

static long heaviest_ancestor_weight(struct task_struct* current_task)
{
    pid_t max_pid = 1;
    long max_weight = -1;
    // If equal to 1 we got to the first process on the system: Init process
    while (current_task->pid != 1) {
        if (current_task->weight > max_weight) {
            max_weight = current_task->weight;
            max_pid = current_task->pid;
        }
        current_task = current_task->real_parent;
    }
    return max_pid;
}

asmlinkage long sys_get_heaviest_ancestor(void)
{
    return heaviest_ancestor_weight(current);
}
