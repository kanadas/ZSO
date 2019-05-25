#ifndef DOOMBUFF
#define DOOMBUFF

#include <linux/kernel.h>
#include <linux/rwsem.h>
#include <linux/completion.h>

struct doombuff_data {
	struct device *dev;
	void __iomem *bar;
	dma_addr_t dma_pagetable;
	uint32_t *cpu_pagetable;
	size_t npages;
	char ** cpu_pages;
	size_t width, height, size;
	struct rw_semaphore sem;
	uint64_t fence;
	struct fence_queue *rwq;
};

struct fence_queue {
	atomic64_t acc_fence;
	struct list_head queue;
	spinlock_t list_lock;
};

struct fence_queue_node {
	struct list_head list;
	uint64_t fence;
	struct completion event;
};

extern struct file_operations doombuff_fops;

#define DOOMBUFF_ENABLED 1
#define DOOMBUFF_WRITABLE 2

int doombuff_surface_create(struct fence_queue *rwq, struct device *dev, void __iomem *bar, size_t width, size_t height);
int doombuff_buffor_create(struct fence_queue *rwq, struct device *dev, void __iomem *bar, uint32_t size);
struct doombuff_data *doombuff_pagetable_create(
		struct device *dev, size_t width, size_t height, uint32_t size, uint8_t flags);
void doombuff_pagetable_fee(struct doombuff_data *data);

#endif //DOOMBUFF
