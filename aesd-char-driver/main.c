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
#include <linux/mutex.h>
#include "aesdchar.h"
#include <linux/slab.h>

MODULE_AUTHOR("Zakaria Madaoui");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
DEFINE_MUTEX(access_control);

/**
 * initializes the AESD specific portion of the device
 */
void aesd_dev_init(struct aesd_dev *dev) {
    memset(dev, 0, sizeof(struct aesd_dev));
    mutex_init(&dev->buffer_lock); // init the mutex for locking the buffer
    // allocate a buffer for pending operations
    dev->pending_entry.buffptr = (char *)kmalloc(KMALLOC_MAX_SIZE, GFP_KERNEL);
}

/**
 * cleans up the AESD specific portion of the device
 */
void aesd_dev_cleanup(struct aesd_dev *dev) {
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
        if (entry->buffptr != NULL)
            kfree(entry->buffptr);
    }
    if (dev->pending_entry.buffptr != NULL)
        kfree(dev->pending_entry.buffptr);
}

int aesd_open(struct inode *inode, struct file *filp) {
    PDEBUG("open");

    struct aesd_dev *dev;                                     /* device information */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev); /*  Find the device */
    filp->private_data = dev; /* and use filp->private_data to point to the device data */

    mutex_lock(&access_control); // only one process can open this at a time
    if (dev->opened)
        return -EBUSY;
    dev->opened = 1;
    mutex_unlock(&access_control);
    return 0;
}

int aesd_close(struct inode *inode, struct file *filp) {
    PDEBUG("release");

    struct aesd_dev *dev;                                     /* device information */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev); /*  Find the device */

    mutex_lock(&access_control);
    dev->opened = 0;
    mutex_unlock(&access_control);
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    struct aesd_dev *dev = filp->private_data;

    // not allowed to read/write if not already open
    mutex_lock_interruptible(&access_control);
    if (!dev->opened) {
        mutex_unlock(&access_control);
        return -EPERM;
    }
    mutex_unlock(&access_control);

    ssize_t result = count;
    mutex_lock_interruptible(&dev->buffer_lock); // lock the buffer mutex before read
    struct aesd_circular_buffer *circ_buff = &dev->buffer;
    // find the entry where f_pos is pointing to
    size_t r_pos = 0;
    struct aesd_buffer_entry *fpos_entry =
        aesd_circular_buffer_find_entry_offset_for_fpos(circ_buff, *f_pos, &r_pos);
    if (!fpos_entry) {
        result = 0;
        goto read_end;
    }
    size_t fpos_entry_idx = fpos_entry - circ_buff->entry; // find index in the buffer

    // Read from the entry index until end of the buffer, respecting the count
    size_t bytes_written = 0;
    char __user *write_ptr = buf;
    size_t buffer_end = circ_buff->in_offs;
    struct aesd_buffer_entry current_entry;

    size_t i = fpos_entry_idx;
    do {
        current_entry = circ_buff->entry[i];
        PDEBUG("reading entry at index %zu, buffent_end %zu", i, buffer_end);
        if ((bytes_written + current_entry.size) > count ||
            current_entry.size == 0) { // this history entry will not fit in the buffer or is empty
            break;
        }
        size_t not_written = copy_to_user(write_ptr, current_entry.buffptr, current_entry.size);
        if (not_written)
            printk(KERN_ERR "aesdchar: failed to write %zu bytes to userspace\n", not_written);
        write_ptr += current_entry.size;     // Advance the write pointer
        bytes_written += current_entry.size; // Increment the total number of bytes written

        i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } while (i != buffer_end);

    result = bytes_written;
    *f_pos+= bytes_written; // update fpos so that next time we continue reading from the same position

read_end:
    mutex_unlock(&dev->buffer_lock); // unlock the buffer mutex after read operation
    return result;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    struct aesd_dev *dev = filp->private_data;

    // not allowed to read/write if not already open
    mutex_lock_interruptible(&access_control);
    if (!dev->opened) {
        mutex_unlock(&access_control);
        return -EPERM;
    }
    mutex_unlock(&access_control);

    // adjust the count to the maximum allocatable size
    count = (count > KMALLOC_MAX_SIZE) ? KMALLOC_MAX_SIZE : count;

    // allocate some memory for reading user data
    char *user_data = (char *)kmalloc(count, GFP_KERNEL);
    if (!user_data) {
        return -ENOMEM;
    }

    // copy the user written data into the allocated memory location
    size_t not_copied = copy_from_user((void *)user_data, buf, count);
    if (not_copied) {
        printk(KERN_ERR "aesdchar: filed to copy %zu bytes to kernelspace", not_copied);
        count -= not_copied;
    }

    // Before modifying the circular buffer, first we aquire the lock to access it
    mutex_lock_interruptible(&dev->buffer_lock);
    struct aesd_circular_buffer *circ_buff = &dev->buffer;

    // adjust `count` if a pending operation is detected to fit our write capacity
    if (dev->pending_write || user_data[count - 1] != '\n') {
        int enough_room = (KMALLOC_MAX_SIZE - dev->pending_entry.size) >= count;
        count = enough_room ? count : (KMALLOC_MAX_SIZE - dev->pending_entry.size);
    }
    ssize_t result = count;

    // new line not found at the end of the user write command
    if (user_data[count - 1] != '\n') {
        PDEBUG("No, new line found in this input, pending this data...");

        dev->pending_write = 1;

        // Copy the user_data into the pending buffer which has a larger capacity
        const char *copy_to = dev->pending_entry.buffptr + dev->pending_entry.size;
        memcpy((void *)copy_to, (void *)user_data, count);
        dev->pending_entry.size += count;
        kfree(user_data);
        goto write_out;
    }

    struct aesd_buffer_entry entry;
    // new line found, but behavior will depend based on whether there is a pending write or not
    if (dev->pending_write) { // finish a pending operation
        PDEBUG("New line char found, finishing the previous pending operation...");

        // initialize the entry size and buffer
        entry.size = count + dev->pending_entry.size;
        char *buffptr = (char *)kmalloc(entry.size, GFP_KERNEL);
        entry.buffptr = buffptr;

        // copy the data to the buffer
        memcpy(buffptr, dev->pending_entry.buffptr, dev->pending_entry.size);
        memcpy(buffptr + dev->pending_entry.size, user_data, count);

        // free the allocated `user_data` as we don't use it anymore
        kfree(user_data);

        // reset the size of the pending buffer and disable the pending flag
        dev->pending_entry.size = 0;
        dev->pending_write = 0;
    }
    // whatever came from userspace, it got immediatly saved to the buffer
    else {
        PDEBUG("Input provided in correct format, inserting immdiately to the buffer...");
        entry.buffptr = user_data;
        entry.size = count;
    }

    // insert the entry to the cicular buffer
    if (circ_buff->full) {
        // if the buffer is full, first de-allocate the old history line we are about to overwrite
        PDEBUG("Buffer is full, deleting the oldest entry");
        kfree(circ_buff->entry[circ_buff->in_offs].buffptr);
        circ_buff->entry[circ_buff->in_offs].size = 0;
    }
    aesd_circular_buffer_add_entry(circ_buff, &entry);

write_out:
    mutex_unlock(&dev->buffer_lock);
    return result;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence) {
    struct aesd_dev *dev = filp->private_data;
    long newpos = 0;
    char *mode[] = {"SEEK_SET", "SEEK_CUR", "SEEK_END"};
    PDEBUG("llseek: mode: %s, off: %lld", mode[whence], off);

    switch (whence) {
    case SEEK_SET: 
        newpos = off;
        break;

    case SEEK_CUR:
        newpos = filp->f_pos + off;
        break;

    case SEEK_END:

        uint8_t index;
        struct aesd_buffer_entry *entry;
        mutex_lock_interruptible(&dev->buffer_lock);
        AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
            if (entry->buffptr != NULL)
                newpos += entry->size;
        }
        mutex_unlock(&dev->buffer_lock);

        break;

    default: /* can't happen */
        return -EINVAL;
    }
    if (newpos < 0)
        return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_close,
    .llseek = aesd_llseek,
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
    aesd_dev_cleanup(&aesd_device);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
