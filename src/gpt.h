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

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include <stdint.h>
#include <uuid/uuid.h>
#ifdef ANDROID
typedef unsigned short char16_t;
#else
#include <uchar.h>
#endif


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
	uint8_t reserved2[];	// headerSize - 0x5C bytes
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
	size_t blocksz;
	struct gpt_entry entry[];
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

/* prepare GPT structure for writing (compute CRC, check compatibility) */
bool preparegpt(struct gpt_data *gpt);

/* write the given GPT to storage media */
extern bool writegpt(int fd, const struct gpt_data *gpt);

/* compare two in-memory GPTs (ignores data modified during write) */
extern bool comparegpt(const struct gpt_data *, const struct gpt_data *);

#endif

