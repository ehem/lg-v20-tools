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
#include <string.h>
#include <stdlib.h>

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


char model_name[23];
char sim_num[2]={'\0', '\0'};
char dsds_str[10]; //info->sim_num==2?"dsds":"none"
char cmdline[2048]; /* lots of slack so all the entries can be appended */


struct entry {
	const char *const key;
	const char *const val;
};

const struct entry entries[]={
	{" model.name=",			model_name},
	{" lge.sim_num=",			sim_num},
	{" lge.dsds=",				dsds_str},
	{" androidboot.bl_unlock_complete=",	"false"},
	{" androidboot.authorized_kernel=",	"true"},
};


/* come on Android, even the Linux kernel has strchrnul() */
static char *strchrnul(const char *s, int c)
{
	char *r;
	return (r=strchr(s, c))?r:(char *)(s+strlen(s));
}

int main(int argc, char **argv)
{
	int fd;
	uint32_t blksz;
	char *buf;
	const struct misc_hwinfo *info;
	boot_img_hdr *bootimg;
	char new[512];
	int i;

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

	strlcpy(model_name, info->lg_model_name, sizeof(model_name));
	if(info->sim_num>9) {
		fprintf(stderr, "SIM count of %d?!  That doesn't sound right...\n", info->sim_num);
		return 1;
	}
	sim_num[0]='0'+info->sim_num;

	strcpy(dsds_str, info->sim_num==1?"none":"dsds");

	snprintf(new, sizeof(new), " model.name=%s lge.sim_num=%s lge.dsds=%s "
"androidboot.bl_unlock_complete=false androidboot.authorized_kernel=true",
model_name, sim_num, dsds_str);

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


	memcpy(cmdline, bootimg->cmdline, BOOT_ARGS_SIZE);
	memcpy(cmdline+BOOT_ARGS_SIZE, bootimg->extra_cmdline, BOOT_EXTRA_ARGS_SIZE);
	cmdline[BOOT_ARGS_SIZE+BOOT_EXTRA_ARGS_SIZE]='\0';

	for(i=0; i<sizeof(entries)/sizeof(entries[0]); ++i) {
		char *key, *val;
		char *end;

		if((key=strstr(cmdline, entries[i].key)))
			val=key+strlen(entries[i].key);

		else if(!strncmp(cmdline, entries[i].key+1, strlen(entries[i].key)-1))
			val=cmdline+strlen(entries[i].key)-1;

		else {
			/* cmdline has more than enough slack for this */
			strcat(cmdline, entries[i].key);
			strcat(cmdline, entries[i].val);

			continue; /* done for this one */
		}

		if(!strncmp(val, entries[i].val, strlen(entries[i].val))) continue;

		/* sigh, need to do something about this */
		end=strchrnul(val, ' ');
		if(end-val!=strlen(entries[i].val)) /* different length */
			memmove(val+strlen(entries[i].val), end, strlen(end)+1);

		memcpy(val, entries[i].val, strlen(entries[i].val));
	}

	i=strlen(cmdline);
	memset(cmdline+i, 0, sizeof(cmdline)-i);

	if(i>BOOT_ARGS_SIZE+BOOT_EXTRA_ARGS_SIZE) {
		fprintf(stderr, "Resultant command-line is too long for boot image, cannot continue\n");
		return 1;
	}

	memcpy(bootimg->cmdline, cmdline, BOOT_ARGS_SIZE);
	memcpy(bootimg->extra_cmdline, cmdline+BOOT_ARGS_SIZE, BOOT_EXTRA_ARGS_SIZE);

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

