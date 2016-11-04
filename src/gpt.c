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


#define _LARGEFILE64_SOURCE

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


/* The magic number for the GPT */
const union _gpt_magic gpt_magic={.ch={'E','F','I',' ','P','A','R','T'}};


/* NOTE: All of these values are little-endian on storage media! */
struct _gpt_entry {
	uuid_t type;
	uuid_t id;
	uint64_t startLBA;
	uint64_t endLBA;
	uint64_t flags;
	char16_t name[36];
};

/* temporary structure which can overlay gpt_data */
struct _gpt_data {
	struct gpt_header head;
	size_t blocksz;
	struct _gpt_entry entry[];
};


static bool testgpt(int fd, char **buf, size_t blocksz);

static void gpt_raw2native(struct gpt_data *);

// static void gpt_native2raw(struct gpt_data *);

static void gpt_entry_raw2native(struct gpt_entry *, struct _gpt_entry *, iconv_t);

// static void gpt_entry_native2raw(struct _gpt_entry *, struct gpt_entry *, iconv_t);


#ifdef GPT_MAIN
/******************* moved to display_gpt.c *******************/
int main(int argc, char **argv)
{
	struct gpt_data *data, *alt;
	int f;
	char buf0[37], buf1[37];
	int i;

	if(argc!=2) {
		fprintf(stderr, "%s <GPT file>\n", argv[0]);
		exit(128);
	}
	if((f=open(argv[1], O_RDONLY))<0) {
		fprintf(stderr, "%s: open() failed: %s\n", argv[0], strerror(errno));
		exit(4);
	}
	if(!(data=readgpt(f, GPT_ANY))) {
		printf("No GPT found in \"%s\"\n", argv[1]);
		return 1;
	}

	if((data->head.myLBA==1)&&(alt=readgpt(f, GPT_BACKUP))) {
		if(comparegpt(data, alt)) printf("Found identical primary and backup GPTs\n");
		else printf("Primary and backup GPTs differ!\n");
		free(alt);
	} else printf("Second GPT is absent!\n");

	printf("Found v%hu.%hu %s GPT in \"%s\" (%zd sector size)\n",
data->head.major, data->head.minor,
data->head.myLBA==1?"primary":"backup", argv[1], data->blocksz);
	uuid_unparse(data->head.diskUuid, buf0);
	printf("device=%s\nmyLBA=%llu altLBA=%llu dataStart=%llu "
"dataEnd=%llu\n\n", buf0, (unsigned long long)data->head.myLBA,
(unsigned long long)data->head.altLBA,
(unsigned long long)data->head.dataStartLBA,
(unsigned long long)data->head.dataEndLBA);

	for(i=0; i<data->head.entryCount; ++i) {
		if(uuid_is_null(data->entry[i].id)) {
			printf("Name: <empty entry>\n");
		} else {
			uuid_unparse(data->entry[i].type, buf0);
			uuid_unparse(data->entry[i].id, buf1);
			printf("Name: \"%s\" start=%llu end=%llu\n"
"typ=%s id=%s\n", data->entry[i].name,
(unsigned long long)data->entry[i].startLBA,
(unsigned long long)data->entry[i].endLBA, buf0, buf1);
		}
	}

	free(data);
	return 0;
}
#endif


struct gpt_data *readgpt(int fd, enum gpt_type type)
{
	struct gpt_data *ret;
	struct _gpt_data *_ret;
	char *buf;
	uint32_t cnt;
	int32_t i;
	size_t blocksz;
	iconv_t iconvctx;

	if(ioctl(fd, BLKSSZGET, &blocksz)==0) {
		// Success, we know the block size
		if(!(buf=malloc(blocksz))) return NULL;
		if((type!=GPT_BACKUP&&lseek(fd, blocksz, SEEK_SET)>0)&&
(read(fd, buf, blocksz)==blocksz)&&testgpt(fd, &buf, blocksz)) goto success;
		if((type!=GPT_PRIMARY&&lseek(fd, -blocksz, SEEK_END)>0)&&
(read(fd, buf, blocksz)==blocksz)&&testgpt(fd, &buf, blocksz)) goto success;
//		printf("DEBUG: Known blocksize failed to find GPT\n");
		free(buf);
		return NULL;
	} else {
		// Failure, we're going to have to guess the block size
		blocksz=1<<9; // conventional 512 bytes
		while(blocksz<(1<<24)) { // that is a big device if 16MB blocks
			if(!(buf=malloc(blocksz))) return NULL;
			if((type!=GPT_BACKUP&&lseek(fd, blocksz, SEEK_SET)>0)&&
(read(fd, buf, blocksz)==blocksz)&&testgpt(fd, &buf, blocksz)) goto success;

			if((type!=GPT_PRIMARY&&lseek(fd, -blocksz, SEEK_END)>0)
&&(read(fd, buf, blocksz)==blocksz)&&testgpt(fd, &buf, blocksz)) goto success;
			free(buf);
			blocksz<<=1;
		}
//		printf("DEBUG: Probing failed to find GPT\n");
		return NULL;
	}

success:
	// realloc()ed by testgpt
	_ret=(struct _gpt_data *)buf;
	ret=(struct gpt_data *)_ret;

	gpt_raw2native(ret);
	ret->blocksz=blocksz;
	cnt=ret->head.entryCount;

	/* This is a legal situation, but one some distance in the future.  As
	** such, make sure we don't SEGV, but this needs to be handled!  TODO */
	if(ret->head.entrySize>sizeof(struct gpt_entry)) {
		free(ret);
		return NULL;
	}

	if((iconvctx=iconv_open("UTF-8", "UTF-16LE"))<0) {
		free(buf);
		return NULL;
	}

	for(i=cnt-1; i>=0; --i)
		gpt_entry_raw2native(ret->entry+i, _ret->entry+i, iconvctx);

	iconv_close(iconvctx);

//	printf("DEBUG: Found GPT with size=%zd\n", blocksz);

	return ret;
}

static bool testgpt(int fd, char **_buf, size_t blocksz)
{
	uint32_t len, crc;
	off64_t start;
	size_t newlen, elen;
	struct gpt_header *head=(struct gpt_header *)*_buf;
	struct gpt_data *ret;
	uint32_t cnt;

	if(head->magic!=gpt_magic.num) return false;
	len=le32toh(head->headerSize);
	if(len<GPT_SIZE||len>blocksz) return false;
	crc=crc32(0, (Byte *)head, (char *)&head->headerCRC32-(char *)&head->magic);
	crc=crc32(crc, (Byte *)"\x00\x00\x00\x00", 4);
	crc=crc32(crc, (Byte *)&head->reserved, len-((char *)&head->reserved-(char *)&head->magic));
	if(le32toh(head->headerCRC32)!=crc) return false;

	elen=le32toh(head->entryCount)*(cnt=le32toh(head->entrySize));
	newlen=sizeof(struct gpt_data)+sizeof(struct gpt_entry)*cnt;
	if(elen>newlen) newlen=elen;
	if(newlen>blocksz&&!(ret=realloc(*_buf, newlen))) return false;

	*_buf=(char *)(head=(struct gpt_header *)ret);
	if(le64toh(head->myLBA)==1) {
		start=le64toh(head->entryStart)*blocksz;
		if(lseek(fd, start, SEEK_SET)!=start) return false;
	} else {
		start=(le64toh(head->myLBA)+1-le64toh(head->entryStart))*blocksz;
		if(lseek(fd, -start, SEEK_END)<0) return false;
	}
	if(read(fd, ret->entry, elen)!=elen) return false;

	crc=crc32(0, (Byte *)ret->entry, elen);
	if(le32toh(head->entryCRC32)!=crc) return false;

	return true;
}


/* bool writegpt(int fd, const struct gpt_data *gpt)
{
} */


/* these are shared by the following two functions */
static const struct gpt_header __;
static const int _field16[]={&__.major-(uint16_t *)&__, &__.minor-(uint16_t *)&__},
_field32[]={&__.headerSize-(uint32_t *)&__, &__.headerCRC32-(uint32_t *)&__,
&__.reserved-(uint32_t *)&__, &__.entryCount-(uint32_t *)&__,
&__.entrySize-(uint32_t *)&__, &__.entryCRC32-(uint32_t *)&__},
_field64[]={&__.magic-(uint64_t *)&__, &__.myLBA-(uint64_t *)&__,
&__.altLBA-(uint64_t *)&__, &__.dataStartLBA-(uint64_t *)&__,
&__.dataEndLBA-(uint64_t *)&__, &__.entryStart-(uint64_t *)&__};

void gpt_raw2native(struct gpt_data *const d)
{
	int i;
	for(i=0; i<sizeof(_field16)/sizeof(_field16[0]); ++i) {
		uint16_t *const v=(uint16_t *)&d->head;
		v[_field16[i]]=le16toh(v[_field16[i]]);
	}
	for(i=0; i<sizeof(_field32)/sizeof(_field32[0]); ++i) {
		uint32_t *const v=(uint32_t *)&d->head;
		v[_field32[i]]=le32toh(v[_field32[i]]);
	}
	for(i=0; i<sizeof(_field64)/sizeof(_field64[0]); ++i) {
		uint64_t *const v=(uint64_t *)&d->head;
		v[_field64[i]]=le64toh(v[_field64[i]]);
	}
}

/* void gpt_native2raw(struct gpt_data *const d)
{
} */


void gpt_entry_raw2native(struct gpt_entry *dst, struct _gpt_entry *src,
iconv_t iconvctx)
{
	char *in, *out, *outp;
	size_t inlen, outlen;

	in=(char *)src->name;
	inlen=sizeof(src->name);
	// need a temporary buffer since dst->name and src->name could overlap
	outp=out=alloca(sizeof(dst->name));
	outlen=sizeof(dst->name);
	iconv(iconvctx, &in, &inlen, &outp, &outlen);

	// ensure the buffer is terminated (unsure whether iconv guarentees)
	out[sizeof(dst->name)-1]='\0';
	strcpy(dst->name, out);
	outlen=strlen(dst->name);

	memset(dst->name+outlen, 0, sizeof(*dst)-(dst->name+outlen-(char*)dst));

	dst->flags=le64toh(src->flags);
	dst->endLBA=le64toh(src->endLBA);
	dst->startLBA=le64toh(src->startLBA);

	// UUIDs are binary strings
	memcpy(dst->id, src->id, sizeof(uuid_t));
	memcpy(dst->type, src->type, sizeof(uuid_t));
}


/* void gpt_entry_native2raw(struct _gpt_entry *dst, struct gpt_entry *src,
iconv_t iconvctx)
{
} */


bool comparegpt(const struct gpt_data *a, const struct gpt_data *b)
{
	/* Note: If they've got differing entry counts, the main structure will
	** differ, and memcmp() will have already returned.
	** We also need to skip myLBA and altLBA since we want to return true
	** for comparing a Primary and Secondary; in turn this throws out
	** headerCRC32.
	** entryStart is also an issue. */
	if(memcmp(a, b, (char *)&a->head.headerCRC32-(char *)a)) return false;
	if(a->head.reserved!=b->head.reserved) return false;
	if(memcmp(&a->head.dataStartLBA, &b->head.dataStartLBA,
(char *)&a->head.entryStart-(char *)&a->head.dataStartLBA)) return false;
	if(memcmp(&a->head.entryCount, &b->head.entryCount,
(char *)&a->head.entryCRC32-(char *)&a->head.entryCount)) return false;
	if(memcmp(a->entry, b->entry, sizeof(struct gpt_entry)*a->head.entryCount)) return false;

	return true;
}

