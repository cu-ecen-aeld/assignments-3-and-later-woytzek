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
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("woytzek"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
void aesd_cleanup_module(void);
int aesd_init_module(void);
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl
};

struct aesd_dev aesd_device;

struct msg_part
{
    char *message;
    size_t length;
} msg_part = 
{
    .message = NULL,
    .length = 0
};

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open" );
    /**
     * TODO: handle open
     */

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    size_t entry_offset;
    mutex_lock( &aesd_device.lock );
    struct aesd_buffer_entry *entry = 
        aesd_circular_buffer_find_entry_offset_for_fpos( &aesd_device.circular_buffer, *f_pos, &entry_offset);
    if( entry != NULL )
    {
        size_t bytes_available = entry->size - entry_offset;
        size_t bytes_to_copy = ( count < bytes_available ) ? count : bytes_available;

        if( copy_to_user( buf, entry->buffptr + entry_offset, bytes_to_copy ) != 0 )
        {
            mutex_unlock( &aesd_device.lock );
            PDEBUG("copy_to_user failed");
            return -EFAULT;
        }
        *f_pos += bytes_to_copy;
        retval = bytes_to_copy;
    }
    mutex_unlock( &aesd_device.lock );
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    
    char *kbuf;
    
    /* check if data ends with new line */
    char eol;
    get_user( eol, buf + count - 1 );
    if( eol == '\n' )
    {
        size_t msg_length;
        if( msg_part.message != NULL )
        {
            /* append to previous part */
            char *new_msg = krealloc( msg_part.message, msg_part.length + count, GFP_KERNEL );
            if( new_msg == NULL )
            {
                //kfree( kbuf );
                PDEBUG("krealloc failed");
                return retval;
            }
            msg_part.message = new_msg;
            if( copy_from_user( msg_part.message + msg_part.length, buf, count ) != 0 )
            {
                //kfree( kbuf );
                PDEBUG("copy_from_user failed");
                return -EFAULT;
            }
            msg_part.length += count;
            //kfree( kbuf );

            kbuf = msg_part.message;
            msg_length = msg_part.length;
            //count = msg_part.length;

            /* reset part */
            msg_part.message = NULL;
            msg_part.length = 0;

            PDEBUG("combined message length %zu", count);
        }
        else
        {
            /* first part only */
            kbuf = kmalloc( count, GFP_KERNEL );
            if( kbuf == NULL )
            {
                PDEBUG("kmalloc for kbuf failed");
                return retval;
            }
            if( copy_from_user( kbuf, buf, count ) != 0 )
            {
                kfree( kbuf );
                PDEBUG("copy_from_user failed");
                return -EFAULT;
            }
            msg_length = count;

            PDEBUG("single message length %zu", count);
        }
        struct aesd_buffer_entry new_entry = 
        {
            .buffptr = kbuf,
            .size = msg_length
        };
        mutex_lock( &aesd_device.lock );
        const char *buf2free = aesd_circular_buffer_add_entry( &aesd_device.circular_buffer, &new_entry );
        mutex_unlock( &aesd_device.lock );

        if( buf2free != NULL )
        {
            kfree( (void *)buf2free );
        }
        retval = count;
    }
    else
    {
        /* save data for next write */
        if( msg_part.message != NULL )
        {
            /* realloc */
            char *new_msg = krealloc( msg_part.message, msg_part.length + count, GFP_KERNEL );
            if( new_msg == NULL )
            {
                kfree( kbuf );
                PDEBUG("krealloc failed");
                return retval;
            }
            msg_part.message = new_msg;
            if( copy_from_user( msg_part.message + msg_part.length, buf, count ) != 0 )
            {
                //kfree( kbuf );
                PDEBUG("copy_from_user failed");
                return -EFAULT;
            }
            msg_part.length += count;
            kfree( kbuf );

            PDEBUG("appended message length %zu", msg_part.length);
        }
        else
        {
            /* first part */
            msg_part.message = kmalloc( count, GFP_KERNEL );
            if( msg_part.message == NULL )
            {
                PDEBUG("kmalloc for msg_part.message failed");
                return retval;
            }
            if( copy_from_user( msg_part.message, buf, count ) != 0 )
            {
                kfree( msg_part.message );
                PDEBUG("copy_from_user failed");
                return -EFAULT;
            }
            msg_part.length = count;

            PDEBUG("stored message length %zu", msg_part.length);
        }
        retval = count;
    }

    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    loff_t newpos = 0;

    PDEBUG("llseek to offset %lld whence %d", offset, whence);
    
    size_t total_size = 0;
    struct aesd_buffer_entry *entry;
    int index;
    mutex_lock( &aesd_device.lock );
    AESD_CIRCULAR_BUFFER_FOREACH( entry, &aesd_device.circular_buffer, index ) 
    {
        total_size += entry->size;
    }
    mutex_unlock( &aesd_device.lock ); 

    switch( whence )
    {
        case SEEK_SET:
            newpos = offset;
            break;
        case SEEK_CUR:
            newpos = filp->f_pos + offset;
            break;
        case SEEK_END:
            newpos = total_size + offset;
            break;
        default:
            return -EINVAL;
    }

    if( newpos < 0 || newpos > total_size )
    {
        return -EINVAL;
    }

    filp->f_pos = newpos;
    return newpos;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;

    PDEBUG("ioctl cmd %u arg %lu", cmd, arg);

    /** handle ioctl */
    struct aesd_seekto seekto;

    if( _IOC_TYPE( cmd ) != AESD_IOC_MAGIC ) 
    {
        return -ENOTTY;
    }
    if( _IOC_NR( cmd ) > AESDCHAR_IOC_MAXNR ) 
    {
        return -ENOTTY;
    }

    switch( cmd ) 
    {
        case AESDCHAR_IOCSEEKTO:
            if( copy_from_user( &seekto, (const void __user *)arg, sizeof( struct aesd_seekto ) ) != 0 ) 
            {
                PDEBUG("copy_from_user failed");
                return -EFAULT;
            }

            struct aesd_buffer_entry *entry;
            size_t fpos = 0;
            int index;
            mutex_lock( &aesd_device.lock );

            AESD_CIRCULAR_BUFFER_FOREACH( entry, &aesd_device.circular_buffer, index ) 
            {
                if( index == seekto.write_cmd ) 
                {
                    break;
                }
                fpos += entry->size;
            }

            mutex_unlock( &aesd_device.lock );
            if( index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) 
            {
                return -EINVAL;
            }
            if( seekto.write_cmd_offset >= entry->size ) 
            {
                return -EINVAL;
            }

            fpos += seekto.write_cmd_offset;
            filp->f_pos = fpos;
            break;
        default:
            PDEBUG("unknown ioctl command %u (expected %u)", cmd, AESDCHAR_IOCSEEKTO);
            return -ENOTTY;
    }
    return retval;
}

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) 
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) 
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    if( msg_part.message != NULL ) 
    {
        kfree( msg_part.message );
    }

    int index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH( entry, &aesd_device.circular_buffer, index ) 
    {
        if( entry->buffptr != NULL ) 
        {
            kfree( (void *)entry->buffptr );
        }
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
