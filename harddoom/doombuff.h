#ifndef DOOMBUFF
#define DOOMBUFF

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdint.h>
#endif

struct doombuff_data {
	struct device *dev;
	dma_addr_t dma_pagetable;
	uint32_t *cpu_pagetable;
	size_t npages;
	void ** cpu_pages;
};

int doombuff_create(struct device *dev, uint32_t size);
dma_addr_t doombuff_pagetable(int fd);

#endif //DOOMBUFF
