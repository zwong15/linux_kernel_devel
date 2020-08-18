#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zach Wong");
MODULE_DESCRIPTION("CS-423 MP1");

#define FILENAME "status"
#define DIRECTORY "mp1"
#define DEBUG 1
#define TIME 5000

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;
static spinlock_t mp1_lock;
static struct timer_list mp1_timer;
static struct workqueue_struct *mp1_workqueue;
static struct work_struct *mp1_work;

typedef struct {
   struct list_head list;
   unsigned int pid;
   unsigned long cpu_time;
} proc_list;
   
LIST_HEAD(mp1_proc_list);

static ssize_t mp1_write(struct file *file, const char __user *buffer, size_t count, loff_t *data){
  proc_list *temp;
  unsigned long flags;
  char *buf;

  temp = (proc_list *)kmalloc(sizeof(proc_list), GFP_KERNEL);
  INIT_LIST_HEAD(&(temp->list));
  
  buf = (char *)kmalloc(count + 1, GFP_KERNEL);
  copy_from_user(buf, buffer, count);
  buf[count] = '\0';
  sscanf(buf, "%u", &temp->pid);

  temp->cpu_time = 0;

  spin_lock_irqsave(&mp1_lock, flags);
  list_add(&(temp->list), &mp1_proc_list);
  spin_unlock_irqrestore(&mp1_lock, flags);

  kfree(buf);

  return count;
}

static ssize_t mp1_read(struct file *file, char __user *buffer, size_t count, loff_t *data){
  unsigned long data_copied = 0;
  char *buf;
  proc_list *temp;
  unsigned long flags;
  
  buf = (char*)kmalloc(count, GFP_KERNEL);

  spin_lock_irqsave(&mp1_lock, flags);

  list_for_each_entry(temp, &mp1_proc_list, list){
    data_copied += sprintf(buf + data_copied, "%u: %u\n", temp->pid, jiffies_to_msecs(cputime_to_jiffies(temp->cpu_time)));
  }

  spin_unlock_irqrestore(&mp1_lock, flags);
  buf[data_copied] = '\0';
  copy_to_user(buffer, buf, data_copied);
  kfree(buf);

  return data_copied;
}

void mp1_timer_callback(unsigned long data){
  queue_work(mp1_workqueue, mp1_work);
}

static void mp1_work_func(struct work_struct *work){
  proc_list *position, *next;
  unsigned long flags;

  spin_lock_irqsave(&mp1_lock, flags);
  list_for_each_entry_safe(position, next, &mp1_proc_list, list){
    if(get_cpu_use(position->pid, &position->cpu_time) == -1){
      list_del(&position->list);
      kfree(position);
    }
  }
  
  spin_unlock_irqrestore(&mp1_lock, flags);
  mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(TIME));
}

static const struct file_operations mp1_file = {
  .owner = THIS_MODULE,
  .read = mp1_read,
  .write = mp1_write,
};

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif
   // Insert your code here ...
   printk("Creating\n");
   
   proc_dir = proc_mkdir(DIRECTORY, NULL);
   proc_entry = proc_create(FILENAME, 0666, proc_dir, &mp1_file);   
   spin_lock_init(&mp1_lock);
   setup_timer(&mp1_timer, &mp1_timer_callback, 0);
   mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(TIME));
   
   if((mp1_workqueue = create_workqueue("mp1_workqueue")) == NULL){
     printk(KERN_INFO "create_workqueue ERROR\n");
     return -ENOMEM;
   }

   mp1_work = (struct work_struct *)kmalloc(sizeof(struct work_struct), GFP_KERNEL);
   INIT_WORK(mp1_work, mp1_work_func);

   printk("Finished\n");
   
   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
  
   proc_list *position, *next;

   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif
   // Insert your code here ...
   printk("Removing\n");

   remove_proc_entry(FILENAME, proc_dir);
   remove_proc_entry(DIRECTORY, NULL);
   
   list_for_each_entry_safe(position, next, &mp1_proc_list, list){
     list_del(&position->list);
     kfree(position);
   }
   
   del_timer_sync(&mp1_timer);
   
   flush_workqueue(mp1_workqueue);
   destroy_workqueue(mp1_workqueue);

   kfree(mp1_work);

   printk("Finished\n");
   

   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
