#ifndef DOOMCMDS
#define DOOMCMDS

#include <linux/kernel.h>
#include "doomdev2.h"
#include "doombuff.h"

struct doombuff_files {
	struct file *surf_dst;
	struct file *surf_src;
	struct file *texture;
	struct file *flat;
	struct file *colormap;
	struct file *translation;
	struct file *tranmap;
};

struct fput_queue_node {
	struct doombuff_files f;
	struct list_head l;
};

#define DOOMBUFF_NO_FILES (struct doombuff_files) {\
	.surf_dst = NULL,\
	.surf_src  = NULL,\
	.texture = NULL,\
	.flat = NULL,\
	.colormap = NULL,\
	.translation = NULL,\
	.tranmap = NULL}

#define CMD_FLAG_INTERLOCK 1 << 4
#define CMD_FLAG_PING_ASYNC 1 << 5
#define CMD_FLAG_PING_SYNC 1 << 6
#define CMD_FLAG_FENCE 1 << 7

inline bool doombuff_files_eq(struct doombuff_files *a, struct doombuff_files *b);

void doom_send_cmd(void __iomem *bar, struct doombuff_data *cmd_buf, const uint32_t *words);
int doom_write_cmd(uint32_t *words, struct doomdev2_cmd cmd, uint32_t flags,
		struct doombuff_files active_buff);
void doom_setup_cmd(uint32_t *words, struct list_head *fput_q, struct doombuff_files *nbuff, struct doombuff_files *active_buff, uint32_t flags);

#endif //DOOMCMDS
