/* **********************************************************************
* Copyright (C) 2017 Elliott Mitchell					*
*									*
*	This program is free software: you can redistribute it and/or	*
*	modify it under the terms of the GNU General Public License as	*
*	published by the Free Software Foundation, either version 3 of	*
*	the License, or (at your option) any later version.		*
*									*
*	This program is distributed in the hope that it will be useful,	*
*	but WITHOUT ANY WARRANTY; without even the implied warranty of	*
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	*
*	GNU General Public License for more details.			*
*									*
*	You should have received a copy of the GNU General Public	*
*	License along with this program.  If not, see			*
*	<http://www.gnu.org/licenses/>.					*
*************************************************************************
*$Id$			*
************************************************************************/


#include <linux/fs.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <inttypes.h>

#include "bootimg.h"


struct misc_hwinfo {
	char magic[16];  // must be set to "LGE ONE BINARY" to be valid.
	uint32_t reserved;
	uint16_t hwinfo_major;
	uint16_t hwinfo_minor;
	char lg_model_name[20];
	uint32_t sim_num;
};


const char magic[]="LGE ONE BINARY\0";


int main(int argc, char **argv)
{
	int fd;
	uint32_t blksz;
	char *buf;
	const struct misc_hwinfo *info;
	boot_img_hdr *bootimg;
	char new[512];
	char *replace;

	if(argc>2) {
		fprintf(stderr, "Usage: %s [<Android boot image>]\n" "If no "
"arguments are provided, simply print out the extra arguments to be added\n"
"to boot image.\n", argv[0]);
		return 1;
	}

	if((fd=open("/dev/block/bootdevice/by-name/misc", O_RDONLY|O_LARGEFILE))<0) {
		fprintf(stderr, "Failed to open misc area: %s\n", strerror(errno));
		return 1;
	}

	if(ioctl(fd, BLKSSZGET, &blksz)<0) {
		fprintf(stderr, "Failed to get device block size: %s\n", strerror(errno));
		return 1;
	}

	/* our minimum standard */
	if(blksz<2048) blksz=2048;

	if(!(buf=malloc(blksz))) {
		fprintf(stderr, "Memory allocation failed.\n");
		return 1;
	}

	if(lseek(fd, blksz*103, SEEK_SET)<0) {
		fprintf(stderr, "Seek failed: %s\n", strerror(errno));
		return 1;
	}

	if(read(fd, buf, blksz)!=blksz) {
		fprintf(stderr, "Read failed: %s\n", strerror(errno));
		return 1;
	}

	close(fd);

	info=(struct misc_hwinfo *)buf;

	if(memcmp(info->magic, magic, sizeof(magic))) {
		fprintf(stderr, "Required hardware information area missing!\n");
		return 1;
	}

	snprintf(new, sizeof(new), " model.name=%s lge.sim_num=%d lge.dsds=%s "
"androidboot.bl_unlock_complete=false androidboot.authorized_kernel=true",
info->lg_model_name, info->sim_num, info->sim_num==2?"dsds":"none");

	printf("LGE model information format version %hd.%hd\n", info->hwinfo_major, info->hwinfo_minor);

	printf("\"%s\"\n", new);

	if(argc==1) return 0;


	if((fd=open(argv[1], O_RDWR|O_LARGEFILE))<0) {
		fprintf(stderr, "Failed when opening \"%s\": %s\n", argv[1],
strerror(errno));
                return 1;
	}

	if(read(fd, buf, blksz)!=blksz) {
		fprintf(stderr, "Read failed: %s\n", strerror(errno));
		return 1;
	}

	bootimg=(boot_img_hdr *)buf;

	replace=strstr((char *)bootimg->cmdline, " model.name=");
	if(!replace) replace=(char *)bootimg->cmdline+strlen((char *)bootimg->cmdline);

	if((strlen(new)+(replace-(char *)bootimg->cmdline))>sizeof(bootimg->cmdline)) {
		fprintf(stderr, "Sorry, output would exceed safety of this program, giving up\n");
		return 1;
	}

	strcpy(replace, new);

	if(lseek(fd, 0, SEEK_SET)) {
		fprintf(stderr, "Seek failed: %s\n", strerror(errno));
		return 1;
	}

	if(write(fd, buf, blksz)!=blksz) {
		fprintf(stderr, "Write failed: %s\n", strerror(errno));
		return 1;
	}

	if(close(fd)) {
		fprintf(stderr, "Close failed: %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

