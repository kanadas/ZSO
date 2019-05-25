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

int doom_write_cmd(uint32_t *words, struct doomdev2_cmd cmd, uint32_t flags,
		struct doombuff_files active_buff)
{
	struct doombuff_data *data, *data2, *data3;
	//printk(KERN_DEBUG "HARDDOOM writing command");
	//printk(KERN_DEBUG "HARDDOOM active_buff.surf_dst = %p\n", active_buff.surf_dst);
	if(active_buff.surf_dst == NULL) return -EINVAL;
	data = active_buff.surf_dst->private_data;
	switch (cmd.type) {
	case DOOMDEV2_CMD_TYPE_COPY_RECT:
		data2 = active_buff.surf_src->private_data;
		if(active_buff.surf_src == NULL) return -EINVAL;
		if(data->width != data2->width || data->height != data2->height)
			return -EINVAL;
		if(cmd.copy_rect.pos_dst_x + cmd.copy_rect.width > data->width ||
			cmd.copy_rect.pos_dst_y + cmd.copy_rect.height > data->height ||
			cmd.copy_rect.pos_src_x + cmd.copy_rect.width > data2->width ||
			cmd.copy_rect.pos_src_y + cmd.copy_rect.height > data2->height)
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
		if(cmd.fill_rect.pos_x + cmd.fill_rect.width > data->width ||
			cmd.fill_rect.pos_y + cmd.fill_rect.height > data->height)
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
		if(max(cmd.draw_line.pos_a_x, cmd.draw_line.pos_b_x) > data->width ||
			max(cmd.draw_line.pos_a_y, cmd.draw_line.pos_b_y) > data->height)
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
		data2 = active_buff.flat->private_data;
		if(cmd.draw_background.pos_x + cmd.draw_background.width > data->width ||
			cmd.draw_background.pos_y + cmd.draw_background.height > data->height ||
			cmd.draw_background.flat_idx << 12 >= data2->size)
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
		data2 = active_buff.texture->private_data;
		if(cmd.draw_column.pos_a_y > cmd.draw_column.pos_b_y) return -EINVAL;
		if(cmd.draw_column.pos_x > data->width ||
			cmd.draw_column.pos_b_y > data->height ||
			cmd.draw_column.texture_offset > data2->size)
			return -EOVERFLOW;
		if(cmd.draw_column.flags & 1) { //TRANSLATION
			if(active_buff.translation == NULL) return -EINVAL;
			data3 = active_buff.translation->private_data;
			if(cmd.draw_column.translation_idx << 8 >= data3->size)
				return -EOVERFLOW;
		} else cmd.draw_column.translation_idx = 0;
		if(cmd.draw_column.flags & 2) {	//COLORMAP
			if(active_buff.colormap == NULL) return -EINVAL;
			data3 = active_buff.colormap->private_data;
			if(cmd.draw_column.colormap_idx << 8 >= data3->size)
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
		words[7] = cmd_texture((data2->size >> 6) - 1, cmd.draw_column.texture_height);
		break;
	case DOOMDEV2_CMD_TYPE_DRAW_SPAN:
		if(active_buff.flat == NULL) return -EINVAL;
		data2 = active_buff.flat->private_data;
		if(cmd.draw_span.pos_a_x > cmd.draw_span.pos_b_x) return -EINVAL;
		if(cmd.draw_span.pos_b_x > data->width ||
			cmd.draw_span.pos_y > data->height ||
			cmd.draw_span.flat_idx << 12 >= data2->size)
			return -EOVERFLOW;
		if(cmd.draw_span.flags & 1) { //TRANSLATION
			if(active_buff.translation == NULL) return -EINVAL;
			data3 = active_buff.translation->private_data;
			if(cmd.draw_span.translation_idx << 8 >= data3->size)
				return -EOVERFLOW;
		} else cmd.draw_span.translation_idx = 0;
		if(cmd.draw_span.flags & 2) {	//COLORMAP
			if(active_buff.colormap == NULL) return -EINVAL;
			data3 = active_buff.colormap->private_data;
			if(cmd.draw_span.colormap_idx << 8 >= data3->size)
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
		data2 = active_buff.colormap->private_data;
		if(cmd.draw_fuzz.fuzz_start > cmd.draw_fuzz.pos_a_y ||
			cmd.draw_fuzz.pos_a_y > cmd.draw_fuzz.pos_b_y ||
			cmd.draw_fuzz.pos_b_y > cmd.draw_fuzz.fuzz_end ||
			cmd.draw_fuzz.fuzz_pos > 55) return -EINVAL;
		if(cmd.draw_fuzz.pos_x > data->width ||
			cmd.draw_fuzz.fuzz_end > data->height ||
			cmd.draw_fuzz.colormap_idx << 8 >= data2->size)
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

	//printk(KERN_DEBUG "HARDDOOM finished writing\n");

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

void doom_setup_cmd(uint32_t *words, struct doombuff_files *nbuff, struct doombuff_files *active_buff, uint32_t flags)
{
	struct doombuff_data *data;
	size_t dst_w = 0, src_w = 0;
	memset(words, 0, 32);
	//printk(KERN_DEBUG "HARDDOOM SETUP prev: %p, nxt: %p\n", active_buff->surf_dst, nbuff->surf_dst);
	if(nbuff->surf_dst != NULL) {
		flags |= SETUP_FLAG_SURF_DST;
		data = nbuff->surf_dst->private_data;
		words[1] = data->dma_pagetable >> 8;
		get_file(nbuff->surf_dst);
		dst_w = data->width;
	}
	if(nbuff->surf_src != NULL) {
		flags |= SETUP_FLAG_SURF_SRC;
		data = nbuff->surf_src->private_data;
		words[2] = data->dma_pagetable >> 8;
		get_file(nbuff->surf_src);
		src_w = data->width;
	}
	if(nbuff->texture != NULL) {
		flags |= SETUP_FLAG_TEXTURE;
		data = nbuff->texture->private_data;
		words[3] = data->dma_pagetable >> 8;
		get_file(nbuff->texture);
	}
	if(nbuff->flat != NULL) {
		flags |= SETUP_FLAG_FLAT;
		data = nbuff->flat->private_data;
		words[4] = data->dma_pagetable >> 8;
		get_file(nbuff->flat);
	}
	if(nbuff->translation != NULL) {
		flags |= SETUP_FLAG_TRANSLATION;
		data = nbuff->translation->private_data;
		words[5] = data->dma_pagetable >> 8;
		get_file(nbuff->translation);
	}
	if(nbuff->colormap != NULL) {
		flags |= SETUP_FLAG_COLORMAP;
		data = nbuff->colormap->private_data;
		words[6] = data->dma_pagetable >> 8;
		get_file(nbuff->colormap);
	}
	if(nbuff->tranmap != NULL) {
		flags |= SETUP_FLAG_TRANMAP;
		data = nbuff->tranmap->private_data;
		words[7] = data->dma_pagetable >> 8;
		get_file(nbuff->tranmap);
	}
	words[0] = setup_flags(SETUP | flags, dst_w, src_w);
	*active_buff = *nbuff;

}

inline bool doombuff_files_eq(struct doombuff_files *a, struct doombuff_files *b) {
	return  a->surf_dst    == b->surf_dst &&
		a->surf_src    == b->surf_src &&
		a->texture     == b->texture &&
		a->flat        == b->flat &&
		a->translation == b->translation &&
		a->colormap    == b->colormap &&
		a->tranmap     == b->tranmap;
}

