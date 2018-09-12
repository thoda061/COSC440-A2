/**
 * File: asgn2.c
 * Date: 13/03/2011
 * Author: Your Name 
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

#define MYDEV_NAME "asgn2"
#define MYIOC_TYPE 'k'

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
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

asgn2_dev asgn2_device;


int asgn2_major = 0;                      /* major number of module */  
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */


/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(void) {
  struct page_node_rec *curr;
  struct list_head *tmp;
  struct list_head *ptr;


  /* COMPLETE ME */
  /**
   * Loop through the entire page list {
   *   if (node has a page) {
   *     free the page
   *   }
   *   remove the node from the page list
   *   free the node
   * }
   * reset device data size, and num_pages
   */  

  list_for_each_safe(ptr, tmp, &asgn2_device.mem_list) {
	curr = list_entry(ptr, struct page_node_rec, list);
	if (curr->page != NULL) __free_pages(curr->page, 0);
	//printk(KERN_INFO "page address %x\n", page_address(curr->page));
	list_del(&curr->list);
	kmem_cache_free(asgn2_device.cache, curr);
  }
  asgn2_device.data_size = 0;
  asgn2_device.num_pages = 0;
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
  int init_nprocs = atomic_read(&asgn2_device.nprocs);
  atomic_set(&asgn2_device.nprocs, init_nprocs + 1);
  if(atomic_read(&asgn2_device.nprocs)>atomic_read(&asgn2_device.max_nprocs)) {
	  atomic_set(&asgn2_device.nprocs, init_nprocs);
	  return -EBUSY;
  }
  if(filp->f_mode == S_IWUSR) {
	  free_memory_pages();
  }

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
  atomic_set(&asgn2_device.nprocs, atomic_read(&asgn2_device.nprocs) - 1);
  return 0;
}


/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count,
		 loff_t *f_pos) {
  size_t size_read = 0;     /* size read from virtual disk in this function */
  size_t begin_offset;      /* the offset from the beginning of a page to
			       start reading */
  int begin_page_no = *f_pos / PAGE_SIZE; /* the first page which contains
					     the requested data */
  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_read;    /* size read from the virtual disk in this round */
  size_t size_to_be_read;   /* size to be read in the current round in 
			       while loop */

  struct list_head *ptr = asgn2_device.mem_list.next;
  page_node *curr;
  //char *tp;

  /* COMPLETE ME */
  /**
   * check f_pos, if beyond data_size, return 0
   * 
   * Traverse the list, once the first requested page is reached,
   *   - use copy_to_user to copy the data to the user-space buf page by page
   *   - you also need to work out the start / end offset within a page
   *   - Also needs to handle the situation where copy_to_user copy less
   *       data than requested, and
   *       copy_to_user should be called again to copy the rest of the
   *       unprocessed data, and the second and subsequent calls still
   *       need to check whether copy_to_user copies all data requested.
   *       This is best done by a while / do-while loop.
   *
   * if end of data area of ramdisk reached before copying the requested
   *   return the size copied to the user space so far
   */

  if(*f_pos > asgn2_device.data_size) return 0;
  list_for_each(ptr, &asgn2_device.mem_list) {
	  //printk(KERN_INFO "ptr addresss %x\n", ptr);
	  if(curr_page_no >= begin_page_no) {
		  curr = list_entry(ptr, struct page_node_rec, list);
		  size_to_be_read = min(count, (size_t)PAGE_SIZE);
		  curr_size_read = 0;
		 // printk(KERN_INFO "size_to_be_read %i\n", size_to_be_read);
		  do {
	          	begin_offset = curr_size_read;
		  	curr_size_read += (size_to_be_read -
		 	  	copy_to_user(buf+begin_offset+(int)size_read, 
					  page_address(curr->page)+begin_offset,
					  size_to_be_read));
		  	//tp = page_address(curr->page);
		  	//printk(KERN_INFO "page_addresss %c %c %c\n", tp[0], tp[1], tp[3]);
		  	//printk(KERN_INFO "current read %i\n", curr_size_read);
		  } while (curr_size_read < size_to_be_read);
		  size_read += curr_size_read;
		  count -= curr_size_read;
	  }
	  //printk(KERN_INFO "size read %i\n", size_read);
	  curr_page_no += 1;
	  //printk(KERN_INFO "curr_page_no %d\n", curr_page_no);
  }
  *f_pos += size_read;
  return size_read;
}

/**
 * This function writes from the user buffer to the virtual disk of this
 * module
 */
/*ssize_t asgn2_write(struct file *filp, const char __user *buf, size_t count,
		  loff_t *f_pos) {*/
  //size_t orig_f_pos = *f_pos;  /* the original file position */
  //size_t size_written = 0;  /* size written to virtual disk in this function */
  //size_t begin_offset;      /* the offset from the beginning of a page to
  //			       start writing */
  //int begin_page_no = *f_pos / PAGE_SIZE;  /* the first page this finction
  //					      should start writing to */

  //int curr_page_no = 0;     /* the current page number */
  //size_t curr_size_written; /* size written to virtual disk in this round */
  //size_t size_to_be_written;  /* size to be read in the current round in 
  //				 while loop */
  
  //struct list_head *ptr = asgn2_device.mem_list.next;
  //page_node *curr;
  //char *tp;

  /* COMPLETE ME */
  /**
   * Traverse the list until the first page reached, and add nodes if necessary
   *
   * Then write the data page by page, remember to handle the situation
   *   when copy_from_user() writes less than the amount you requested.
   *   a while loop / do-while loop is recommended to handle this situation. 
   */

  //Allocates new page nodes and pages while there is not enough space to perform write
  /*while(count > (asgn2_device.num_pages*PAGE_SIZE) - asgn2_device.data_size) {
	if(!(curr = kmem_cache_alloc(asgn2_device.cache, GFP_KERNEL))){
		printk(KERN_ERR "failed to allocate memory\n");
		return -ENOMEM;
	}
	if(!(curr->page = alloc_page(GFP_KERNEL))){
		printk(KERN_ERR "falied to allocate page\n");
		return -ENOMEM;
	}
	//printk(KERN_INFO "page address %x\n", page_address(curr->page));
	list_add_tail(&(curr->list), &(asgn2_device.mem_list));
	asgn2_device.num_pages += 1;
  }
  //printk(KERN_INFO "number of page %d count %d\n", asgn2_device.num_pages, count);
  //printk(KERN_INFO "begin page num %d f_pos %d\n", begin_page_no, *f_pos);

  //Loops through pages to find first requested page then performs write
  list_for_each(ptr, &asgn2_device.mem_list) {
	if(curr_page_no >= begin_page_no) {
		curr = list_entry(ptr, struct page_node_rec, list);
		//printk(KERN_INFO "curr page address %x\n", page_address(curr->page));
		size_to_be_written = min(count, min((size_t)PAGE_SIZE, (size_t)PAGE_SIZE - (*f_pos%PAGE_SIZE)));
		curr_size_written = 0;
		//printk(KERN_INFO "size to be written %i\n", size_to_be_written);
		do {
			begin_offset = curr_size_written;
			//printk(KERN_INFO "begin offset %i\n", begin_offset); 
			curr_size_written += (size_to_be_written - copy_from_user
				(page_address(curr->page)+begin_offset, 
				 buf+begin_offset+(int)size_written, size_to_be_written));
			//tp = page_address(curr->page);
			//printk(KERN_INFO "curr size written %i\n", curr_size_written);
			//printk(KERN_INFO "The page address %x %c %c %c \n", tp, tp[0], tp[1], tp[2]);
			if(curr_size_written == 0) {
				return count;
			}	
		} while (curr_size_written < size_to_be_written);
		size_written += curr_size_written;
		count -= curr_size_written;
	}
  }

  //printk(KERN_INFO "The count is %d and size_written is %d\n", count, size_written);

  *f_pos += size_written;
  asgn2_device.data_size = max(asgn2_device.data_size,
                               orig_f_pos + size_written);
  return size_written;
}*/

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

  if(_IOC_TYPE(cmd) != MYIOC_TYPE) {
	  printk(KERN_WARNING "invalid ioctl command, cmd = %d\n", cmd);
          return -EINVAL;
  }

  if(cmd == TEM_SET_NPROC) {
	  if((result = get_user(new_nprocs, &arg)) < 0) {
		  printk(KERN_ERR "failed to copy nprocs value");
		  return -EFAULT;
	  }
	  //Checks if valid nproc value
	  if(new_nprocs < 0 || new_nprocs > atomic_read(&asgn2_device.nprocs)){
		  printk(KERN_WARNING "invalid value for nprocs");
		  return -EINVAL;
          } else atomic_set(&asgn2_device.max_nprocs, new_nprocs);
	  return 0;
  }	

  return -ENOTTY;
}

struct file_operations asgn2_fops = {
  .owner = THIS_MODULE,
  .read = asgn2_read,
  .unlocked_ioctl = asgn2_ioctl,
  .open = asgn2_open,
  .release = asgn2_release
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

  seq_printf(s, "Page: %i\nMemory: %i\n",
		  asgn2_device.num_pages,
		  asgn2_device.data_size);
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
 
  atomic_set(&asgn2_device.nprocs, 0);
  atomic_set(&asgn2_device.max_nprocs, 2);

  //Allocate major number
  if(alloc_chrdev_region(&asgn2_device.dev, asgn2_minor, asgn2_dev_count, 
			  "asgn2") < 0) {
  }

  //Allocate and add cdev
  if(!(asgn2_device.cdev = cdev_alloc())) {
	printk(KERN_ERR "cdev_alloc() failed\n");
        unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
	return -1;
  }
  cdev_init(asgn2_device.cdev, &asgn2_fops);
  if(cdev_add(asgn2_device.cdev, asgn2_device.dev, asgn2_dev_count) < 0) {
	printk(KERN_ERR "cdev_add() failed\n");
        cdev_del(asgn2_device.cdev);
        unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
	return -1;
  }

  printk(KERN_INFO "device register successfully");
  
  //Initlise page list and memory cache
  INIT_LIST_HEAD(&asgn2_device.mem_list);
  
  if(!(asgn2_device.cache = kmem_cache_create("asgn2_device.cache", 
				  PAGE_SIZE, 0, SLAB_HWCACHE_ALIGN, NULL))) {
	printk(KERN_ERR "kmem_cache_create failed\n");
        return -ENOMEM;
  }
  printk(KERN_INFO "mem cache allocated");

  //Create proc file
  if(proc_create("asgn2_proc", 0, NULL, &asgn2_proc_ops) == NULL) {
	printk(KERN_ERR "proc_create failed\n");
	cdev_del(asgn2_device.cdev);
	unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
	return -1;
  }    

  asgn2_device.class = class_create(THIS_MODULE, MYDEV_NAME);
  if (IS_ERR(asgn2_device.class)) {
  }

  asgn2_device.device = device_create(asgn2_device.class, NULL, 
                                      asgn2_device.dev, "%s", MYDEV_NAME);
  if (IS_ERR(asgn2_device.device)) {
    printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_device;
  }
  
  printk(KERN_WARNING "set up udev entry\n");
  printk(KERN_WARNING "Hello world from %s\n", MYDEV_NAME);
  return 0;

  /* cleanup code called when any of the initialization steps fail */
fail_device:
   class_destroy(asgn2_device.class);


   remove_proc_entry("asgn2_proc", NULL);
   free_memory_pages();
   kmem_cache_destroy(asgn2_device.cache);
   cdev_del(asgn2_device.cdev);
   unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);

  /* COMPLETE ME */
  /* PLEASE PUT YOUR CLEANUP CODE HERE, IN REVERSE ORDER OF ALLOCATION */


  return result;
}


/**
 * Finalise the module
 */
void __exit asgn2_exit_module(void){
  device_destroy(asgn2_device.class, asgn2_device.dev);
  class_destroy(asgn2_device.class);
  printk(KERN_WARNING "cleaned up udev entry\n");
  
  /* COMPLETE ME */
  /**
   * free all pages in the page list 
   * cleanup in reverse order
   */
  free_memory_pages();
  device_destroy(asgn2_device.class, asgn2_device.dev);
  class_destroy(asgn2_device.class);
  remove_proc_entry("asgn2_proc", NULL);
  kmem_cache_destroy(asgn2_device.cache);
  cdev_del(asgn2_device.cdev);
  unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
  printk(KERN_WARNING "Good bye from %s\n", MYDEV_NAME);
}


module_init(asgn2_init_module);
module_exit(asgn2_exit_module);
