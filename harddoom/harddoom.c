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
	struct doombuff_data *cmd_buff;
	struct doombuff_files active_buff;
	uint32_t cmds_to_ping;
	uint64_t next_fence;
	struct fence_queue fence_q;
	struct list_head fput_q;
	struct completion empty_queue;
	struct mutex io;
	spinlock_t fput_queue;
};

struct fput_queue_node {
	struct doombuff_files f;
	struct list_head l;
	uint64_t fence;
};

struct dev_file_data {
	struct devdata *devdata;
	struct doombuff_files active_buff;
	spinlock_t act_buf_lock;
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

void doomdriv_reset_device(struct devdata *data)
{
	iowrite32(RESET_DEVICE, data->bar + RESET);
	iowrite32(data->cmd_buff->dma_pagetable >> 8, data->bar + CMD_PT);
	iowrite32(CMD_BUFF_SIZE, data->bar + CMD_SIZE);
	iowrite32(0, data->bar + CMD_READ_IDX);
	iowrite32(0, data->bar + CMD_WRITE_IDX);
	iowrite32(INTR_CLEAR, data->bar + INTR);
	iowrite32(INTR_CLEAR & ~(INTR_PONG_ASYNC | INTR_PONG_SYNC), data->bar + INTR_ENABLE);
	iowrite32(0, data->bar + FENCE_COUNTER);
	iowrite32(0, data->bar + FENCE_WAIT);
	iowrite32(START_DEVICE, data->bar + ENABLE);
}

DECLARE_BITMAP(doomdriv_minor_numbers, 256);

static int doomdriv_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err;
	struct devdata *data = (struct devdata*) kmalloc(sizeof(struct devdata), GFP_KERNEL);
	int minor_off = find_first_zero_bit(doomdriv_minor_numbers, 256);
	int code_len = ARRAY_SIZE(doomcode2);
	int i;
	printk(KERN_DEBUG "Probing yo HARDDOOM device!\n");
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
	if((err = request_irq(dev->irq, doomdev_irq_handler, IRQF_SHARED, "harddoom", dev)))
		goto err_irq;

	data->cmd_buff = doombuff_pagetable_create(&dev->dev,
			0, 0, CMD_BUFF_SIZE * 32, DOOMBUFF_ENABLED);
	if(IS_ERR(data->cmd_buff)) {
		err = PTR_ERR(data->cmd_buff);
		goto err_cmdbuff;
	}

	data->doom_device = &dev->dev;
	set_bit(minor_off, doomdriv_minor_numbers);
	init_completion(&data->empty_queue);
	mutex_init(&data->io);
	data->active_buff = DOOMBUFF_NO_FILES;
	data->cmds_to_ping = CMD_BUFF_PING_DIST;
	data->next_fence = 0;
	data->fence_q.acc_fence = 0;
	spin_lock_init(&data->fence_q.list_lock);
	spin_lock_init(&data->fput_queue);
	INIT_LIST_HEAD(&data->fence_q.queue);
	INIT_LIST_HEAD(&data->fput_q);
	pci_set_drvdata(dev, data);

	iowrite32(0, data->bar + FE_CODE_ADDR);
	for(i = 0; i < code_len; ++i)
		iowrite32(doomcode2[i], data->bar + FE_CODE_WINDOW);
	doomdriv_reset_device(data);
	printk(KERN_DEBUG "Yo HARDDOOM device is now running!\n");
	return 0;

err_cmdbuff:
	free_irq(dev->irq, dev);
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
	printk(KERN_DEBUG "Removing yo HARDDOOM device :(\n");
	clear_bit(data->major - doomdev_major, doomdriv_minor_numbers);
	mutex_destroy(&data->io);
	free_irq(dev->irq, dev);
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
	struct devdata *data = pci_get_drvdata(dev);
	struct fence_queue_node node;
	printk(KERN_DEBUG "HARDDOOM Going to sleep\n");
	spin_lock(&data->fence_q.list_lock);
	if(data->next_fence > data->fence_q.acc_fence) {
		node.fence = data->next_fence;
		init_completion(&node.event);
		list_add(&node.list, &data->fence_q.queue);
		iowrite32(data->next_fence, data->bar + FENCE_WAIT);
		spin_unlock(&data->fence_q.list_lock);
		wait_for_completion_killable(&node.event);
	} else spin_unlock(&data->fence_q.list_lock);
	iowrite32(0, data->bar + ENABLE);
	iowrite32(RESET_DEVICE, data->bar + RESET);
	iowrite32(0, data->bar + INTR_ENABLE);
	return 0;
}

static int doomdriv_resume(struct pci_dev *dev)
{
	struct devdata* data = pci_get_drvdata(dev);
	int code_len = ARRAY_SIZE(doomcode2);
	int i;
	iowrite32(0, data->bar + FE_CODE_ADDR);
	for(i = 0; i < code_len; ++i)
		iowrite32(doomcode2[i], data->bar + FE_CODE_WINDOW);
	doomdriv_reset_device(data->bar);
	return 0;
}

static void doomdriv_shutdown(struct pci_dev *dev)
{
	struct devdata* data = pci_get_drvdata(dev);
	iowrite32(0, data->bar + ENABLE);
}

static int doomfile_open(struct inode *ino, struct file *filep)
{
	struct dev_file_data *data = kmalloc(sizeof(struct dev_file_data), GFP_KERNEL);
	if(data == NULL) return -ENOMEM;
	data->active_buff = DOOMBUFF_NO_FILES;
	data->devdata = container_of(ino->i_cdev, struct devdata, doom_cdev);
	spin_lock_init(&data->act_buf_lock);
	filep->private_data = data;
	printk(KERN_DEBUG "Some madman opened HARDDOOM device file!\n");
	return 0;
}

static void doomdev_change_config(struct dev_file_data *fdata, uint32_t *writep, uint32_t pos, uint32_t flags) {
	struct devdata *data = fdata->devdata;
	unsigned long sflags;
	struct fput_queue_node *qnode = kmalloc(sizeof(struct fput_queue_node), GFP_KERNEL);
	spin_lock_irqsave(&data->fput_queue, sflags);
	++data->next_fence;
	qnode->fence = data->next_fence;
	qnode->f = data->active_buff;
	printk(KERN_DEBUG "HARDDOOM qnode to free, fence: %llu\n", qnode->fence);
	list_add_tail(&qnode->l, &data->fput_q);
	spin_unlock_irqrestore(&data->fput_queue, sflags);
	if(!cyc_between(lower_32_bits(data->fence_q.acc_fence),
			ioread32(data->bar + FENCE_COUNTER), lower_32_bits(qnode->fence)))
		iowrite32(lower_32_bits(qnode->fence), data->bar + FENCE_WAIT);
	doom_setup_cmd(writep, &fdata->active_buff, &data->active_buff, flags | CMD_FLAG_FENCE);
	data->active_buff = fdata->active_buff;
	iowrite32((pos + 1) % CMD_BUFF_SIZE, data->bar + CMD_WRITE_IDX);
}

static int doomfile_release(struct inode *ino, struct file *filep)
{
	struct dev_file_data *fdata = (struct dev_file_data*)filep->private_data;
	struct devdata *data = fdata->devdata;
	uint32_t pos;
	uint32_t *writep;
	int err;
	printk(KERN_DEBUG "All loosers closed HARDDOOM device file :(\n");
	//put active buffer files
	if(fdata->active_buff.surf_dst != NULL) fput(fdata->active_buff.surf_dst);
	if(fdata->active_buff.surf_src != NULL) fput(fdata->active_buff.surf_src);
	if(fdata->active_buff.texture != NULL) fput(fdata->active_buff.texture);
	if(fdata->active_buff.flat != NULL) fput(fdata->active_buff.flat);
	if(fdata->active_buff.translation != NULL) fput(fdata->active_buff.translation);
	if(fdata->active_buff.colormap != NULL) fput(fdata->active_buff.colormap);
	if(fdata->active_buff.tranmap != NULL) fput(fdata->active_buff.tranmap);
	//send empty setup, to clear file descriptors
	if((err = mutex_lock_killable(&data->io))) return err;
	pos = ioread32(data->bar + CMD_WRITE_IDX);
	fdata->active_buff = DOOMBUFF_NO_FILES;
	writep = (uint32_t *)(data->cmd_buff->cpu_pages[(pos * 32) /
		DOOMBUFF_PAGE_SIZE] + (pos * 32) % DOOMBUFF_PAGE_SIZE);
	doomdev_change_config(fdata, writep, pos, 0);
	mutex_unlock(&data->io);
	kfree(filep->private_data);
	return 0;
}

static ssize_t doomfile_write(struct file *file, const char __user *buf, size_t count, loff_t *filepos)
{
	ssize_t n = count / sizeof(struct doomdev2_cmd);
	struct dev_file_data *fdata = (struct dev_file_data*)file->private_data;
	struct devdata *data = fdata->devdata;
	int err;
	struct doomdev2_cmd *cmds = (struct doomdev2_cmd*)buf;
	int i = -1;
	uint32_t flags;
	uint32_t pos;
	uint32_t *writep;
	printk(KERN_DEBUG "Writing to yo HARDDOM device!\n");
	if(count % sizeof(struct doomdev2_cmd)) return -EINVAL;
	if(n == 0) return 0;
	if((err = mutex_lock_killable(&data->io))) return err;
	if(doombuff_files_eq(&data->active_buff, &fdata->active_buff))
		i = 0;

	//printk(KERN_DEBUG "HARDDOOM dupa %d %ld %d\n", i, n, i < n);
	while(i < n) {

		//printk(KERN_DEBUG "HARDDOOM dupa1 %d\n", i);

		if((ioread32(data->bar + CMD_WRITE_IDX) + 1) % CMD_BUFF_SIZE ==
				ioread32(data->bar + CMD_READ_IDX)) {
			iowrite32(INTR_PONG_ASYNC, data->bar + INTR);
			if((ioread32(data->bar + CMD_WRITE_IDX) + 1) % CMD_BUFF_SIZE ==
					ioread32(data->bar + CMD_READ_IDX)) {
				reinit_completion(&data->empty_queue);
				iowrite32(ioread32(data->bar + INTR_ENABLE) | INTR_PONG_ASYNC,
					data->bar + INTR_ENABLE);
				printk(KERN_DEBUG "HARDDOOM GOIN SLEEP\n");
				wait_for_completion_killable(&data->empty_queue);
				iowrite32(ioread32(data->bar + INTR_ENABLE) & ~INTR_PONG_ASYNC,
					data->bar + INTR_ENABLE);
				printk(KERN_DEBUG "HARDDOOM WAKE UP\n");
			}
		}
		for(pos = ioread32(data->bar + CMD_WRITE_IDX);
			(pos + 1) % CMD_BUFF_SIZE != ioread32(data->bar + CMD_READ_IDX) && i < n;
			pos = (pos + 1) % CMD_BUFF_SIZE, ++i) {

			flags = 0;
			writep = (uint32_t *)(data->cmd_buff->cpu_pages[(pos * 32) /
					DOOMBUFF_PAGE_SIZE] + (pos * 32) % DOOMBUFF_PAGE_SIZE);
			if(data->cmds_to_ping == 0) flags |= CMD_FLAG_PING_ASYNC;
			if(i + 1 == n) flags |= CMD_FLAG_FENCE;
			if(i == -1) {
				doomdev_change_config(fdata, writep, pos, 0);
				continue;
			}
			if((err = doom_write_cmd(writep, cmds[i], flags, fdata->active_buff))) {
				//printk(KERN_DEBUG "HARDDOOM dupa0\n");
				mutex_unlock(&data->io);
				if(i == 0) return err;
				printk(KERN_DEBUG "HARDDOOM partial write %u commands\n", i);
				return i * sizeof(struct doomdev2_cmd);
			}
			//printk(KERN_DEBUG "HARDDOOM dupa1\n");
			if(data->cmds_to_ping == 0) data->cmds_to_ping = CMD_BUFF_PING_DIST;
			else --data->cmds_to_ping;
			if(i + 1 == n) {
				++data->next_fence;
				((struct doombuff_data *)fdata->active_buff.surf_dst->private_data)
					->fence = data->next_fence;
			}
			//printk(KERN_DEBUG "HARDDOOM dupa2\n");
			iowrite32((pos + 1) % CMD_BUFF_SIZE, data->bar + CMD_WRITE_IDX);
		}
	}
	mutex_unlock(&data->io);
	printk(KERN_DEBUG "HARDDOOM written full %ld commands\n", n);
	return count;
}

static long doomfile_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dev_file_data *fdata = (struct dev_file_data*)file->private_data;
	struct devdata *data = fdata->devdata;
	struct device *dev = data->doom_device;
	printk(KERN_DEBUG "Ioctling yo HARDDOM device!\n");
	switch (cmd) {
	case DOOMDEV2_IOCTL_CREATE_SURFACE: {
		struct doomdev2_ioctl_create_surface *args = (struct doomdev2_ioctl_create_surface *)arg;
		if(args->width % 64 || args->width < 64 || args->height < 1) return -EINVAL;
		if(args->width > 2048 || args->height > 2048) return -EOVERFLOW;
		return doombuff_surface_create(&data->fence_q, dev, data->bar, args->width, args->height);
	}
	case DOOMDEV2_IOCTL_CREATE_BUFFER: {
		struct doomdev2_ioctl_create_buffer *args = (struct doomdev2_ioctl_create_buffer*) arg;
		if(args->size < 1) return -EINVAL;
		if(args->size > 1 << 22) return -EOVERFLOW;
		return doombuff_buffor_create(&data->fence_q, dev, data->bar, args->size);
	}
	case DOOMDEV2_IOCTL_SETUP: {
		struct doomdev2_ioctl_setup *args = (struct doomdev2_ioctl_setup *) arg;
		struct doombuff_files nbuff;
		struct doombuff_data *bdata;

		if(args->surf_dst_fd > 0) {
			if((nbuff.surf_dst = fget(args->surf_dst_fd)) == NULL)
				return -EINVAL;
			if(nbuff.surf_dst->f_op != &doombuff_fops)
				goto put_surf_dst;
			bdata = nbuff.surf_dst->private_data;
			if(bdata->width <= 0) goto put_surf_dst;
		} else nbuff.surf_dst = NULL;
		if(args->surf_src_fd > 0) {
			if((nbuff.surf_src = fget(args->surf_src_fd)) == NULL)
				goto put_surf_dst;
			if(nbuff.surf_src->f_op != &doombuff_fops)
				goto put_surf_src;
			bdata = nbuff.surf_src->private_data;
			if(bdata->width <= 0) goto put_surf_src;
		} else nbuff.surf_src = NULL;
		if(args->texture_fd > 0) {
			if((nbuff.texture = fget(args->texture_fd)) == NULL)
				goto put_surf_src;
			if(nbuff.texture->f_op != &doombuff_fops)
				goto put_texture;
		} else nbuff.texture = NULL;
		if(args->flat_fd > 0) {
			if((nbuff.flat = fget(args->flat_fd)) == NULL)
				goto put_texture;
			if(nbuff.flat->f_op != &doombuff_fops)
				goto put_flat;
			bdata = nbuff.flat->private_data;
			if(bdata->size % (1 << 12)) goto put_flat;
		} else nbuff.flat = NULL;
		if(args->translation_fd > 0) {
			if((nbuff.translation = fget(args->translation_fd)) == NULL)
				goto put_flat;
			if(nbuff.translation->f_op != &doombuff_fops)
				goto put_translation;
			bdata = nbuff.translation->private_data;
			if(bdata->size % (1 << 8)) goto put_translation;
		} else nbuff.translation = NULL;
		if(args->colormap_fd > 0) {
			if((nbuff.colormap = fget(args->colormap_fd)) == NULL)
				goto put_translation;
			if(nbuff.colormap->f_op != &doombuff_fops)
				goto put_colormap;
			bdata = nbuff.colormap->private_data;
			if(bdata->size % (1 << 8)) goto put_colormap;
		} else nbuff.colormap = NULL;
		if(args->tranmap_fd > 0) {
			if((nbuff.tranmap = fget(args->tranmap_fd)) == NULL)
				goto put_colormap;
			if(nbuff.tranmap->f_op != &doombuff_fops)
				goto put_tranmap;
			bdata = nbuff.tranmap->private_data;
			if(bdata->size != 1 << 16) goto put_tranmap;
		} else nbuff.tranmap = NULL;

		spin_lock(&fdata->act_buf_lock);

		if(fdata->active_buff.surf_dst != NULL) fput(fdata->active_buff.surf_dst);
		if(fdata->active_buff.surf_src != NULL) fput(fdata->active_buff.surf_src);
		if(fdata->active_buff.texture != NULL) fput(fdata->active_buff.texture);
		if(fdata->active_buff.flat != NULL) fput(fdata->active_buff.flat);
		if(fdata->active_buff.translation != NULL) fput(fdata->active_buff.translation);
		if(fdata->active_buff.colormap != NULL) fput(fdata->active_buff.colormap);
		if(fdata->active_buff.tranmap != NULL) fput(fdata->active_buff.tranmap);

		fdata->active_buff = nbuff;
		spin_unlock(&fdata->act_buf_lock);
		return 0;

put_tranmap:
		if(nbuff.tranmap != NULL) fput(nbuff.tranmap);
put_colormap:
		if(nbuff.colormap != NULL) fput(nbuff.colormap);
put_translation:
		if(nbuff.translation != NULL) fput(nbuff.translation);
put_flat:
		if(nbuff.flat != NULL) fput(nbuff.flat);
put_texture:
		if(nbuff.texture != NULL) fput(nbuff.texture);
put_surf_src:
		if(nbuff.surf_src != NULL) fput(nbuff.surf_src);
put_surf_dst:
		if(nbuff.surf_dst != NULL) fput(nbuff.surf_dst);
		return -EINVAL;
	}
	}
	return -EINVAL;
}

/*static inline uint64_t cyc_dist(int a, int b) {
	const uint64_t N = 1llu<< 32;
	return (N - a + b) % N;
}*/

irqreturn_t doomdev_irq_handler(int irq, void *dev) {
	struct devdata *data = pci_get_drvdata(dev);
	uint32_t intrs;
	unsigned long flags;
	//printk(KERN_DEBUG "HARDOOM interrupt");
	intrs = ioread32(data->bar + INTR);
	if(intrs == 0) return IRQ_NONE;
	//printk(KERN_DEBUG "HARDOOM interrupt: Intr register: 0x%x\n", intrs);
	if(intrs & INTR_FENCE) {
		uint64_t af, nf;
		struct list_head *acc, *pom;
		struct fence_queue_node *ac_node;
		struct fput_queue_node *qnode;
		printk(KERN_DEBUG "HARDDOOM FENCE\n");
		iowrite32(INTR_PONG_SYNC, data->bar + INTR);
		//printk(KERN_DEBUG "HARDDOOM puting buffer file\n");
		nf = data->fence_q.acc_fence;
		do {
			//Free waiting for reading
			spin_lock_irqsave(&data->fence_q.list_lock, flags);
			af = ((nf >> 32) << 32) + ioread32(data->bar + FENCE_COUNTER);
			if(af <= nf) af += 1llu << 32;
			nf = af;
			list_for_each_safe(acc, pom, &data->fence_q.queue) {
				ac_node = list_entry(acc, struct fence_queue_node, list);
				//printk(KERN_DEBUG "HARDDOOM fence node: %llu\n", ac_node->fence);
				if(ac_node->fence <= af) {
					list_del(&ac_node->list);
					complete(&ac_node->event);
				} else nf = min(nf, ac_node->fence);
			}
			data->fence_q.acc_fence = af;
			spin_unlock_irqrestore(&data->fence_q.list_lock, flags);
			//Free not used nodes
			spin_lock_irqsave(&data->fput_queue, flags);
			while(!list_empty(&data->fput_q)) {
				qnode = list_first_entry(&data->fput_q, struct fput_queue_node, l);
				if(qnode->fence > af) break;
				printk(KERN_DEBUG "HARDDOOM freeing qnode fence: %llu\n", qnode->fence);
				list_del(&qnode->l);
				if(qnode->f.surf_dst != NULL) fput(qnode->f.surf_dst);
				if(qnode->f.surf_src != NULL) fput(qnode->f.surf_src);
				if(qnode->f.texture != NULL) fput(qnode->f.texture);
				if(qnode->f.flat != NULL) fput(qnode->f.flat);
				if(qnode->f.translation != NULL) fput(qnode->f.translation);
				if(qnode->f.colormap != NULL) fput(qnode->f.colormap);
				if(qnode->f.tranmap != NULL) fput(qnode->f.tranmap);
				kfree(qnode);
			}
			spin_unlock_irqrestore(&data->fput_queue, flags);
			iowrite32(lower_32_bits(nf), data->bar + FENCE_WAIT);
			iowrite32(INTR_FENCE, data->bar + INTR);
			printk(KERN_DEBUG "HARDDOOM fence: af = %llu, nf = %llu, counter = %u\n", af, nf, ioread32(data->bar + FENCE_COUNTER));
		} while (!cyc_between(lower_32_bits(af), lower_32_bits(nf),
					ioread32(data->bar + FENCE_COUNTER)));
		printk(KERN_DEBUG "HARDDOOM FENCE finished af = %llu, nf = %llu counter = %u \n", af, nf, ioread32(data->bar + FENCE_COUNTER));
	} else if(intrs & INTR_PONG_SYNC) {
		printk(KERN_ERR "HARDDOOM INTR_PONG_SYNC sould be disabled\n");
	} else if(intrs & INTR_PONG_ASYNC) {
		printk(KERN_DEBUG "HARDDOOM PING - PONG PONG PONGING!!!");
		iowrite32(INTR_PONG_ASYNC, data->bar + INTR);
		complete(&data->empty_queue);
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
		//printk(KERN_DEBUG "Intr register: %x\n", intrs);
		doomdriv_reset_device(data->bar);
	}
	//printk(KERN_DEBUG "HARDOOM handled interrupt: Intr register: 0x%x\n", ioread32(data->bar + INTR));
	return IRQ_HANDLED;
}

static int harddoom_init(void)
{
	int err;
	printk(KERN_DEBUG "Initing yo HARDDOOM2 driver!\n");
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
	printk(KERN_DEBUG "Removing yo HARDDOOM2 driver :(\n");
	pci_unregister_driver(&doomdev_driver);
	class_unregister(&doomdev_class);
	unregister_chrdev_region(doomdev_major, 256);
}

module_init(harddoom_init);
module_exit(harddoom_cleanup);


