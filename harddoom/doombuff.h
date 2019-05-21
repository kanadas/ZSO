#ifndef DOOMBUFF
#define DOOMBUFF

#include <linux/kernel.h>
#include <linux/rwsem.h>

struct doombuff_data {
	struct device *dev;
	dma_addr_t dma_pagetable;
	uint32_t *cpu_pagetable;
	size_t npages;
	char ** cpu_pages;
	size_t width, height, size;
	struct rw_semaphore sem;
};

#define DOOMBUFF_ENABLED 1
#define DOOMBUFF_WRITABLE 2

extern struct file_operations doombuff_fops;

int doombuff_surface_create(struct device *dev, size_t width, size_t height);
int doombuff_create(struct device *dev, uint32_t size, uint8_t flags);
dma_addr_t doombuff_pagetable(int fd);
struct doombuff_data *doombuff_get_data(int fd);

#endif //DOOMBUFF
