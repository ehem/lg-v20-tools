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

#ifndef __GPT_H__
#define __GPT_H__

#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS==64
#define GPT_OFF_T off_t
#elif defined(_LARGEFILE64_SOURCE)
#define GPT_OFF_T off64_t
#else
#error "gpt.h needs either _FILE_OFFSET_BITS=64 or _LARGEFILE64_SOURCE defined!"
/* suppress other warnings/errors from compiler */
#define GPT_OFF_T long int
#endif

#include <stdint.h>
#include <uuid/uuid.h>
#ifdef ANDROID
typedef unsigned short char16_t;
#else
#include <uchar.h>
#endif

#include <stdbool.h>


/* Values from the GPT header */
/* NOTE: All of these values are little-endian on storage media! */
struct gpt_header {
	uint64_t magic;		// "EFI PART"
	uint16_t minor, major;  // major=1, minor=0 at this time
	uint32_t headerSize;	// 0x5C is minimum
	uint32_t headerCRC32;
	uint32_t reserved;
	uint64_t myLBA;
	uint64_t altLBA;
	uint64_t dataStartLBA;
	uint64_t dataEndLBA;
	uuid_t diskUuid;
	uint64_t entryStart;
	uint32_t entryCount;
	uint32_t entrySize;
	uint32_t entryCRC32;
//	uint8_t reserved2[];	// headerSize - 0x5C bytes
};

/* Entries for the storage slices */
struct gpt_entry {
	uuid_t type;
	uuid_t id;
	uint64_t startLBA;
	uint64_t endLBA;
	uint64_t flags;
	char name[109]; /* UTF-8 uses at most 3 bytes for every 2 of UTF-16 */
};

/* structure holding all the information from the GPT */
struct gpt_data {
	struct gpt_header head;
	GPT_OFF_T blocksz;
	struct gpt_entry entry[];
};


/* NOTE: All of these values are little-endian */
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
	GPT_OFF_T blocksz;
	struct _gpt_entry entry[];
};



/* magic number in the GPT header */
extern const union _gpt_magic {
	char ch[8];
	uint64_t num;
} gpt_magic;

/* This is without the tail padding inserted by most compilers... */
#define GPT_SIZE 0x5C

/* used for telling loadgpt() which type to look for */
enum gpt_type {GPT_NONE, GPT_ANY, GPT_PRIMARY, GPT_BACKUP};

/* load data from a GPT */
extern struct gpt_data *readgpt(int fd, enum gpt_type);

/* load GPT data from somewhere */
/* NOTE: gptpreadfunc() MUST interpret negative numbers as being relative
* to the end of file/device/buffer */
typedef ssize_t (*gptpreadfunc)(void *opaque, void *buf, size_t count, GPT_OFF_T offset);
extern struct gpt_data *readgptb(gptpreadfunc, void *opaque, uint32_t blocksz,
enum gpt_type);

/* convert the entries into storage-media format */
extern bool gpt_entries2raw(struct _gpt_data *dst, const struct gpt_data *gpt);

/* compare two in-memory GPTs (ignores data modified during write) */
extern bool comparegpt(const struct gpt_data *, const struct gpt_data *);

/* write the given host format GPT to storage media */
extern bool writegpt(int fd, const struct gpt_data *gpt);

/* WARNING: _writegpt() and __writegpt WILL modify the data */
/* write the given GPT to storage media, where header is still in host fmt */
extern bool _writegpt(int fd, struct _gpt_data *gpt);

/* write the given GPT to storage media, which is fully prepared */
extern bool __writegpt(int fd, struct _gpt_data *gpt);

/* don't contaiminate others' namespace */
#undef GPT_OFF_T

#endif

