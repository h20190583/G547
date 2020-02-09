#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/uaccess.h>        
#include <linux/time.h>

#include "adc8.h"
#define SUCCESS 0
#define DEVICE_NAME "adc8"
#define DEBUG

static char alignment;
static char channel;

static int Device_Open = 0;

static uint16_t message;


struct timespec ts;
/*
 * This is called whenever a process attempts to open the device file
 */
static int device_open(struct inode *inode, struct file *file)
{
#ifdef DEBUG
        printk(KERN_INFO "device_open(%p)\n", file);
#endif

    /*
     * We don't want to talk to two processes at the same time
     */
    if (Device_Open)
        return -EBUSY;

    Device_Open++;

    try_module_get(THIS_MODULE);
    return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file)
{
#ifdef DEBUG
    printk(KERN_INFO "device_release(%p,%p)\n", inode, file);
#endif

    /*
     * We're now ready for our next caller
     */
    Device_Open--;

    module_put(THIS_MODULE);
    return SUCCESS;
}

static ssize_t device_read(struct file *file,   /* see include/linux/fs.h   */
                           char __user * buffer,        /* buffer to be
                                                         * filled with data */
                           size_t length,       /* length of the buffer     */
                           loff_t * offset)
{
    /*
     * Number of bytes actually written to the buffer
     */
    char *b;
    int bytes_read = 0;
    int temp;

#ifdef DEBUG
    printk(KERN_INFO "device_read(%p,%p,%d)\n", file, buffer, length);
#endif

    /*
     * Actually put the data into the buffer
     */
    int j;
    uint16_t i;
    get_random_bytes(&i, sizeof(i));
    i = i%1023;
    printk(KERN_INFO "random number generated : %d\n", i);

    if(alignment == 49)
	message = i;
    else 
	message = (i<<6);
	
    printk(KERN_INFO "after alignment data = %d\n", message);
    copy_to_user(buffer, &message, sizeof(message));
   

#ifdef DEBUG
    printk(KERN_INFO "Read %d bytes, %d left\n", bytes_read, length);
#endif
    return bytes_read;
}

long device_ioctl(struct file *file,             /* ditto */
                  unsigned int ioctl_num,        /* number and param for ioctl */
                  unsigned long ioctl_param)
{
    int i;
    char *temp;
    char ch;

    /*
     * Switch according to the ioctl called
     */
    switch (ioctl_num) {
    case IOCTL_SET_ALIGNMENT:
         
          temp = (char *)ioctl_param;
          get_user(ch, temp);
	      alignment = ch;
          printk(KERN_INFO "\n1 : right aligned 2 : left aligned Default : left aligned\n alignment selected : %c",ch);

         break;

    case IOCTL_SELECT_CHANNEL:
       
         temp = (char *)ioctl_param;
         get_user(ch, temp);
	     channel = ch;
         if(ch >= 49 && ch <= 56)
         	printk(KERN_INFO "channel selected: %c",ch);
         else
         	printk(KERN_INFO "\nChannel out of range default channel 1 selected\n");
        break;
    }

    return SUCCESS;
}

struct file_operations Fops = {
	    .owner = THIS_MODULE,
        .read = device_read,
        .unlocked_ioctl = device_ioctl,
        .open = device_open,
        .release = device_release,      /* a.k.a. close */
};

/*
 * Initialize the module - Register the character device
 */
int init_module(void)
{
    int ret_val;
    /*
     * Register the character device (atleast try)
     */
    ret_val = register_chrdev(MAJOR_NUM, DEVICE_NAME, &Fops);

     if (ret_val < 0) {
        printk(KERN_ALERT "%s failed with %d\n",
               "Sorry, registering the character device ", ret_val);
        return ret_val;
    }

    printk(KERN_INFO "%s The major device number is %d.\n",
           "Registeration is a success", MAJOR_NUM);
    printk(KERN_INFO "If you want to talk to the device driver,\n");
    printk(KERN_INFO "you'll have to create a device file. \n");
    printk(KERN_INFO "We suggest you use:\n");
    printk(KERN_INFO "mknod %s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM);
    printk(KERN_INFO "The device file name is important, because\n");
    printk(KERN_INFO "the ioctl program assumes that's the\n");
    printk(KERN_INFO "file you'll use.\n");
    

    return 0;
}

void cleanup_module()
{
    /*
     * Unregister the device
     */
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
}
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("TEJAS");
MODULE_DESCRIPTION("Assignment 1 adc8");
