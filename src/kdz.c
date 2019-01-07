/* **********************************************************************
* Copyright (C) 2017-2019 Elliott Mitchell				*
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
#include <sys/mount.h>
#include <sys/stat.h>
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


/* open the appropriate device (flags=O_RDONLY or O_RDWR, most often) */
static int open_device(const struct kdz_file *kdz, int dev, int flags);

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
		if((fd=open_device(ret, i, O_RDONLY))<0) goto abort;

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
		if(!(maxreturn&matches[mid].result)) {
			if(verbose>4) fprintf(stderr,
"DEBUG: Skipping check of \"%s\", match level too low\n", slice_name);
			continue;
		}


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
		if(!mismatch) {
			if(verbose>4) fprintf(stderr,
"DEBUG: \"%s\" matched perfectly\n", slice_name);
			continue;
		}


		/* nuke some maxreturn bits, unless special-case match */
		if(matches[mid].result<4) {
			maxreturn&=~matches[mid].result;
			if(verbose>4) fprintf(stderr,
"DEBUG: Matching \"%s\" failed, new maxreturn=%d\n", slice_name, maxreturn);
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



		/* check header fields, okay for the CRCs to differ
		** Standard requires reserving space for 128 entries, whether
		** or not that many are actually initialized.  As such the
		** count can be legitimately increased. */
		if(memcmp(&gptdev->head, &gptkdz->head,
(char *)&gptdev->head.headerCRC32-(char *)&gptdev->head.magic)||
memcmp(&gptdev->head.reserved, &gptkdz->head.reserved,
(char *)&gptdev->head.altLBA-(char *)&gptdev->head.reserved)||
memcmp(&gptdev->head.dataStartLBA, &gptkdz->head.dataStartLBA,
(char *)&gptdev->head.entryStart-(char *)&gptdev->head.dataStartLBA))
			maxreturn=0; /* fail */
		/* Extremely careful reading of the specification is needed.
		** The Alternate LBA field is supposed to point to the LBA of
		** the alternate GPT, whether or not the primary is being
		** examined.  This is easy to misunderstand as the Alternate
		** LBA pointing at the /other/ GPT. */
		if(gptdev->head.altLBA!=gptkdz->head.altLBA&&
gptkdz->head.altLBA!=1&&gptdev->head.altLBA!=gptkdz->head.myLBA)
			maxreturn=0;


		for(ii=0; ii<gptdev->head.entryCount-1&&maxreturn>0; ++ii)
			maxreturn=test_kdzfile_gpt_entry(dev, maxreturn,
gptkdz->entry+ii, gptdev->entry+ii);

		free(gptdev);
		free(gptkdz);

		if(verbose>4) {
			if(maxreturn>0) fprintf(stderr,
"DEBUG: GPT matched sufficiently to remain candidate\n");
			else fprintf(stderr, "DEBUG: GPT matching failed\n");
		}

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


static bool fix_gpt_persistent(struct gpt_data *dst,
struct gpt_buf *const gptdev, unsigned short target, unsigned short index,
const uint32_t blksz);
static bool add_fix_gpt_entry(struct gpt_data *dst,
struct gpt_buf *const unused0, unsigned short target, unsigned short index,
const uint32_t unused1);


static const struct {
	const char *const name;
	bool (*func)(struct gpt_data *gptkdz, struct gpt_buf *const gptdevbuf,
unsigned short target, unsigned short index, const uint32_t blksz);
	unsigned order; /* where we want to put it, 0==NOP */
} gpt_targets[]={
	{ "OP",		add_fix_gpt_entry,	2 },
	{ "cache",	add_fix_gpt_entry,	5 },
	{ "cust",	add_fix_gpt_entry,	3 },
	{ "persistent",	fix_gpt_persistent,	0 },
	{ "system",	add_fix_gpt_entry,	4 },
	{ "userdata",	add_fix_gpt_entry,	1 },
};

static int gpt_index[sizeof(gpt_targets)/sizeof(gpt_targets[0])];

static int finish_gpt_normal(struct gpt_data *gpt);

static int finish_gpt_reverse(struct gpt_data *gpt);

static int get_target(const char *const key);

static long long get_OP_size(void);

bool fix_gpts(const struct kdz_file *kdz, const bool alt_order,
const bool simulate)
{
	int i, j;
	int dev=-1;
	struct unpackctx _ctx={0,}, *const ctx=&_ctx;
	size_t bufsz=4096;
	char *buf=NULL;
	struct gpt_data *gptkdz=NULL;
	long long opsz;
	bool ret=true;

	if((opsz=get_OP_size())<0) return false;

	if(!(buf=malloc(bufsz))) return false;


	for(i=1; i<=kdz->dz_file.chunk_count; ++i) {
		const struct dz_chunk *const dz=&kdz->chunks[i].dz;
		uint32_t blksz;
		struct gpt_buf gpt_buf;

		unsigned touched=0; /* flag modified GPTs */

		if(strcmp(dz->slice_name, "PrimaryGPT")) continue;

		if((dev=open_device(kdz, dz->device, O_RDWR))<0) goto abort;

		memset(gpt_index, -1, sizeof(gpt_index));

		blksz=kdz->devs[dz->device].blksz;


		if(dz->target_size>bufsz) {
			free(buf);
			bufsz=dz->target_size;
			if(!(buf=malloc(bufsz))) {
				fprintf(stderr, "Memory allocation failure.\n");
				goto abort;
			}
		}

		if(!unpackchunk_alloc(ctx, kdz, i)) goto abort;

		if(unpackchunk(ctx, buf, dz->target_size)!=dz->target_size)
			goto abort;

		if(!unpackchunk_free(ctx, false)) goto abort;

		gpt_buf.bufsz=dz->target_size;
		gpt_buf.buf=buf;
		if(!(gptkdz=readgptb(gptbuffunc, &gpt_buf, blksz, GPT_PRIMARY))) {
			fprintf(stderr,
"Failed to load GPT from primary image in KDZ, aborting.\n");
			goto abort;
		}

		gpt_buf.bufsz=kdz->devs[dz->device].len;
		gpt_buf.buf=kdz->devs[dz->device].map;


		/* Search phase */

		for(j=0; j<gptkdz->head.entryCount; ++j) {
			int mat;

			if((mat=get_target(gptkdz->entry[j].name))>=0)
				if(gpt_targets[mat].func(gptkdz, &gpt_buf, mat,
j, blksz)) touched=1;
		}

		if(touched&&
!(alt_order?finish_gpt_reverse:finish_gpt_normal)(gptkdz))
			return false;

		if(!simulate) {
			if(!writegpt(dev, gptkdz)) {
				fprintf(stderr, "\bGPT write operation failed!\n");
				goto abort;
			}

			/* the GPT code ignores the first block */
			if(memcmp(buf, kdz->devs[dz->device].map, 512))
				pwrite(dev, buf, 512, 0);
		} else close(dev);

		free(gptkdz);
		gptkdz=NULL;

		if(!simulate) {
			/* failure indicates kernel may be using old table */
#ifndef DEBUG
			if(ioctl(dev, BLKRRPART, NULL)) {
				if(verbose>=7) fprintf(stderr,
"ioctl(BLKRRPART) failed, kernel still uses old GPT\n");
				ret=false;
			}
#endif
		}
	}

	return ret;

abort:
	if(buf) free(buf);

	if(gptkdz) free(gptkdz);

	if(dev>=0) close(dev);

	unpackchunk_free(ctx, true);

	return false;
}

static bool fix_gpt_persistent(struct gpt_data *gptkdz,
struct gpt_buf *const gptdevbuf, unsigned short target, unsigned short index,
const uint32_t blksz)
{
	struct gpt_data *gptdev=NULL;
	struct gpt_entry *const kdzentr=gptkdz->entry+index;

	gptdev=readgptb(gptbuffunc, gptdevbuf, blksz, GPT_ANY);

	if(gptdev&&!uuid_is_null(gptdev->entry[index].id))
		uuid_copy(kdzentr->id, gptdev->entry[index].id);

	else {
		int urandom;
		urandom=open("/dev/urandom", O_RDONLY);

		/* I'm guessing this is appropriate */
		/* if this fails, well can't do much */
		if(urandom>=0) {
			read(urandom, &kdzentr->id, sizeof(kdzentr->id));
			close(urandom);
		}
	}

	if(gptdev) free(gptdev);

	return false;
}


static int finish_gpt_normal(struct gpt_data *gpt)
{
	int64_t delta;
	long long opsz;

	struct gpt_entry *opentr, *datentr, *startentr, *endentr;

	int i;


	i=get_target("OP");
	if(verbose>=7) fprintf(stderr, "DEBUG: OP: target=%d ", i);
	if((i=gpt_index[i])<0) return true;
	if(verbose>=7) fprintf(stderr, "index=%d name=\"%s\"\n", i,
gpt->entry[i].name);
	opentr=gpt->entry+i;

	i=get_target("userdata");
	if(verbose>=7) fprintf(stderr, "DEBUG: userdata: target=%d ", i);
	if((i=gpt_index[i])<0) {
		fprintf(stderr, "Error: userdata and OP on different major "
"devices, cannot compensate!\n");
		return true;
	}
	if(verbose>=7) fprintf(stderr, "index=%d name=\"%s\"\n", i,
gpt->entry[i].name);
	datentr=gpt->entry+i;




	if((opsz=get_OP_size())<0) return false;

	/* convert to block count */
	delta=opsz/gpt->blocksz;

	/* oddly OP in KDZ files is non-zero size */
	delta-=(opentr->endLBA-opentr->startLBA+1);


	if(datentr->startLBA<opentr->startLBA) {
		if(verbose>=7) fprintf(stderr,
"DEBUG: userdata before OP, userdata %lu-%lu, OP %lu-%lu\n",
datentr->startLBA, datentr->endLBA, opentr->startLBA, opentr->endLBA);
		if(datentr->endLBA+1!=opentr->startLBA) {
			fprintf(stderr, "Error: userdata is non-contiguous "
"with OP, unable to adjust\n");
			return true;
		}
		startentr=opentr;
		endentr=datentr;
		delta=-delta;
	} else if(datentr->startLBA>opentr->endLBA) {
		if(verbose>=7) fprintf(stderr,
"DEBUG: OP before userdata, OP %lu-%lu, userdata %lu-%lu\n",
opentr->startLBA, opentr->endLBA, datentr->startLBA, datentr->endLBA);
		if(datentr->startLBA!=opentr->endLBA+1) {
			fprintf(stderr, "Error: userdata is non-contiguous "
"with OP, unable to adjust\n");
			return true;
		}
		startentr=datentr;
		endentr=opentr;
	} else {
		fprintf(stderr, "Error: userdata and OP overlap???  Removing "
"OP entry!\n");
		memset(opentr, 0, sizeof(struct gpt_entry));
		return true;
	}
	if(verbose>=4) fprintf(stderr,
"DEBUG: OP: old start idx %lu old end idx %lu, delta=%+ld\n",
startentr->startLBA, endentr->endLBA, delta);
	startentr->startLBA+=delta;
	endentr->endLBA+=delta;
	if(verbose>=4) fprintf(stderr,
"DEBUG: OP: new start %lu, new end %lu\n", startentr->startLBA,
endentr->endLBA);

	/* Yes, they ARE this perverse; adjust userdata first */
	if(opsz<=0)
		memset(opentr, 0, sizeof(struct gpt_entry));

	return 1;
}

static struct gpt_entry *gptsort;
static int _finish_gpt_reverse_sort_cur(const void *a, const void *b);
static int _finish_gpt_reverse_sort_ord(const void *a, const void *b);
static int finish_gpt_reverse(struct gpt_data *gpt)
{
	int ordered[sizeof(gpt_targets)/sizeof(gpt_targets[0])];
	int i, lo, hi, cnt=0;

	for(i=0; i<sizeof(gpt_targets)/sizeof(gpt_targets[0]); ++i)
		if(~gpt_index[i]&&gpt_targets[i].order) ordered[cnt++]=i;

	if(verbose>=7) {
		fprintf(stderr, "DEBUG: %s: ordered", __func__);
		for(i=0; i<cnt; ++i) fprintf(stderr, " %d", ordered[i]);
		putc('\n', stderr);
	}

	if(cnt<=1) {
		char out[40];
		uuid_unparse(gpt->head.diskUuid, out);

		if(verbose>=7) fprintf(stderr, "DEBUG: Skipping dev %s, %d "
"notable\n", out, cnt);
		return 1; /* not much we can do here */
	}

	if(verbose>=4) {
		char out[40];
		uuid_unparse(gpt->head.diskUuid, out);
		fprintf(stderr, "DEBUG: Going to do %s, %d can be reordered\n",
out, cnt);
	}

	gptsort=gpt->entry;
	qsort(ordered, cnt, sizeof(int), _finish_gpt_reverse_sort_cur);

	if(verbose>=7) {
		fprintf(stderr, "DEBUG: %s: LBA sorted entries:\n", __func__);
		for(i=0; i<cnt; ++i)
			fprintf(stderr, "DEBUG: %s: start=%lu end=%lu\n",
gpt->entry[gpt_index[ordered[i]]].name,
gpt->entry[gpt_index[ordered[i]]].startLBA,
gpt->entry[gpt_index[ordered[i]]].endLBA);
	}

	lo=0;
	while(cnt-lo>=2) {
		uint64_t base=gpt->entry[gpt_index[ordered[lo]]].startLBA;
		hi=lo;
		while(++hi<cnt)
			if(gpt->entry[gpt_index[ordered[hi-1]]].endLBA+1!=
gpt->entry[gpt_index[ordered[hi]]].startLBA) break;
		if(hi-lo<2) {
			if(verbose>=7) fprintf(stderr, "DEBUG: %s: Skipping "
"reordering group of %d slices\n", __func__, hi-lo);
			lo=hi;
			continue;
		}
		if(verbose>=7) fprintf(stderr, "DEBUG: %s: Reordering group "
"of %d slices\n", __func__, hi-lo);
		qsort(ordered+lo, hi-lo, sizeof(int),
_finish_gpt_reverse_sort_ord);
		do {
			struct gpt_entry *const entr=gpt->entry+gpt_index[ordered[lo]];
			entr->endLBA-=entr->startLBA;
			entr->startLBA=base;
			base+=entr->endLBA+1;
			entr->endLBA=base-1;
			if(verbose>=7) fprintf(stderr, "DEBUG: %s: "
"new entry for \"%s\" start=%lu end=%lu\n", __func__, entr->name,
entr->startLBA, entr->endLBA);
		} while(++lo<hi);
	}

	/* this is needed to deal with adjusting the size of OP */
	finish_gpt_normal(gpt);

	return 1;
}
static int _finish_gpt_reverse_sort_cur(const void *a, const void *b)
{
	return gptsort[gpt_index[*(int *)a]].startLBA-gptsort[gpt_index[*(int *)b]].startLBA;
}
static int _finish_gpt_reverse_sort_ord(const void *a, const void *b)
{
	return gpt_targets[*(int *)a].order-gpt_targets[*(int *)b].order;
}


static bool add_fix_gpt_entry(struct gpt_data *dst,
struct gpt_buf *const unused0, unsigned short target, unsigned short index,
const uint32_t unused1)
{
	if(verbose>=7) fprintf(stderr, "DEBUG: %s: target=%hu index=%hu, "
"entry name=\"%s\"\n", __func__, target, index, dst->entry[index].name);
	gpt_index[target]=index;
	return true;
}


static int get_target(const char *const key)
{
	unsigned char lo=0, hi=sizeof(gpt_targets)/sizeof(gpt_targets[0]);
	int res, mid;

	while(mid=(hi+lo)/2, res=strcmp(key, gpt_targets[mid].name)) {
		if(res<0) hi=mid;
		else if(res>0) lo=mid+1;
		if(lo==hi) return -1;
	}
	return mid;
}


static long long get_OP_size(void)
{
	int i;
	size_t bufsz=4096;
	char *buf=NULL;
	int fd;
	unsigned long long opsz=-1;

	if(access("/dev/block/bootdevice/by-name/cust", F_OK)<0) {
		fprintf(stderr,
"Cannot access /dev/block/bootdevice/by-name/cust, assuming OP size of 0.\n");
		return 0;
	}

	if(mkdir("/cust", 0777)<0) {
		struct stat buf;
		if(errno!=EEXIST) {
			fprintf(stderr,
"Failed to create /cust mount point: %s\n", strerror(errno));
			goto end;
		}
		if(stat("/cust", &buf)<0||!S_ISDIR(buf.st_mode)) {
			fprintf(stderr,
"Failed when creating /cust mount point, unable to continue\n");
			goto end;
		}
		/* this could mean we were run recently... */
		umount("/cust");
	}

	if(mount("/dev/block/bootdevice/by-name/cust", "/cust",
"ext4", MS_RDONLY, "discard")) {
		fprintf(stderr,
"Failed OP resize data retrieval: %s\n", strerror(errno));
		goto end;
	}

	if((fd=open("/cust/official_op_resize.cfg",
O_RDONLY|MS_NOATIME))<0) {
		fprintf(stderr,
"Unable to open official_op_resize.cfg: %s\n", strerror(errno));
		umount("/cust");
		goto end;
	}

	if(!(buf=malloc(bufsz))) goto end;

	if(read(fd, buf, bufsz)<0) {
		fprintf(stderr,
"Failed during read of official_op_resize.cfg: %s\n", strerror(errno));
		opsz=0;
	} else {
		buf[bufsz-1]='\0';

		i=strchr(buf, '=')-buf+1;

		opsz=strtoull(buf+i, NULL, 0);
	}

	if(verbose>=1) fprintf(stderr,
"official_op_resize.cfg makes /OP %lld bytes\n", opsz);

	close(fd);
	umount("/cust");


end:
	if(buf) free(buf);

	return opsz;
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
		/* not the one */
		if(strcmp(slice_name, kdz->chunks[i].dz.slice_name)) continue;

		dev=kdz->chunks[i].dz.device;
	}

	/* Unfortunately we MUST compare with the GPT found in the KDZ file.
	** If the device's GPT has been modified, the offsets found on the
	** device GPT will differ from what the KDZ file has.  */
	for(i=1; i<=kdz->dz_file.chunk_count; ++i) {
		struct gpt_data *gptkdz;
		struct gpt_buf gpt_buf;

		struct unpackctx _ctx={0,}, *const ctx=&_ctx;

		if(strcmp("PrimaryGPT", kdz->chunks[i].dz.slice_name)) continue;
		if(kdz->chunks[i].dz.device!=dev) continue;


		blksz=kdz->devs[dev].blksz;

		gpt_buf.bufsz=(1<<14)+(blksz<<2); /* 16K for entries, +2 blk */
		if(!(gpt_buf.buf=malloc(gpt_buf.bufsz))) {
			fprintf(stderr, "Memory allocation failure when "
"examining KDZ GPT\n");
			return 0;
		}

                if(!unpackchunk_alloc(ctx, kdz, i)) {
			fprintf(stderr, "Failed initializing decompression "
"when examining KDZ GPT\n");
			free(gpt_buf.buf);
			return 0;
		}


		if(unpackchunk(ctx, gpt_buf.buf, gpt_buf.bufsz)<=0) {
			fprintf(stderr, "Failed during decompression when "
"examining KDZ GPT\n");
			free(gpt_buf.buf);
			return 0;
		}


		if(!unpackchunk_free(ctx, true)) {
			fprintf(stderr, "Failed cleanup after decompressing "
"KDZ GPT\n");
			free(gpt_buf.buf);
			return 0;
		}


		if(!(gptkdz=readgptb(gptbuffunc, &gpt_buf, blksz, GPT_ANY))) {
			fprintf(stderr, "Failed to load GPT from KDZ file\n");
			free(gpt_buf.buf);
			return 0;
		}

		free(gpt_buf.buf);

		for(j=0; j<gptkdz->head.entryCount-1; ++j) {
			/* not the one */
			if(strcmp(slice_name, gptkdz->entry[j].name)) continue;

			startLBA=gptkdz->entry[j].startLBA;
			offset=startLBA*blksz;

			break;
		}

		free(gptkdz);
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
			if(ioctl(fd, BLKDISCARD, range)<0&&verbose>=1)
fprintf(stderr, "Discard failed: %s\n", strerror(errno));
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


static int open_device(const struct kdz_file *kdz, int dev, int flags)
{
	char name[32];
	const char *fmt;
	char unit;
#ifdef DEBUG
	if((flags&O_RDWR)==O_RDWR||(flags&O_WRONLY)==O_WRONLY) {
		fmt="/sd%c";
		unit='a';
	} else
#endif
	if((kdz->dz_file.flag_ufs&256)==256) {
		fmt="/dev/block/sd%c";
		unit='a';
	} else {
		fmt="/dev/block/mmcblk%c";
		unit='0';
	}
	snprintf(name, sizeof(name), fmt, unit+dev);

	if(verbose>=5) {
		switch(flags&O_ACCMODE) {
		case O_RDONLY:
			fmt="reading";
			break;
		case O_RDWR:
		default:
			fmt="read-write";
			break;
		case O_WRONLY:
			fmt="writing";
			break;
		}
		fprintf(stderr, "DEBUG: Opening %s, for %s (flags=%d)\n",
name, fmt, flags);
	}

#ifdef DISABLE_WRITES
	flags&=~O_RDWR&~O_WRONLY;
#endif

	if((dev=open(name, flags|O_LARGEFILE))<0) {
		perror("open");
	}

	return dev;
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

	(*pMD5_Update)(&ctx->md5, buf, bufsz-ctx->zstr.avail_out);
	ctx->crc=crc32(ctx->crc, (Bytef *)buf, bufsz-ctx->zstr.avail_out);

	return bufsz-ctx->zstr.avail_out;
}


static bool unpackchunk_free(struct unpackctx *const ctx, bool discard)
{
	char md5out[16];

	if(verbose>=12) fprintf(stderr, "DEBUG: %s called, %sdiscarding\n",
__func__, discard?"":"not ");

	if(!ctx->valid) {
		if(!discard&&verbose>=12) fprintf(stderr,
"unpackchunk_free called on invalid context\n");
		return false;
	}

	/* Yuck, but until this point ctx->kdz could be NULL */
	const struct dz_chunk *const dz=&ctx->kdz->chunks[ctx->chunk].dz;

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

