#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#include "harddoom2.h"
#include "doomdev2.h"
#include "doomdefs.h"
#include "doomcode2.h"
#include "doombuff.h"
#include "doomcmds.h"

MODULE_LICENSE("GPL");

struct devdata {
	dev_t major;
	struct device *doom_file;
	struct device *doom_device;
	struct cdev doom_cdev;
	void __iomem* bar;
	struct doomdev2_ioctl_setup active_buff;
	struct doombuff_sizes buff_size;
	struct completion write;
	struct mutex io;
};

static dev_t doomdev_major;
static struct class doomdev_class = {
	.name = "harddoom",
	.owner = THIS_MODULE,
};

static ssize_t doomfile_write(struct file *file, const char __user *buf, size_t count, loff_t *filepos);
static int doomfile_open(struct inode *ino, struct file *filep);
static long doomfile_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int doomfile_release(struct inode *ino, struct file *filep);

static int doomdriv_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void doomdriv_remove(struct pci_dev *dev);
static int doomdriv_suspend(struct pci_dev *dev, pm_message_t state);
static int doomdriv_resume(struct pci_dev *dev);
static void doomdriv_shutdown(struct pci_dev *dev);

irqreturn_t doomdev_irq_handler(int irq, void *dev);

static struct file_operations doomdev_fops = {
	.owner = THIS_MODULE,
	.write = doomfile_write,
	.open = doomfile_open,
	.unlocked_ioctl = doomfile_ioctl,
	.compat_ioctl = doomfile_ioctl,
	.release = doomfile_release,
	.llseek = no_llseek
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

void doomdriv_reset_device(void __iomem* bar)
{
	iowrite32(RESET_DEVICE, bar + RESET);
	//initialize cmd_pt, size, idx to use order buffer
	iowrite32(INTR_CLEAR, bar + INTR);
	//enabling all interrupts except fence and pongs
	iowrite32(INTR_CLEAR & !(INTR_FENCE | INTR_PONG_ASYNC), bar + INTR_ENABLE);
	iowrite32(0, bar + FENCE_COUNTER);
	iowrite32(START_DEVICE & !ENABLE_FETCH, bar + ENABLE); //Disable order buffer

}

DECLARE_BITMAP(doomdriv_minor_numbers, 256);

static int doomdriv_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err;
	struct devdata *data = (struct devdata*) kmalloc(sizeof(struct devdata), GFP_KERNEL);
	int minor_off = find_first_zero_bit(doomdriv_minor_numbers, 256);
	int code_len = ARRAY_SIZE(doomcode2);
	int i;
	printk(KERN_INFO "Probing yo HARDDOOM device!\n");
	if(data == NULL) return -ENOMEM;
	data->major = doomdev_major + minor_off;
	cdev_init(&data->doom_cdev, &doomdev_fops);
	if ((err = cdev_add(&data->doom_cdev, data->major, 1)))
		goto err_cdev;
	data->doom_file = device_create(&doomdev_class, &dev->dev, data->major, 0, "doom%d", minor_off);
	if (IS_ERR(data->doom_file)) {
		err = PTR_ERR(data->doom_file);
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
	if((err = request_irq(dev->irq, doomdev_irq_handler, IRQF_SHARED, "harddoom", dev/*TODO*/)))
		goto err_irq;

	data->doom_device = &dev->dev;
	set_bit(minor_off, doomdriv_minor_numbers);
	init_completion(&data->write);
	mutex_init(&data->io);
	data->active_buff = (struct doomdev2_ioctl_setup) {-1,-1,-1,-1,-1,-1,-1};
	data->buff_size = DOOMBUFF_CLEAR_SIZES;
	pci_set_drvdata(dev, data);

	iowrite32(0, data->bar + FE_CODE_ADDR);
	for(i = 0; i < code_len; ++i)
		iowrite32(doomcode2[i], data->bar + FE_CODE_WINDOW);
	doomdriv_reset_device(data->bar);
	printk(KERN_INFO "Yo HARDDOOM device is now running!\n");
	return 0;

err_irq:
	pci_clear_master(dev);
	pci_iounmap(dev, data->bar);
err_bar:
	pci_release_regions(dev);
err_regions:
	pci_disable_device(dev);
err_device:
	device_destroy(&doomdev_class, data->major);
	cdev_del(&data->doom_cdev);
err_cdev:
	kfree(data);

	return err;
}

static void doomdriv_remove(struct pci_dev *dev)
{
	struct devdata *data = pci_get_drvdata(dev);
	printk(KERN_INFO "Removing yo HARDDOOM device :(\n");
	clear_bit(data->major - doomdev_major, doomdriv_minor_numbers);
	mutex_destroy(&data->io);
	//completion destroy??
	free_irq(dev->irq, dev/*TODO*/);
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
	//TODO
	return 0;
}

static int doomdriv_resume(struct pci_dev *dev)
{
	//TODO
	return 0;
}

static void doomdriv_shutdown(struct pci_dev *dev)
{
	//TODO
}

static int doomfile_open(struct inode *ino, struct file *filep)
{
	//get devdata struct
	filep->private_data = container_of(ino->i_cdev, struct devdata, doom_cdev);
	printk(KERN_INFO "Some madman opened HARDDOOM device file!\n");
	return 0;
}

static int doomfile_release(struct inode *ino, struct file *filep)
{
	return 0;
}

static ssize_t doomfile_write(struct file *file, const char __user *buf, size_t count, loff_t *filepos)
{
	size_t n = count / sizeof(struct doomdev2_cmd);
	struct devdata *data = (struct devdata*)file->private_data;
	int err;
	struct doomdev2_cmd *cmds = (struct doomdev2_cmd*)buf;
	uint32_t *(words[2])[8] = {0};
	int i = 1;
	printk(KERN_INFO "Writing to yo HARDDOM device!\n");
	if(count % sizeof(struct doomdev2_cmd)) return -EINVAL;
	if(n == 0) return 0;
	if((err = mutex_lock_killable(&data->io))) return err;
	if((err = doom_write_cmd(*words[0], cmds[0], 0, data->active_buff, data->buff_size))) {
		mutex_unlock(&data->io);
		return err;
	}
	while(i < n) {
		while((err = doom_write_cmd(*words[1], cmds[i], 0,
				data->active_buff, data->buff_size)) &&
				(i + 1) % QUEUE_SIZE && i + 1 < n) {
			doom_send_cmd(data->bar, *words[0]);
			*words[0] = *words[1]; //TODO check it: thats magic type
			++i;
		}
		if(err) {
			if(i % QUEUE_SIZE) {
				mutex_unlock(&data->io);
				return i * sizeof(struct doomdev2_cmd);
			}
			*words[0][0] |= CMD_FLAG_PING_SYNC;
			reinit_completion(&data->write);
			doom_send_cmd(data->bar, *words[0]);
			wait_for_completion_killable(&data->write);
			mutex_unlock(&data->io);
			return (i + 1) * sizeof(struct doomdev2_cmd);
		}
		++i;
	}
	mutex_unlock(&data->io);
	printk(KERN_INFO "Finished writing to yo HARDDOM device!\n");
	return count;
}

static long doomfile_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct devdata *data = (struct devdata*)(file->private_data);
	struct device *dev = data->doom_device;
	printk(KERN_INFO "Ioctling yo HARDDOM device!\n");
	switch (cmd) {
	case DOOMDEV2_IOCTL_CREATE_SURFACE: {
		struct doomdev2_ioctl_create_surface *args = (struct doomdev2_ioctl_create_surface *)arg;
		if(args->width % 64 || args->width < 64 || args->height < 1) return -EINVAL;
		if(args->width > 2048 || args->height > 2048) return -EOVERFLOW;
		return doombuff_create(dev, args->width * args->height, DOOMBUFF_ENABLED | DOOMBUFF_WRITABLE);
	}
	case DOOMDEV2_IOCTL_CREATE_BUFFER: {
		struct doomdev2_ioctl_create_buffer *args = (struct doomdev2_ioctl_create_buffer*) arg;
		if(args->size < 1) return -EINVAL;
		if(args->size > 1 << 22) return -EOVERFLOW;
		return doombuff_create(dev, args->size, DOOMBUFF_ENABLED);
	}
	case DOOMDEV2_IOCTL_SETUP: {
		struct doomdev2_ioctl_setup *args = (struct doomdev2_ioctl_setup *) arg;
		long err;
		struct file *surf_dst = fget(args->surf_dst_fd);
		struct file *surf_src = fget(args->surf_src_fd);
		struct file *texture = fget(args->texture_fd);
		struct file *flat = fget(args->flat_fd);
		struct file *colormap = fget(args->colormap_fd);
		struct file *translation = fget(args->translation_fd);
		struct file *tranmap = fget(args->tranmap_fd);

		printk(KERN_DEBUG "HARDDOOM: ioctl setup arg: %d %d %d %d %d %d %d\n", args->surf_dst_fd, args->surf_src_fd, args->texture_fd, args->flat_fd, args->translation_fd, args->colormap_fd, args->tranmap_fd);

		printk(KERN_DEBUG "HARDDOOM: surf_dst: %lx, surf_dst->f_op: %lx, doomdev_fops: %lx\n", surf_dst, surf_dst->f_op, &doombuff_fops);

		if(args->surf_dst_fd > 0 && (IS_ERR_OR_NULL(surf_dst) ||
			surf_dst->f_op != &doombuff_fops))
			return -EINVAL;

		printk(KERN_DEBUG "HARDDODODOODODDOM dupa1\n");

		if(args->surf_src_fd > 0 && (IS_ERR_OR_NULL(surf_src) ||
			surf_src->f_op != &doombuff_fops))
			return -EINVAL;
		if(args->texture_fd > 0 &&  (IS_ERR_OR_NULL(texture) ||
			surf_dst->f_op != &doombuff_fops))
			return -EINVAL;

		printk(KERN_DEBUG "HARDODODODODODOM dupa2\n");

		if(args->flat_fd > 0 &&  (IS_ERR_OR_NULL(flat) ||
			flat->f_op != &doombuff_fops))
			return -EINVAL;
		if(args->colormap_fd > 0 &&  (IS_ERR_OR_NULL(colormap) ||
			colormap->f_op != &doombuff_fops))
			return -EINVAL;
		if(args->translation_fd > 0 &&  (IS_ERR_OR_NULL(translation) ||
			translation->f_op != &doombuff_fops))
			return -EINVAL;
		if(args->tranmap_fd > 0 &&  (IS_ERR_OR_NULL(tranmap) ||
			tranmap->f_op != &doombuff_fops))
			return -EINVAL;
		if((err = mutex_lock_killable(&data->io))) return err;


		printk(KERN_DEBUG "HARDODODODODODOM dupa3\n");

		err = doom_setup_cmd(data->bar, *args, 0, &data->active_buff, &data->buff_size);
		mutex_unlock(&data->io);
		return err;
	}
	}
	return -EINVAL;
}

irqreturn_t doomdev_irq_handler(int irq, void *dev) {
	struct devdata *data;
	uint32_t intrs;
	if(((struct pci_dev *)dev)->driver != &doomdev_driver) return IRQ_NONE;
	data = pci_get_drvdata(dev);
	intrs = ioread32(data->bar + INTR);
	if(intrs & INTR_FENCE) {
		//Not using
	} else if(intrs & INTR_PONG_SYNC) {
		printk(KERN_INFO "HARDDOOM ping-pong");
		complete(&data->write);
	} else if(intrs & INTR_PONG_ASYNC) {
		//Not using
	} else {
		char error[25];
		if(intrs & INTR_FE_ERROR) strcpy(error, "FE_ERROR");
		else if(intrs & INTR_CMD_OVERFLOW) strcpy(error, "CMD_OVERFLOW");
		else if(intrs & INTR_SURF_DST_OVERFLOW) strcpy(error, "SURF_DST_OVERFLOW");
		if(intrs & INTR_SURF_SRC_OVERFLOW) strcpy(error, "SURF_SRC_OVERFLOW");
		if(intrs & INTR_PAGE_FAULT_CMD) strcpy(error, "PAGE_FAULT_CMD");
		if(intrs & INTR_PAGE_FAULT_SURF_DST) strcpy(error, "PAGE_FAULT_SURF_DST");
		if(intrs & INTR_PAGE_FAULT_SURF_SRC) strcpy(error, "PAGE_FAULT_SURF_SRC");
		if(intrs & INTR_PAGE_FAULT_TEXTURE) strcpy(error, "PAGE_FAULT_TEXTURE");
		if(intrs & INTR_PAGE_FAULT_FLAT) strcpy(error, "PAGE_FAULT_FLAT");
		if(intrs & INTR_PAGE_FAULT_TRANSLATION) strcpy(error, "PAGE_FAULT_TRANSLATION");
		if(intrs & INTR_PAGE_FAULT_COLORMAP) strcpy(error, "PAGE_FAULT_COLORMAP");
		if(intrs & INTR_PAGE_FAULT_TRANMAP) strcpy(error, "PAGE_FAULT_TRANMAP");
		printk(KERN_ERR "Doom device error: %s. Restarting device.\n", error);
		printk(KERN_DEBUG "Intr register: %x\n", intrs);
		doomdriv_reset_device(data->bar);
	}
	return IRQ_HANDLED;
}

static int harddoom_init(void)
{
	int err;
	printk(KERN_INFO "Initing yo HARDDOOM2 driver!\n");
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
	printk(KERN_INFO "Removing yo HARDDOOM2 driver :(\n");
	pci_unregister_driver(&doomdev_driver);
	class_unregister(&doomdev_class);
	unregister_chrdev_region(doomdev_major, 256);
}

module_init(harddoom_init);
module_exit(harddoom_cleanup);

