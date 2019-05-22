#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/file.h>

#include "doomcmds.h"
#include "doomdev2.h"
#include "doombuff.h"
#include "doomdefs.h"

//word 0
#define COPY_RECT 0
#define FILL_RECT 1
#define DRAW_LINE 2
#define DRAW_BACKGROUND 3
#define DRAW_COLUMN 4
#define DRAW_FUZZ 5
#define DRAW_SPAN 6
#define SETUP 7

//flags from header file
//#define CMD_FLAG_INTERLOCK 1 << 4
//#define CMD_FLAG_PING_ASYNC 1 << 5
//#define CMD_FLAG_PING_SYNC 1 << 6
//#define CMD_FLAG_FENCE 1 << 7
#define CMD_FLAG_TRANSLATION 1 << 8
#define CMD_FLAG_COLORMAP 1 << 9
#define CMD_FLAG_TRANMAP 1 << 10

//word 1
#define cmd_colors(TRANSLATION_IDX, COLORMAP_IDX) ((TRANSLATION_IDX) | ((COLORMAP_IDX) << 16))

//word 2
#define cmd_pos(x, y, flat) ((x) | ((y) << 11) | ((flat) << 22))

//word 3
#define cmd_2_pos(x,y) ((x) | ((y) << 11))

//word 4 USTART

//word 5 USTEP

//word 6
#define cmd_whc(width, height, color) ((width) | ((height) << 12) | ((color << 24)))

#define cmd_fuzz(start, end, pos) ((start) | ((end) << 12) | ((pos << 24)))

//word 7
#define cmd_texture(limit, height) ((limit) | ((height) << 16))

void doom_send_cmd(void __iomem* bar, const uint32_t *words)
{
	int i;
	//printk(KERN_DEBUG "HARDDOOM sending command\n");
	//printk(KERN_DEBUG "HARDDOOM words %x %x %x %x %x %x %x %x\n", words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
	for(i = 0; i < 8; ++i) iowrite32(words[i], bar + CMD_SEND + i * 4);
}

int doom_write_cmd(uint32_t *words, struct doomdev2_cmd cmd, uint32_t flags,
		struct doombuff_files active_buff, struct doombuff_sizes buff_size)
{
	if(active_buff.surf_dst == NULL) return -EINVAL;
	switch (cmd.type) {
	case DOOMDEV2_CMD_TYPE_COPY_RECT:
		if(active_buff.surf_src == NULL) return -EINVAL;
		if(buff_size.surf_dst_w != buff_size.surf_src_w ||
			buff_size.surf_dst_h != buff_size.surf_src_h) return -EINVAL;
		if(cmd.copy_rect.pos_dst_x + cmd.copy_rect.width > buff_size.surf_dst_w ||
			cmd.copy_rect.pos_dst_y + cmd.copy_rect.height > buff_size.surf_dst_h ||
			cmd.copy_rect.pos_src_x + cmd.copy_rect.width > buff_size.surf_src_w ||
			cmd.copy_rect.pos_src_y + cmd.copy_rect.height > buff_size.surf_src_h)
			return -EOVERFLOW;

		//printk(KERN_DEBUG "HARDDOOM copy rect %d %d %d %d %d %d\n", cmd.copy_rect.pos_src_x, cmd.copy_rect.pos_src_y, cmd.copy_rect.pos_dst_x, cmd.copy_rect.pos_dst_y, cmd.copy_rect.width, cmd.copy_rect.height);

		words[0] = COPY_RECT | flags | CMD_FLAG_INTERLOCK;
		words[1] = 0;
		words[2] = cmd_pos(cmd.copy_rect.pos_dst_x, cmd.copy_rect.pos_dst_y, 0);
		words[3] = cmd_2_pos(cmd.copy_rect.pos_src_x, cmd.copy_rect.pos_src_y);
		words[4] = 0;
		words[5] = 0;
		words[6] = cmd_whc(cmd.copy_rect.width, cmd.copy_rect.height, 0);
		words[7] = 0;
		break;
	case DOOMDEV2_CMD_TYPE_FILL_RECT:
		if(cmd.fill_rect.pos_x + cmd.fill_rect.width > buff_size.surf_dst_w ||
			cmd.fill_rect.pos_y + cmd.fill_rect.height > buff_size.surf_dst_h)
			return -EOVERFLOW;

		//printk(KERN_DEBUG "HARDDOOM fill frect %d %d %d %d %d\n", cmd.fill_rect.pos_x, cmd.fill_rect.pos_y, cmd.fill_rect.width, cmd.fill_rect.height, cmd.fill_rect.fill_color);
		words[0] = FILL_RECT | flags;
		words[1] = 0;
		words[2] = cmd_pos(cmd.fill_rect.pos_x, cmd.fill_rect.pos_y, 0);
		words[3] = 0;
		words[4] = 0;
		words[5] = 0;
		words[6] = cmd_whc(cmd.fill_rect.width, cmd.fill_rect.height, cmd.fill_rect.fill_color);
		words[7] = 0;
		break;
	case DOOMDEV2_CMD_TYPE_DRAW_LINE:
		if(max(cmd.draw_line.pos_a_x, cmd.draw_line.pos_b_x) > buff_size.surf_dst_w ||
			max(cmd.draw_line.pos_a_y, cmd.draw_line.pos_b_y) > buff_size.surf_dst_h)
			return -EOVERFLOW;
		words[0] = DRAW_LINE | flags;
		words[1] = 0;
		words[2] = cmd_pos(cmd.draw_line.pos_a_x, cmd.draw_line.pos_a_y, 0);
		words[3] = cmd_2_pos(cmd.draw_line.pos_b_x, cmd.draw_line.pos_b_y);
		words[4] = 0;
		words[5] = 0;
		words[6] = cmd_whc(0, 0, cmd.draw_line.fill_color);
		words[7] = 0;
		break;
	case DOOMDEV2_CMD_TYPE_DRAW_BACKGROUND:
		if(active_buff.flat == NULL) return -EINVAL;
		if(cmd.draw_background.pos_x + cmd.draw_background.width > buff_size.surf_dst_w ||
			cmd.draw_background.pos_y + cmd.draw_background.height > buff_size.surf_dst_h ||
			cmd.draw_background.flat_idx << 12 >= buff_size.flat)
			return -EOVERFLOW;
		words[0] = DRAW_BACKGROUND | flags;
		words[1] = 0;
		words[2] = cmd_pos(cmd.draw_background.pos_x, cmd.draw_background.pos_y, cmd.draw_background.flat_idx);
		words[3] = 0;
		words[4] = 0;
		words[5] = 0;
		words[6] = cmd_whc(cmd.draw_background.width, cmd.draw_background.height, 0);
		words[7] = 0;
		break;
	case DOOMDEV2_CMD_TYPE_DRAW_COLUMN:
		if(active_buff.texture == NULL) return -EINVAL;
		if(cmd.draw_column.pos_a_y > cmd.draw_column.pos_b_y) return -EINVAL;
		if(cmd.draw_column.pos_x > buff_size.surf_dst_w ||
			cmd.draw_column.pos_b_y > buff_size.surf_dst_h ||
			cmd.draw_column.texture_offset > buff_size.texture)
			return -EOVERFLOW;
		if(cmd.draw_column.flags & 1) { //TRANSLATION
			if(active_buff.translation == NULL) return -EINVAL;
			if(cmd.draw_column.translation_idx << 8 >= buff_size.translation)
				return -EOVERFLOW;
		} else cmd.draw_column.translation_idx = 0;
		if(cmd.draw_column.flags & 2) {	//COLORMAP
			if(active_buff.colormap == NULL) return -EINVAL;
			if(cmd.draw_column.colormap_idx << 8 >= buff_size.colormap)
				return -EOVERFLOW;
		} else cmd.draw_column.colormap_idx = 0;
		if((cmd.draw_column.flags & 4) && active_buff.tranmap == NULL) return -EINVAL;
		words[0] = DRAW_COLUMN | flags | (cmd.draw_column.flags << 8);
		words[1] = cmd_colors(cmd.draw_column.translation_idx, cmd.draw_column.colormap_idx);
		words[2] = cmd_pos(cmd.draw_column.pos_x, cmd.draw_column.pos_a_y, 0);
		words[3] = cmd_2_pos(cmd.draw_column.pos_x, cmd.draw_column.pos_b_y);
		words[4] = cmd.draw_column.ustart;
		words[5] = cmd.draw_column.ustep;
		words[6] = cmd.draw_column.texture_offset;
		words[7] = cmd_texture((buff_size.texture >> 6) - 1, cmd.draw_column.texture_height);
		break;
	case DOOMDEV2_CMD_TYPE_DRAW_SPAN:
		if(active_buff.flat == NULL) return -EINVAL;
		if(cmd.draw_span.pos_a_x > cmd.draw_span.pos_b_x) return -EINVAL;
		if(cmd.draw_span.pos_b_x > buff_size.surf_dst_w ||
			cmd.draw_span.pos_y > buff_size.surf_dst_h ||
			cmd.draw_span.flat_idx << 12 >= buff_size.flat)
			return -EOVERFLOW;
		if(cmd.draw_span.flags & 1) { //TRANSLATION
			if(active_buff.translation == NULL) return -EINVAL;
			if(cmd.draw_span.translation_idx << 8 >= buff_size.translation)
				return -EOVERFLOW;
		} else cmd.draw_span.translation_idx = 0;
		if(cmd.draw_span.flags & 2) {	//COLORMAP
			if(active_buff.colormap == NULL) return -EINVAL;
			if(cmd.draw_span.colormap_idx << 8 >= buff_size.colormap)
				return -EOVERFLOW;
		} else cmd.draw_span.colormap_idx = 0;
		if((cmd.draw_span.flags & 4) && active_buff.tranmap == NULL) return -EINVAL;
		words[0] = DRAW_SPAN | flags | (cmd.draw_span.flags << 8);
		words[1] = cmd_colors(cmd.draw_span.translation_idx, cmd.draw_span.colormap_idx);
		words[2] = cmd_pos(cmd.draw_span.pos_a_x, cmd.draw_span.pos_y, cmd.draw_span.flat_idx);
		words[3] = cmd_2_pos(cmd.draw_span.pos_b_x, cmd.draw_span.pos_y);
		words[4] = cmd.draw_span.ustart;
		words[5] = cmd.draw_span.ustep;
		words[6] = cmd.draw_span.vstart;
		words[7] = cmd.draw_span.vstep;
		break;
	case DOOMDEV2_CMD_TYPE_DRAW_FUZZ:
		if(active_buff.colormap == NULL) return -EINVAL;
		if(cmd.draw_fuzz.fuzz_start > cmd.draw_fuzz.pos_a_y ||
			cmd.draw_fuzz.pos_a_y > cmd.draw_fuzz.pos_b_y ||
			cmd.draw_fuzz.pos_b_y > cmd.draw_fuzz.fuzz_end ||
			cmd.draw_fuzz.fuzz_pos > 55) return -EINVAL;
		if(cmd.draw_fuzz.pos_x > buff_size.surf_dst_w ||
			cmd.draw_fuzz.fuzz_end > buff_size.surf_dst_h ||
			cmd.draw_fuzz.colormap_idx << 8 >= buff_size.colormap)
			return -EOVERFLOW;
		words[0] = DRAW_FUZZ | flags;
		words[1] = cmd_colors(0, cmd.draw_fuzz.colormap_idx);
		words[2] = cmd_pos(cmd.draw_fuzz.pos_x, cmd.draw_fuzz.pos_a_y, 0);
		words[3] = cmd_2_pos(cmd.draw_fuzz.pos_x, cmd.draw_fuzz.pos_b_y);
		words[4] = 0;
		words[5] = 0;
		words[6] = cmd_fuzz(cmd.draw_fuzz.fuzz_start, cmd.draw_fuzz.fuzz_end, cmd.draw_fuzz.fuzz_pos);
		words[7] = 0;
	}

	//printk(KERN_DEBUG "HARDDOOM words %x %x %x %x %x %x %x %x\n", words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);

	return 0;
}

#define SETUP_FLAG_SURF_DST (1 << 9)
#define SETUP_FLAG_SURF_SRC (1 << 10)
#define SETUP_FLAG_TEXTURE (1 << 11)
#define SETUP_FLAG_FLAT (1 << 12)
#define SETUP_FLAG_TRANSLATION (1 << 13)
#define SETUP_FLAG_COLORMAP (1 << 14)
#define SETUP_FLAG_TRANMAP (1 << 15)

#define setup_flags(flags, dst_width, src_width) ((flags) | ((((dst_width) >> 6) << 16) | (((src_width) >> 6) << 24)))

long doom_setup_cmd(void __iomem* bar, struct doomdev2_ioctl_setup arg, uint32_t flags,
	struct doombuff_files *active_buff, struct doombuff_sizes *buff_size)
{
	uint32_t words[8] = {0};
	struct doombuff_sizes new_sizes = DOOMBUFF_CLEAR_SIZES;
	struct doombuff_data *data;
	struct doombuff_files nbuff;

	nbuff.surf_dst = fget(arg.surf_dst_fd);
	nbuff.surf_src = fget(arg.surf_src_fd);
	nbuff.texture = fget(arg.texture_fd);
	nbuff.flat = fget(arg.flat_fd);
	nbuff.colormap = fget(arg.colormap_fd);
	nbuff.translation = fget(arg.translation_fd);
	nbuff.tranmap = fget(arg.tranmap_fd);

	//printk(KERN_DEBUG "HARDDOOM: ioctl setup arg: %d %d %d %d %d %d %d\n", arg.surf_dst_fd, arg.surf_src_fd, arg.texture_fd, arg.flat_fd, arg.translation_fd, arg.colormap_fd, arg.tranmap_fd);

	if(arg.surf_dst_fd > 0) {
		if(IS_ERR_OR_NULL(nbuff.surf_dst) || nbuff.surf_dst->f_op != &doombuff_fops)
			return -EINVAL;
		flags |= SETUP_FLAG_SURF_DST;
		data = nbuff.surf_dst->private_data;
		new_sizes.surf_dst_w = data->width;
		new_sizes.surf_dst_h = data->height;
		if(data->width <= 0) return -EINVAL;
		words[1] = data->dma_pagetable >> 8;
	} else nbuff.surf_dst = NULL;
	if(arg.surf_src_fd > 0) {
		if(IS_ERR_OR_NULL(nbuff.surf_src) || nbuff.surf_src->f_op != &doombuff_fops)
			return -EINVAL;
		flags |= SETUP_FLAG_SURF_SRC;
		data = nbuff.surf_src->private_data;
		new_sizes.surf_src_w = data->width;
		new_sizes.surf_src_h = data->height;
		if(data->width <= 0) return -EINVAL;
		words[2] = data->dma_pagetable >> 8;
	} else nbuff.surf_src = NULL;
	if(arg.texture_fd > 0) {
		if(IS_ERR_OR_NULL(nbuff.texture) || nbuff.texture->f_op != &doombuff_fops)
			return -EINVAL;
		flags |= SETUP_FLAG_TEXTURE;
		data = nbuff.texture->private_data;
		new_sizes.texture = data->size;
		words[3] = data->dma_pagetable >> 8;
	} else nbuff.texture = NULL;
	if(arg.flat_fd > 0) {
		if(IS_ERR_OR_NULL(nbuff.flat) || nbuff.flat->f_op != &doombuff_fops)
			return -EINVAL;
		flags |= SETUP_FLAG_FLAT;
		data = nbuff.flat->private_data;
		new_sizes.flat = data->size;
		if(data->size % (1 << 12)) return -EINVAL;
		words[4] = data->dma_pagetable >> 8;
	} else nbuff.flat = NULL;
	if(arg.translation_fd > 0) {
		if(IS_ERR_OR_NULL(nbuff.translation) || nbuff.translation->f_op != &doombuff_fops)
			return -EINVAL;
		flags |= SETUP_FLAG_TRANSLATION;
		data = nbuff.translation->private_data;
		new_sizes.translation = data->size;
		if(data->size % (1 << 8)) return -EINVAL;
		words[5] = data->dma_pagetable >> 8;
	} else nbuff.translation = NULL;
	if(arg.colormap_fd > 0) {
		if(IS_ERR_OR_NULL(nbuff.colormap) || nbuff.colormap->f_op != &doombuff_fops)
			return -EINVAL;
		flags |= SETUP_FLAG_COLORMAP;
		data = nbuff.colormap->private_data;
		new_sizes.colormap = data->size;
		if(data->size % (1 << 8)) return -EINVAL;
		words[6] = data->dma_pagetable >> 8;
	} else nbuff.colormap = NULL;
	if(arg.tranmap_fd > 0) {
		if(IS_ERR_OR_NULL(nbuff.tranmap) || nbuff.tranmap->f_op != &doombuff_fops)
			return -EINVAL;
		flags |= SETUP_FLAG_TRANMAP;
		data = nbuff.tranmap->private_data;
		if(data->size != 1 << 16) return -EINVAL;
		words[7] = data->dma_pagetable >> 8;
	} else nbuff.tranmap = NULL;

	//printk(KERN_DEBUG "HARDDOOM LEAK debug flat f_count = %ld\n", atomic_long_read(&nbuff.flat->f_count));

	/*if(arg.surf_dst_fd > 0) get_file(nbuff.surf_dst);
	if(arg.surf_src_fd > 0) get_file(nbuff.surf_src);
	if(arg.texture_fd > 0) get_file(nbuff.texture);
	if(arg.flat_fd > 0) get_file(nbuff.flat);
	if(arg.translation_fd > 0) get_file(nbuff.translation);
	if(arg.colormap_fd > 0) get_file(nbuff.colormap);
	if(arg.tranmap_fd > 0) get_file(nbuff.tranmap);*/
	//printk(KERN_DEBUG "HARDDOOM LEAK debug surf_dst f_count = %ld\n", atomic_long_read(&nbuff.surf_dst->f_count));

	//if(active_buff->flat != NULL) printk(KERN_DEBUG "HARDDOOM LEAK debug active flat f_count = %ld\n", atomic_long_read(&active_buff->flat->f_count));

	if(active_buff->surf_dst != NULL) fput(active_buff->surf_dst);
	if(active_buff->surf_src != NULL) fput(active_buff->surf_src);
	if(active_buff->texture != NULL) fput(active_buff->texture);
	if(active_buff->flat != NULL) fput(active_buff->flat);
	if(active_buff->translation != NULL) fput(active_buff->translation);
	if(active_buff->colormap != NULL) fput(active_buff->colormap);
	if(active_buff->tranmap != NULL) fput(active_buff->tranmap);

	words[0] = setup_flags(SETUP | flags, new_sizes.surf_dst_w, new_sizes.surf_src_w);
	doom_send_cmd(bar, words);
	*buff_size = new_sizes;
	*active_buff = nbuff;

	//printk(KERN_DEBUG "HARDDOOM LEAK debug active surf_dst f_count = %ld\n", atomic_long_read(&active_buff->surf_dst->f_count));

	return 0;
}

