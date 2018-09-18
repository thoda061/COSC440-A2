/**
 * File: asgn2.c
 * Date: 13/03/2011
 * Author: Nick Sparrow (4742998) 
 * Version: 0.1
 *
 * This is a module which serves as a virtual ramdisk which disk size is
 * limited by the amount of memory available and serves as the requirement for
 * COSC440 assignment 1 in 2012.
 *
 * Note: multiple devices and concurrent modules are not supported in this
 *       version.
 */
 
/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/circ_buf.h>
#include "gpio.c"

#define MYDEV_NAME "asgn2"
#define MYIOC_TYPE 'k'
#define BUFF_SIZE 1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick Sparrow");
MODULE_DESCRIPTION("COSC440 asgn2");


/**
 * The node structure for the memory page linked list.
 */ 
typedef struct page_node_rec {
  struct list_head list;
  struct page *page;
} page_node;

typedef struct asgn2_dev_t {
  dev_t dev;            /* the device */
  struct cdev *cdev;
  struct list_head mem_list; 
  int num_pages;        /* number of memory pages this module currently holds */
  size_t data_size;     /* total data size in this module */
  atomic_t nprocs;      /* number of processes accessing this device */ 
  atomic_t max_nprocs;  /* max number of processes accessing this device */
  struct kmem_cache *cache;      /* cache memory */
  struct class *class;     /* the udev class */
  struct device *device;   /* the udev device node */
} asgn2_dev;

typedef struct c_buff_def {
  u8 buffer[BUFF_SIZE];
  int head;
  int tail;
} c_buff_t;

typedef struct multi_page_q_def {
  int head;
  int tail;
  size_t head_offset;
  size_t tail_offset;
} multi_page_q_t;

asgn2_dev asgn2_device;
c_buff_t c_buff;
multi_page_q_t page_q;

void producer_fun(unsigned long t_arg);
static DECLARE_TASKLET(producer, producer_fun, 0);
static DECLARE_WAIT_QUEUE_HEAD(pq);
static DECLARE_WAIT_QUEUE_HEAD(consumer_q);

static atomic_t data_ready;
static atomic_t terminator;
int null_position = -1;

int asgn2_major = 0;                      /* major number of module */  
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */

// Proc entry.
struct proc_dir_entry *asgn2_proc_entry;

/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(void) {
  page_node *curr;
  page_node *tmp;
   
  list_for_each_entry_safe(curr, tmp, &asgn2_device.mem_list, list) {
    if ((curr->page) != NULL) __free_page(curr->page);
    list_del(&(curr->list));
    kfree(curr);
  }
  asgn2_device.data_size = 0;
  asgn2_device.num_pages = 0;
  printk(KERN_INFO "free_memory_pages completed OK.");
}


/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
int asgn2_open(struct inode *inode, struct file *filp) {
  /* COMPLETE ME */
  /**
   * Increment process count, if exceeds max_nprocs, return -EBUSY
   *
   * if opened in write-only mode, free all memory pages
   *
   */

  if(atomic_read(&asgn2_device.nprocs) >= atomic_read(&asgn2_device.max_nprocs))
    wait_event_interruptible(pq, atomic_read(&asgn2_device.nprocs) == 0);

  atomic_inc(&asgn2_device.nprocs);
  if((filp->f_flags & O_ACCMODE) == O_RDONLY) {
    return -EACCES;
  }
  
  printk(KERN_INFO "asgn2_open: completed OK.");
  return 0; /* success */
}


/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn2_release (struct inode *inode, struct file *filp) {
  /* COMPLETE ME */
  /**
   * decrement process count
   */
  atomic_set(&terminator, 0);
  atomic_dec(&asgn2_device.nprocs);
  wake_up_interruptible(&pq);
  return 0;
}

irqreturn_t dummyport_interrupt(int irq, void *dev_id) {
  static u8 byte;
  static int flag = 1;

  if (flag == 1) {
    byte = read_half_byte() << 4;
    flag = 0;
  } else {
    byte = byte | read_half_byte();
    flag = 1;

    if(CIRC_SPACE(c_buff.tail, c_buff.head, BUFF_SIZE) > 0) {
      c_buff.buffer[c_buff.tail] = byte;
      c_buff.tail = (c_buff.tail + 1) % BUFF_SIZE;
      tasklet_schedule(&producer);
    } else if ((char)byte == '\0') {
      c_buff.buffer[c_buff.tail] = byte;
    }   
   
  }
  return IRQ_HANDLED;
}

/**
 * This function writes from the c_buffer to the virtual disk of this 
 * module
 */
/*
ssize_t asgn2_write(struct file *filp, const char __user *buf, size_t count,
		    loff_t *f_pos) {
  size_t orig_f_pos = *f_pos;  // the original file position *
  size_t size_written = 0;  // size written to virtual disk in this function *
  size_t begin_offset;      // the offset from the beginning of a page to
			       start writing *
  int begin_page_no = *f_pos / PAGE_SIZE;  // the first page this finction
					      should start writing to *
  int curr_page_no = 0;     // the current page number *
  size_t curr_size_written; // size written to virtual disk in this round *
  size_t size_to_be_written;  // size to be read in the current round in 
				 while loop *
  
  page_node *curr;
  * COMPLETE ME *
  **
   * Traverse the list until the first page reached, and add nodes if necessary
   *
   * Then write the data page by page, remember to handle the situation
   *   when copy_from_user() writes less than the amount you requested.
   *   a while loop / do-while loop is recommended to handle this situation. 
   *
 
  * Allocates as many pages needed to store count bytes*
  while (asgn2_device.num_pages * PAGE_SIZE < orig_f_pos + count) {
    curr = kmalloc(sizeof(page_node), GFP_KERNEL);
    if(curr) {
      curr->page = alloc_page(GFP_KERNEL);
    } else { 
      printk(KERN_WARNING "Page_node allocation failure\n");
      return -ENOMEM;
    }
    if (NULL == curr->page) {
      printk(KERN_WARNING "Page allocation failure\n");
      return -ENOMEM;
    }
    list_add_tail(&(curr->list), &asgn2_device.mem_list);
    asgn2_device.num_pages++;
  }
  printk(KERN_INFO "asgn2_write: pages allocated %d", asgn2_device.num_pages);
  printk(KERN_INFO "asgn2_write: page allocation completed OK");
  list_for_each_entry(curr, &asgn2_device.mem_list, list) {
    if (begin_page_no <= curr_page_no) {  
      begin_offset = *f_pos % PAGE_SIZE;
      size_to_be_written = (count > PAGE_SIZE - begin_offset) ? PAGE_SIZE - begin_offset : count;
      printk(KERN_INFO "asgn2_write: size_to_be_written=%d", size_to_be_written);
      do {
        curr_size_written = size_to_be_written -
	  copy_from_user(page_address(curr->page) + begin_offset, buf + size_written, size_to_be_written);
	size_to_be_written -= curr_size_written;
        size_written += curr_size_written;
	count -= curr_size_written;
	*f_pos += curr_size_written;
	begin_offset = *f_pos % PAGE_SIZE;
	printk(KERN_INFO "asgn2_write: size_written=%d", size_written);
      } while (size_to_be_written > 0);
    } 
    curr_page_no++;
  }
  asgn2_device.data_size = max(asgn2_device.data_size, orig_f_pos + size_written);
  printk(KERN_INFO "asgn2_write: completed OK.");
  return size_written;
}*/

void producer_fun(unsigned long t_arg) {
  
  size_t size_written = 0;  // size written to virtual disk in this function 
  size_t begin_offset = 0;      // the offset from the beginning of a page to start writing 
  int begin_page_no = page_q.tail;  // the first page this function should start writing to 

  int curr_page_no = 0;     // the current page number
  size_t curr_page_offset = page_q.tail_offset;
  size_t size_to_be_written;  // size to be read in the current round in while loop
  int count = CIRC_CNT(c_buff.tail, c_buff.head, BUFF_SIZE);
  
  page_node *curr;

  //printk(KERN_WARNING "count: %d", count);

  /* Allocates as many pages needed to store count bytes*/
  while (asgn2_device.num_pages * PAGE_SIZE < asgn2_device.data_size + count) {
    curr = kmalloc(sizeof(page_node), GFP_KERNEL);
    if(curr) {
      curr->page = alloc_page(GFP_KERNEL);
    } else { 
      printk(KERN_WARNING "Page_node allocation failure\n");
      return;
    }
    if (NULL == curr->page) {
      printk(KERN_WARNING "Page allocation failure\n");
      return;
    }
    list_add_tail(&(curr->list), &asgn2_device.mem_list);
    asgn2_device.num_pages++;
  }
  //printk(KERN_INFO "producer_fun: pages allocated %d", asgn2_device.num_pages);
  //printk(KERN_INFO "producer_fun: page allocation completed OK");
  
  list_for_each_entry(curr, &asgn2_device.mem_list, list) {
    if (begin_page_no <= curr_page_no && count > 0) {  
      begin_offset = curr_page_offset % PAGE_SIZE;
      size_to_be_written = (count > PAGE_SIZE - begin_offset) ? PAGE_SIZE - begin_offset : count;
      
      //printk(KERN_INFO "producer_fun: size_to_be_written=%d", size_to_be_written);

      memcpy(page_address(curr->page) + begin_offset, c_buff.buffer + c_buff.head, size_to_be_written);
      c_buff.head = (c_buff.head + size_to_be_written) % BUFF_SIZE;
      
      size_written += size_to_be_written;
      count -= size_to_be_written;
      curr_page_offset += size_to_be_written;
      size_to_be_written = 0;
      begin_offset = curr_page_offset % PAGE_SIZE;
      
      //printk(KERN_INFO "producer_fun: size_written=%d", size_written);
    } 
    curr_page_no++;
  }
  //printk(KERN_INFO "producer_fun: total size_written=%d", size_written);
  asgn2_device.data_size += size_written;

  if (begin_offset == 0) page_q.tail++;

  page_q.tail_offset = begin_offset;

  /*wake up for read*/
  atomic_set(&data_ready, 0);
  wake_up_interruptible(&consumer_q);
}

/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count,
		   loff_t *f_pos) {
  
  size_t size_read = 0;     /* size read from virtual disk in this function */
  size_t begin_offset = 0;      /* the offset from the beginning of a page to
			       start reading */ 
  int begin_page_no = page_q.head; /* the first page which contains
					     the requested data */
  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_read;    /* size read from the virtual disk in this round */
  size_t size_to_be_read;   /* size to be read in the current round in 
			       while loop */
  int pages_freed = 0;
  page_node *curr;
  
  printk(KERN_INFO "asgn2_read: starting:");

  /*If head_offset is null_postion, increment head and return 0*/
  if((int)page_q.head_offset == null_position) {
    null_position = -1;
    page_q.head_offset++;
    return 0;
  }

  /*If statment true, page_q is empty*/
  if(page_q.tail == page_q.head && page_q.tail_offset == page_q.head_offset)
    atomic_set(&data_ready, 1);

  /*read goes to sleep until data is ready*/
  if(atomic_read(&data_ready) == 1)
    wait_event_interruptible(consumer_q, atomic_read(&data_ready) == 0);

  if(atomic_read(&terminator) == 1) return 0;

  count = min(count, asgn2_device.data_size - page_q.head_offset);
  /* Loops through page list and reads correct amount from each page*/
  list_for_each_entry(curr, &asgn2_device.mem_list, list) {
    
    if(begin_page_no <= curr_page_no) {

      size_t min_size;
      size_t i;
  
      begin_offset = page_q.head_offset;
      min_size = (count > PAGE_SIZE - begin_offset) ? PAGE_SIZE - begin_offset : count;
      size_to_be_read = min_size;

      /*Searching for null terminator and setting terminator flag if found*/
      for(i = 0; i < min_size; i++) {
	if(*((u8*)(page_address(curr->page) + begin_offset + i)) == '\0') {
	  int temp = PAGE_SIZE - (i + begin_offset);
	  size_to_be_read = ((PAGE_SIZE - begin_offset) - temp);
	  atomic_set(&terminator, 1);
	  break;
	}
      }
      printk(KERN_INFO "asgn2_read: size_to_be_read= %d", size_to_be_read); /*for debugging*/
      while(size_to_be_read > 0){
	curr_size_read = size_to_be_read -
	  copy_to_user(buf + size_read, page_address(curr->page) + begin_offset, size_to_be_read); 

	//printk(KERN_INFO "asgn2_read: curr_size_read= %d", curr_size_read); /*for debugging*/
	
	size_to_be_read -= curr_size_read;
	size_read += curr_size_read;
	count -= curr_size_read;

	page_q.head_offset = (page_q.head_offset + curr_size_read) % PAGE_SIZE;
        begin_offset = page_q.head_offset;
      }
    }

    /*free previous page if at the start of a page*/
    if(begin_offset == 0) {
      page_q.head++;
      __free_page(curr->page);
      list_del(&curr->list);
      kfree(curr);
      pages_freed++;
      curr_page_no++;
      asgn2_device.num_pages--;
    }
    
    printk(KERN_INFO "asgn2_read: curr_page_no=%d", curr_page_no);

    if(atomic_read(&terminator) == 1) break;
  }

  page_q.head -= pages_freed;
  page_q.tail -= pages_freed;

  if(atomic_read(&terminator) == 1)
    null_position = page_q.head_offset;

  asgn2_device.data_size -= pages_freed * PAGE_SIZE;
  
  printk(KERN_INFO "asgn2_read: completed OK.");
  return size_read;
}



#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

/**
 * The ioctl function, which nothing needs to be done in this case.
 */
long asgn2_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {
  int nr;
  int new_nprocs;
  int result;

  /* COMPLETE ME */
  /** 
   * check whether cmd is for our device, if not for us, return -EINVAL 
   *
   * get command, and if command is SET_NPROC_OP, then get the data, and
   set max_nprocs accordingly, don't forget to check validity of the 
   value before setting max_nprocs
  */
  if(_IOC_TYPE(cmd) != MYIOC_TYPE) return -EINVAL;
  nr = _IOC_NR(cmd);
  if(nr == SET_NPROC_OP) {
    result = __get_user(new_nprocs, (int *)arg); /*retreives the data from the user space*/
    if(result != 0) {
      printk(KERN_WARNING "__get_user unallowed access");
      return -EFAULT;
    }
    if(new_nprocs <= 0) {
      printk(KERN_WARNING "new_nprocs value needs to be greater than 0");
      return -EINVAL;
    }
    atomic_set(&asgn2_device.max_nprocs, new_nprocs); 
    return 0;
  }
  printk(KERN_INFO "asgn2_ioctl: completed OK.");
  return -ENOTTY;
}

struct file_operations asgn2_fops = {
  .owner = THIS_MODULE,
  .read = asgn2_read,
  .unlocked_ioctl = asgn2_ioctl,
  .open = asgn2_open,
  .release = asgn2_release,
};

static void *my_seq_start(struct seq_file *s, loff_t *pos)
{
  if(*pos >= 1) return NULL;
  else return &asgn2_dev_count + *pos;
}
static void *my_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
  (*pos)++;
  if(*pos >= 1) return NULL;
  else return &asgn2_dev_count + *pos;
}
static void my_seq_stop(struct seq_file *s, void *v)
{
  /* There's nothing to do here! */
}

int my_seq_show(struct seq_file *s, void *v) {
  /* COMPLETE ME */
  /**
   * use seq_printf to print some info to s 
   */
  seq_printf(s, "Pages: %d\nMemory: %d\nnProcs: %d\n", asgn2_device.num_pages, 
	     asgn2_device.data_size, atomic_read(&asgn2_device.nprocs));
  return 0;
}

static struct seq_operations my_seq_ops = {
  .start = my_seq_start,
  .next = my_seq_next,
  .stop = my_seq_stop,
  .show = my_seq_show
};

static int my_proc_open(struct inode *inode, struct file *filp)
{
  return seq_open(filp, &my_seq_ops);
}

struct file_operations asgn2_proc_ops = {
  .owner = THIS_MODULE,
  .open = my_proc_open,
  .llseek = seq_lseek,
  .read = seq_read,
  .release = seq_release,
};

/**
 * Initialise the module and create the master device
 */
int __init asgn2_init_module(void){
  int result; 

  /* COMPLETE ME */
  /**
   * set nprocs and max_nprocs of the device
   *
   * allocate major number
   * allocate cdev, and set ops and owner field 
   * add cdev
   * initialize the page list
   * create proc entries
   */

  printk(KERN_WARNING "_init: starting:");
  // Setting nprocs and max_nprocs of device.
  atomic_set(&asgn2_device.nprocs, 0);
  atomic_set(&asgn2_device.max_nprocs, 1);

  // Setting num_pages and data_size to initial states.
  asgn2_device.num_pages = 0;
  asgn2_device.data_size = 0;
  
  // Allocating major number.
  asgn2_device.dev = MKDEV(asgn2_major, asgn2_minor);
  result = alloc_chrdev_region(&asgn2_device.dev, asgn2_minor, asgn2_dev_count, MYDEV_NAME);
  if (result != 0) {
    printk(KERN_WARNING "asgn2: can't get major %d\n",result);
    goto fail_device_4;
  }
  printk(KERN_INFO "_init: major completed OK.");

  // Allocate cdev, and set ops and owner field then adding cdev.
  asgn2_device.cdev = cdev_alloc();
  cdev_init(asgn2_device.cdev, &asgn2_fops);
  asgn2_device.cdev->owner = THIS_MODULE;
  result = cdev_add(asgn2_device.cdev, asgn2_device.dev, asgn2_dev_count);
  if (result) {
    printk(KERN_WARNING "Error %d adding device asgn1", result);
    goto fail_device_3;
  }
  printk(KERN_INFO "_init: cdev_add completed OK.");

  // Initialize page list.
  INIT_LIST_HEAD(&asgn2_device.mem_list);
  printk(KERN_INFO "_init: pagelist init completed OK.");
  
  // Create proc entries.
  asgn2_proc_entry = proc_create(MYDEV_NAME, 0, NULL, &asgn2_proc_ops);   
  if(!asgn2_proc_entry) {
    printk(KERN_WARNING "/proc/%s faild to initialise", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_device_2;
  }
  printk(KERN_INFO "_init: proc_create completed OK.");

  // Init gpio
  gpio_dummy_init();

  c_buff.head = 0;
  c_buff.tail = 0;

  page_q.head = 0;
  page_q.head_offset = 0;
  page_q.tail = 0;
  page_q.tail_offset = 0;

  atomic_set(&terminator, 0);
  atomic_set(&data_ready, 1);
  
  asgn2_device.class = class_create(THIS_MODULE, MYDEV_NAME);
  if (IS_ERR(asgn2_device.class)) {
  }

  asgn2_device.device = device_create(asgn2_device.class, NULL, 
                                      asgn2_device.dev, "%s", MYDEV_NAME);
  if (IS_ERR(asgn2_device.device)) {
    printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_device_1;
  }
  
  printk(KERN_INFO "_init: set up udev entry completed OK\n");
  printk(KERN_WARNING "Hello world from %s\n", MYDEV_NAME);
  return 0;

  /* cleanup code called when any of the initialization steps fail */
 fail_device_1:
  printk(KERN_WARNING "_init: starting fail_device_1");
  class_destroy(asgn2_device.class);
  printk(KERN_INFO "_init: class_destroy completed OK");
  goto fail_device_2;
 fail_device_2:
  printk(KERN_WARNING "_init: starting fail_device_2");
  if(asgn2_proc_entry) {
    remove_proc_entry(MYDEV_NAME, NULL);
    printk(KERN_INFO "_init: remove_proc_entry completed OK.");
  }
  goto fail_device_3;
 fail_device_3:
  printk(KERN_WARNING "_init: starting fail_device_3");
  cdev_del(asgn2_device.cdev);
  printk(KERN_INFO "_init: cdev_del completed OK.");
  goto fail_device_4;
 fail_device_4:
  printk(KERN_WARNING "_init: starting fail_device_4");
  unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
  printk(KERN_INFO "_init: unregister_chrdev completed OK.");
  printk(KERN_WARNING "_init: finished fail_device");
  return result;  
}

/**
 * Finalise the module
 */
void __exit asgn2_exit_module(void){
  printk(KERN_INFO "_exit: starting:");
  device_destroy(asgn2_device.class, asgn2_device.dev);
  class_destroy(asgn2_device.class);
  printk(KERN_INFO "_exit: clean up udev entry completed OK\n");
  
  /* COMPLETE ME */
  /**
   * free all pages in the page list 
   * cleanup in reverse order
   */
  free_memory_pages();
  if(asgn2_proc_entry) {
    remove_proc_entry(MYDEV_NAME, NULL);
    printk(KERN_INFO "_exit: remove_proc_entry completed OK.");
  }
  gpio_dummy_exit();
  cdev_del(asgn2_device.cdev);
  printk(KERN_INFO "_exit: cdev_del completed OK.");
  unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
  printk(KERN_INFO "_exit: unregister_chrdev completed OK.");
  printk(KERN_WARNING "Good bye from %s\n", MYDEV_NAME);
}

module_init(asgn2_init_module);
module_exit(asgn2_exit_module);
