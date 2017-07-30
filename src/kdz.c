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


#define _FILE_OFFSET_BITS 64

#include <unistd.h>
#include <endian.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <stdio.h>
#include <zlib.h>
#include <stdlib.h>

#include <uuid/uuid.h>

#include "kdz.h"
#include "md5.h"
#include "gpt.h"


const char kdz_file_magic[KDZ_MAGIC_LEN]={0x28, 0x05, 0x00, 0x00,
0x24, 0x38, 0x22, 0x25};

const char dz_file_magic[DZ_MAGIC_LEN]={0x32, 0x96, 0x18, 0x74};

const char dz_chunk_magic[DZ_MAGIC_LEN]={0x30, 0x12, 0x95, 0x78};


static voidpf zalloc(void *ignore, uInt items, uInt size)
{
	return calloc(items, size);
}

static void zfree(void *ignore, void *addr)
{
	free(addr);
}


struct kdz_file *open_kdzfile(const char *filename)
{
	int fd=-1;
	off_t len;
	char *map=NULL;
	struct kdz_file *ret=NULL;
	struct kdz_chunk *kdz;
	struct dz_file dz;
	int i;
	uint32_t chunks;
	off_t cur;
	MD5_CTX md5;
	char md5out[16];
	int devs=-1;

	if((fd=open(filename, O_RDONLY|O_LARGEFILE))<0) {
		perror("failed open()ing file");
		return ret;
	}

	if((len=lseek(fd, 0, SEEK_END))<(1<<20)) {
		perror("file is too short");
		goto abort;
	}

	if((map=mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0))==MAP_FAILED) {
		perror("mmap() failed");
		goto abort;
	}

	/* no need to keep it lying around, the mmap remains */
	close(fd);
	fd=-1;

	if(memcmp(kdz_file_magic, map, KDZ_MAGIC_LEN)) {
		perror("missing magic number");
		goto abort;
	}

	kdz=(struct kdz_chunk *)(map+KDZ_MAGIC_LEN);
	while((i=strlen(kdz->name))>0) {
		if(!strcmp(kdz->name+i-3, ".dz")) break;

		++kdz;
	}
	if(len==0) {
		perror("failed to find inner DZ file");
		goto abort;
	}

	memcpy(&dz, map+le64toh(kdz->off), sizeof(struct dz_file));

	if(memcmp(dz.magic, dz_file_magic, DZ_MAGIC_LEN)) {
		perror("failed to find inner DZ magic");
		goto abort;
	}

	chunks=le32toh(dz.chunk_count);

	if(!(ret=malloc(sizeof(*ret)))) {
		perror("memory allocation failure");
		goto abort;
	} else if(!(ret->chunks=malloc(sizeof(ret->chunks[0])*(chunks+1)))) {
		perror("memory allocation failure");
		goto abort;
	}

	ret->devs=NULL;
	ret->map=NULL;

	ret->off=le64toh(kdz->off);

	memcpy(&ret->dz_file, &dz, sizeof(struct dz_file));
	ret->dz_file.major=le32toh(dz.major);
	ret->dz_file.minor=le32toh(dz.minor);
	ret->dz_file.reserved0=le32toh(dz.reserved0);
	ret->dz_file.chunk_count=chunks;

	if(chunks==0||chunks>(1<<20)) {
		perror("chunk count isn't sane");
		goto abort;
	}

	if(verbose>=3)
		fprintf(stderr, "DEBUG: %s: DZ file maj=%u, min=%u, dev=\"%s\" chunks=%u\n",
__func__, ret->dz_file.major, ret->dz_file.minor, ret->dz_file.device, chunks);


	cur=ret->off;

	(*pMD5_Init)(&md5);

	for(cur=ret->off, i=0; i<=chunks; ++i) {
		if(cur+sizeof(struct dz_chunk)>len) {
			fprintf(stderr, "Next chunk starts beyond end of file!\n");
			goto abort;
		}

		ret->chunks[i].zoff=cur+sizeof(struct dz_chunk);
		if(i)
			(*pMD5_Update)(&md5, map+cur, sizeof(struct dz_chunk));
		memcpy(&ret->chunks[i].dz, map+cur, sizeof(struct dz_chunk));

		ret->chunks[i].dz.target_size=le32toh(ret->chunks[i].dz.target_size);
		ret->chunks[i].dz.data_size=le32toh(ret->chunks[i].dz.data_size);
		ret->chunks[i].dz.target_addr=le32toh(ret->chunks[i].dz.target_addr);
		ret->chunks[i].dz.trim_count=le32toh(ret->chunks[i].dz.trim_count);
		ret->chunks[i].dz.device=le32toh(ret->chunks[i].dz.device);

		if((int32_t)ret->chunks[i].dz.device>devs) devs=ret->chunks[i].dz.device;

		if(verbose>=3)
			fprintf(stderr, "DEBUG: Slice Name: \"%s\"\n", ret->chunks[i].dz.slice_name);

		cur+=sizeof(struct dz_chunk)+ret->chunks[i].dz.data_size;
	}

	(*pMD5_Final)((unsigned char *)md5out, &md5);

	if(memcmp(md5out, ret->dz_file.md5, sizeof(md5out))) {
		fprintf(stderr, "Header MD5 didn't match!\n");
		fprintf(stderr, "\nHeader val: "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
ret->dz_file.md5[0x0], ret->dz_file.md5[0x1],
ret->dz_file.md5[0x2], ret->dz_file.md5[0x3],
ret->dz_file.md5[0x4], ret->dz_file.md5[0x5],
ret->dz_file.md5[0x6], ret->dz_file.md5[0x7],
ret->dz_file.md5[0x8], ret->dz_file.md5[0x9],
ret->dz_file.md5[0xA], ret->dz_file.md5[0xB],
ret->dz_file.md5[0xC], ret->dz_file.md5[0xD],
ret->dz_file.md5[0xE], ret->dz_file.md5[0xF]);
		fprintf(stderr, "MD5 val:    "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
md5out[0x0], md5out[0x1], md5out[0x2], md5out[0x3],
md5out[0x4], md5out[0x5], md5out[0x6], md5out[0x7],
md5out[0x8], md5out[0x9], md5out[0xA], md5out[0xB],
md5out[0xC], md5out[0xD], md5out[0xE], md5out[0xF]);
		goto abort;
	}

	/* set these, then clear map so abort won't double munmap() */
	ret->map=map;
	ret->len=len;
	map=NULL;


	if(!(ret->devs=malloc(sizeof(ret->devs[0])*(devs+1)))) {
		perror("memory allocation failure");
		goto abort;
	}

	ret->max_device=devs;

	/* ensure these start cleared, so abort procedure won't munmap() */
	for(i=0; i<=devs; ++i) ret->devs[i].map=NULL;

	for(i=0; i<=devs; ++i) {
		char name[16];
		snprintf(name, sizeof(name), "/dev/block/sd%c", (char)('a'+i));

		if((fd=open(name, O_RDONLY|O_LARGEFILE))<0) {
			perror("open");
			goto abort;
		}

		if(ioctl(fd, BLKSSZGET, &ret->devs[i].blksz)<0) {
			perror("ioctl");
			goto abort;
		}

		if((len=lseek(fd, 0, SEEK_END))<0) {
			perror("lseek");
			goto abort;
		}
		ret->devs[i].len=len;

		if((map=mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0))==MAP_FAILED) {
			map=NULL;
			perror("mmap");
			goto abort;
		}
		ret->devs[i].map=map;
		/* prevent double-munmap() */
		map=NULL;

		close(fd);
	}

	return ret;

abort:
	if(fd>=0) close(fd);
	if(map) munmap(map, len);
	if(ret) {
		if(ret->map) munmap(ret->map, ret->len);

		if(ret->chunks) free(ret->chunks);

		if(ret->devs) {
			for(i=0; i<=devs; ++i) if(ret->devs[i].map)
				munmap(ret->devs[i].map, ret->devs[i].len);
			free(ret->devs);
		}

		free(ret);
	}
	return NULL;
}


void close_kdzfile(struct kdz_file *kdz)
{
	int i;

	if(!kdz) return;

	munmap(kdz->map, kdz->len);

	for(i=0; i<=kdz->max_device; ++i) {
		munmap(kdz->devs[i].map, kdz->devs[i].len);
	}

	free(kdz->devs);

	free(kdz->chunks);

	free(kdz);
}


struct gpt_buf {
	off64_t bufsz;
	char *buf;
};

static ssize_t gptbuffunc(struct gpt_buf *bufp, void *dst, size_t count,
off64_t offset)
{
	if(offset<0) offset+=bufp->bufsz;
	if(offset+count>bufp->bufsz) count=bufp->bufsz-offset;
	memcpy(dst, bufp->buf+offset, count);
	return count;
}

int test_kdzfile(struct kdz_file *kdz)
{
	int i;
	int dev=-1;
	off64_t blksz;
	z_stream zstr={
		.zalloc=zalloc,
		.zfree=zfree,
	};
	MD5_CTX md5;
	char md5out[16];
	uint32_t crc;
	char *map=NULL;
	char *buf=NULL;
	uint32_t bufsz=0;
	int maxreturn=3;
	struct gpt_buf gpt_buf;

	for(i=1; i<=kdz->dz_file.chunk_count&&maxreturn>0; ++i) {
		const char *slice_name=kdz->chunks[i].dz.slice_name;
		struct {
			const char *name;
			const short result;
		} matches[]={
			{"BackupGPT",		0x7}, /* special */
			{"PrimaryGPT",		0x7}, /* special */
			{"apdp",		0x2}, /* worthwhile??? */
			{"cmnlib",		0x2},
			{"cmnlib64",		0x2},
			{"cmnlib64bak",		0x2},
			{"cmnlibbak",		0x2},
			{"devcfg",		0x2},
			{"devcfgbak",		0x2},
			{"factory",		0x2}, /* worthwhile??? */
			{"hyp",			0x2},
			{"hypbak",		0x2},
			{"keymaster",		0x2},
			{"keymasterbak",	0x2},
			{"laf",			0x2},
			{"lafbak",		0x2},
			{"msadp",		0x2}, /* worthwhile??? */
			{"pmic",		0x2},
			{"pmicbak",		0x2},
			{"raw_resources",	0x3}, /* required */
			{"raw_resourcesbak",	0x3}, /* required */
			{"rpm",			0x2},
			{"rpmbak",		0x2},
			{"sec",			0x3}, /* required */
			{"tz",			0x2},
			{"tzbak",		0x2},
			{"xbl",			0x2},
			{"xbl2",		0x2},
			{"xbl2bak",		0x2},
			{"xblbak",		0x2},
		};
		unsigned char lo=0, hi=sizeof(matches)/sizeof(matches[0]);
		uint32_t cur=0;
		bool mismatch=0;
		enum gpt_type gpt_type;
		struct gpt_data *gptdev, *gptkdz;
		int res, mid;
		int ii;

		while(mid=(hi+lo)/2, res=strcmp(slice_name, matches[mid].name)) {
			if(res<0) hi=mid;
			else if(res>0) lo=mid+1;
			if(lo==hi) goto notfound; /* unimportant, ignore */
		}

		/* going for a lower-quality match */
		if(!(maxreturn&matches[mid].result)) continue;


		if(kdz->chunks[i].dz.device!=dev) {
			dev=kdz->chunks[i].dz.device;

			blksz=kdz->devs[dev].blksz;
			if(bufsz!=blksz*5) {
				free(buf);
				bufsz=blksz*5;
				if(!(buf=malloc(bufsz))) {
					fprintf(stderr,
"Memory allocation error, cannot continue\n");
					goto abort;
				}
			}

			map=kdz->devs[dev].map;
		}


		(*pMD5_Init)(&md5);
		crc=crc32(0, Z_NULL, 0);

		zstr.next_in=(Bytef *)(kdz->map+kdz->chunks[i].zoff);
		zstr.avail_in=kdz->chunks[i].dz.data_size;
		if(inflateInit(&zstr)!=Z_OK) {
			fprintf(stderr, "inflateInit() failed: %s\n", zstr.msg);
			inflateEnd(&zstr);
			goto abort;
		}

		if(kdz->chunks[i].dz.target_size%blksz) {
			fprintf(stderr, "Block, not a multiple of block size!\n");
			goto abort;
		}


		while(cur<kdz->chunks[i].dz.target_size) {
			uint32_t cmp=kdz->chunks[i].dz.target_size-cur;
			if(cmp>bufsz) cmp=bufsz;

			zstr.next_out=(unsigned char *)buf;
			zstr.avail_out=cmp;

			switch(inflate(&zstr, Z_SYNC_FLUSH)) {
			case Z_OK:
				break;
			case Z_STREAM_END:
				break;
			default:
				printf("Chunk %d(%s): inflate() failed: %s\n",
i, kdz->chunks[i].dz.slice_name, zstr.msg);

				goto abort;
			}

			(*pMD5_Update)(&md5, buf, cmp);
			crc=crc32(crc, (Bytef *)buf, cmp);

			/* keep going to verify the CRC and MD5 */
			if(!mismatch&&
memcmp(map+kdz->chunks[i].dz.target_addr*blksz+cur, buf, cmp)) mismatch=1;

			cur+=cmp;
		}

		inflateEnd(&zstr);

		(*pMD5_Final)((unsigned char *)md5out, &md5);

		if(crc!=le32toh(kdz->chunks[i].dz.crc32)) goto abort;

		if(memcmp(md5out, kdz->chunks[i].dz.md5, sizeof(md5out)))
			goto abort;

		/* exact match, nothing to worry about */
		if(!mismatch) continue;


		/* nuke some maxreturn bits, unless special-case match */
		if(matches[mid].result<4) {
			maxreturn&=~matches[mid].result;
			continue;
		}

/* special-case match for GPTs:
** the type-UUIDs *must* match, but Id-UUIDs can differ.
** for sda, the border between OP and userdata does in fact differ for
** reason unknown.  For sdg, the types differ even for perfect match KDZ. */

		zstr.next_in=(Bytef *)(kdz->map+kdz->chunks[i].zoff);
		zstr.avail_in=kdz->chunks[i].dz.data_size;
		if(inflateInit(&zstr)!=Z_OK) {
			fprintf(stderr, "inflateInit() failed: %s\n", zstr.msg);
			inflateEnd(&zstr);
			goto abort;
		}

		if(kdz->chunks[i].dz.target_addr<=3) gpt_type=GPT_PRIMARY;
		else {
			gpt_type=GPT_BACKUP;
			cur=0;
			/* chew off enough for the remainder to fill buffer */
			while(kdz->chunks[i].dz.target_size-cur>bufsz) {
				zstr.next_out=(unsigned char *)buf;
				zstr.avail_out=kdz->chunks[i].dz.target_size-bufsz-cur;
				if(zstr.avail_out>bufsz) zstr.avail_out=bufsz;


				switch(inflate(&zstr, Z_SYNC_FLUSH)) {
				case Z_OK:
					break;
				default:
					printf("Chunk %d(%s): inflate() failed: %s\n",
i, kdz->chunks[i].dz.slice_name, zstr.msg);

					goto abort;
				}
			}
		}

		zstr.next_out=(unsigned char *)buf;
		zstr.avail_out=bufsz;

		switch(inflate(&zstr, Z_SYNC_FLUSH)) {
		case Z_OK:
			break;
		case Z_STREAM_END:
			break;
		default:
			printf("Chunk %d(%s): inflate() failed: %s\n",
i, kdz->chunks[i].dz.slice_name, zstr.msg);

			goto abort;
		}

		inflateEnd(&zstr);


		/* load the corresponding device GPT */
		gpt_buf.bufsz=kdz->devs[dev].len;
		gpt_buf.buf=kdz->devs[dev].map;

		if(!(gptdev=readgptb(gptbuffunc, &gpt_buf, blksz, gpt_type))) {
			fprintf(stderr, "Failed reading %s sd%c GPT\n",
gpt_type==GPT_PRIMARY?"primary":"backup", 'a'+dev);
			goto abort;
		}


		/* just in case, compare the other device GPT... */
		gpt_buf.bufsz=kdz->devs[dev].len;
		gpt_buf.buf=kdz->devs[dev].map;

		if(!(gptkdz=readgptb(gptbuffunc, &gpt_buf, blksz, gpt_type==GPT_BACKUP?GPT_PRIMARY:GPT_BACKUP))) {
			fprintf(stderr, "Failed reading %s sd%c GPT\n",
gpt_type==GPT_BACKUP?"primary":"backup", 'a'+dev);
			free(gptdev);
			goto abort;
		}

		if(!comparegpt(gptdev, gptkdz)) {
			free(gptdev);
			free(gptkdz);
			goto abort;
		}

		free(gptkdz);


		/* load the KDZ GPT */
		gpt_buf.bufsz=bufsz;
		gpt_buf.buf=buf;

		if(!(gptkdz=readgptb(gptbuffunc, &gpt_buf, blksz, gpt_type))) {
			fprintf(stderr, "Failed reading %s KDZ sd%c GPT\n",
gpt_type==GPT_PRIMARY?"primary":"backup", 'a'+dev);
			free(gptdev);
			goto abort;
		}



		/* check header fields, okay for the CRCs to differ */
		if(memcmp(&gptdev->head, &gptkdz->head,
(char *)&gptdev->head.headerCRC32-(char *)&gptdev->head.magic)||
memcmp(&gptdev->head.reserved, &gptkdz->head.reserved,
(char *)&gptdev->head.altLBA-(char *)&gptdev->head.reserved)||
memcmp(&gptdev->head.dataStartLBA, &gptkdz->head.dataStartLBA,
(char *)&gptdev->head.entryCRC32-(char *)&gptdev->head.dataStartLBA))
			maxreturn=0; /* fail */
		/* a device was encountered with the backup GPT's altLBA
		** pointing at itself, rather than the primary GPT; this is
		** apparently okay, but violates specifications... */
		if(gptdev->head.altLBA!=gptkdz->head.altLBA&&
gptkdz->head.altLBA!=1&&gptdev->head.altLBA!=gptkdz->head.myLBA)
			maxreturn=0;

		for(ii=0; ii<gptdev->head.entryCount-1&&maxreturn>0; ++ii) {
			if(gptdev->entry[ii].flags!=gptkdz->entry[ii].flags||
uuid_compare(gptdev->entry[ii].type, gptkdz->entry[ii].type)||
strncmp(gptdev->entry[ii].name, gptkdz->entry[ii].name, sizeof(gptdev->entry[ii].name))) {
				maxreturn=0;
				break;
			}

			/* the Ids always differ on sdg */
			if(maxreturn&2&&dev!=6&&
uuid_compare(gptdev->entry[ii].id, gptkdz->entry[ii].id)) maxreturn&=~2;


			/* on-device border between OP and userdata differs */
			if(gptdev->entry[ii].startLBA!=gptkdz->entry[ii].startLBA)
				if(strcmp(gptdev->entry[ii].name, "userdata")||
ii<=0||strcmp(gptdev->entry[ii-1].name, "OP")||
gptdev->entry[ii].startLBA!=gptdev->entry[ii-1].endLBA+1||
gptkdz->entry[ii].startLBA!=gptkdz->entry[ii-1].endLBA+1) {
					maxreturn=0;
					break;
				}

			if(gptdev->entry[ii].endLBA!=gptkdz->entry[ii].endLBA)
				if(strcmp(gptdev->entry[ii].name, "OP")||
ii>=gptdev->head.entryCount-1||strcmp(gptdev->entry[ii+1].name, "userdata")||
gptdev->entry[ii].endLBA!=gptdev->entry[ii+1].startLBA-1||
gptkdz->entry[ii].endLBA!=gptkdz->entry[ii+1].startLBA-1) {
					maxreturn=0;
					break;
				}
		}

		free(gptdev);
		free(gptkdz);


	notfound:
		/* basically a continue for this loop */
		;
	}

	free(buf);

	if(maxreturn>2) maxreturn=2;

	return maxreturn;

abort:
	/* map will only be non-NULL after mmap(), by which time mlen!=0 */
	if(buf) free(buf);

	inflateEnd(&zstr);

	return -1;
}


int report_kdzfile(struct kdz_file *kdz)
{
	int i;
	int dev=-1;
	off64_t blksz;
	z_stream zstr={
		.zalloc=zalloc,
		.zfree=zfree,
	};
	MD5_CTX md5;
	char md5out[16];
	uint32_t crc;
	char *map=NULL;
	char *buf=NULL;
	uint32_t bufsz=0;
	uint32_t cur;
	uint32_t mismatch;

	for(i=1; i<=kdz->dz_file.chunk_count; ++i) {
		const char *fmt;

		if(kdz->chunks[i].dz.device!=dev) {
			dev=kdz->chunks[i].dz.device;

			blksz=kdz->devs[dev].blksz;

			if(bufsz!=blksz) {
				free(buf);
				bufsz=blksz;
				if(!(buf=malloc(bufsz))) {
					fprintf(stderr,
"Memory allocation error, cannot continue\n");
					goto abort;
				}
			}

			map=kdz->devs[dev].map;
		}

		(*pMD5_Init)(&md5);
		crc=crc32(0, Z_NULL, 0);

		zstr.next_in=(Bytef *)(kdz->map+kdz->chunks[i].zoff);
		zstr.avail_in=kdz->chunks[i].dz.data_size;
		if(inflateInit(&zstr)!=Z_OK) {
			fprintf(stderr, "inflateInit() failed: %s\n", zstr.msg);
			inflateEnd(&zstr);
			goto abort;
		}

		mismatch=0;



		if(kdz->chunks[i].dz.target_size%blksz) {
			fprintf(stderr, "Block, not a multiple of block size!\n");
		}

		cur=0;

		while(cur<kdz->chunks[i].dz.target_size) {

			zstr.next_out=(unsigned char *)buf;
			zstr.avail_out=bufsz;

			switch(inflate(&zstr, Z_SYNC_FLUSH)) {
			case Z_OK:
				break;
			case Z_STREAM_END:
				break;
			default:
				printf("Chunk %d(%s): inflate() failed: %s\n",
i, kdz->chunks[i].dz.slice_name, zstr.msg);

				goto abort_block;
			}

			(*pMD5_Update)(&md5, buf, bufsz);
			crc=crc32(crc, (Bytef *)buf, bufsz);

			if(memcmp(map+kdz->chunks[i].dz.target_addr*blksz+cur,
buf, blksz)) ++mismatch;

			cur+=blksz;
		}

		(*pMD5_Final)((unsigned char *)md5out, &md5);

		if(crc!=le32toh(kdz->chunks[i].dz.crc32))
			printf("Chunk %d(%s): CRC32 mismatch!\n", i, kdz->chunks[i].dz.slice_name);

		if(memcmp(md5out, kdz->chunks[i].dz.md5, sizeof(md5out)))
			printf("Chunk %d(%s): MD5 mismatch!\n", i, kdz->chunks[i].dz.slice_name);

		if(mismatch)
			fmt="Chunk %1$d(%2$s): %7$d of %3$ld blocks mismatched (%4$lu-%5$lu,trim=%6$lu)\n";
		else
			fmt="Chunk %d(%s): all %ld blocks matched (%lu-%lu,trim=%lu)\n";
		printf(fmt, i, kdz->chunks[i].dz.slice_name,
kdz->chunks[i].dz.target_size/blksz, kdz->chunks[i].dz.target_addr,
kdz->chunks[i].dz.target_addr+kdz->chunks[i].dz.trim_count,
kdz->chunks[i].dz.trim_count, mismatch);

	abort_block:
		inflateEnd(&zstr);
	}

	free(buf);

	return 0;

abort:
	if(buf) free(buf);

	inflateEnd(&zstr);

	return 128;
}


int write_kdzfile(struct kdz_file *kdz, const char *const slice_name,
bool simulate)
{
	int i, j;
	int dev;
	z_stream zstr={
		.zalloc=zalloc,
		.zfree=zfree,
	};
	MD5_CTX md5;
	char md5out[16];
	uint32_t crc;
	size_t bufsz=0;
	char *buf=NULL;
	off64_t offset;
	uint64_t startLBA=0;
	int fd=-1;
	uint64_t blksz;
	short wrote=0, skip=0;

	for(i=1; i<=kdz->dz_file.chunk_count; ++i) {
		struct gpt_data *gptdev;
		struct gpt_buf gpt_buf;

		/* not the one */
		if(strcmp(slice_name, kdz->chunks[i].dz.slice_name)) continue;

		dev=kdz->chunks[i].dz.device;

		gpt_buf.bufsz=kdz->devs[dev].len;
		gpt_buf.buf=kdz->devs[dev].map;

		blksz=kdz->devs[dev].blksz;

		if(!(gptdev=readgptb(gptbuffunc, &gpt_buf, blksz, GPT_ANY))) {
			fprintf(stderr, "Failed to read GPT from /dev/block/sd%c\n", 'a'+dev);
			return 0;
		}

		for(j=0; j<gptdev->head.entryCount-1; ++j) {
			/* not the one */
			if(strcmp(slice_name, gptdev->entry[j].name)) continue;

			startLBA=gptdev->entry[j].startLBA;
			offset=startLBA*blksz;

			break;
		}

		free(gptdev);
		/* fail */
		if(!startLBA) return 0;
		break;
	}

	/* fail */
	if(!startLBA) return 0;


	{
		/* on Linux O_EXCL refuses if mounted */
		int flags=O_LARGEFILE|O_EXCL;
		char name[64];
		if(!simulate) flags|=O_WRONLY;
		snprintf(name, sizeof(name), "/dev/block/bootdevice/by-name/%s",
slice_name);

		if((fd=open(name, flags))<0) {
			const char *fmt;
			if(errno==EBUSY) fmt="Device to write, \"%s\" mounted, refusing to continue\n";
			else fmt="Failed to open \"%s\": %s";

			fprintf(stderr, fmt, name, strerror(errno));
			return 0;
		}
	}


	for(; i<=kdz->dz_file.chunk_count; ++i) {
		uint64_t range[2];

		/* obviously skip other slices */
		if(strcmp(slice_name, kdz->chunks[i].dz.slice_name)) continue;

		if(dev!=kdz->chunks[i].dz.device) { /* trouble! */
			fprintf(stderr, "PANIC: \"%s\"'s chunks cross multiple devices?!\n", slice_name);
			goto abort;
		}

		if(bufsz<kdz->chunks[i].dz.target_size) {
			bufsz=kdz->chunks[i].dz.target_size;
			free(buf);
			if(!(buf=malloc(bufsz))) {
				fprintf(stderr, "Memory allocation failure!\n");
				goto abort;
			}
		}


		if(kdz->chunks[i].dz.target_size%blksz) {
			fprintf(stderr, "Block, not a multiple of block size!\n");
			goto abort;
		}


		(*pMD5_Init)(&md5);
		crc=crc32(0, Z_NULL, 0);

		zstr.next_in=(Bytef *)(kdz->map+kdz->chunks[i].zoff);
		zstr.avail_in=kdz->chunks[i].dz.data_size;
		if(inflateInit(&zstr)!=Z_OK) {
			fprintf(stderr, "inflateInit() failed: %s\n", zstr.msg);
			inflateEnd(&zstr);
			goto abort;
		}


		zstr.next_out=(unsigned char *)buf;
		zstr.avail_out=kdz->chunks[i].dz.target_size;

		if(inflate(&zstr, Z_FINISH)!=Z_STREAM_END) {
			fprintf(stderr, "inflate() failed: %s\n", zstr.msg);
			inflateEnd(&zstr);
			goto abort;
		}

		(*pMD5_Update)(&md5, buf, kdz->chunks[i].dz.target_size);
		crc=crc32(crc, (Bytef *)buf, kdz->chunks[i].dz.target_size);

		inflateEnd(&zstr);

		(*pMD5_Final)((unsigned char *)md5out, &md5);

		if(crc!=le32toh(kdz->chunks[i].dz.crc32)) {
			fprintf(stderr, "Chunk %d CRC-32 mismatch!\n", i);
			goto abort;
		}

		if(memcmp(md5out, kdz->chunks[i].dz.md5, sizeof(md5out))) {
			fprintf(stderr, "Chunk %d MD5 mismatch!\n", i);
			goto abort;
		}


		if(verbose>=3) fprintf(stderr, "DEBUG: chunk %u\n", i);

		/* write the device while trying to keep wear to a minimum */
		for(j=0; j<kdz->chunks[i].dz.target_size; j+=blksz) {
			uint64_t target=kdz->chunks[i].dz.target_addr*blksz+j;

			if(!memcmp(buf+j, kdz->devs[dev].map+target, blksz)) {

				if(verbose>=3) fprintf(stderr,
"DEBUG: skipping %lu bytes at %lu (block %lu)\n", blksz, target-offset,
(target-offset)/blksz);
				else if(++skip>=512) {
					skip-=512;
					putchar('.');
					fflush(stdout);
				}
				continue;
			}

			if(verbose>=3) fprintf(stderr,
"DEBUG: writing %lu bytes at %lu (block %lu)\n", blksz, target-offset,
(target-offset)/blksz);
			else if(++wrote>=512) {
				wrote-=512;
				putchar('o');
				fflush(stdout);
			}

			if(!simulate)
				pwrite64(fd, buf, blksz, target-offset);
		}


		/* Discard (TRIM) all possible space */
		/* Note, this is being done on the slice, so slice-relative */

		/* start byte */
		range[0]=kdz->chunks[i].dz.target_addr*blksz-offset+
kdz->chunks[i].dz.target_size;

		range[1]=kdz->chunks[i].dz.trim_count*blksz-
kdz->chunks[i].dz.target_size;


		if(verbose>=3) fprintf(stderr,
"DEBUG: discarding %lu bytes (%lu blocks) at %lu (block %lu)\n", range[1],
range[1]/blksz, range[0], range[0]/blksz);
		else {
			putchar('*');
			fflush(stdout);
		}

		/* do the deed (sanity check, and not simulating) */
		if(range[1]>0&&range[1]<((uint64_t)1<<40)&&!simulate)
			ioctl(fd, BLKDISCARD, range);
	}

	if(fd>=0) close(fd);
	if(buf) free(buf);

	if(verbose<3) putchar('\n');

	return 1;

abort:
	if(fd>=0) close(fd);
	if(buf) free(buf);

	if(verbose<3) putchar('\n');

	return 0;
}

