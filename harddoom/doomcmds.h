#ifndef DOOMCMDS
#define DOOMCMDS

#include <linux/kernel.h>
#include "doomdev2.h"

struct doombuff_sizes {
	size_t surf_dst_w, surf_dst_h;
	size_t surf_src_w, surf_src_h;
	size_t texture;
	size_t flat;
	size_t colormap;
	size_t translation;
};

struct doombuff_files {
	struct file *surf_dst;
	struct file *surf_src;
	struct file *texture;
	struct file *flat;
	struct file *colormap;
	struct file *translation;
	struct file *tranmap;
};

#define DOOMBUFF_CLEAR_SIZES (struct doombuff_sizes) {\
	.surf_dst_w = 0,\
	.surf_dst_h = 0,\
	.surf_src_w = 0,\
	.surf_src_h = 0,\
	.texture = 0,\
	.flat = 0,\
	.colormap = 0,\
	.translation = 0}

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

void doom_send_cmd(void __iomem* bar, const uint32_t *words);
int doom_write_cmd(uint32_t *words, struct doomdev2_cmd cmd, uint32_t flags,
	struct doombuff_files active_buff, struct doombuff_sizes buff_size);
long doom_setup_cmd(void __iomem* bar, struct doomdev2_ioctl_setup arg, uint32_t flags,
	struct doombuff_files *active_buff, struct doombuff_sizes *buff_size);

#endif //DOOMCMDS
