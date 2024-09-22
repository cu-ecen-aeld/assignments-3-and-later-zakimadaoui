/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

MODULE_AUTHOR("Zakaria Madaoui");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/**
 * initializes the AESD specific portion of the device
 */
void aesd_dev_init(struct aesd_dev *dev) { memset(dev, 0, sizeof(struct aesd_dev)); }

int aesd_open(struct inode *inode, struct file *filp) {
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_close(struct inode *inode, struct file *filp) {
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    
    /**
     * TODO: handle read
     */
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t result = 0;

    // TODO: use mutex to protect accessing the buffer
    // TODO: add support for pending operations

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    if (count > KMALLOC_MAX_SIZE) {
        return -ENOMEM;
    }
    struct aesd_circular_buffer *circ_buff = &aesd_device.buffer;
    char *user_data = (char *)kmalloc(count, GFP_KERNEL);
    if (!user_data) {
        return -ENOMEM;
    }
    // copy the data into the allocated buffer
    result = copy_from_user((void *)user_data, buf, count);

    struct aesd_buffer_entry entry;
    entry.buffptr = user_data;
    entry.size = count;

    // if the buffer is full, first de-allocate the old history line we are about to over-write
    if (circ_buff->full) {
        kfree(&circ_buff->entry[circ_buff->out_offs]);
    }
    aesd_circular_buffer_add_entry(circ_buff, &entry);

    return result;
}
struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_close,
};

static int aesd_setup_cdev(struct aesd_dev *dev, dev_t devno) {
    int err;

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void) {
    int result;
    dev_t dev = 0;
    const unsigned int minor_nbrs = 1;
    aesd_dev_init(&aesd_device);
    // register a range of char device numbers using `alloc_...` instead of `register_...` because
    // we need a dynamic major number
    result = alloc_chrdev_region(&dev, aesd_device.minor, minor_nbrs, "aesdchar");
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_device.major);
        return result;
    }
    aesd_device.major = MAJOR(dev);
    result = aesd_setup_cdev(&aesd_device, dev);
    if (result) {
        unregister_chrdev_region(dev, minor_nbrs);
    }
    return result;
}

void aesd_cleanup_module(void) {
    dev_t devno = MKDEV(aesd_device.major, aesd_device.minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
