#define LINUX

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include "mp3_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zach");
MODULE_DESCRIPTION("CS-423 MP3");

#define FILENAME "status"
#define DIRECTORY "mp3"
#define REGISTRATION 'R'
#define DEREGISTRATION 'U'
#define NPAGES 128
#define DELAY 50
#define PROFILERLEN (NPAGES * PAGE_SIZE / sizeof(unsigned long))
#define CDEV "mp3_character_device"

#define DEBUG 1

struct mp3_task_struct{

  struct task_struct *linux_task;
  struct list_head task_node;
  unsigned int pid;

};

static struct proc_dir_entry *proc_dir, *proc_entry;
static spinlock_t mp3_lock;
LIST_HEAD(mp3_task_struct_list);
static void mp3_work_function(struct work_struct *work);
static struct workqueue_struct *mp3_workqueue;
static dev_t mp3_dev;
static struct cdev mp3_cdev;
unsigned long *profiler_buffer;
unsigned long delay;
int profiler_ptr = 0;
DECLARE_DELAYED_WORK(mp3_delayed_work, mp3_work_function);

struct mp3_task_struct *__get_task_by_pid(unsigned int pid){

  struct mp3_task_struct *temp;
  list_for_each_entry(temp, &mp3_task_struct_list, task_node){

    if(temp->pid == pid){

      return temp;

    }
  }

  return NULL;

}

void mp3_register(char *buf){

  unsigned long flags;
  struct mp3_task_struct *temp = (struct mp3_task_struct*)kmalloc(sizeof(struct mp3_task_struct), GFP_KERNEL);

  INIT_LIST_HEAD(&(temp->task_node));

  sscanf(strsep(&buf, ","), "%u", &temp->pid);
  temp->linux_task = find_task_by_pid(temp->pid);

  spin_lock_irqsave(&mp3_lock, flags);

  if(list_empty(&mp3_task_struct_list)){

    queue_delayed_work(mp3_workqueue, &mp3_delayed_work, delay);

  }

  list_add(&(temp->task_node), &mp3_task_struct_list);
  spin_unlock_irqrestore(&mp3_lock, flags);

}

void mp3_deregister(char *buf){

  unsigned int pid;
  struct mp3_task_struct *temp;
  unsigned long flags;

  sscanf(buf, "%u", &pid);

  spin_lock_irqsave(&mp3_lock, flags);
  temp = __get_task_by_pid(pid);
  list_del(&temp->task_node);
  spin_unlock_irqrestore(&mp3_lock, flags);

  if(list_empty(&mp3_task_struct_list)){

    cancel_delayed_work(&mp3_delayed_work);
    flush_workqueue(mp3_workqueue);

  }

  kfree(temp);

}

static void mp3_work_function(struct work_struct *work){

  unsigned long utime, stime, min_flt, maj_flt;
  unsigned long flags;
  struct mp3_task_struct *temp;
  unsigned long total_min_flt = 0, total_cpu_time = 0, total_maj_flt = 0;
  int tag = 0;

  spin_lock_irqsave(&mp3_lock, flags);
  list_for_each_entry(temp, &mp3_task_struct_list, task_node){

    if(get_cpu_use(temp->pid, &min_flt, &maj_flt, &utime, &stime) == -1){

      continue;
    
    }

    total_min_flt += min_flt;
    total_cpu_time += utime + stime;
    total_maj_flt += maj_flt;
    tag++;

  }
  spin_unlock_irqrestore(&mp3_lock, flags);

  profiler_buffer[profiler_ptr++] = jiffies;
  profiler_buffer[profiler_ptr++] = total_min_flt;
  profiler_buffer[profiler_ptr++] = total_maj_flt;
  profiler_buffer[profiler_ptr++] = jiffies_to_msecs(cputime_to_jiffies(total_cpu_time));

  if(profiler_ptr >= PROFILERLEN){

    profiler_ptr = 0;

  }

  queue_delayed_work(mp3_workqueue, &mp3_delayed_work, delay);

}

int mp3_cdev_mmap(struct file *fp, struct vm_area_struct *vma){

  int ret;
  unsigned long length = vma->vm_end - vma->vm_start;
  unsigned long start = vma->vm_start;

  struct page *page;
  char *profiler_buffer_ptr = (char *)profiler_buffer;

  if(length > NPAGES * PAGE_SIZE){
    return -EIO;
  }

  while(length > 0){

    page = vmalloc_to_page(profiler_buffer_ptr);

    if((ret =  vm_insert_page(vma, start, page)) < 0){

      return ret;

    }

    start += PAGE_SIZE;
    profiler_buffer_ptr += PAGE_SIZE;
    length -= PAGE_SIZE;

  }

  return 0;

}

static ssize_t mp3_read(struct file *file, char __user *buffer, size_t count, loff_t *data){

  ssize_t data_copied = 0;
  unsigned long flags;
  static int finished = 0;
  struct mp3_task_struct *temp;
  char *buf = (char *)kmalloc(count, GFP_KERNEL);

  if(finished){

    finished = 0;
    return 0;

  }

  else{

    finished = 1;

  }

  spin_lock_irqsave(&mp3_lock, flags);
  list_for_each_entry(temp, &mp3_task_struct_list, task_node){

    data_copied += sprintf(buf + data_copied, "%u\n", temp->pid);

  }
  spin_unlock_irqrestore(&mp3_lock, flags);

  copy_to_user(buffer, buf, data_copied);

  kfree(buf);

  return data_copied;

}

static ssize_t mp3_write(struct file *file, const char __user *buffer, size_t count, loff_t *data){

  char *buf = (char *)kmalloc(count + 1, GFP_KERNEL);
  copy_from_user(buf, buffer, count);
  buf[count] = '\0';

  switch(buf[0]){

    case REGISTRATION:
      mp3_register(buf + 2);
      break;

    case DEREGISTRATION:
      mp3_deregister(buf + 2);
      break;

    default:
      printk(KERN_INFO "Error!\n");

  }

  kfree(buf);

  return count;

}

static const struct file_operations mp3_fops = {

  .owner = THIS_MODULE,
  .read = mp3_read,
  .write = mp3_write

};

static const struct file_operations mp3_cdev_fops = {

  .owner = THIS_MODULE,
  .open = NULL,
  .release = NULL,
  .mmap = mp3_cdev_mmap

};


static int __init mp3_init(void){

  int ret = 0;
  #ifdef DEBUG
  printk(KERN_ALERT "MP3 MODULE LOADING\n");
  #endif

  if((proc_dir = proc_mkdir(DIRECTORY, NULL)) == NULL){

    ret = -ENOMEM;
    goto out;

  }

  if((proc_entry = proc_create(FILENAME, 0666, proc_dir, &mp3_fops)) == NULL){

     ret = -ENOMEM;
     goto out_proc_dir;

   }

  delay = msecs_to_jiffies(DELAY);

  spin_lock_init(&mp3_lock);

  if((mp3_workqueue = create_singlethread_workqueue("mp3_workqueue")) == NULL){

    ret = -ENOMEM;
    goto out_proc_entry;

  }

  if((ret = alloc_chrdev_region(&mp3_dev, 0, 1, CDEV)) < 0 ){

    goto out_workqueue;

  }

  cdev_init(&mp3_cdev, &mp3_cdev_fops);

  if((ret = cdev_add(&mp3_cdev, mp3_dev, 1))){

    goto out_chrdev_region;

  }

  if((profiler_buffer = (unsigned long*)vmalloc(NPAGES *PAGE_SIZE)) == NULL){

    ret = -ENOMEM;
    goto out_cdev;

  }

  profiler_buffer = (unsigned long *)vmalloc(NPAGES * PAGE_SIZE);

  return ret;


out_cdev:
  cdev_del(&mp3_cdev);

out_chrdev_region:
  unregister_chrdev_region(mp3_dev, 1);

out_workqueue:
  destroy_workqueue(mp3_workqueue);

out_proc_entry:
  remove_proc_entry(FILENAME, proc_dir);

out_proc_dir:
  remove_proc_entry(DIRECTORY, NULL);

out:
  return ret;

}

static void __exit mp3_exit(void){

  struct mp3_task_struct *position, *next;
  #ifdef DEBUG
  printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
  #endif

  remove_proc_entry(FILENAME, proc_dir);
  remove_proc_entry(DIRECTORY, NULL);

  list_for_each_entry_safe(position, next, &mp3_task_struct_list, task_node){

    list_del(&position->task_node);
    kfree(position);

  }

  cancel_delayed_work(&mp3_delayed_work);
  flush_workqueue(mp3_workqueue);
  destroy_workqueue(mp3_workqueue);

  cdev_del(&mp3_cdev);
  unregister_chrdev_region(mp3_dev, 1);

  vfree(profiler_buffer);

  printk(KERN_ALERT "MP3 MODULE UNLOADED\n");

}

module_init(mp3_init);
module_exit(mp3_exit);

