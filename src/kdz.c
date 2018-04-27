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

/* the unpack context structure */
struct unpackctx {
	unsigned valid:1, z_finished:1, fail:1;
	const struct kdz_file *kdz;
	unsigned chunk;
	MD5_CTX md5;
	long crc;
	z_stream zstr;
};


/* initialize the unpacking context */
static bool unpackchunk_alloc(struct unpackctx *ctx,
const struct kdz_file *const kdz, const unsigned chunk);

/* retrieve uncompressed data from the chunk, returns bytes in buffer */
static int unpackchunk(struct unpackctx *ctx, void *buf, size_t bufsz);

/* perform the chunk verification steps */
static bool unpackchunk_free(struct unpackctx *const ctx, bool discard);



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

	unsigned le32offs[]={
		&dz.major	-(uint32_t *)&dz,
		&dz.minor	-(uint32_t *)&dz,
		&dz.chunk_count	-(uint32_t *)&dz,
		&dz.flag_mmc	-(uint32_t *)&dz,
		&dz.flag_ufs	-(uint32_t *)&dz,
	};

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

	for(i=0; i<sizeof(le32offs)/sizeof(le32offs[0]); ++i) {
		uint32_t *tmp=(uint32_t *)&dz;
		tmp[le32offs[i]]=le32toh(tmp[le32offs[i]]);
	}

	chunks=dz.chunk_count;

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
		struct dz_chunk *const dz=&ret->chunks[i].dz;

		if(cur+sizeof(struct dz_chunk)>len) {
			fprintf(stderr, "Next chunk starts beyond end of file!\n");
			goto abort;
		}

		ret->chunks[i].zoff=cur+sizeof(struct dz_chunk);
		if(i)
			(*pMD5_Update)(&md5, map+cur, sizeof(struct dz_chunk));
		memcpy(&ret->chunks[i].dz, map+cur, sizeof(struct dz_chunk));

		dz->target_size=le32toh(dz->target_size);
		dz->data_size=le32toh(dz->data_size);
		dz->target_addr=le32toh(dz->target_addr);
		dz->trim_count=le32toh(dz->trim_count);
		dz->device=le32toh(dz->device);

		if((int32_t)dz->device>devs) devs=dz->device;

		if(verbose>=3)
			fprintf(stderr, "DEBUG: Slice Name: \"%s\"\n", ret->chunks[i].dz.slice_name);

		cur+=sizeof(struct dz_chunk)+dz->data_size;
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
		char name[32];
		char *fmt, unit;
		if((ret->dz_file.flag_ufs&256)==256) {
			fmt="/dev/block/sd%c";
			unit='a';
		} else {
			fmt="/dev/block/mmcblk%c";
			unit='0';
		}
		snprintf(name, sizeof(name), fmt, unit+i);

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

	if(verbose>=9) fprintf(stderr, "DEBUG: KDZ file successfully opened\n");

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

	if(verbose>=9) fprintf(stderr, "DEBUG: Failed to open KDZ file\n");
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


static int test_kdzfile_gpt_entry(int dev, int maxreturn,
struct gpt_entry *kdzentry, struct gpt_entry *deventry);
int test_kdzfile(struct kdz_file *kdz)
{
	int i;
	int dev=-1;
	off64_t blksz;
	struct unpackctx _ctx={0,}, *const ctx=&_ctx;
	char *map=NULL;
	char *buf=NULL;
	uint32_t bufsz=0;
	int maxreturn=3;
	struct gpt_buf gpt_buf;

	for(i=1; i<=kdz->dz_file.chunk_count&&maxreturn>0; ++i) {
		const struct dz_chunk *const dz=&kdz->chunks[i].dz;
		const char *const slice_name=dz->slice_name;
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
			{"raw_resources",	0x2}, /* required, mod cand */
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
		bool mismatch=false;
		enum gpt_type gpt_type;
		struct gpt_data *gptdev, *gptdev2, *gptkdz;
		int res, mid;
		int ii;

		while(mid=(hi+lo)/2, res=strcmp(slice_name, matches[mid].name)) {
			if(res<0) hi=mid;
			else if(res>0) lo=mid+1;
			if(lo==hi) goto notfound; /* unimportant, ignore */
		}

		/* going for a lower-quality match */
		if(!(maxreturn&matches[mid].result)) continue;


		if(dz->device!=dev) {
			dev=dz->device;

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

		if(!unpackchunk_alloc(ctx, kdz, i)) goto abort;

		while(cur<dz->target_size) {
			uint32_t cmp=dz->target_size-cur;
			if(cmp>bufsz) cmp=bufsz;

			if(unpackchunk(ctx, buf, cmp)<=0) goto abort;

			/* keep going to verify the CRC and MD5 */
			if(!mismatch&&
memcmp(map+dz->target_addr*blksz+cur, buf, cmp)) mismatch=1;

			cur+=cmp;
		}

		if(!unpackchunk_free(ctx, false)) goto abort;

		/* exact match, nothing to worry about */
		if(!mismatch) continue;


		/* nuke some maxreturn bits, unless special-case match */
		if(matches[mid].result<4) {
			maxreturn&=~matches[mid].result;
			continue;
		}

/* special-case match for GPTs:
** The type-UUIDs *must* match, but Id-UUIDs can differ.  For sda, the border
** between OP and userdata normally differs due to varying amounts of LGE
** bloatware.  A tool to remove OP exists, and LineageOS is likely to modify
** system area.  For sdg, the types differ even for perfect match KDZ. */

		if(!unpackchunk_alloc(ctx, kdz, i)) goto abort;

		if(dz->target_addr<=3) gpt_type=GPT_PRIMARY;
		else {
			gpt_type=GPT_BACKUP;
			cur=0;
			/* chew off enough for the remainder to fill buffer */
			while(dz->target_size-cur>bufsz) {
				size_t next=dz->target_size-bufsz-cur;
				if(next>bufsz) next=bufsz;

				if(unpackchunk(ctx, buf, next)<=0)
					goto abort;

			}
		}

		if(unpackchunk(ctx, buf, bufsz)<=0) goto abort;

		if(!unpackchunk_free(ctx, gpt_type==GPT_PRIMARY)) goto abort;


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

		if(!(gptdev2=readgptb(gptbuffunc, &gpt_buf, blksz, gpt_type==GPT_BACKUP?GPT_PRIMARY:GPT_BACKUP))) {
			fprintf(stderr, "Failed reading %s sd%c GPT\n",
gpt_type==GPT_BACKUP?"primary":"backup", 'a'+dev);
			free(gptdev);
			goto abort;
		}

		if(!comparegpt(gptdev, gptdev2)) {
			free(gptdev);
			free(gptdev2);
			goto abort;
		}

		free(gptdev2);


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


		for(ii=0; ii<gptdev->head.entryCount-1&&maxreturn>0; ++ii)
			maxreturn=test_kdzfile_gpt_entry(dev, maxreturn,
gptkdz->entry+ii, gptdev->entry+ii);

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

	unpackchunk_free(ctx, true);

	return -1;
}

static int test_kdzfile_gpt_entry(int dev, int maxreturn,
struct gpt_entry *kdzentry, struct gpt_entry *deventry)
{
	const char *const ignore[]={
		"",
		"OP",
		"cache",
		"cust",
		"grow",
		"grow2",
		"grow3",
		"grow4",
		"grow5",
		"grow6",
		"grow7",
		"system",
		"userdata",
	};
	unsigned char lo=0, hi=sizeof(ignore)/sizeof(ignore[0]);
	int res, mid;


	while(mid=(hi+lo)/2, res=strcmp(kdzentry->name, ignore[mid])) {
		if(res<0) hi=mid;
		else if(res>0) lo=mid+1;
		if(lo==hi) {
			mid=-1; /* run all tests */
			break;
		}
	}

	/* ignore slices which are likely to be modified by LineageOS or LGE */
	if(mid>=0) return maxreturn;

	/* check name, flags, and type */
	if(deventry->flags!=kdzentry->flags||
uuid_compare(deventry->type, kdzentry->type)||
strncmp(deventry->name, kdzentry->name, sizeof(deventry->name)))
		return 0;

	/* the Ids always differ on sdg */
	if(maxreturn&2&&dev!=6&&
uuid_compare(deventry->id, kdzentry->id)) maxreturn&=~2;

	/* question is whether to try the above tests on non-empty entries? */

	/* on-device border between OP and userdata differs */
	if(deventry->startLBA!=kdzentry->startLBA||
deventry->endLBA!=kdzentry->endLBA)
		return 0;

	return maxreturn;
}


int report_kdzfile(struct kdz_file *kdz)
{
	int i;
	int dev=-1;
	off64_t blksz;
	struct unpackctx _ctx={0,}, *const ctx=&_ctx;
	char *map=NULL;
	char *buf=NULL;
	uint32_t bufsz=0;
	uint32_t cur;
	uint32_t mismatch;

	if(verbose>=11) fprintf(stderr, "DEBUG: starting report code\n");

	for(i=1; i<=kdz->dz_file.chunk_count; ++i) {
		const char *fmt;
		const struct dz_chunk *const dz=&kdz->chunks[i].dz;

		if(dz->device!=dev) {
			dev=dz->device;

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

		if(!unpackchunk_alloc(ctx, kdz, i)) goto abort;

		mismatch=0;



		if(dz->target_size%blksz) {
			fprintf(stderr, "Block, not a multiple of block size!\n");
		}

		cur=0;

		while(cur<dz->target_size) {
			if(unpackchunk(ctx, buf, bufsz)!=bufsz)
				goto abort_block;

			if(memcmp(map+dz->target_addr*blksz+cur, buf, blksz))
				++mismatch;

			cur+=blksz;
		}

		if(!unpackchunk_free(ctx, false))
			++mismatch; /* not exactly, but sort of */

		if(mismatch)
			fmt="Chunk %1$d(%2$s): %7$d of %3$ld blocks mismatched (%4$lu-%5$lu,trim=%6$lu)\n";
		else
			fmt="Chunk %d(%s): all %ld blocks matched (%lu-%lu,trim=%lu)\n";
		printf(fmt, i, dz->slice_name, dz->target_size/blksz,
dz->target_addr, dz->target_addr+dz->trim_count, dz->trim_count, mismatch);

	abort_block:
		unpackchunk_free(ctx, true);
	}

	free(buf);

	return 0;

abort:
	if(buf) free(buf);

	unpackchunk_free(ctx, true);

	return 128;
}


int write_kdzfile(const struct kdz_file *const kdz,
const char *const slice_name, const bool simulate)
{
	int i, j;
	int dev;
	struct unpackctx _ctx={0,}, *const ctx=&_ctx;
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
		int flags=O_LARGEFILE;
		char name[64];
		/* on Linux O_EXCL refuses if mounted */
		if(!simulate) flags|=O_EXCL|O_WRONLY;
		snprintf(name, sizeof(name), "/dev/block/bootdevice/by-name/%s",
slice_name);

		if((fd=open(name, flags))<0) {
			const char *fmt;
			if(errno==EBUSY) fmt="\"%s\" mounted, refusing to continue\n";
			else fmt="Failed to open \"%s\": %s\n";

			fprintf(stderr, fmt, name, strerror(errno));
			return 0;
		}
	}


	for(; i<=kdz->dz_file.chunk_count; ++i) {
		const struct dz_chunk *const dz=&kdz->chunks[i].dz;
		uint64_t range[2];

		/* obviously skip other slices */
		if(strcmp(slice_name, dz->slice_name)) continue;

		if(dev!=dz->device) { /* trouble! */
			fprintf(stderr, "PANIC: \"%s\"'s chunks cross multiple devices?!\n", slice_name);
			goto abort;
		}

		if(bufsz<dz->target_size) {
			bufsz=dz->target_size;
			free(buf);
			if(!(buf=malloc(bufsz))) {
				fprintf(stderr, "Memory allocation failure!\n");
				goto abort;
			}
		}


		if(!unpackchunk_alloc(ctx, kdz, i)) goto abort;

		if(unpackchunk(ctx, buf, dz->target_size)!=dz->target_size)
			goto abort;

		if(!unpackchunk_free(ctx, false)) goto abort;


		if(verbose>=3) fprintf(stderr, "DEBUG: chunk %u\n", i);

		/* write the device while trying to keep wear to a minimum */
		for(j=0; j<dz->target_size; j+=blksz) {
			uint64_t target=dz->target_addr*blksz+j;

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
				pwrite64(fd, buf+j, blksz, target-offset);
		}


		/* Discard (TRIM) all possible space */
		/* Note, this is being done on the slice, so slice-relative */

		/* start byte */
		range[0]=dz->target_addr*blksz-offset+dz->target_size;

		range[1]=dz->trim_count*blksz-dz->target_size;


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
	unpackchunk_free(ctx, true);

	if(fd>=0) close(fd);
	if(buf) free(buf);

	if(verbose<3) putchar('\n');

	return 0;
}


static bool unpackchunk_alloc(struct unpackctx *const ctx,
const struct kdz_file *const kdz, const unsigned chunk)
{
	const struct dz_chunk *const dz=&kdz->chunks[chunk].dz;

	if(verbose>=12) fprintf(stderr, "DEBUG: %s called\n", __func__);

	if(dz->target_size%kdz->devs[dz->device].blksz) {
		fprintf(stderr, "Block, not a multiple of block size!\n");
		return false;
	}

	ctx->kdz=kdz;
	ctx->chunk=chunk;


	/* initializing Zlib */
	ctx->zstr.zalloc=Z_NULL;
	ctx->zstr.zfree=Z_NULL;

	ctx->zstr.next_in=(Bytef *)(kdz->map+kdz->chunks[chunk].zoff);
	ctx->zstr.avail_in=dz->data_size;
	ctx->zstr.total_in=0;
	ctx->zstr.avail_out=0;
	ctx->zstr.total_out=0;

	if(inflateInit(&ctx->zstr)!=Z_OK) {
		fprintf(stderr, "inflateInit() failed: %s\n", ctx->zstr.msg);
		inflateEnd(&ctx->zstr);
		return false;
        }

	ctx->z_finished=0;

	ctx->fail=0;


	/* libcrypto's MD5 */
	(*pMD5_Init)(&ctx->md5);

	/* Zlib's CRC32 */
	ctx->crc=crc32(0, Z_NULL, 0);


	/* lastly mark as initialized */
	ctx->valid=1;


	return true;
}


static int unpackchunk(struct unpackctx *const ctx, void *buf, size_t bufsz)
{
	const struct dz_chunk *const dz=&ctx->kdz->chunks[ctx->chunk].dz;
	int zret;

	if(verbose>=12) fprintf(stderr, "DEBUG: %s called\n", __func__);

	ctx->zstr.next_out=(unsigned char *)buf;
	ctx->zstr.avail_out=bufsz;

	if(ctx->z_finished) return 0;

	switch((zret=inflate(&ctx->zstr, Z_SYNC_FLUSH))) {
	case Z_STREAM_END:
		ctx->z_finished=1;
	case Z_OK:
		break;
	default:
		fprintf(stderr, "Chunk %d(%s): inflate() failed: %s\n",
ctx->chunk, dz->slice_name, ctx->zstr.msg);
		if(verbose>=3) fprintf(stderr,
"DEBUG: inflate()=%d, @ %lu bytes of input, %lu bytes of output\n", zret,
ctx->zstr.total_in, ctx->zstr.total_out);

		ctx->fail=1;

		return -1;
	}

	(*pMD5_Update)(&ctx->md5, buf, bufsz);
	ctx->crc=crc32(ctx->crc, (Bytef *)buf, bufsz);

	return bufsz;
}


static bool unpackchunk_free(struct unpackctx *const ctx, bool discard)
{
	const struct dz_chunk *const dz=&ctx->kdz->chunks[ctx->chunk].dz;
	char md5out[16];

	if(verbose>=12) fprintf(stderr, "DEBUG: %s called, %sdiscarding\n",
__func__, discard?"":"not ");

	if(!ctx->valid) {
		if(!discard&&verbose>=12) fprintf(stderr,
"unpackchunk_free called on invalid context\n");
		return false;
	}
	ctx->valid=0; /* or about to be invalid */

	if(inflateEnd(&ctx->zstr)!=Z_OK||!ctx->z_finished) {
		if(!discard) goto fail;
	} else discard=false; /* if we got to the end, why not try? */

	if(ctx->fail) {
		if(verbose>=12) fprintf(stderr, "DEBUG: %s context failed\n",
__func__);
		goto fail;
	}

	if(!discard) {
		if(ctx->crc!=le32toh(dz->crc32)) goto fail;

		(*pMD5_Final)((unsigned char *)md5out, &ctx->md5);

		if(memcmp(md5out, dz->md5, sizeof(md5out))) goto fail;
	}

	return true;

fail:
	fprintf(stderr, "Failed while finishing to chunk %d (\"%s\")\n",
ctx->chunk, dz->slice_name);

	return false;
}

