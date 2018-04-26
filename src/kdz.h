/* **********************************************************************
* Copyright (C) 2017-2018 Elliott Mitchell				*
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

#ifndef _KDZ_H_
#define _KDZ_H_

#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>


#define KDZ_MAGIC_LEN 8

extern const char kdz_file_magic[KDZ_MAGIC_LEN];

#define DZ_MAGIC_LEN 4

extern const char dz_file_magic[DZ_MAGIC_LEN];

extern const char dz_chunk_magic[DZ_MAGIC_LEN];


struct dz_chunk {
	char magic[DZ_MAGIC_LEN];
	char slice_name[32];	/* name of the slice ("partition") */
	char chunk_name[64];	/* name of this chunk */
	uint32_t target_size;	/* size of target area */
	uint32_t data_size;	/* amount of compressed data in chunk */
	char md5[16];		/* MD5 of uncompressed data */
	uint32_t target_addr;	/* first block to write */
	uint32_t trim_count;	/* blocks to TRIM before writing */
	uint32_t device;	/* flash device Id */
	uint32_t crc32;		/* CRC32 of uncompressed data */
	char pad[372];
};

struct dz_file {
	char magic[DZ_MAGIC_LEN];
	uint32_t major;		/* format major version */
	uint32_t minor;		/* format minor version */
	uint32_t reserved0;	/* patch level? */
	char device[32];	/* device name */
	char version[144];	/* "factoryversion" */
	uint32_t chunk_count;	/* number of chunks */
	char md5[16];		/* MD5 of chunk headers */
	uint32_t flag_mmc;	/* 256 == managed flash */
	uint32_t reserved1;
	uint16_t reserved4;
	char unknown1[16];
	char unknown2[50];	/* A##-M##-C##-U##-0 ? */
	char build_type[20];	/* "user"? */
	char unknown3[4];
	char android_version[10]; /* Android version */
	char old_date_code[10];	/* anti-rollback? */
	uint32_t reserved5;
	uint32_t flag_ufs;	/* 256 == UFS and multiple LUNs */
	uint64_t unknown5;
	char pad[164];
};

struct kdz_chunk {
	/* filename */
	char name[256];
	/* length of file */
	uint64_t len;
	/* offset of file */
	uint64_t off;
};

struct kdz_file {
	char *map;
	off64_t len;
	off64_t off; /* offset of DZ header */
/*	uint32_t max_target; ** maximum chunk data payload size */
	uint8_t max_device;  /* maximum device number */
	struct dz_file dz_file;
	struct {
		uint32_t blksz;
		char *map;
		off64_t len;
	} *devs;
	struct {
		off64_t zoff; /* offset of Z-stream */
		struct dz_chunk dz;
	} *chunks;
};


/* verbosity level */
extern int verbose;


/* open a file and load KDZ structure */
extern struct kdz_file *open_kdzfile(const char *filename);
/* close file and deallocate KDZ structure */
extern void close_kdzfile(struct kdz_file *kdz);

/* test for "safe" application */
extern int test_kdzfile(struct kdz_file *kdz);

/* test and report state of device/KDZ */
extern int report_kdzfile(struct kdz_file *kdz);

/* restore GPTs from KDZ file, unless simulate */
extern bool fix_gpts(const struct kdz_file *kdz, const bool simulate);

/* (re)write the named flash slice, unless simulate */
extern int write_kdzfile(const struct kdz_file *kdz, const char *slice_name,
bool simulate);

#endif

