/* **********************************************************************
* Copyright (C) 2016 Elliott Mitchell <ehem+android@m5p.com>		*
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


#include <sys/ioctl.h>
#include <unistd.h>
#include <endian.h>
#include <iconv.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <errno.h>
#include <stdlib.h>
#include <linux/fs.h>
#include "gpt.h"

static struct gpt_entry *testgpt(int fd, char *buf, size_t blocksz);

const union {
	char ch[8];
	uint64_t num;
} gpt_magic={GPT_MAGIC};


#ifdef GPT_MAIN
int main(int argc, char **argv)
{
	struct gpt_data *data;
	int f;
	if(argc!=2) {
		fprintf(stderr, "%s <GPT file>\n", argv[0]);
		exit(128);
	}
	if((f=open(argv[1], O_RDONLY))<0) {
		fprintf(stderr, "%s: open() failed: %s\n", argv[0], strerror(errno));
		exit(4);
	}
	if((data=loadgpt(f, GPT_ANY))) {
		char buf0[37], buf1[37];
		int i;
		const struct gpt_header *nat=&data->native;
		printf("Found v%hu.%hu %s GPT in \"%s\" (%zd sector size)\n",
data->native.major, data->native.minor,
data->native.myLBA==1?"primary":"backup", argv[1], data->blocksz);
		uuid_unparse(data->native.diskUuid, buf0);
		printf("device=%s\nmyLBA=%llu altLBA=%llu dataStart=%llu "
"dataEnd=%llu\n\n", buf0, (unsigned long long)nat->myLBA,
(unsigned long long)nat->altLBA, (unsigned long long)nat->dataStartLBA,
(unsigned long long)nat->dataEndLBA);

		for(i=0; i<nat->entryCount; ++i) {
			if(uuid_is_null(data->entry[i].native.id)) {
				printf("Name: <empty entry>\n");
			} else {
				uuid_unparse(data->entry[i].native.type, buf0);
				uuid_unparse(data->entry[i].native.id, buf1);
				printf("Name: \"%s\" start=%llu end=%llu\n"
"typ=%s id=%s\n", data->entry[i].native.name,
(unsigned long long)data->entry[i].native.startLBA,
(unsigned long long)data->entry[i].native.endLBA, buf0, buf1);
			}
		}

		free(data);
		return 0;
	}
	printf("No GPT found in \"%s\"\n", argv[1]);
	return 1;
}
#endif

struct gpt_data *loadgpt(int fd, enum gpt_type type)
{
	struct gpt_data *ret;
	struct gpt_entry *ebuf;
	char buf[512];
	uint32_t esz;
	uint32_t i, cnt;
	size_t blocksz;
	struct gpt_header __, *head;
	int f16[]={&__.major-(uint16_t *)&__, &__.minor-(uint16_t *)&__},
f32[]={&__.headerSize-(uint32_t *)&__, &__.headerCRC32-(uint32_t *)&__,
&__.reserved-(uint32_t *)&__, &__.entryCount-(uint32_t *)&__,
&__.entrySize-(uint32_t *)&__, &__.entryCRC32-(uint32_t *)&__},
f64[]={&__.magic-(uint64_t *)&__, &__.myLBA-(uint64_t *)&__,
&__.altLBA-(uint64_t *)&__, &__.dataStartLBA-(uint64_t *)&__,
&__.dataEndLBA-(uint64_t *)&__, &__.entryStart-(uint64_t *)&__};
	iconv_t iconvctx;
	if(ioctl(fd, BLKSSZGET, &blocksz)==0) {
		// Success, we know the block size
		if(type!=GPT_BACKUP&&lseek(fd, blocksz, SEEK_SET)>0)
			if(read(fd, buf, sizeof(buf))==sizeof(buf))
				if((ebuf=testgpt(fd, buf, blocksz))) goto success;
		if(type!=GPT_PRIMARY&&lseek(fd, -blocksz, SEEK_END)>0)
			if(read(fd, buf, sizeof(buf))==sizeof(buf))
				if((ebuf=testgpt(fd, buf, blocksz))) goto success;
//		printf("DEBUG: Known blocksize failed to find GPT\n");
		return NULL;
	} else {
		// Failure, we're going to have to guess the block size
		blocksz=1<<9; // conventional 512 bytes
		while(blocksz) { // that is a bloody big device if 4GB blocks
			if(type!=GPT_BACKUP&&lseek(fd, blocksz, SEEK_SET)>0)
				if(read(fd, buf, sizeof(buf))==sizeof(buf))
					if((ebuf=testgpt(fd, buf, blocksz))) goto success;


			if(type!=GPT_PRIMARY&&lseek(fd, -blocksz, SEEK_END)>0)
				if(read(fd, buf, sizeof(buf))==sizeof(buf))
					if((ebuf=testgpt(fd, buf, blocksz))) goto success;
			blocksz<<=1;
		}
//		printf("DEBUG: Probing failed to find GPT\n");
		return NULL;
	}

	success:
	head=(struct gpt_header *)buf;
	cnt=le32toh(head->entryCount);
	esz=le32toh(head->entrySize);
	ret=malloc(sizeof(struct gpt_data)+sizeof(ret->entry[0])*cnt);
	memcpy(ret, buf, sizeof(struct gpt_header));
	for(i=0; i<sizeof(f16)/sizeof(f16[0]); ++i) {
		((uint16_t *)&ret->native)[f16[i]]=le16toh(((uint16_t *)&ret->head)[f16[i]]);
	}
	for(i=0; i<sizeof(f32)/sizeof(f32[0]); ++i) {
		((uint32_t *)&ret->native)[f32[i]]=le32toh(((uint32_t *)&ret->head)[f32[i]]);
	}
	for(i=0; i<sizeof(f64)/sizeof(f64[0]); ++i) {
		((uint64_t *)&ret->native)[f64[i]]=le64toh(((uint64_t *)&ret->head)[f64[i]]);
	}
	memcpy(&ret->native.diskUuid, &ret->head.diskUuid, sizeof(uuid_t));
	ret->blocksz=blocksz;
	if((iconvctx=iconv_open("UTF-8", "UTF-16LE"))<0) {
		free(ret);
		free(ebuf);
		return NULL;
	}
	for(i=0; i<cnt; ++i) {
		char *in, *out;
		size_t inlen, outlen;
		memcpy(&ret->entry[i].raw, (char *)ebuf+i*esz, esz);
		memcpy(&ret->entry[i].native.type, &ret->entry[i].raw.type, sizeof(uuid_t));
		memcpy(&ret->entry[i].native.id, &ret->entry[i].raw.id, sizeof(uuid_t));
		ret->entry[i].native.startLBA=le64toh(ret->entry[i].raw.startLBA);
		ret->entry[i].native.endLBA=le64toh(ret->entry[i].raw.endLBA);
		ret->entry[i].native.flags=le64toh(ret->entry[i].raw.flags);
		in=(char *)ret->entry[i].raw.name;
		inlen=sizeof(ret->entry[i].raw.name);
		out=ret->entry[i].native.name;
		outlen=sizeof(ret->entry[i].native.name);
		iconv(iconvctx, &in, &inlen, &out, &outlen);
	}
	iconv_close(iconvctx);
	free(ebuf);
//	printf("DEBUG: Found GPT with size=%zd\n", blocksz);

	return ret;
}

static struct gpt_entry *testgpt(int fd, char *_buf, size_t blocksz)
{
	uint32_t len, crc;
	off_t start;
	size_t elen;
	struct gpt_header *buf=(struct gpt_header *)_buf;
	struct gpt_entry *ebuf;
	if(buf->magic!=gpt_magic.num) return NULL;
	len=le32toh(buf->headerSize);
	crc=crc32(0, (Byte *)buf, (char *)&buf->headerCRC32-(char *)&buf->magic);
	crc=crc32(crc, (Byte *)"\x00\x00\x00\x00", 4);
	crc=crc32(crc, (Byte *)&buf->reserved, len-((char *)&buf->reserved-(char *)&buf->magic));
	if(le32toh(buf->headerCRC32)!=crc) return NULL;

	crc=le32toh(buf->entryCRC32);
	elen=le32toh(buf->entryCount)*le32toh(buf->entrySize);
	ebuf=malloc(elen);
	if(le64toh(buf->myLBA)==1) {
		start=le64toh(buf->entryStart)*blocksz;
		if(lseek(fd, start, SEEK_SET)!=start) goto abort;
	} else {
		start=(le64toh(buf->myLBA)+1-le64toh(buf->entryStart))*blocksz;
		if(lseek(fd, -start, SEEK_END)<0) goto abort;
	}
	if(read(fd, ebuf, elen)!=elen) goto abort;
	if(crc32(0, (Byte *)ebuf, elen)!=crc) goto abort;

	return ebuf;

abort:
	free(ebuf);
	return NULL;
}

