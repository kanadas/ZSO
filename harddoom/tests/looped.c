#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stropts.h>

#include "../doomdev2.h"


static void err(const char *err)
{
	fprintf(stderr, "%s: %s\n", err, strerror(errno));
	exit(1);
}

static uint8_t check_all_eq(const char *buff, size_t size) {
	for(size_t i = 1; i < size; ++i)
		if(buff[i] != buff[i - 1]) return 0;
	return 1;
}

int main() {
	int doomdev;
	int s1_fd, s2_fd;
	char buff[128];
	doomdev = open("/dev/doom0", O_WRONLY);
	if(doomdev < 0) err("No doom device");
	struct doomdev2_ioctl_create_surface s_arg;
	s_arg.width = 2048;
	s_arg.height = 2048;
	if((s1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0) err("ioctl");
	if((s2_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0) err("ioctl");
	struct doomdev2_cmd_fill_rect cmd;
	cmd.type = DOOMDEV2_CMD_TYPE_FILL_RECT;
	cmd.fill_color = 1;
	cmd.width = 2048;
	cmd.height = 2048;
	cmd.pos_x = 0;
	cmd.pos_y = 0;
	struct doomdev2_ioctl_setup setup;
	setup.surf_dst_fd = s1_fd;
	setup.surf_src_fd = -1;
	setup.texture_fd = -1;
	setup.flat_fd = -1;
	setup.colormap_fd = -1;
	setup.translation_fd = -1;
	setup.tranmap_fd = -1;
	if(ioctl(doomdev, DOOMDEV2_IOCTL_SETUP, &setup) < 0) err("ioctl setup");
	int ncommands = 10;
	struct doomdev2_cmd commands[300];
	for(int i = 0; i < 300; ++i) {
		if(i % 2) {
			commands[i].fill_rect.type = DOOMDEV2_CMD_TYPE_FILL_RECT;
			commands[i].fill_rect.fill_color = (31 * i) % 256;
			commands[i].fill_rect.pos_x = (357 * i) % 1024;
			commands[i].fill_rect.pos_y = (239 * i) % 1024;
			commands[i].fill_rect.width = 512 + ((337 * i) % 512);
			commands[i].fill_rect.height = 512 + ((253 * i) % 512);
		} else {
			commands[i].draw_line.type = DOOMDEV2_CMD_TYPE_DRAW_LINE;
			commands[i].draw_line.fill_color = (73 * i) % 256;
			commands[i].draw_line.pos_a_x = (973 * i) % 2048;
			commands[i].draw_line.pos_a_y = (581 * i) % 2048;
			commands[i].draw_line.pos_b_x = (237 * i) % 2048;
			commands[i].draw_line.pos_b_y = (623 * i) % 2048;
		}
	}
	int val = 0;
	uint64_t i = 1;
	while(1) {
		if(i % 10 == 0) printf("Still running: %lu\n", i);
		if(i % 3 == 0) {
			setup.surf_dst_fd = i % 2 ? s1_fd : s2_fd;
			if((val = ioctl(doomdev, DOOMDEV2_IOCTL_SETUP, &setup)) < 0) {
				printf("ioctl i = %lu, val = %d\n", i, val);
				err("ioctl setup");
			}
		}
		if(i % 7 == 0) {
			if((val = pread(s1_fd, buff, 128, (327 * i) % (2048 * 1024))) != 128) {
				printf("pread s1_fd i = %lu, val = %d\n", i, val);
				err("pread");
			}
			if((val = pread(s2_fd, buff, 128, (637 * i) % (2048 * 1024))) != 128) {
				printf("pread s2_fd i = %lu, val = %d\n", i, val);
				err("pread");
			}
		}
		if(i % 14 == 0) {
			if(i % 4) {
				close(s1_fd);
				if((s1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg))
						< 0) {
					printf("ioctl s1_fd %d\n", s1_fd);
					err("ioctl");
				}
			} else {
				close(s2_fd);
				if((s2_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg))
						< 0) {
					printf("ioctl s2_fd %d\n", s2_fd);
					err("ioctl");
				}
			}
		}
		if((val = write(doomdev, commands, sizeof(struct doomdev2_cmd) * ncommands)) !=
				sizeof(struct doomdev2_cmd) * ncommands) {
			printf("write i = %lu, val = %d\n", i, val);
			err("write");
		}
		ncommands = (ncommands % 300) + 10;
		++i;
	}
	return 0;
}
