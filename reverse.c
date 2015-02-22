#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

/* A beginner's Linux kernel module.
    (Based on an incomplete tutorial in Linux Voice magazine.) */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scott Wakeling <scott@diskfish.org>");
MODULE_DESCRIPTION("In-kernel phrase reverser.");

static unsigned long buffer_size = 8192;
module_param(buffer_size, ulong, (S_IRUSR | S_IRGRP | S_IROTH));
MODULE_PARM_DESC(buffer_size, "Internal buffer size");

struct buffer {
	char *data, *end, *read_ptr;
	unsigned long size;
	wait_queue_head_t read_queue;
	struct mutex lock;
};

static struct buffer *buffer_alloc(unsigned long size)
{
	/* Keep working buffer in normal kernel mem, not DMA or anything.. */
	struct buffer *buf;
	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (unlikely(!buf))
		goto out;
	buf->data = kzalloc(size, GFP_KERNEL);
	if (unlikely(!buf->data))
		goto out;
	init_waitqueue_head(&buf->read_queue);
	mutex_init(&buf->lock);
out:
	return buf;
}

static void buffer_free(struct buffer *buf)
{
	kfree(buf->data);
	kfree(buf);
}

static void reverse_phrase(char * const from, size_t size)
{
	int i, j;
	printk(KERN_INFO "reversing phrase %lu bytes long", size);
	for (i = 0; i < (size/2); i++) {
		j = i + (size-(i*2)) - 1;
		from[i] ^= from[j];
		from[j] ^= from[i];
		from[i] ^= from[j];	
	}
}

static int reverse_open(struct inode *inode, struct file *file)
{
	int err = 0;
	file->private_data = buffer_alloc(buffer_size);	
	return err;
}

static int reverse_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "releasing private_data in modreverse\n");
	buffer_free(file->private_data);
	return 0;
}

/* Returns number of bytes read or an (-ve.) error code */
static ssize_t reverse_read(struct file *file, char __user *out, size_t size, loff_t *off)
{
	struct buffer *buf = file->private_data;
	ssize_t result;
	/* Sleep until the mutex is available.. */
	if (mutex_lock_interruptible(&buf->lock)) {
		/* ..interrupted */
		result = -ERESTARTSYS;
		goto out;
	}
	/* LOCK START */
	/* While there's nothing to read.. */
	while (buf->read_ptr == buf->end) {
		/* If non-blocking IO is requested go back.. */
		if (file->f_flags & O_NONBLOCK) {
			result = -EAGAIN;
			goto out_unlock;
		}
		/* Release lock and wait on read_queue so reverse_write can work */
		mutex_unlock(&buf->lock);
		/* LOCK STOP */
		if (wait_event_interruptible_timeout(buf->read_queue, buf->read_ptr != buf->end, 1*HZ) <= 0) {
			/* Timed out or interrupted.. */
			result = -ERESTARTSYS;
			goto out;
		}
		/* Something's come in; lock so no writing takes place while we read */
		if (mutex_lock_interruptible(&buf->lock)) {
			result = -ERESTARTSYS;
			goto out;
		}
		/* LOCK START */
	}
	size = min(size, (size_t)(buf->end - buf->read_ptr));
	printk(KERN_INFO "read %lu bytes\n", size);
	if (copy_to_user(out, buf->read_ptr, size)) {
		result = -EFAULT;
		goto out_unlock;
	}
	buf->read_ptr += size;
	result = size;
out_unlock:
	mutex_unlock(&buf->lock);
	/* LOCK STOP */
out:
	return result;
}

static ssize_t reverse_write(struct file *file, const char __user *from, size_t size, loff_t *off)
{
	/* The file has been opened and has an allocated buffer, point at that.. */
	struct buffer *buf = file->private_data;
	size_t result;
	printk(KERN_INFO "reversing %lu bytes from user space\n", size);	
	if (mutex_lock_interruptible(&buf->lock)) {
		result = -ERESTARTSYS;
		goto out;
	}
	/* LOCK START */
	if (copy_from_user(buf->data, from, size)) {
		result = -EFAULT;
		goto out_unlock;
	}
	result = size;
	buf->end = buf->data + size;	
	buf->read_ptr = buf->data;
	if (buf->end > buf->data) {
		reverse_phrase(buf->data, buf->end - buf->data);
		wake_up_interruptible(&buf->read_queue);
		mutex_unlock(&buf->lock);
		/* LOCK STOP */
		goto out;
	}
out_unlock:
	mutex_unlock(&buf->lock);
	/* LOCK STOP */
out:
	return result;
}

static struct file_operations reverse_fops = {
	.owner = THIS_MODULE,
	.open = reverse_open,
	.llseek = noop_llseek,
	.read = &reverse_read,
	.write = &reverse_write,
	.release = &reverse_release
};

static struct miscdevice reverse_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "reverse",
	.fops = &reverse_fops,
};

static int __init reverse_init(void)
{
	if (!buffer_size)
		return -1;
	misc_register(&reverse_misc_device);
	printk(KERN_INFO
	       "reverse device has been registered, buffer size is %lu bytes\n",
	       buffer_size);
	return 0;
}

static void __exit reverse_exit(void)
{
	misc_deregister(&reverse_misc_device);
	printk(KERN_INFO "reverse device has been unregistered\n");
}

module_init(reverse_init);
module_exit(reverse_exit);
