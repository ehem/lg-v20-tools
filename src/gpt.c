/* **********************************************************************
* Copyright (C) 2016-2018 Elliott Mitchell <ehem+android@m5p.com>	*
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
/* NDK's clang #defines this */
#ifndef __STDC_UTF_16__
#define __STDC_UTF_16__
#endif

#include <sys/ioctl.h>
#include <unistd.h>
#include <endian.h>
#ifdef USE_ICONV
#include <iconv.h>
#else
#include <uchar.h>
typedef void *iconv_t;
#endif
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

/* used for the pread wrapper */
struct preaddat {
	int fd;
	off64_t eof;
};



static bool testgpt(gptpreadfunc preadfunc, void *opaque, char **buf,
uint32_t blocksz);

static void gpt_raw2native(struct gpt_header *);

static void gpt_native2raw(struct gpt_header *);

static void gpt_entry_raw2native(struct gpt_entry *, struct _gpt_entry *, iconv_t);

static void gpt_entry_native2raw(struct _gpt_entry *__restrict, const struct gpt_entry *__restrict, iconv_t);

static ssize_t preadfunc(void *opaque, void *buf, size_t count, off64_t offset);



struct gpt_data *readgpt(int fd, enum gpt_type type)
{
	struct gpt_data *ret;
	uint32_t blocksz;
	struct preaddat dat={fd, 0};

	off64_t old=lseek64(fd, 0, SEEK_CUR);
	dat.eof=lseek64(fd, 0, SEEK_END);
	lseek64(fd, old, SEEK_SET);

	if(ioctl(fd, BLKSSZGET, &blocksz)==0) {
		// Success, we know the block size
		if((ret=readgptb(preadfunc, &dat, blocksz, type)))
			return ret;

//		printf("DEBUG: Known blocksize failed to find GPT\n");
		return ret;
	} else {
		// Failure, we're going to have to guess the block size
		blocksz=1<<9; // conventional 512 bytes
		while(blocksz<(1<<24)) { // that is a big device if 16MB blocks
			if((ret=readgptb(preadfunc, &dat, blocksz, type)))
				return ret;

			blocksz<<=1;
		}
//		printf("DEBUG: Probing failed to find GPT\n");
		return ret;
	}
}

ssize_t preadfunc(void *_arg, void *buf, size_t count, off64_t offset)
{
	struct preaddat *arg=_arg;
	if(offset<0) offset+=arg->eof;
	return pread64(arg->fd, buf, count, offset);
}


struct gpt_data *readgptb(gptpreadfunc preadfunc, void *opaque,
uint32_t blocksz, enum gpt_type type)
{
	struct gpt_data *ret;
	struct _gpt_data *_ret;
	char *buf;
	uint32_t cnt;
	int32_t i;
	iconv_t iconvctx;


	if(!(buf=malloc(blocksz))) return NULL;
	if(type!=GPT_BACKUP&&preadfunc(opaque, buf, blocksz, blocksz)==blocksz
&&testgpt(preadfunc, opaque, &buf, blocksz)) goto success;
	if(type!=GPT_PRIMARY&&preadfunc(opaque, buf, blocksz, -(off64_t)blocksz)==blocksz
&&testgpt(preadfunc, opaque, &buf, blocksz)) goto success;

//	printf("DEBUG: Failed to find GPT\n");
	free(buf);
	return NULL;


success:
	// realloc()ed by testgpt
	_ret=(struct _gpt_data *)buf;
	ret=(struct gpt_data *)_ret;

	gpt_raw2native(&ret->head);
	ret->blocksz=blocksz;
	cnt=ret->head.entryCount;

	/* This is a legal situation, but one some distance in the future.  As
	** such, make sure we don't SEGV, but this needs to be handled!  TODO */
	if(ret->head.entrySize>sizeof(struct gpt_entry)) {
		free(ret);
		return NULL;
	}

#ifdef USE_ICONV
	if((iconvctx=iconv_open("UTF8", "UTF16LE"))<0) {
		free(buf);
		return NULL;
	}
#endif

	for(i=cnt-1; i>=0; --i)
#ifndef USE_ICONV
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
		gpt_entry_raw2native(ret->entry+i, _ret->entry+i, iconvctx);
#ifndef USE_ICONV
#pragma GCC diagnostic pop
#endif

#ifdef USE_ICONV
	iconv_close(iconvctx);
#endif

//	printf("DEBUG: Found GPT with size=%zd\n", blocksz);

	return ret;
}

static bool testgpt(gptpreadfunc preadfunc, void *opaque, char **_buf,
uint32_t blocksz)
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
	crc=crc32(0, Z_NULL, 0);
	crc=crc32(crc, (Byte *)head, (char *)&head->headerCRC32-(char *)&head->magic);
	crc=crc32(crc, (Byte *)"\x00\x00\x00\x00", 4);
	crc=crc32(crc, (Byte *)&head->reserved, len-((char *)&head->reserved-(char *)&head->magic));
	if(le32toh(head->headerCRC32)!=crc) return false;

	elen=(cnt=le32toh(head->entryCount))*le32toh(head->entrySize);
	newlen=sizeof(struct gpt_data)+sizeof(struct gpt_entry)*cnt;
	if(elen+sizeof(struct gpt_data)>newlen) newlen=elen+sizeof(struct gpt_data);

	/* likely to realloc(), but once media starts using 32KB blocks... */
	if(newlen<=blocksz) ret=(struct gpt_data *)*_buf;
	else if(!(ret=realloc(*_buf, newlen))) return false;
	else *_buf=(char *)(head=(struct gpt_header *)ret);

	if(le64toh(head->myLBA)==1) {
		start=le64toh(head->entryStart)*blocksz;
		if(preadfunc(opaque, ret->entry, elen, start)!=elen) return false;
	} else {
		start=(le64toh(head->myLBA)+1-le64toh(head->entryStart))*blocksz;
		if(preadfunc(opaque, ret->entry, elen, -start)!=elen) return false;
	}

	crc=crc32(0, (Byte *)ret->entry, elen);
	if(le32toh(head->entryCRC32)!=crc) return false;

	return true;
}


bool writegpt(int fd, const struct gpt_data *gpt)
{
	struct _gpt_data *new;
	bool ret;

	/* we don't modify the passed-in arguments... */
	new=malloc(sizeof(struct _gpt_data)+sizeof(struct _gpt_entry)*gpt->head.entryCount);

	/* keep things simple, even though we're going to modify it */
	memcpy(new, gpt, sizeof(struct gpt_header));

	new->blocksz=gpt->blocksz;

	/* convert everything to little-endian */
	gpt_entries2raw(new, gpt);

	new->head.entryCRC32=crc32(0, (Byte *)new->entry, gpt->head.entrySize*gpt->head.entryCount);

	ret=_writegpt(fd, new);
	free(new);

	return ret;
}


static bool __writegpt(int fd, struct _gpt_data *new, size_t blocksz);
bool _writegpt(int fd, struct _gpt_data *new)
{
	size_t blocksz;
	uint64_t entryblks;

	if(ioctl(fd, BLKSSZGET, &blocksz)==0) {
		/* If the passed-in one has it set, it better be right. */
		if(new->blocksz&&new->blocksz!=blocksz) return false;
	} else {
		/* Failure, hopefully it was passed in. */
		char *buf;
		struct preaddat dat={fd, lseek64(fd, 0, SEEK_END)};

		if(!(blocksz=new->blocksz)||blocksz&(blocksz-1)) return false;

		/* check for the presence of *something* vaguely sane */
		if(!(buf=malloc(blocksz))) return false;
		if(preadfunc(&dat, buf, blocksz,
new->head.myLBA*blocksz)!=blocksz) {
			free(buf);
			return false;
		}
		free(buf);
	}

	/* we're not written for handling these situations */
	if(new->head.headerSize!=GPT_SIZE) return false;
	if(new->head.entrySize!=sizeof(struct _gpt_entry)) return false;


	entryblks=(new->head.entryCount*sizeof(struct _gpt_entry)+blocksz-1)/blocksz;
	/* the GPT specification requires reserving space for this many
	** entries, but doesn't actually specify the table should be placed
	** for this much room between start of table and header */
	if(entryblks<sizeof(struct _gpt_entry)*128/blocksz)
		entryblks=(sizeof(struct _gpt_entry)*128+blocksz-1)/blocksz;

	if(new->head.myLBA==1) { /* we were passed primary */
		/* convert to secondary, which we write first */
		new->head.myLBA=new->head.altLBA;
		new->head.entryStart=new->head.myLBA-entryblks;
	}

	/* hopefully shouldn't ever occur, but include a sanity test */
	{
		uint64_t entry=entryblks+new->head.entryStart;

		/* checking space at end */
		if(new->head.entryStart<=new->head.dataEndLBA) return false;

		/* checking sanity */
		if(entry>new->head.myLBA) return false;
		entry=entryblks+1; /* LBA of last begining entry table */
		if(entry>=new->head.dataStartLBA) return false;
	}



/* UEFI specification, write backupGPT first, primaryGPT second */

	if(!__writegpt(fd, new, blocksz)) return false;


	/* now for the primary */
	gpt_raw2native(&new->head);

	new->head.myLBA=1;

	new->head.entryStart=new->head.myLBA+1;

	/* entry CRC remains the same */


	if(!__writegpt(fd, new, blocksz)) return false;


	return true;
}

static bool __gptpwrite(int fd, const void *buf, size_t cnt, off64_t off,
size_t blocksz);
static bool __writegpt(int fd, struct _gpt_data *new, size_t blocksz)
{
	uint32_t crc;
	off64_t head=new->head.myLBA*blocksz;

	if(!__gptpwrite(fd, new->entry,
new->head.entryCount*new->head.entrySize, new->head.entryStart*blocksz,
blocksz)) return false;

	gpt_native2raw(&new->head);

	memset(&new->head.headerCRC32, 0, 4);
	crc=crc32(0, (Byte *)&new->head, GPT_SIZE);

	new->head.headerCRC32=htole32(crc);

	if(!__gptpwrite(fd, &new->head, sizeof(new->head), head, blocksz))
		return false;

	return true;
}

static bool __gptpwrite(int fd, const void *_buf, size_t cnt, off64_t off,
size_t blksz)
{
	const char *buf=_buf;
	char cmp[blksz];
	const unsigned long fblks=cnt/blksz;
	unsigned long i;

	if(lseek64(fd, off, SEEK_SET)!=off) return false;

	for(i=0; i<fblks; ++i) {
		if(read(fd, cmp, blksz)!=blksz) return false;
		if(!memcmp(buf+i*blksz, cmp, blksz)) continue;
#ifndef DISABLE_WRITES
		if(lseek64(fd, -blksz, SEEK_CUR)<0) return false;
		if(write(fd, buf+i*blksz, blksz)!=blksz) return false;
#endif
	}

	/* this SHOULD NOT OCCUR */
	if(cnt%blksz) {
		/* can occur for header which is less than a block */
		if(cnt>blksz) fprintf(stderr,
"WARNING: GPT code hit unexpected condition, inspect results carefully!\n");
		if(read(fd, cmp, cnt%blksz)!=cnt%blksz) return false;
		if(memcmp(buf+i*blksz, cmp, cnt%blksz)) {
#ifndef DISABLE_WRITES
			if(lseek64(fd, -(cnt%blksz), SEEK_CUR)<0) return false;
			if(write(fd, buf, cnt%blksz)!=cnt%blksz) return false;
#endif
		}
	}

	return true;
}


bool gpt_entries2raw(struct _gpt_data *dst, const struct gpt_data *gpt)
{
	/* convert entires to little-endian */
	iconv_t iconvctx;
	int i;

#ifdef USE_ICONV
	if((iconvctx=iconv_open("UTF16LE", "UTF8"))<0) return false;
#endif

	for(i=0; i<gpt->head.entryCount; ++i) {
		struct _gpt_entry tmp;
#ifndef USE_ICONV
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
		gpt_entry_native2raw(&tmp, gpt->entry+i, iconvctx);
#ifndef USE_ICONV
#pragma GCC diagnostic pop
#endif
		memcpy(dst->entry+i, &tmp, sizeof(struct _gpt_entry));
	}

#ifdef USE_ICONV
	iconv_close(iconvctx);
#endif

	return true;
}


/* these are shared by the following two functions */
static const struct gpt_header __;
static const int _field16[]={&__.major-(uint16_t *)&__, &__.minor-(uint16_t *)&__},
_field32[]={&__.headerSize-(uint32_t *)&__, &__.headerCRC32-(uint32_t *)&__,
&__.reserved-(uint32_t *)&__, &__.entryCount-(uint32_t *)&__,
&__.entrySize-(uint32_t *)&__, &__.entryCRC32-(uint32_t *)&__},
_field64[]={&__.magic-(uint64_t *)&__, &__.myLBA-(uint64_t *)&__,
&__.altLBA-(uint64_t *)&__, &__.dataStartLBA-(uint64_t *)&__,
&__.dataEndLBA-(uint64_t *)&__, &__.entryStart-(uint64_t *)&__};

void gpt_raw2native(struct gpt_header *const d)
{
	int i;
	for(i=0; i<sizeof(_field16)/sizeof(_field16[0]); ++i) {
		uint16_t *const v=(uint16_t *)d;
		v[_field16[i]]=le16toh(v[_field16[i]]);
	}
	for(i=0; i<sizeof(_field32)/sizeof(_field32[0]); ++i) {
		uint32_t *const v=(uint32_t *)d;
		v[_field32[i]]=le32toh(v[_field32[i]]);
	}
	for(i=0; i<sizeof(_field64)/sizeof(_field64[0]); ++i) {
		uint64_t *const v=(uint64_t *)d;
		v[_field64[i]]=le64toh(v[_field64[i]]);
	}
}

void gpt_native2raw(struct gpt_header *const d)
{
	int i;
	for(i=0; i<sizeof(_field16)/sizeof(_field16[0]); ++i) {
		uint16_t *const v=(uint16_t *)d;
		v[_field16[i]]=htole16(v[_field16[i]]);
	}
	for(i=0; i<sizeof(_field32)/sizeof(_field32[0]); ++i) {
		uint32_t *const v=(uint32_t *)d;
		v[_field32[i]]=htole32(v[_field32[i]]);
	}
	for(i=0; i<sizeof(_field64)/sizeof(_field64[0]); ++i) {
		uint64_t *const v=(uint64_t *)d;
		v[_field64[i]]=htole64(v[_field64[i]]);
	}
}


void gpt_entry_raw2native(struct gpt_entry *dst, struct _gpt_entry *src,
iconv_t iconvctx)
{
#ifndef USE_ICONV
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused"
#endif
	char *in=(char *)src->name, out[sizeof(dst->name)], *outp=out;
#ifndef USE_ICONV
#pragma GCC diagnostic pop
#endif
	size_t inlen, outlen;
#ifndef USE_ICONV
	mbstate_t state;
#endif

	in=(char *)src->name;
	inlen=sizeof(src->name);
	// need a temporary buffer since dst->name and src->name could overlap
	outlen=sizeof(dst->name);
#ifdef USE_ICONV
	iconv(iconvctx, &in, &inlen, &outp, &outlen);
#else
	memset(&state, 0, sizeof(state));
	outlen=0;
	for(inlen=0; inlen<sizeof(src->name); ++inlen) {
		int rc=c16rtomb(out+outlen, le16toh(src->name[inlen]), &state);
		if(rc<0||!out[outlen]) break;
		outlen+=rc;
	}
	out[outlen]='\0';
#endif

	// ensure the buffer is terminated (unsure whether iconv guarentees)
	out[sizeof(dst->name)-1]='\0';
	strcpy(dst->name, out);
	outlen=strlen(dst->name);

	memset(dst->name+outlen, 0, sizeof(*dst)-(dst->name+outlen-(char*)dst));

	dst->flags=le64toh(src->flags);
	dst->endLBA=le64toh(src->endLBA);
	dst->startLBA=le64toh(src->startLBA);

	// UUIDs are binary strings
	uuid_copy(dst->id, src->id);
	uuid_copy(dst->type, src->type);
}


void gpt_entry_native2raw(struct _gpt_entry *__restrict dst,
const struct gpt_entry *__restrict src, iconv_t iconvctx)
{
	const char *in;
	char *out;
	size_t inlen, outlen;
#ifndef USE_ICONV
	mbstate_t state;
	int rc;
#endif

	/* UUIDs are binary strings */
	uuid_copy(dst->type, src->type);
	uuid_copy(dst->id, src->id);

	dst->startLBA=htole64(src->startLBA);
	dst->endLBA=htole64(src->endLBA);
	dst->flags=htole64(src->flags);

	in=src->name;
	inlen=strlen(src->name);
	out=(char *)dst->name;
	outlen=sizeof(dst->name);
#ifdef USE_ICONV
/* sigh, in argument should be declared "const", but isn't */
	iconv(iconvctx, (char **)&in, &inlen, &out, &outlen);
#else
	memset(&state, 0, sizeof(state));
	inlen=0;
	outlen=0;
	while((rc=mbrtoc16(dst->name+outlen, in+inlen, sizeof(src->name)-inlen, &state))) {
		if(rc>0) {
			inlen+=rc;
			++outlen;
		} else if(rc==-3)
			++outlen;
		else break;
	}
	dst->name[outlen]=0;
	rc=0;
	while(dst->name[rc]) {
		dst->name[rc]=htole16(dst->name[rc]);
		++rc;
	}
#endif

	/* ensure any remainder is cleared */
	memset(dst->name+outlen, 0,
sizeof(dst->name)-outlen*sizeof(dst->name[0]));
}


bool comparegpt(const struct gpt_data *a, const struct gpt_data *b)
{
	/* Note: If they've got differing entry counts, the main structure will
	** differ, and memcmp() will have already returned.
	** We also need to skip myLBA and altLBA since we want to return true
	** for comparing a Primary and Secondary; in turn this throws out
	** headerCRC32. */
	if(memcmp(a, b, (char *)&a->head.headerCRC32-(char *)a)) return false;
	/* notably data start/end and the media UUID */
	if(a->head.reserved!=b->head.reserved) return false;
	if(memcmp(&a->head.dataStartLBA, &b->head.dataStartLBA,
(char *)&a->head.entryStart-(char *)&a->head.dataStartLBA)) return false;
	if(memcmp(&a->head.entryCount, &b->head.entryCount,
(char *)&a->head.entryCRC32-(char *)&a->head.entryCount)) return false;

	/* Appears LG's tools place the entries at the start of the reserved
	** area.  Just after the GPT header for primary, just after the end
	** of the data area for secondary. (both of these tend to be 1) */
	if(
(a->head.entryStart-(a->head.myLBA==1?a->head.myLBA:a->head.dataEndLBA)) !=
(b->head.entryStart-(b->head.myLBA==1?b->head.myLBA:b->head.dataEndLBA)))
return false;

	/* This is the space between the start of the entries and the end of
	** the reserved area. (both of these tend to be 32) */
	if(
((a->head.myLBA==1?a->head.dataStartLBA:a->head.myLBA)-a->head.entryStart) !=
((b->head.myLBA==1?b->head.dataStartLBA:b->head.myLBA)-b->head.entryStart))
return false;

	/* This is simply the full area reserved for slice^Wpartition entries.
	** These three tests are redundant, any *one* of the three can be
	** removed.  This serves as a note for writing the GPT. (both of these
	** tend to be 33) */
	if(
(a->head.myLBA==1?a->head.dataStartLBA-a->head.myLBA:a->head.myLBA-a->head.dataEndLBA) !=
(b->head.myLBA==1?b->head.dataStartLBA-b->head.myLBA:b->head.myLBA-b->head.dataEndLBA))
return false;

	/* compare the actual entries, *not* their combined CRC */
	if(memcmp(a->entry, b->entry, sizeof(struct gpt_entry)*a->head.entryCount)) return false;

	return true;
}

