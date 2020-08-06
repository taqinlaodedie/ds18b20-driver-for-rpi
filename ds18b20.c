#include <linux/init.h>   
#include <linux/module.h>   
#include <linux/delay.h>   
#include <linux/kernel.h>   
#include <linux/moduleparam.h>   
#include <linux/init.h>   
#include <linux/types.h>   
#include <linux/fs.h>     
#include <linux/cdev.h>   
#include <linux/uaccess.h>   
#include <linux/errno.h>   
#include <linux/gpio.h>   
#include <linux/device.h>   


// #define BCM_GPIO_BASE           0x3F200000  /* Physical memory of  GPIO */
// #define BCM_GPIO_FSEL0_OFFSET   0x0         /* GPIO function select register*/
// #define BCM_GPIO_SET0_OFFSET    0x1C        /* GPIO set register */
// #define BCM_GPIO_CLR0_OFFSET    0x28        /* GPIO clear register */
// #define BCM_GPIO_PLEV0_OFFSET   0x34        /* GPIO pin level register */

#define DQ_IO   4

/* Major minor numbers */
static int ds18b20_major = 0;  
static int ds18b20_minor = 0;  
static int ds18b20_nr_devs = 1;  

/* Define driver device */
static struct ds18b20_device {
    struct cdev cdev;
};

struct ds18b20_device *ds18b20_devp;
static struct class *ds18b20_class;  
static struct device *ds18b20_class_dev;  

/* Declarations of functions */
static void ds18b20_dat_input(void);
static void ds18b20_dat_output(void);
static unsigned ds18b20_read_DQ(void);
static void ds18b20_write_DQ(unsigned val);
static unsigned ds18b20_read_bit(void);
static int ds18b20_open(struct inode *inode, struct file *filp);  
static int ds18b20_init(void);  
static void ds18b20_write_byte(unsigned char data);  
static unsigned char ds18b20_read_byte(void);  
static ssize_t ds18b20_read(struct file *filp, char __user * buf, size_t count, loff_t * f_pos);  
void ds18b20_setup_cdev(struct ds18b20_device *dev, int index);  

/* Open device */
static int ds18b20_open(struct inode *inode, struct file *filp)  
{  
    int flag = 0;  
  
    flag = ds18b20_init();  
    if (flag & 0x01) {  
        printk(KERN_WARNING "open ds18b20 failed\n");  
        return -1;  
    }  
    printk(KERN_NOTICE "open ds18b20 successful\n");  
    return 0;  
}  

/* Init ds18b20 */
static int ds18b20_init(void)  
{  
    int retval = 0;  
  
    /* Reset module */
    ds18b20_dat_output();  
    ds18b20_write_DQ(1);  
    udelay(2);  
    ds18b20_write_DQ(0);  
    udelay(500);  
    ds18b20_write_DQ(1); 
    udelay(40);  
  
    /* Chek result of reset */  
    ds18b20_dat_input();  
    retval = ds18b20_read_DQ();  
  
    /* Release module */
    udelay(500);  
    ds18b20_dat_output();   
    ds18b20_write_DQ(1);
  
    return retval;  
}  

/* Write a byte to ds18b20 */
static void ds18b20_write_byte(unsigned char data)  
{  
    unsigned j, testb;
    ds18b20_dat_output();
    for (j = 1; j <= 8; j++) 
    {
        testb = data & 0x01;
        data = data >> 1;
        if (testb) {
            ds18b20_write_DQ(0);// Write 1
            udelay(2);                            
            ds18b20_write_DQ(1);
            udelay(60);             
        }
        else {
            ds18b20_write_DQ(0);// Write 1
            udelay(60);                            
            ds18b20_write_DQ(1);
            udelay(2);                        
        }
    }
}  

/* Read a byte from ds18b20 */
static unsigned char ds18b20_read_byte(void)  
{  
    unsigned i, j, dat;
    dat = 0;
    for (i = 1; i <= 8; i++) {
        j = ds18b20_read_bit();
        dat = (j << 7) | (dat >> 1);
    }						    
    return dat;
}  

static void ds18b20_dat_input(void)
{
    gpio_direction_input(DQ_IO);
}

static void ds18b20_dat_output(void)
{
    gpio_direction_output(DQ_IO, 1);
}

static unsigned ds18b20_read_DQ(void)
{
    return gpio_get_value(DQ_IO);
}

static void ds18b20_write_DQ(unsigned val)
{
    gpio_set_value(DQ_IO, val);
}

static unsigned ds18b20_read_bit(void)
{
    unsigned data;

    ds18b20_dat_output();
    ds18b20_write_DQ(0);
    udelay(2);
    ds18b20_write_DQ(1);
    ds18b20_dat_input();
    udelay(12);
    if (ds18b20_read_DQ())
        data=1;
    else 
        data=0;	 
    udelay(50); 

    return data;
    
}

/* Read to user */
static ssize_t ds18b20_read(struct file *filp, char __user * buf, size_t count, loff_t * f_pos)  
{  
    int flag;  
    unsigned long err;  
    unsigned char result[2] = { 0x00, 0x00 };    
  
    flag = ds18b20_init();  
    if (flag & 0x01)  
    {  
        printk(KERN_WARNING "ds18b20 init failed\n");  
        return -1;  
    }  
  
    ds18b20_write_byte(0xcc);  
    ds18b20_write_byte(0x44);  
  
    flag = ds18b20_init();  
    if (flag & 0x01)  
        return -1;  
  
    ds18b20_write_byte(0xcc);  
    ds18b20_write_byte(0xbe);  
  
    result[0] = ds18b20_read_byte();    // Low byte of temperature   
    result[1] = ds18b20_read_byte();    // High byte of temperature  
  
    err = copy_to_user(buf, &result, sizeof(result));  
    return err ? -EFAULT : min(sizeof(result), count);  
}  

static struct file_operations ds18b20_dev_fops = {  
    .owner = THIS_MODULE,  
    .open = ds18b20_open,  
    .read = ds18b20_read,  
};  

void ds18b20_setup_cdev(struct ds18b20_device *dev, int index)  
{  
    int err, devno = MKDEV(ds18b20_major, ds18b20_minor + index);  
  
    cdev_init(&dev->cdev, &ds18b20_dev_fops);  
    dev->cdev.owner = THIS_MODULE;  
    err = cdev_add(&(dev->cdev), devno, 1);  
    if (err)  
    {  
        printk(KERN_NOTICE "ERROR %d add ds18b20\n", err);  
    }  
}  

// Init module
static int __init ds18b20_dev_init(void)  
{  
    int result;  
    dev_t dev = 0;  

    // gpio = ioremap(BCM_GPIO_BASE, 0xB0);
    result = gpio_request(DQ_IO, "ds18b20_DQ");
    if (result < 0) {
        printk("ds18b20: failed to get GPIO\n");
        return result;
    }
  
    dev = MKDEV(ds18b20_major, ds18b20_minor);  
  
    if (ds18b20_major) {  
        result = register_chrdev_region(dev, ds18b20_nr_devs, "ds18b20");  
    }  
    else {  
        result = alloc_chrdev_region(&dev, ds18b20_minor, ds18b20_nr_devs, "ds18b20");  
        ds18b20_major = MAJOR(dev);  
    }  
    if (result < 0) {  
        printk(KERN_WARNING "ds18b20: failed to get major\n");  
        return result;  
    }  
  
    /* Memory allocation */  
    ds18b20_devp = kmalloc(sizeof(struct ds18b20_device), GFP_KERNEL);  
    if (!ds18b20_devp) {    /*  Check fail */  
        result = -ENOMEM;  
        goto fail_malloc;  
    }  
    memset(ds18b20_devp, 0, sizeof(struct ds18b20_device));  
  
    ds18b20_setup_cdev(ds18b20_devp, 0);  
  
    /* Create inode */  
    ds18b20_class = class_create(THIS_MODULE, "ds18b20_sys_class");  
    if (IS_ERR(ds18b20_class))  
        return PTR_ERR(ds18b20_class);  
  
    ds18b20_class_dev = device_create(ds18b20_class, NULL, MKDEV(ds18b20_major, 0), NULL, "ds18b20");  
    if (unlikely(IS_ERR(ds18b20_class_dev)))  
        return PTR_ERR(ds18b20_class_dev);  
  
    return 0;  
  
fail_malloc:  
    unregister_chrdev_region(dev, 1);  
    return result;  
}  

/* Remove device */
static void __exit ds18b20_dev_exit(void)  
{  
    gpio_release(DQ_IO);
    cdev_del(&ds18b20_devp->cdev);  /* Delete cdeve */  
    kfree(ds18b20_devp);    /* Free memory */  
    unregister_chrdev_region(MKDEV(ds18b20_major, 0), ds18b20_nr_devs); /* Free device number */  
    device_unregister(ds18b20_class_dev);  
    class_destroy(ds18b20_class);  
}  

module_init(ds18b20_dev_init);  
module_exit(ds18b20_dev_exit);  
MODULE_LICENSE("GPL");  
