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

MODULE_AUTHOR("Zakaria Madaoui");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/**
 * initializes the AESD specific portion of the device
 */
void aesd_dev_init(struct aesd_dev *dev) { 
    memset(dev, 0, sizeof(struct aesd_dev));
    mutex_init(&dev->buffer_lock); // init the mutex for locking the buffer
    // allocate a buffer for pending operations
    dev->pending_entry.buffptr = (char *) kmalloc(KMALLOC_MAX_SIZE, GFP_KERNEL);
}

/**
 * cleans up the AESD specific portion of the device
 */
void aesd_dev_cleanup(struct aesd_dev *dev) { 
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&dev->buffer,index) {
        if (entry->buffptr != NULL) kfree(entry->buffptr);
    }
    if (dev->pending_entry.buffptr != NULL) kfree(dev->pending_entry.buffptr);
}


int aesd_open(struct inode *inode, struct file *filp) {
    PDEBUG("open");
    if (aesd_device.opened) return -EMFILE;
    aesd_device.opened = 1;
    aesd_device.history_read_complete = 0;
    return 0;
}

int aesd_close(struct inode *inode, struct file *filp) {
    PDEBUG("release");
    aesd_device.opened = 0;
    aesd_device.history_read_complete = 0;
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (aesd_device.history_read_complete) return 0;

    mutex_lock(&aesd_device.buffer_lock); // lock the buffer mutex before read
    struct aesd_circular_buffer *circ_buff = &aesd_device.buffer;
    // find the entry where f_pos is pointing to
    size_t r_pos = 0;
    struct aesd_buffer_entry * fpos_entry = aesd_circular_buffer_find_entry_offset_for_fpos(circ_buff, *f_pos, &r_pos);
    size_t fpos_entry_idx = fpos_entry - circ_buff->entry; // find index in the buffer


    // read from the entry index until end of the buffer, respecting the count
    size_t bytes_written = 0;
    char __user * write_ptr = buf;
    size_t buffer_end = circ_buff->in_offs;
    struct aesd_buffer_entry current_entry;
    int full_read = 1;

    size_t i = fpos_entry_idx;
    do {
        current_entry = circ_buff->entry[i];
        PDEBUG("reading entry at index %zu, buffent_end %zu", i, buffer_end);
        if ((bytes_written + current_entry.size) > count || current_entry.size == 0) { // this history entry will not fit in the buffer or is empty
            full_read = 0;
            break;
        }
        size_t not_written =  copy_to_user(write_ptr, current_entry.buffptr, current_entry.size);
        if (not_written) printk(KERN_ERR "aesdchar: failed to write %zu bytes to userspace\n", not_written );
        write_ptr+=current_entry.size; // Advance the write pointer
        bytes_written+= current_entry.size; // Increment the total number of bytes written

        i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } while(i != buffer_end);

    if (full_read) aesd_device.history_read_complete = 1;
    mutex_unlock(&aesd_device.buffer_lock); // unlock the buffer mutex after read operation
    
    return bytes_written;
}




ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    
    if (count > KMALLOC_MAX_SIZE) {
        return -ENOMEM;
    }

    // allocate some memory for reading user data
    char *user_data = (char *)kmalloc(count, GFP_KERNEL);
    if (!user_data) {
        return -ENOMEM;
    }
    
    // copy the user written data into the allocated memory location
    size_t not_read =  copy_from_user((void *)user_data, buf, count);
    if (not_read) printk(KERN_ERR "aesdchar: filed to copy %zu bytes to kernelspace", not_read );

    // TODO: add support for pending operations. for now we assume write commands all end with a \n character
    // Before modifying the circular buffer, first we aquire the lock to access it
    mutex_lock(&aesd_device.buffer_lock);
    struct aesd_circular_buffer *circ_buff = &aesd_device.buffer;

    // new line not found at the end of the user write command
    if (user_data[count-1] != '\n') {
        printk(KERN_WARNING "No, new line found in this input, pending this data...");
        aesd_device.pending_write = 1;
        ssize_t ret = count;
        
        int enough_room = (count + aesd_device.pending_entry.size) <= KMALLOC_MAX_SIZE;
        if (enough_room) {
            // copy the user_data into the pending buffer which has a larger capacity
            const char *copy_to = aesd_device.pending_entry.buffptr + aesd_device.pending_entry.size;
            memcpy((void*) copy_to, (void*) user_data, count);
            aesd_device.pending_entry.size+=count;
            kfree(user_data);
        } else {
            ret = -ENOMEM;
        }
        mutex_unlock(&aesd_device.buffer_lock);
        return ret;
    } 

    struct aesd_buffer_entry entry;
    // new line found, but behavior will depend based on whether there is a pending write or not
    if (aesd_device.pending_write) { // finish a pending operation
        printk(KERN_WARNING "New line char found, finishing the previous pending operation...");
        entry.size = count + aesd_device.pending_entry.size;
        char* buffptr = (char*) kmalloc(entry.size, GFP_KERNEL); // allocate space to put the data
        memcpy(buffptr, aesd_device.pending_entry.buffptr, aesd_device.pending_entry.size);
        memcpy(buffptr + aesd_device.pending_entry.size, user_data, count);
        entry.buffptr = buffptr;
        // free the allocated `user_data` as we don't use it anymore
        kfree(user_data);
    }
    else { // whatever came from userspace, it got immediatly saved to the buffer
        printk(KERN_WARNING "Input provided in correct format, inserting immdiately to the buffer...");
        entry.buffptr = user_data;
        entry.size = count;
    }
    
    // reset the size of the pending buffer and disable the pending flag
    aesd_device.pending_entry.size = 0;
    aesd_device.pending_write = 0;
    
    // if the buffer is full, first de-allocate the old history line we are about to overwrite
    if (circ_buff->full) {
        kfree(&circ_buff->entry[circ_buff->in_offs]);
    }
    aesd_circular_buffer_add_entry(circ_buff, &entry);
    mutex_unlock(&aesd_device.buffer_lock);

    return count;
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
    aesd_dev_cleanup(&aesd_device);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
