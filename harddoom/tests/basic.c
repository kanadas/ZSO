#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
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

int main() {
	int doomdev = open("/dev/doom0", O_WRONLY);
	if(doomdev < 0) err("No doom device\n");
	struct doomdev2_ioctl_create_surface s_arg;
	s_arg.width = 1;
	s_arg.height = 1;
	int s1_fd;
	if((s1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0)
		printf("1. ioctl : OK\n");
	else {
		printf("1. ioctl : WRONG\n");
		return 0;
	}
	s_arg.width = 64;
	s_arg.height = 64;
	if((s1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0) {
		printf("2. ioctl : WRONG\n");
		return 0;
	} else printf("2. ioctl : OK\n");
	int s2_fd;
	s_arg.width = 1024;
	s_arg.height = 32;
	if((s2_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0) {
		printf("3. ioctl : WRONG\n");
		return 0;
	} else printf("3. ioctl : OK\n");

	int b1_fd;
	struct doomdev2_ioctl_create_buffer b_arg;
	b_arg.size = 1 << 13;
	if((b1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_BUFFER, &b_arg)) < 0) {
		printf("4. ioctl : WRONG\n");
		return 0;
	} else printf("4. ioctl : OK\n");
	struct doomdev2_cmd_fill_rect cmd1;
	cmd1.type = DOOMDEV2_CMD_TYPE_FILL_RECT;
	cmd1.fill_color = 1;
	cmd1.width = 5;
	cmd1.height = 3;
	cmd1.pos_x = 20;
	cmd1.pos_y = 10;
	if(write(doomdev, &cmd1, sizeof(cmd1)) > 0) {
		printf("5. write: WRONG\n");
		return 0;
	} else printf("5. write: OK\n");

	struct doomdev2_ioctl_setup setup;
	setup.surf_dst_fd = s1_fd;
	setup.surf_src_fd = s2_fd;
	setup.texture_fd = -1;
	setup.flat_fd = b1_fd;
	setup.colormap_fd = -1;
	setup.translation_fd = -1;
	setup.tranmap_fd = -1;
	if(ioctl(doomdev, DOOMDEV2_IOCTL_SETUP, &setup) < 0) err("6. ioctl setup : WRONG\n");
	else printf("6. ioctl setup : OK\n");

	if(write(doomdev, &cmd1, sizeof(cmd1)) < (ssize_t) sizeof(cmd1)) err("7. write : WRONG\n");
	else printf("7. write : OK\n");
	return 0;
}
