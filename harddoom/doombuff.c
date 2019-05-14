#include "doombuff.h"

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/anon_inodes.h>
#include <linux/dma-mapping.h>

static ssize_t doombuff_read(struct file *file, char __user *buf, size_t count, loff_t *filepos);
static ssize_t doombuff_write(struct file *, const char __user *, size_t, loff_t *);
static loff_t doombuff_llseek(struct file *, loff_t, int);
static int doombuff_release(struct inode *ino, struct file *filep);

static struct file_operations doombuff_fops = {
	.owner = THIS_MODULE,
	.read = doombuff_read,
	.write = doombuff_write,
	.llseek = doombuff_llseek,
	.release = doombuff_release,
};

static ssize_t doombuff_read(struct file *file, char __user *buf, size_t count, loff_t *filepos)
{

}

static ssize_t doombuff_write(struct file *file, const char __user *buf, size_t count, loff_t *filepos)
{

}

static loff_t doombuff_llseek(struct file *file, loff_t off, int whence)
{

}

static int doombuff_release(struct inode *ino, struct file *filep)
{

}

//flags: enabled, writable, 0, 0
int doombuff_create(struct device *dev, uint32_t size, uint8_t flags)
{
	int err = -ENOMEM;
	int npages = DIV_ROUND_UP(size, 4096);
	if (npages > 1024) return -EINVAL;
	int page = 0;
	struct doombuff_data *data = kmalloc(sizeof(struct doombuff_data), GFP_KERNEL);
	if(data == NULL) return -ENOMEM;
	data->npages = npages;
	data->cpu_pagetable = (uint32_t*) dma_alloc_coherent(dev,
		max(sizeof(uint32_t)*npages, 256UL), &data->dma_pagetable, GFP_KERNEL);
	if(data == NULL) goto err_pagetable;
	data->cpu_pages = kmalloc(sizeof(void*)*npages, GFP_KERNEL);
	if(data->cpu_pages == NULL) goto err_cpu_pages;
	dma_addr_t dpage;
	for(; page < npages; ++page) {
		if((data->cpu_pages[page] = dma_alloc_coherent(dev, 4096, &dpage, GFP_KERNEL))
			== NULL) goto err_page;
		data->cpu_pagetable[page] = (flags & 0xf) | (lower_32_bits(dpage >> 12) << 4);
	}
	if ((err = anon_inode_getfd("doombuff", &doombuff_fops, data, FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE)) < 0) goto err_page;

	return err;

err_page:
	for(int i = 0; i < page; ++i)
		dma_free_coherent(dev, 4096, data->cpu_pages[i],
			(data->cpu_pagetable[page] >> 4) << 12);
	kfree(data->cpu_pages);
err_cpu_pages:
	dma_free_coherent(dev, max(sizeof(uint32_t)*npages, 256UL), data->cpu_pagetable, data->dma_pagetable);
err_pagetable:
	kfree(data);

	return err;
}

dma_addr_t doombuff_pagetable(int fd)
{
	return ((struct doombuff_data*)fget(fd)->private_data)->dma_pagetable;
}
