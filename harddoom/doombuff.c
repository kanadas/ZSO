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
static loff_t doombuff_llseek(struct file *, loff_t, int);
static int doombuff_release(struct inode *ino, struct file *filep);

struct file_operations doombuff_fops = {
	.owner = THIS_MODULE,
	.read = doombuff_read,
	.write = doombuff_write,
	.llseek = doombuff_llseek,
	.release = doombuff_release,
};

static ssize_t doombuff_read(struct file *file, char __user *buf, size_t count, loff_t *filepos)
{
	struct doombuff_data *data = (struct doombuff_data*)file->private_data;
	int pg = *filepos / DOOMBUFF_PAGE_SIZE;
	int offpg = *filepos % DOOMBUFF_PAGE_SIZE;
	size_t to_write = count;
	if(pg >= data->npages) return 0;
	down_read(&data->sem);
	printk(KERN_DEBUG "HARDDOOM reading from buffer file\n");
	memcpy(buf, data->cpu_pages[pg++] + offpg, min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg));
	count -= min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg);
	for(; count > 0 && pg < data->npages; count -= DOOMBUFF_PAGE_SIZE)
		memcpy(buf, data->cpu_pages[pg++], min(count, (size_t) DOOMBUFF_PAGE_SIZE));
	*filepos += to_write - max(count, 0UL);
	up_read(&data->sem);
	printk(KERN_DEBUG "HARDDOOM read %lu bytes from buffer file\n", to_write - max(count, 0UL));
	return to_write - max(count, 0UL);
}

static ssize_t doombuff_write(struct file *file, const char __user *buf, size_t count, loff_t *filepos)
{
	struct doombuff_data *data = (struct doombuff_data*)file->private_data;
	int pg = *filepos / DOOMBUFF_PAGE_SIZE;
	int offpg = *filepos % DOOMBUFF_PAGE_SIZE;
	size_t to_write = count;
	if(pg >= data->npages) return 0;
	down_write(&data->sem);
	printk(KERN_DEBUG "HARDDOOM writing to buffer file\n");
	memcpy(data->cpu_pages[pg++] + offpg, buf, min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg));
	count -= min(count, (size_t) DOOMBUFF_PAGE_SIZE - offpg);
	for(; count > 0 && pg < data->npages; count -= DOOMBUFF_PAGE_SIZE)
		memcpy(data->cpu_pages[pg++], buf, min(count, (size_t) DOOMBUFF_PAGE_SIZE));
	*filepos += to_write - max(count, 0UL);
	up_write(&data->sem);
	printk(KERN_DEBUG "HARDDOOM read %lu bytes from buffer file\n", to_write - max(count, 0UL));
	return to_write - max(count, 0UL);
}

static loff_t doombuff_llseek(struct file *file, loff_t off, int whence)
{
	struct doombuff_data *data = (struct doombuff_data*)file->private_data;
	printk(KERN_DEBUG "HARDDOOM llseek buffer file\n");
	switch(whence) {
	case SEEK_SET:
		if(off < 0 || off >= data->size) return -EOVERFLOW;
		file->f_pos = off;
		return 0;
	case SEEK_CUR:
		if(off < -file->f_pos || file->f_pos + off >= data->size) return -EOVERFLOW;
		file->f_pos += off;
		return 0;
	case SEEK_END:
		if(off >= 0 || off < -data->size) return -EOVERFLOW;
		file->f_pos = data->size + off;
		return 0;
	}
	return -EINVAL;
}

static int doombuff_release(struct inode *ino, struct file *filep)
{
	//TODO check if it is not used in device
	struct doombuff_data *data = (struct doombuff_data*)filep->private_data;
	int i;
	printk(KERN_DEBUG "HARDDOOM releasing buffer file\n");
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
	int fd;
	struct file *fp;
	int npages = DIV_ROUND_UP(size, DOOMBUFF_PAGE_SIZE);
	int page = 0;
	struct doombuff_data *data;
	dma_addr_t dpage;
	printk(KERN_DEBUG "HARDDOOM creating buffer file\n");
	if (npages > 1024) return -EINVAL;
	data = kmalloc(sizeof(struct doombuff_data), GFP_KERNEL);
	if(data == NULL) return -ENOMEM;
	data->npages = npages;
	data->size = size;
	data->width = 0;
	data->height = 0;
	init_rwsem(&data->sem);
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
		//printk(KERN_DEBUG "HARDDOOM buffer pagetable: page %d: %x\n", page, data->cpu_pagetable[page]);
	}
	if((fd = get_unused_fd_flags(O_RDWR)) < 0) {
		err = fd;
		goto err_page;
	}
	fp = anon_inode_getfile("doombuff", &doombuff_fops, data, O_RDWR);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		goto err_getfile;
	}
	fp->f_mode |= (FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	fd_install(fd, fp);
	printk(KERN_DEBUG "HARDDOOM created buffer : %d\n", err);
	return fd;

err_getfile:
	put_unused_fd(fd);
err_page: {
	int i;
	for(i = 0; i < page; ++i) {
		dma_free_coherent(dev, DOOMBUFF_PAGE_SIZE, data->cpu_pages[i],
			(data->cpu_pagetable[i] >> 6) << 12);
	}
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

