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

int doomdev;
int s1_fd, s2_fd, b1_fd;

static void err(const char *err)
{
	fprintf(stderr, "%s: %s\n", err, strerror(errno));
	close(s1_fd);
	close(s2_fd);
	close(b1_fd);
	close(doomdev);
	exit(1);
}

int main() {
	char buff[128 * 16];
	memset(buff, 2, 128 * 16);
	doomdev = open("/dev/doom0", O_WRONLY);
	if(doomdev < 0) err("No doom device");
	struct doomdev2_ioctl_create_surface s_arg;
	s_arg.width = 1;
	s_arg.height = 1;
	if((s1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0)
		printf("1. ioctl : OK\n");
	else {
		printf("1. ioctl : WRONG\n");
		goto close_exit;
	}
	s_arg.width = 128;
	s_arg.height = 128;
	if((s1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0) {
		printf("2. ioctl : WRONG\n");
		goto close_exit;
	} else printf("2. ioctl : OK\n");
	s_arg.width = 128;
	s_arg.height = 128;
	if((s2_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_SURFACE, &s_arg)) < 0) {
		printf("3. ioctl : WRONG\n");
		goto close_exit;
	} else printf("3. ioctl : OK\n");

	struct doomdev2_ioctl_create_buffer b_arg;
	b_arg.size = 1 << 13;
	if((b1_fd = ioctl(doomdev, DOOMDEV2_IOCTL_CREATE_BUFFER, &b_arg)) < 0) {
		printf("4. ioctl : WRONG\n");
		goto close_exit;
	} else printf("4. ioctl : OK\n");
	struct doomdev2_cmd_fill_rect cmd1;
	cmd1.type = DOOMDEV2_CMD_TYPE_FILL_RECT;
	cmd1.fill_color = 1;
	cmd1.width = 32;
	cmd1.height = 32;
	cmd1.pos_x = 64 - 16;
	cmd1.pos_y = 64 - 16;
	if(write(doomdev, &cmd1, sizeof(cmd1)) > 0) {
		printf("5. write: WRONG\n");
		goto close_exit;
	} else printf("5. write: OK\n");

	struct doomdev2_ioctl_setup setup;
	setup.surf_dst_fd = s1_fd;
	setup.surf_src_fd = s2_fd;
	setup.texture_fd = -1;
	setup.flat_fd = b1_fd;
	setup.colormap_fd = -1;
	setup.translation_fd = -1;
	setup.tranmap_fd = -1;
	if(ioctl(doomdev, DOOMDEV2_IOCTL_SETUP, &setup) < 0) err("6. ioctl setup : WRONG");
	else printf("6. ioctl setup : OK\n");

	if(write(doomdev, &cmd1, sizeof(cmd1)) < (ssize_t) sizeof(cmd1)) err("7. write : WRONG");
	else printf("7. write : OK\n");

	char tmp;
	if(read(s1_fd, &tmp, 1) != 1) err("7.5: read");

	if(lseek(s1_fd, 64 * 128, SEEK_SET)) err("8. llseek : WRONG");
	else printf("8. llseek : OK\n");

	if(write(s1_fd, buff, 16 * 128) != 16 * 128) err("9. write buff : WRONG ");
	else printf("9. write buff: OK\n");

	setup.surf_dst_fd = s2_fd;
	setup.surf_src_fd = s1_fd;

	if(ioctl(doomdev, DOOMDEV2_IOCTL_SETUP, &setup) < 0) err("10. ioctl setup : WRONG");
	else printf("10. ioctl setup : OK\n");

	struct doomdev2_cmd commands[3];
	//commands[1].fill_rect = cmd1;

	cmd1.fill_color = 3;
	cmd1.width = 128;
	cmd1.height = 128;
	cmd1.pos_x = 0;
	cmd1.pos_y = 0;

	//commands[0].fill_rect = cmd1;

	struct doomdev2_cmd_copy_rect cmd2;
	cmd2.type = DOOMDEV2_CMD_TYPE_COPY_RECT;
	cmd2.width = 32;
	cmd2.height = 32;
	cmd2.pos_src_x = 64 - 16;
	cmd2.pos_src_y = 64 - 16;
	cmd2.pos_dst_x = 64 - 16;
	cmd2.pos_dst_y = 64 - 16;

	commands[0].fill_rect = cmd1;
	commands[1].copy_rect = cmd2;

	if(write(doomdev,commands, 2*sizeof(struct doomdev2_cmd)) != 2*sizeof(struct doomdev2_cmd))
		err("11. write: WRONG ");
	else printf("11. write: OK\n");

	/*if(write(doomdev, commands, sizeof(struct doomdev2_cmd)) != sizeof(struct doomdev2_cmd))
		err("aa. write: WRONG ");
	else printf("aa. write: OK\n");

	if(write(doomdev, &commands[1], sizeof(struct doomdev2_cmd)) != sizeof(struct doomdev2_cmd))
		err("bb. write: WRONG ");
	else printf("bb. write: OK\n");*/


	char read_b[64];
	if(pread(s2_fd, read_b, 64, 128 * 64 - 32) != 64) err("12. pread: WRONG ");
	else printf("12. pread: OK\n");

	if(read_b[0] != 3 || read_b[32] != 3 || read_b[63] != 3) {
		printf("12. pread: WRONG OUTPUT: %d %d %d\n", read_b[0], read_b[32], read_b[63]);
		goto close_exit;
	} else printf("12. pread: GOOD OUTPUT\n");

	commands[0].fill_rect.fill_color = 4;
	struct doomdev2_cmd_draw_line cmd3;
	cmd3.type = DOOMDEV2_CMD_TYPE_DRAW_LINE;
	cmd3.fill_color = 5;
	cmd3.pos_a_x = 32;
	cmd3.pos_a_y = 48;
	cmd3.pos_b_x = 129;
	cmd3.pos_b_y = 128;
	commands[2].draw_line = cmd3;

	if(write(doomdev,commands, 3*sizeof(struct doomdev2_cmd)) != 2*sizeof(struct doomdev2_cmd))
		err("13. write: WRONG ");
	else printf("13. write: OK\n");

	if(pread(s2_fd, read_b, 64, 48 * 128 + 32) != 64) err("14. pread: WRONG ");
	else printf("14. pread: OK\n");

	if(read_b[0] != 4 || read_b[32] != 1 || read_b[63] != 4) {
		printf("14. pread: WRONG OUTPUT: %d %d %d\n", read_b[0], read_b[32], read_b[63]);
		goto close_exit;
	} else printf("14. pread: GOOD OUTPUT\n");

	if(pread(s2_fd, read_b, 64, 79 * 128 + 32) != 64) err("15. pread: WRONG ");
	else printf("15. pread: OK\n");

	if(read_b[0] != 4 || read_b[32] != 2 || read_b[63] != 4) {
		printf("15. pread: WRONG OUTPUT: %d %d %d\n", read_b[0], read_b[32], read_b[63]);
		goto close_exit;
	} else printf("15. pread: GOOD OUTPUT\n");

	setup.surf_dst_fd = -1;
	setup.surf_src_fd = -1;
	setup.flat_fd = -1;

	if(ioctl(doomdev, DOOMDEV2_IOCTL_SETUP, &setup) < 0) err("16. ioctl setup : WRONG");
	else printf("16. ioctl setup : OK\n");

close_exit:
	close(s1_fd);
	close(s2_fd);
	close(b1_fd);
	//close(doomdev);
	return 0;
}
