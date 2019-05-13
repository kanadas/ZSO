#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include "doomdev2.h"

MODULE_LICENSE("GPL");

struct devdata {
	dev_t major;
	struct device *doom_device;
	struct cdev doom_cdev;
	struct list_head dev_list;
};

static dev_t doomdev_major;
static struct list_head dev_list;
static struct class doomdev_class = {
	.name = "doomdev",
	.owner = THIS_MODULE,
};

static int doomdev_open(struct inode *ino, struct file *filep);
static int doomdev_release(struct inode *ino, struct file *filep);
static ssize_t doomdev_read(struct file *file, char __user *buf, size_t count, loff_t *filepos);
static long doomdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int doomdev_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void doomdev_remove(struct pci_dev *dev);
static int doomdev_suspend(struct pci_dev *dev, pm_message_t state);
static int doomdev_resume(struct pci_dev *dev);
static void doomdev_shutdown(struct pci_dev *dev);

static struct file_operations doomdev_fops = {
	.owner = THIS_MODULE,
	.read = doomdev_read,
	.open = doomdev_open,
	.unlocked_ioctl = doomdev_ioctl,
	.compat_ioctl = doomdev_ioctl,
	.release = doomdev_release,
};

static const struct pci_device_id doomdev_dev_ids[] = {{PCI_DEVICE(0x0666, 0x1994)}, {PCI_DEVICE(0,0)}};
static struct pci_driver doomdev_driver = {
	.name = "doomdev",
	.id_table = doomdev_dev_ids,
	.probe = doomdev_probe,
	.remove = doomdev_remove,
	.suspend = doomdev_suspend,
	.resume = doomdev_resume,
	.shutdown = doomdev_shutdown,
};

static int doomdev_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err;
	struct devdata *data = (struct devdata*) kmalloc(sizeof(struct devdata), GFP_KERNEL);
	data->major = doomdev_major;
	if(data == NULL) return -ENOMEM;
	cdev_init(&data->doom_cdev, &doomdev_fops);
	if ((err = cdev_add(&data->doom_cdev, doomdev_major, 1)))
		goto err_cdev;
	data->doom_device = device_create(&doomdev_class, &dev->dev, doomdev_major, 0, "doom%d", MINOR(doomdev_major));
	if (IS_ERR(data->doom_device)) {
		err = PTR_ERR(data->doom_device);
		goto err_device;
	}
	list_add(&data->dev_list, &dev_list);
	++doomdev_major;
	pci_set_drvdata(dev, data);
	return 0;

err_device:
	device_destroy(&doomdev_class, doomdev_major);
	class_unregister(&doomdev_class);
err_class:
	cdev_del(&data->doom_cdev);
err_cdev:
	unregister_chrdev_region(doomdev_major, 256);
	kfree(data);

	return err;
}

static void doomdev_remove(struct pci_dev *dev)
{
	struct devdata *data = pci_get_drvdata(dev);
	device_destroy(&doomdev_class, data->major);
	cdev_del(&data->doom_cdev);
	list_del(&data->dev_list);
	kfree(data);
}

static int doomdev_suspend(struct pci_dev *dev, pm_message_t state)
{
	return 0;
}

static int doomdev_resume(struct pci_dev *dev)
{
	return 0;
}

static void doomdev_shutdown(struct pci_dev *dev)
{
}

static int doomdev_open(struct inode *ino, struct file *filep)
{
	return 0;
}

static int doomdev_release(struct inode *ino, struct file *filep)
{
	return 0;
}

static ssize_t doomdev_read(struct file *file, char __user *buf, size_t count, loff_t *filepos)
{
/*	size_t file_len = hello_len * hello_repeats;
	loff_t pos = *filepos;
	loff_t end;
	if (pos >= file_len || pos < 0)
		return 0;
	if (count > file_len - pos)
		count = file_len - pos;
	end = pos + count;
	while (pos < end)
		if (put_user(hello_reply[pos++ % hello_len], buf++))
			return -EFAULT;
	*filepos = pos;
	return count;*/
	return 0;
}

static long doomdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
/*	if (cmd != HELLO_IOCTL_SET_REPEATS)
		return -ENOTTY;
	if (arg > HELLO_MAX_REPEATS)
		return -EINVAL;
	hello_repeats = arg;*/
	return 0;
}

static int doomdev_init(void)
{
	int err;
	if ((err = alloc_chrdev_region(&doomdev_major, 0, 256, "doomdev")))
		return err;
	if ((err = class_register(&doomdev_class))) {
		unregister_chrdev_region(doomdev_major, 256);
		return err;
	}
	if ((err = pci_register_driver(&doomdev_driver)))
		return err;
	INIT_LIST_HEAD(&dev_list);
	return 0;
}

static void doomdev_cleanup(void)
{
	//TODO cleanup every device instance
	pci_unregister_driver(&doomdev_driver);
	struct list_head *acc, *nxt;
	struct devdata *entry;
	list_for_each_safe(acc, nxt, &dev_list) {
		entry = (struct devdata*) list_entry(acc, struct devdata, dev_list);
		device_destroy(&doomdev_class, entry->major);
		cdev_del(&entry->doom_cdev);
		list_del(&entry->dev_list);
		kfree(entry);
	}
	class_unregister(&doomdev_class);
	unregister_chrdev_region(doomdev_major, 256);
}

module_init(doomdev_init);
module_exit(doomdev_cleanup);
