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
#include "doomdefs.h"
#include "doomcode2.h"

MODULE_LICENSE("GPL");

struct devdata {
	dev_t major;
	struct device *doom_device;
	struct cdev doom_cdev;
	void __iomem* bar;
};

static dev_t doomdev_major;
static struct class doomdev_class = {
	.name = "harddoom",
	.owner = THIS_MODULE,
};

static ssize_t doomfile_read(struct file *file, char __user *buf, size_t count, loff_t *filepos);
static int doomfile_open(struct inode *ino, struct file *filep);
static long doomfile_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int doomfile_release(struct inode *ino, struct file *filep);

static int doomdriv_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void doomdriv_remove(struct pci_dev *dev);
static int doomdriv_suspend(struct pci_dev *dev, pm_message_t state);
static int doomdriv_resume(struct pci_dev *dev);
static void doomdriv_shutdown(struct pci_dev *dev);

static struct file_operations doomdev_fops = {
	.owner = THIS_MODULE,
	.read = doomfile_read,
	.open = doomfile_open,
	.unlocked_ioctl = doomfile_ioctl,
	.compat_ioctl = doomfile_ioctl,
	.release = doomfile_release,
};

static const struct pci_device_id doomdriv_dev_ids[] = {{PCI_DEVICE(0x0666, 0x1994)}, {PCI_DEVICE(0,0)}};
static struct pci_driver doomdev_driver = {
	.name = "harddoom",
	.id_table = doomdriv_dev_ids,
	.probe = doomdriv_probe,
	.remove = doomdriv_remove,
	.suspend = doomdriv_suspend,
	.resume = doomdriv_resume,
	.shutdown = doomdriv_shutdown,
};

static int doomdriv_probe(struct pci_dev *dev, const struct pci_device_id *id)
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
	if ((err = pci_enable_device(dev)))
		goto err_device;
	if ((err = pci_request_regions(dev, "harddoom")))
		goto err_regions;
	data->bar = pci_iomap(dev, 0, 1 << 13);
	if (IS_ERR(data->bar)) {
		err = PTR_ERR(data->bar);
		goto err_bar;
	}
	pci_set_master(dev);
	pci_set_dma_mask(dev, DMA_BIT_MASK(40));
	pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(40));

	++doomdev_major;
	pci_set_drvdata(dev, data);

	iowrite32(0, data->bar + FE_CODE_ADDR);
	int code_len = ARRAY_SIZE(doomcode2);
	for(int i = 0; i < code_len; ++i)
		iowrite32(doomcode2[i], data->bar + FE_CODE_WINDOW);
	iowrite32(RESET_DEVICE, data->bar + RESET);
	//initialize cmd_pt, size, idx to use order buffer
	iowrite32(INTR_CLEAR, data->bar + INTR);
	//enable interrupts
	iowrite32(0, data->bar + FENCE_COUNTER);
	iowrite32(START_DEVICE & !ENABLE_FETCH, data->bar + ENABLE); //Disable order buffer
	return 0;

err_bar:
	pci_release_regions(dev);
err_regions:
	pci_disable_device(dev);
err_device:
	device_destroy(&doomdev_class, data->major);
err_class:
	cdev_del(&data->doom_cdev);
err_cdev:
	kfree(data);

	return err;
}

static void doomdriv_remove(struct pci_dev *dev)
{
	struct devdata *data = pci_get_drvdata(dev);
	pci_clear_master(dev);
	pci_iounmap(dev, data->bar);
	pci_release_regions(dev);
	pci_disable_device(dev);
	device_destroy(&doomdev_class, data->major);
	cdev_del(&data->doom_cdev);
	kfree(data);
}

static int doomdriv_suspend(struct pci_dev *dev, pm_message_t state)
{
	return 0;
}

static int doomdriv_resume(struct pci_dev *dev)
{
	return 0;
}

static void doomdriv_shutdown(struct pci_dev *dev)
{
}

static int doomfile_open(struct inode *ino, struct file *filep)
{
	return 0;
}

static int doomfile_release(struct inode *ino, struct file *filep)
{
	return 0;
}

static ssize_t doomfile_read(struct file *file, char __user *buf, size_t count, loff_t *filepos)
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



static long doomfile_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
/*	if (cmd != HELLO_IOCTL_SET_REPEATS)
		return -ENOTTY;
	if (arg > HELLO_MAX_REPEATS)
		return -EINVAL;
	hello_repeats = arg;*/
	return 0;
}

static int harddoom_init(void)
{
	int err;
	if ((err = alloc_chrdev_region(&doomdev_major, 0, 256, "harddoom")))
		return err;
	if ((err = class_register(&doomdev_class))) {
		unregister_chrdev_region(doomdev_major, 256);
		return err;
	}
	if ((err = pci_register_driver(&doomdev_driver)))
		return err;
	return 0;
}

static void harddoom_cleanup(void)
{
	pci_unregister_driver(&doomdev_driver);
	class_unregister(&doomdev_class);
	unregister_chrdev_region(doomdev_major, 256);
}

module_init(harddoom_init);
module_exit(harddoom_cleanup);
