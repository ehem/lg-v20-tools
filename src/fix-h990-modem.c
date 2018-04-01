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


#include <linux/fs.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <endian.h>
#include <errno.h>

#include "bootimg.h"


struct misc_hwinfo {
	char magic[16];  // must be set to "LGE ONE BINARY" to be valid.
	uint32_t reserved;
	uint16_t hwinfo_major;
	uint16_t hwinfo_minor;
	char lg_model_name[20];
	uint32_t sim_num;
};

const char magic[]="LGE ONE BINARY\0";


char model_name[23]="";
char sim_str[2]={'\0', '\0'};
#ifndef LINEAGEOS
char dsds_str[10]; //info->sim_num==2?"dsds":"none"
#endif
char cmdline[2048]; /* lots of slack so all the entries can be appended */


struct entry {
	const char *const key;
	const char *const val;
	const unsigned overwrite:1;
};

const struct entry entries[]={
	{" model.name=",			model_name,	1},
	{" lge.sim_num=",			sim_str,	1},
#ifndef LINEAGEOS
	{" lge.dsds=",				dsds_str,	1},
	{" androidboot.bl_unlock_complete=",	"false",	0},
	{" androidboot.authorized_kernel=",	"true",		0},
#endif
};

static int load_misc(int verbose, struct misc_hwinfo *hwinfo, int mode);
static int read_misc(int verbose);
static int write_misc(int verbose, const char *model_name);
static int write_sysfs(const char *model_name, const char *sim_str);
static int write_bootimg(int fd, int verbose);


/* come on Android, even the Linux kernel has strchrnul() */
static char *strchrnul(const char *s, int c)
{
	char *r;
	return (r=strchr(s, c))?r:(char *)(s+strlen(s));
}

int main(int argc, char **argv)
{
	int fd;
	uint8_t sim_num=0;
	int i;
	int opt;
	int bootmode=0, writemode=0;
	int verbose=1;


	while((opt=getopt(argc, argv, "1234m:bwqvhH?"))>=0) {
		switch(opt) {
		case '1':
		case '2':
		case '3':
		case '4':
			sim_num=opt-'0';
			break;
		case 'm':
			if(verbose&&memcmp("LG-", optarg, 3))
				fprintf(stderr,
"Warning: Provided model name doesn't start with \"LG-\", this is abnormal!\n");
			strlcpy(model_name, optarg, sizeof(model_name));
			break;

		case 'b':
			bootmode=1;
			break;

		case 'w':
			writemode=1;
			break;

		case 'q':
			verbose=0;
			break;
		case 'v':
			verbose=2;
			break;

		case 'h':
		case 'H':
		case '?':
		default:
			fprintf(stderr,
"Usage: %s [-m <model>] [-1|-2] [-b] [-w] [-q] [-v] [-h] [<Android boot image>]\n"
"  -m <model>  Specify model name which generally starts with \"LG-H\".  Known\n"
"      strings include \"LG-H990\", \"LG-H990ds\", \"LG-H990N\" and \
\"LG-H990TR\"\n"
"  -1  Specify model is single-SIM\n"
"  -2  Specify model is dual-SIM\n"
"  -b  Boot mode, set a fallback for sysfs during boot\n"
"  -w  Write \"misc\" area, HAZARDOUS!\n"
"  -q  Quiet mode (no output)\n"
"  -v  Verbose mode\n"
"If the model isn't specified on command-line, an attempt will be made to\n"
"retrieve the model name from the \"misc\" area.  References to a LG-H990V and\n"
"LG-H998 have been found, but no hardware has been seen yet.\n", argv[0]);
			return 1;
		}
	}

	if((argc-optind)>1) {
		fprintf(stderr, "Too many non-option arguments, only 1 boot image allowed.\n");
		return 1;
	}



	if(model_name[0])
		read_misc(verbose); /* someone modified image, warnings */
	else if(read_misc(verbose)) {
		/* LG-H990ds seems the safest fallback string, no HW features
missing, others appear okay with value */
		if(bootmode) strcpy(model_name, "LG-H990ds");
		else return 1;
	}


	if(writemode)
		write_misc(verbose, model_name);


	if(!sim_num) sim_num=!strcmp(model_name, "LG-H990ds")||!strcmp(model_name, "LG-H990N")?2:1;
	else if(verbose&&(!strcmp(model_name, "LG-H990ds")||!strcmp(model_name, "LG-H990N")?2:1)!=sim_num)
		fprintf(stderr,
"Alert!  Specified SIM count differs from expected count (%d).\n\a\n",
!strcmp(model_name, "LG-H990ds")||!strcmp(model_name, "LG-H990N")?2:1);


	sim_str[0]='0'+sim_num;



	if((i=write_sysfs(model_name, sim_str))<0) return 1;
	if(i>0&&argc-optind==0&&!verbose) return 0;



#ifndef LINEAGEOS
	strcpy(dsds_str, sim_num==1?"none":"dsds");
#endif


	if(argc-optind==0) {
		if(verbose) {
			fputs("Options to add/override in boot image: \"",
stdout);
			for(i=0; i<(int)(sizeof(entries)/sizeof(entries[0])); ++i) {
				fputs(entries[i].key+!i, stdout);
				fputs(entries[i].val, stdout);
			}
			fputs("\"\n", stdout);
		}

		return 0;
	}


	if(!strcmp(argv[optind], "-")) fd=0;
	else if((fd=open(argv[optind], O_RDWR|O_LARGEFILE))<0) {
		if(verbose) fprintf(stderr, "Failed when opening \"%s\": %s\n",
argv[optind], strerror(errno));
		close(fd);
		return 1;
	}


	return write_bootimg(fd, verbose);
}


static int load_misc(int verbose, struct misc_hwinfo *info, int mode)
{
	uint32_t blksz;
	const char *error;
	int fd;

	if((fd=open("/dev/block/bootdevice/by-name/misc", mode|O_LARGEFILE))<0) {
		error="Failed to open misc area: %s\n";
		goto problem;
	}

	/* I hope no one produces UFS with multiple block sizes */
	if(ioctl(fd, BLKSSZGET, &blksz)<0) {
		error="Failed to get device block size: %s\n";
		goto problem;
	}

	/* our minimum standard */
	if(blksz<2048) blksz=2048;


	if(lseek(fd, blksz*103, SEEK_SET)!=blksz*103) {
		error="Seek failed: %s\n";
		goto problem;
	}

	if(read(fd, info, sizeof(struct misc_hwinfo))!=sizeof(struct misc_hwinfo)) {
		error="Read failed: %s\n";
		goto problem;
	}

	if(lseek(fd, blksz*103, SEEK_SET)!=blksz*103) {
		error="Seek failed: %s\n";
		goto problem;
	}

	return fd;

problem:
	if(verbose) fprintf(stderr, error, strerror(errno));
	if(fd>=0) close(fd);
	return -1;
}


static int read_misc(int verbose)
{
	int fd;
	struct misc_hwinfo info;
	const char *error;

	if((fd=load_misc(!model_name[0]||verbose, &info, O_RDONLY))<0) return 1;

	/* this function doesn't need to keep it around */
	close(fd);


	if(memcmp(info.magic, magic, sizeof(magic))) {
		error="Hardware information missing from \"misc\" area!\n";
		goto problem;
	}

	if(verbose) printf("LGE model information format version %hd.%hd\n", info.hwinfo_major, info.hwinfo_minor);

	if(!model_name[0]||!strcmp(model_name, info.lg_model_name))
		strlcpy(model_name, info.lg_model_name,
sizeof(model_name));
	else if(verbose)
		fprintf(stderr,
"Warning model name differs from misc area (\"%s\").\n\a\n",
info.lg_model_name);

	return 0;

problem:
	if(!model_name[0]||verbose) fprintf(stderr, error, strerror(errno));
	return model_name[0]?0:1;
}


static int write_misc(int verbose, const char *model_name)
{
	int fd;
	struct misc_hwinfo hwinfo;
	const char *error;


	if((fd=load_misc(verbose|1, &hwinfo, O_RDWR))<0)
		return 1;

	/* the magic number is our only check */
	memcpy(hwinfo.magic, magic, sizeof(magic));

	/* completely overwrite the model name */
	memset(hwinfo.lg_model_name, 0, sizeof(hwinfo.lg_model_name));
	strlcpy(hwinfo.lg_model_name, model_name, sizeof(hwinfo.lg_model_name));


	if(write(fd, &hwinfo, sizeof(struct misc_hwinfo))!=sizeof(struct misc_hwinfo)) {
		error="Write failed: %s\n";
		goto problem;
	}


	if(close(fd)<0) {
		error="Error on close(): %s\n";
		goto problem;
	}

	return 0;

problem:
	fprintf(stderr, error, strerror(errno));
	close(fd);
	return 1;
}


static int _write_sysfs(const char *name, const char *val, const char *type);
static int write_sysfs(const char *model_name, const char *sim_str)
{
	int ret=0;

	ret+=_write_sysfs("/sys/devices/soc/2080000.qcom,mss/"
"dirtysanta_lg_model_name", model_name, "model name");

	ret+=_write_sysfs("/sys/devices/soc/2080000.qcom,mss/"
"dirtysanta_sim_num", sim_str, "SIM count");

	return ret;
}
static int _write_sysfs(const char *name, const char *val, const char *type)
{
	int fd;
	int ret=0;

	if((fd=open(name, O_RDWR))<0) switch(errno) {
	case EPERM:
		if(!getuid()&&!geteuid()) ++ret; /* was set via command-line */
		break;
	case ENOENT:
		/* absent, likely TWRP kernel which lacks modem sup */
		break;

	default:
		fprintf(stderr, "Error opening %s: %s\n", type, strerror(errno));
		ret=-1000;
	} else {
		++ret;
		if(write(fd, val, strlen(val))!=(ssize_t)strlen(val)) {
			fprintf(stderr, "Error writing %s: %s\n", type,
strerror(errno));
			ret=-1000;
		}

		if(close(fd)) {
			fprintf(stderr, "Error closing %s: %s\n", type,
strerror(errno));
			ret=-1000;
		}
	}

	return ret;
}


static int write_bootimg(int fd, int verbose)
{
	uint32_t blksz=64;
	int ret=0;
	boot_img_hdr *bootimg;
	int i;


	if(!(bootimg=malloc(blksz))) {
		fprintf(stderr, "Memory allocation failed.\n");
		return 1;
	}

	if(read(fd, bootimg, blksz)!=blksz) {
		fprintf(stderr, "Read failed: %s\n", strerror(errno));
		ret=1;
		goto fail;
	}

	if(memcmp(bootimg->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE)) {
		fprintf(stderr, "Bad magic number on boot image!\n");
		ret=1;
		goto fail;
	}

	blksz=le32toh(bootimg->page_size);

	if(blksz<512||(blksz&(blksz-1))) {
		fprintf(stderr, "Block size in boot image impossible!\n");
		ret=1;
		goto fail;
	}

	{
		boot_img_hdr *tmp=bootimg;
		if(!(bootimg=realloc(tmp, blksz))) {
			fprintf(stderr, "Memory allocation failed.\n");
			ret=1;
			bootimg=tmp;
			goto fail;
		}
	}

	if(read(fd, (char *)bootimg+64, blksz-64)!=blksz-64) {
		fprintf(stderr, "Read failed: %s\n", strerror(errno));
		ret=1;
		goto fail;
	}


	memcpy(cmdline, bootimg->cmdline, BOOT_ARGS_SIZE);
	memcpy(cmdline+BOOT_ARGS_SIZE, bootimg->extra_cmdline, BOOT_EXTRA_ARGS_SIZE);
	cmdline[BOOT_ARGS_SIZE+BOOT_EXTRA_ARGS_SIZE]='\0';

	for(i=0; i<(int)(sizeof(entries)/sizeof(entries[0])); ++i) {
		char *key, *val;
		char *end;

		if((key=strstr(cmdline, entries[i].key)))
			val=key+strlen(entries[i].key);

		else if(!strncmp(cmdline, entries[i].key+1, strlen(entries[i].key)-1))
			val=cmdline+strlen(entries[i].key)-1;

		else {
			/* cmdline has more than enough slack for this */
			strcat(cmdline, entries[i].key);
			strcat(cmdline, entries[i].val);

			continue; /* done for this one */
		}

		/* some of the values can safely be left alone */
		if(!entries[i].overwrite) continue;

		if(!strncmp(val, entries[i].val, strlen(entries[i].val))) continue;

		/* sigh, need to do something about this */
		end=strchrnul(val, ' ');
		if(end-val!=(long)strlen(entries[i].val)) /* different length */
			memmove(val+strlen(entries[i].val), end, strlen(end)+1);

		memcpy(val, entries[i].val, strlen(entries[i].val));
	}

	i=strlen(cmdline);
	memset(cmdline+i, 0, sizeof(cmdline)-i);

	if(verbose) fprintf(stderr, "New boot image command line is: \"%s\"\n", cmdline);

	if(i>BOOT_ARGS_SIZE+BOOT_EXTRA_ARGS_SIZE) {
		if(verbose) fprintf(stderr,
"Resultant command-line is too long for boot image, cannot continue\n");
		ret=1;
		goto fail;
	}

	memcpy(bootimg->cmdline, cmdline, BOOT_ARGS_SIZE);
	memcpy(bootimg->extra_cmdline, cmdline+BOOT_ARGS_SIZE, BOOT_EXTRA_ARGS_SIZE);


	if(fd) {
		if(lseek(fd, 0, SEEK_SET)) {
			if(verbose) fprintf(stderr, "Seek failed: %s\n",
strerror(errno));
			ret=1;
			goto fail;
		}

		if(write(fd, bootimg, blksz)!=blksz) {
			if(verbose) fprintf(stderr, "Write failed: %s\n",
strerror(errno));
			ret=1;
			goto fail;
		}
	} else {
		int32_t cnt=blksz;
		do {
			if(write(1, bootimg, blksz)!=cnt) {
				if(verbose) fprintf(stderr,
"Write failed: %s\n", strerror(errno));
				ret=1;
				goto fail;
			}

			if((cnt=read(0, bootimg, blksz))<0) {
				if(verbose) fprintf(stderr, "Read failed: %s\n",
strerror(errno));
				ret=1;
				goto fail;
			}
		} while(cnt>0);
		fd=1; /* check for errors on close() */
	}


	if(close(fd)) {
		if(verbose) fprintf(stderr, "Close failed: %s\n",
strerror(errno));
		ret=1;
		goto fail;
	}

fail:
	free(bootimg);
	return ret;
}

