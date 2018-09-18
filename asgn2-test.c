/**
 * File: asgn2.c
 * Date: 13/03/2011
 * Author: Daniel Thomson 
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
#define BUFF_SIZE 1024 

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Thomson");
MODULE_DESCRIPTION("COSC440 asgn2");

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/delay.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 3, 0)
        #include <asm/switch_to.h>
#else
        #include <asm/system.h>
#endif

#define BCM2835_PERI_BASE 0x3f000000

static u32 gpio_dummy_base;

/* Define GPIO pins for the dummy device */
static struct gpio gpio_dummy[] = {
                { 7, GPIOF_IN, "GPIO7" },
                { 8, GPIOF_OUT_INIT_HIGH, "GPIO8" },
                { 17, GPIOF_IN, "GPIO17" },
                { 18, GPIOF_OUT_INIT_HIGH, "GPIO18" },
                { 22, GPIOF_IN, "GPIO22" },
                { 23, GPIOF_OUT_INIT_HIGH, "GPIO23" },
                { 24, GPIOF_IN, "GPIO24" },
                { 25, GPIOF_OUT_INIT_HIGH, "GPIO25" },
                { 4, GPIOF_OUT_INIT_LOW, "GPIO4" },
                { 27, GPIOF_IN, "GPIO27" },
};

static int dummy_irq;
extern irqreturn_t dummyport_interrupt(int irq, void *dev_id);
	
static inline u32
gpio_inw(u32 addr)
{
    u32 data;

    asm volatile("ldr %0,[%1]" : "=r"(data) : "r"(addr));
    return data;
}

static inline void
gpio_outw(u32 addr, u32 data)
{
    asm volatile("str %1,[%0]" : : "r"(addr), "r"(data));
}

void setgpiofunc(u32 func, u32 alt)
{
        u32 sel, data, shift;

        if(func > 53) return;
        sel = 0;
        while (func > 10) {
            func = func - 10;
            sel++;
        }
	sel = (sel << 2) + gpio_dummy_base;
        data = gpio_inw(sel);
        shift = func + (func << 1);
        data &= ~(7 << shift);
        data |= alt << shift;
        gpio_outw(sel, data);
}

u8 read_half_byte(void)
{
u32 c;
u8 r;

r = 0;
c = gpio_inw(gpio_dummy_base + 0x34);
if (c & (1 << 7)) r |= 1;
if (c & (1 << 17)) r |= 2;
if (c & (1 << 22)) r |= 4;
if (c & (1 << 24)) r |= 8;

return r;
}

static void write_to_gpio(char c)
{
volatile unsigned *gpio_set, *gpio_clear;

gpio_set = (unsigned *)((char *)gpio_dummy_base + 0x1c);
gpio_clear = (unsigned *)((char *)gpio_dummy_base + 0x28);

if(c & 1) *gpio_set = 1 << 8;
else *gpio_clear = 1 << 8;
udelay(1);

if(c & 2) *gpio_set = 1 << 18;
else *gpio_clear = 1 << 18;
udelay(1);

if(c & 4) *gpio_set = 1 << 23;
else *gpio_clear = 1 << 23;
udelay(1);

if(c & 8) *gpio_set = 1 << 25;
else *gpio_clear = 1 << 25;
udelay(1);

}

int gpio_dummy_init(void)
{
    int ret;
 
    gpio_dummy_base = (u32)ioremap_nocache(BCM2835_PERI_BASE + 0x200000, 4096);
    printk(KERN_WARNING "The gpio base is mapped to %x\n", gpio_dummy_base);
    ret = gpio_request_array(gpio_dummy, ARRAY_SIZE(gpio_dummy));

    if (ret) {
	printk(KERN_ERR "Unable to request GPIOs for the dummy device: %d\n", ret);
	return ret;
        }
    ret = gpio_to_irq(gpio_dummy[ARRAY_SIZE(gpio_dummy)-1].gpio);
    if(ret < 0) {
	printk(KERN_ERR "Unable to request IRQ for gpio %d: %d\n", gpio_dummy[ARRAY_SIZE(gpio_dummy)-1].gpio, ret);
        goto fail1;
    }
    dummy_irq = ret;
    printk(KERN_WARNING "Successfully requested IRQ# %d for %s\n", dummy_irq, gpio_dummy[ARRAY_SIZE(gpio_dummy)-1].label);

    ret = request_irq(dummy_irq, dummyport_interrupt, IRQF_TRIGGER_RISING | IRQF_ONESHOT, "gpio27", NULL);

    if(ret) {
	printk(KERN_ERR "Unable to request IRQ for dummy device: %d\n", ret);
	goto fail1;
    }
    write_to_gpio(15);
return 0;

fail1:
    gpio_free_array(gpio_dummy, ARRAY_SIZE(gpio_dummy));
    iounmap((void *)gpio_dummy_base);
    return ret;
}

void gpio_dummy_exit(void)
{
    free_irq(dummy_irq, NULL);
    gpio_free_array(gpio_dummy, ARRAY_SIZE(gpio_dummy));
    iounmap((void *)gpio_dummy_base);
}

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
  int half_byte_count; /*number of half bytes read that haven't been combined into a full byte */
} asgn2_dev;

typedef struct circular_buf_t {
	u8 buffer[BUFF_SIZE];
	int head;
	int tail;
	int full;
} circular_buf;

typedef struct multi_page_queue_t {
	int head;
	int tail;
	size_t head_off;
	size_t tail_off;
} mpq;

asgn2_dev asgn2_device;
circular_buf cbuf;
mpq page_queue;

static atomic_t data_ready;

//extern irqreturn_t dummyport_interrupt(int irq, void *dev_id);
void readbuf_fun(unsigned long t_arg);
static DECLARE_TASKLET(readbuf, readbuf_fun, 0);
static DECLARE_WAIT_QUEUE_HEAD(read_wq);
static DECLARE_WAIT_QUEUE_HEAD(open_wq);

int asgn2_major = 0;                      /* major number of module */  
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */

void circular_buf_put(circular_buf cbuf, u8 data) {
	cbuf.buffer[cbuf.tail] = data;
	cbuf.tail = (cbuf.tail+1)%BUFF_SIZE;
	if(cbuf.full) {
		cbuf.head = (cbuf.head+1)%BUFF_SIZE;
	} else if (cbuf.head == cbuf.tail) {
		cbuf.full = 1;
	}
}

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
	  wait_event_interruptible(open_wq, atomic_read(&asgn2_device.nprocs) == 0);
  }
  if(!((filp->f_mode & FMODE_READ) && !(filp->fmode & FMODE_WRITE))) {
	  printk(KERN_ERROR "only read-only mode allowed");
	  return -EACCES;
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
  wake_up_interruptible(&open_wq);
  return 0;
}


/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count,
		 loff_t *f_pos) {
  size_t size_read = 0;     /* size read from virtual disk in this function */
  size_t begin_offset = 0;      /* the offset from the beginning of a page to
			       start reading */
  int begin_page_no = page_queue.head; /* the first page which contains
					     the requested data */
  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_read;    /* size read from the virtual disk in this round */
  size_t size_to_be_read;   /* size to be read in the current round in 
			       while loop */

  int pages_freed = 0;
  int null_terminator = 0;
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
  if(page_queue.tail == page_queue.head && page_queue.tail_off == page_queue.head_off)
	  atomic_set(&data_ready, 0);

  if(atomic_read(&data_ready) == 0)
	  wait_event_interruptible(read_wq, atomic_read(&data_ready == 1));

  count = min(asgn2_device.data_size - page_queue.head_off, count);

  list_for_each_entry(curr, &asgn2_device.mem_list, list) {
	  if(begin_page_no <= curr_page_no) {
		  begin_offset = page_q.head_off;
		  size_to_be_read = min(count, PAGE_SIZE - begin_offset);

		  int i;
		  for(i = 0; i < size_to_be_read; i++) {
			  if((u8)(page_address(curr_page) + begin_offset + i) == '\0') {
				  size_to_be_read -= (size_to_be_read - i);
				  null_terminator = 1;
				  break;
			  }
		  }

		  while(size_to_be_read > 0) {
			curr_size_read = size_to_be_read - copy_to_user(buf + size_read, page_address(curr->page) + begin_offset, size_to_be_read);
			size_read += curr_size_read;
			size_to_be_read -= curr_size_read;
			count -= curr_size_read;
			page_queue.head_offset = (page_queue.head_offset + curr_size_read)%PAGE_SIZE;
			begin_offset = page_queue.head_offset;
		}
	}
	if(begin_offset == 0) {
		page_queue.head++;
		__free_page(curr->page);
		list_del(&curr->list);
		kfree(curr);
		pages_freed++;
		curr_page_no++;
		asgn2_device.num_pages--;
	}
	if(null_terminator == 1) break;

  }

  page_queue.head -= pages_freed;
  page_queue.tail -= pages_freed;
  asgn2_device.data_size -= pages_freed * PAGE_SIZE;

  /*while (size_read < count) {
	  curr = list_entry(ptr, page_node, list);
	  if(ptr == &asgn1_device.mem_list) {
		  printk(KERN_WARNING "invalid virtual memory access");
		  return size_read;
	  } else if (curr_page_no < begin_page_no) {
		  ptr = ptr->next;
		  curr_page_no++;
    	  } else {
		  begin_offset = *f_pos % PAGE_SIZE;
		  size_to_be_read = (size_t)min((size_t)(count - size_read), (size_t)(PAGE_SIZE - begin_offset));
		  do {
			  curr_size_read = size_to_be_read - copy_to_user(buf + size_read, page_address(curr->page) + begin_offset, size_to_be_read);
			  size_read += curr_size_read;
			  *f_pos += curr_size_read;
			  begin_offset += curr_size_read;
			  size_to_be_read -= curr_size_read;
		  } while (curr_size_read > 0);
		  curr_page_no++;
		  ptr = ptr->next;
	}
  }*/

  return size_read;
}

void readbuf_fun (unsigned long t_arg) {
	size_t size_writtern = 0;
	size_t begin_offset = 0;
	int begin_page_no = page_queue.tail;

	int curr_page_no = 0;
	size_t curr_page_offset = page_queue.tail_offset;
	size_t size_to_be_written;
	int buf_count;

	page_node *curr;

	if(cbuf.tail > cbuf.head) {
		buf_count = cbuf.tail - cbuf.head;
	} else if (cbuf.head > cbuf.tail) {
		buf_count = cbuf.tail + (BUFF_SIZE - cbuf.head);
	} else buf_count = 0;

	while(buf_count > (asgn2_device.num_pages*PAGE_SIZE) - asgn2_device.data_size) {
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

	list_for_each_entry(curr, &asgn2_device.mem_list, list) {
		if(curr_page_no >= begin_page_no && buf_count > 0) {
			begin_offset = curr_page_offset % PAGE_SIZE;
			size_to_be_written = min(buf_count, (size_t)PAGE_OFFSET - begin_offset);

			memcpy(page_address(curr->page) + begin_offset, cbuf.buffer + cbuf.head, size_to_be_written);
			cbuf.head = (cbuf.head + size_to_written) % BUFF_SIZE;

			size_written += size_to_be_written;
			buf_count -= size_to_be_written;
			curr_page_offset += size_to_be_written;
			size_to_be_written = 0;
			begin_offset = curr_page_offset % PAGE_SIZE;
		}
		curr_page_no++;
	}
	
	if(size_written > 0 && cbuf.full == 1) cbuf.full = 0;
	asgn2_device.data_size += size_written;
	page_q.tail_offset = begin_offset;

	atomic_set(&data_ready, 1);
	wake_up_interruptible(&read_wq);

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

irqreturn_t dummyport_interrupt(int irq, void *dev_id) {
	static u8 full_byte;

	if(asgn2_device.half_byte_count == 1) {
		asgn2_device.half_byte_count = 0;
                full_byte |= read_half_byte();
		printk(KERN_INFO "read %c of port\n", (char) full_byte);
 		circular_buf_put(cbuf, full_byte);
		tasklet_schedule(&readbuf);

        } else {
		full_byte = read_half_byte() << 4;
		asgn2_device.half_byte_count++;
	}
	return IRQ_HANDLED;	
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
  atomic_set(&asgn2_device.max_nprocs, 1);

  //Allocate major number
  if(alloc_chrdev_region(&asgn2_device.dev, asgn2_minor, asgn2_dev_count, 
			  "asgn2") < 0) {
	  return -EBUSY;
  }

  asgn2_major = MAJOR(asgn2_device.dev);

  //Allocate and add cdev
  if(!(asgn2_device.cdev = cdev_alloc())) {
	printk(KERN_ERR "cdev_alloc() failed\n");
	result = -ENOMEM;
        goto fail_cdev;
  }
  cdev_init(asgn2_device.cdev, &asgn2_fops);
  if(cdev_add(asgn2_device.cdev, asgn2_device.dev, asgn2_dev_count) < 0) {
	printk(KERN_ERR "cdev_add() failed\n");
	result = -ENOMEM;
	goto fail_cdev;
  }

  printk(KERN_INFO "device register successfully");
  
  //Initlise page list and memory cache
  INIT_LIST_HEAD(&asgn2_device.mem_list);
  
  if(!(asgn2_device.cache = kmem_cache_create("asgn2_device.cache", 
				  PAGE_SIZE, 0, SLAB_HWCACHE_ALIGN, NULL))) {
	printk(KERN_ERR "kmem_cache_create failed\n");
        result =  -ENOMEM;
	goto fail_kmem_cache_create;
  }
  printk(KERN_INFO "mem cache allocated");

  //Create proc file
  if(proc_create("asgn2_proc", 0, NULL, &asgn2_proc_ops) == NULL) {
	printk(KERN_ERR "proc_create failed\n");
	result = -ENOMEM;
	goto fail_proc_entry;
  }

  gpio_dummy_init();

  cbuf.head = 0;
  cbuf.tail = 0;
  cbuf.full = 0;

  page_queue.head = 0;
  page_queue.tail = 0;
  page_queue.head_off = 0;
  page_queue.tail_off = 0;

  atomic_set(&data_ready, 0);


  asgn2_device.class = class_create(THIS_MODULE, MYDEV_NAME);
  if (IS_ERR(asgn2_device.class)) {
	  result = -ENOMEM;
	  goto fail_class;
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
fail_class:
   kmem_cach_destroy(asgn2_device_cache);
fail_proc_entry:
   cdev_del(asgn2_device.cdev);
fail_kmem_cache_create:
   remove_proc_entry("asgn2_proc", NULL);
fail_cdev:
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
  gpio_dummy_exit();
  remove_proc_entry("asgn2_proc", NULL);
  kmem_cache_destroy(asgn2_device.cache);
  cdev_del(asgn2_device.cdev);
  unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
  printk(KERN_WARNING "Good bye from %s\n", MYDEV_NAME);
}


module_init(asgn2_init_module);
module_exit(asgn2_exit_module);
