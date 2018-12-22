/* **********************************************************************
* Copyright (C) 2018 Elliott Mitchell <ehem+android@m5p.com>		*
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
************************************************************************/


#define _LARGEFILE64_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "gpt.h"

struct sliceinfo {
	unsigned short id;
	char dev[];
};

struct sliceinfo *getsliceinfo(const char *const name);

int main(int argc, char **argv)
{
	int fd;
	struct sliceinfo *sysinfo, *vendinfo;
	struct gpt_data *info;
	unsigned short vendid=~0;
	struct gpt_entry *sysentr, *vendentr;
	uint64_t size;
	int64_t delta;

	sysinfo=getsliceinfo("/dev/block/bootdevice/by-name/system");
	vendinfo=getsliceinfo("/dev/block/bootdevice/by-name/vendor");

	if(sysinfo) printf("system is slice %hu of device \"%s\"\n",
sysinfo->id, sysinfo->dev);
	else printf("system does not exist or getsliceinfo() failed\n");
	if(vendinfo) printf("vendor is slice %hu of device \"%s\"\n",
vendinfo->id, vendinfo->dev);
	else printf("vendor does not exist or getsliceinfo() failed\n");

	if(!sysinfo) {
		fprintf(stderr, "Unable to retrieve system device, failed.\n");
		return -1;
	}

	if(vendinfo) {
		if(strcmp(sysinfo->dev, vendinfo->dev)) {
			fprintf(stderr,
"vendor already exists and is on different device; unable to continue\n");
			return 128;
		} else vendid=vendinfo->id;

		free(vendinfo);
	}

	if(argc<=1) {
		unsigned long long size, blocks;
		if((fd=open(sysinfo->dev, O_LARGEFILE|O_RDONLY))) {
			fprintf(stderr, "Failed opening system device: %s\n",
strerror(errno));
			return 64;
		}

		if(!(info=readgpt(fd, GPT_ANY))) {
			fprintf(stderr,
"Failed to loading GPT of system device\n");
			return 32;
		}

		blocks=info->entry[sysinfo->id].endLBA-
info->entry[sysinfo->id].startLBA+1;
		size=blocks*info->blocksz;
		printf("%llu bytes (%llu blocks) currently allocated to "
"system\n", size, blocks);

		if(~vendid) {
			blocks=info->entry[vendid].endLBA-
info->entry[vendid].startLBA+1;
			size=blocks*info->blocksz;
			printf("%llu bytes (%llu blocks) currently allocated "
"to vendor\n", size, blocks);
		} else printf("No vendor slice present\n");

		return 0;
	}

#ifdef DEBUG
	if((fd=open(strrchr(sysinfo->dev, '/')+1, O_LARGEFILE|O_RDWR))<0) {
#else
	if((fd=open(sysinfo->dev, O_LARGEFILE|O_RDWR))<0) {
#endif
		fprintf(stderr, "Failed opening system device: %s\n",
strerror(errno));
		return 128;
	}

	if(!(info=readgpt(fd, GPT_ANY))) {
		fprintf(stderr, "Failed to loading GPT of system device\n");
		return 128;
	}

	errno=0;
	size=strtoull(argv[1], NULL, 0);
	if(errno) {
		fprintf(stderr, "Error from strtoul(): %s\n", strerror(errno));
		return 7;
	}

	if(size%info->blocksz) {
		fprintf(stderr, "Specified size (%lu bytes) not a multiple of "
"block (%lu bytes), fail\n", size, info->blocksz);
		return 7;
	}
	size/=info->blocksz;

	sysentr=info->entry+sysinfo->id;

	if(~vendid) vendentr=info->entry+vendid;
	else {
		int i;
		const char *const vendor_name="vendor";
		const uuid_t vendor_type={0xA2,0x48,0x54,0xCD,0x64,0x26,0x49,0xF1,0xAA,0xE0,0x76,0xD1,0xD0,0x68,0x75,0x0A};
		const uuid_t vendor_id={0xD0,0x95,0x35,0x3A,0x87,0x65,0x44,0xDE,0x83,0x05,0xB1,0xB6,0xDA,0x2D,0x8E,0xFA};
		for(i=info->head.entryCount-1; i>=0; --i) {
			const struct gpt_entry *const entr=info->entry+i;
			if(!uuid_is_null(entr->type)) break;
			if(!uuid_is_null(entr->id)) break;
			if(entr->startLBA>=entr->endLBA) break;
			if(entr->startLBA<info->head.dataStartLBA) break;
			if(entr->endLBA>info->head.dataEndLBA) break;
		}
		if(i>=0) vendid=i;
		else {
			vendid=info->head.entryCount;
			if(!(info=realloc(info, sizeof(struct gpt_data)+
sizeof(struct gpt_entry)*++info->head.entryCount))) {
				fprintf(stderr, "Memory allocation failure\n");
				return -1;
			}

			sysentr=info->entry+sysinfo->id;
		}
		vendentr=info->entry+vendid;

		memset(vendentr, 0, sizeof(struct gpt_entry));
		strcpy(vendentr->name, vendor_name);
		uuid_copy(vendentr->type, vendor_type);
		uuid_copy(vendentr->id, vendor_id);

		/* otherwise we've created an entry which looks separate */
		vendentr->startLBA=sysentr->endLBA+1;
		vendentr->endLBA=sysentr->endLBA;
	}

	delta=size-(vendentr->endLBA-vendentr->startLBA+1);
	if(sysentr->startLBA<vendentr->startLBA) {
		if(vendentr->startLBA-sysentr->endLBA!=1) {
			fprintf(stderr,
"system slice not next to vendor slice, unable to adjust\n");
			return 16;
		}
		sysentr->endLBA-=delta;
		vendentr->startLBA-=delta;
	} else {
		if(sysentr->startLBA-vendentr->endLBA!=1) {
			fprintf(stderr,
"system slice not next to vendor slice, unable to adjust\n");
			return 16;
		}
		sysentr->startLBA+=delta;
		vendentr->endLBA+=delta;
	}

	if(!writegpt(fd, info)) {
		fprintf(stderr, "GPT writing failed, state is unknown.\n");
		return 8;
	}

#ifndef DEBUG
	if(ioctl(fd, BLKRRPART, NULL)) {
		fprintf(stderr,
"ioctl(BLKRRPART) failed, kernel still uses old GPT\n");
		return 1;
	}
#endif

	return 0;
}

struct sliceinfo *getsliceinfo(const char *const name)
{
	char buf[PATH_MAX+1];
	ssize_t len;
	unsigned short id;
	struct sliceinfo *ret;
	if((len=readlink(name, buf, sizeof(buf)))<0||len>=sizeof(buf)) return NULL;
	buf[len]='\0';
	while(isdigit(buf[--len]));
	++len;
	id=atoi(buf+len)-1;
	if(!(ret=malloc(sizeof(struct sliceinfo)+len+1))) return NULL;
	ret->id=id;
	memcpy(ret->dev, buf, len);
	ret->dev[len]='\0';
	return ret;
}

