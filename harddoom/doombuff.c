#include "doombuff.h"

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/anon_inodes.h>
#include <linux/dma-mapping.h>

#define DOOMBUFF_PAGE_SIZE 4096

static ssize_t doombuff_read(struct file *file, char __user *buf, size_t count, loff_t *filepos);
static ssize_t doombuff_write(struct file *, const char __user *, size_t, loff_t *);
//static loff_t doombuff_llseek(struct file *, loff_t, int);
static int doombuff_release(struct inode *ino, struct file *filep);

static struct file_operations doombuff_fops = {
	.owner = THIS_MODULE,
	.read = doombuff_read,
	.write = doombuff_write,
//	.llseek = doombuff_llseek,
	.release = doombuff_release,
};

static ssize_t doombuff_read(struct file *file, char __user *buf, size_t count, loff_t *filepos)
{
	struct doombuff_data *data = (struct doombuff_data*)file->private_data;
	int pg = *filepos / DOOMBUFF_PAGE_SIZE;
	int offpg = *filepos % DOOMBUFF_PAGE_SIZE;
	size_t to_write = count;
	if(pg >= data->npages) return 0;
	memcpy(buf, data->cpu_pages[pg++] + offpg, min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg));
	count -= min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg);
	for(; count > 0 && pg < data->npages; count -= DOOMBUFF_PAGE_SIZE)
		memcpy(buf, data->cpu_pages[pg++], min(count, (size_t) DOOMBUFF_PAGE_SIZE));

	*filepos += to_write - max(count, 0UL);
	return to_write - max(count, 0UL);
}

static ssize_t doombuff_write(struct file *file, const char __user *buf, size_t count, loff_t *filepos)
{
	struct doombuff_data *data = (struct doombuff_data*)file->private_data;
	int pg = *filepos / DOOMBUFF_PAGE_SIZE;
	int offpg = *filepos % DOOMBUFF_PAGE_SIZE;
	size_t to_write = count;
	if(pg >= data->npages) return 0;
	memcpy(data->cpu_pages[pg++] + offpg, buf, min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg));
	count -= min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg);
	for(; count > 0 && pg < data->npages; count -= DOOMBUFF_PAGE_SIZE)
		memcpy(data->cpu_pages[pg++], buf, min(count, (size_t) DOOMBUFF_PAGE_SIZE));

	*filepos += to_write - max(count, 0UL);
	return to_write - max(count, 0UL);
}

/*static loff_t doombuff_llseek(struct file *file, loff_t off, int whence)
{

}*/

static int doombuff_release(struct inode *ino, struct file *filep)
{
	//TODO check if it is not used in device
	struct doombuff_data *data = (struct doombuff_data*)filep->private_data;
	int i;
	for(i = 0; i < data->npages; ++i)
		dma_free_coherent(data->dev, DOOMBUFF_PAGE_SIZE, data->cpu_pages[i],
			(data->cpu_pagetable[i] >> 6) << 12);
	kfree(data->cpu_pages);
	dma_free_coherent(data->dev, max(sizeof(uint32_t) * data->npages, 256UL), data->cpu_pagetable, data->dma_pagetable);
	kfree(data);
	return 0;
}

int doombuff_surface_create(struct device *dev, size_t width, size_t height)
{
	int fd = doombuff_create(dev, width * height, DOOMBUFF_ENABLED | DOOMBUFF_WRITABLE);
	struct doombuff_data *data;
	if(fd < 0) return fd;
	data = doombuff_get_data(fd);
	data->width = width;
	data->height = height;
	return fd;
}

int doombuff_create(struct device *dev, uint32_t size, uint8_t flags)
{
	int err = -ENOMEM;
	int npages = DIV_ROUND_UP(size, DOOMBUFF_PAGE_SIZE);
	int page = 0;
	struct doombuff_data *data;
	dma_addr_t dpage;
	if (npages > 1024) return -EINVAL;
	data = kmalloc(sizeof(struct doombuff_data), GFP_KERNEL);
	if(data == NULL) return -ENOMEM;
	data->npages = npages;
	data->size = size;
	data->width = 0;
	data->height = 0;
	data->cpu_pagetable = (uint32_t*) dma_alloc_coherent(dev,
		max(sizeof(uint32_t)*npages, 256UL), &data->dma_pagetable, GFP_KERNEL);
	if(data == NULL) goto err_pagetable;
	data->cpu_pages = kmalloc(sizeof(void*)*npages, GFP_KERNEL);
	if(data->cpu_pages == NULL) goto err_cpu_pages;
	for(; page < npages; ++page) {
		if((data->cpu_pages[page] =
			dma_alloc_coherent(dev, DOOMBUFF_PAGE_SIZE, &dpage, GFP_KERNEL))
			== NULL) goto err_page;
		data->cpu_pagetable[page] = (flags & 0xf) | (lower_32_bits(dpage >> 12) << 4);
	}
	if ((err = anon_inode_getfd("doombuff", &doombuff_fops, data, FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE)) < 0) goto err_page;

	return err;

err_page: {
	int i;
	for(i = 0; i < page; ++i)
		dma_free_coherent(dev, DOOMBUFF_PAGE_SIZE, data->cpu_pages[i],
			(data->cpu_pagetable[i] >> 6) << 12);
	kfree(data->cpu_pages);
}
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

struct doombuff_data *doombuff_get_data(int fd)
{
	return (struct doombuff_data*)fget(fd)->private_data;
}

