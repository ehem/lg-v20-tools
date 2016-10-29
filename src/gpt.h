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

#include <stdint.h>
#include <uuid/uuid.h>
#include <uchar.h>

enum gpt_type;

extern struct gpt_data *loadgpt(int fd, enum gpt_type);

#define GPT_MAGIC {'E','F','I',' ','P','A','R','T'}

enum gpt_type {GPT_NONE, GPT_ANY, GPT_PRIMARY, GPT_BACKUP};

/* NOTE: All of these values are little-endian! */
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


//	_gpt_slice_length = 128 # this is *default*!

/* NOTE: All of these values are little-endian! */
struct gpt_entry {
	uuid_t type;
	uuid_t id;
	uint64_t startLBA;
	uint64_t endLBA;
	uint64_t flags;
	char16_t name[28];
};

struct gpt_entry_native {
	uuid_t type;
	uuid_t id;
	uint64_t startLBA;
	uint64_t endLBA;
	uint64_t flags;
	char name[85]; // UTF-8 uses at most 3 bytes for every 2 of UTF-16
};

struct gpt_data {
	struct gpt_header head;
	struct gpt_header native;
	size_t blocksz;
	struct {
		struct gpt_entry raw;
		struct gpt_entry_native native;
	} entry[];
};

#endif

