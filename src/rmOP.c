/* **********************************************************************
* Copyright (C) 2018 Elliott Mitchell					*
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


#define _LARGEFILE64_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <ctype.h>
#include <string.h>
#include <sys/mount.h>
#include <termios.h>
#include <getopt.h>
#include <stdio.h>

#include <selinux/selinux.h>
#include <dlfcn.h>

#include "gpt.h"


#if defined(DEBUG)||defined(DISABLE_WRITES)
#define CUST ""
#else
#define CUST "/cust"
#endif


static void libselinux_start(void);
static void libselinux_stop(void);
static int check_intervene(const char *const argv0, const struct gpt_data *gpt,
uint64_t lo, uint64_t hi, int skip0, int skip1);


#ifndef BUILDTIME_LINK_LIBS
/* wrap the libselinux symbols we need */
static void *libselinux=NULL;

#define WRAPSYM(sym) __typeof__(sym) *p_##sym=NULL

WRAPSYM(fgetfilecon);
WRAPSYM(fsetfilecon);
WRAPSYM(freecon);
#undef WRAPSYM
#define fgetfilecon (*p_fgetfilecon)
#define fsetfilecon (*p_fsetfilecon)
#define freecon (*p_freecon)
#endif


int verbose=0;


int main(int argc, char **argv)
{
	int ret=128;
	int dev=-1, fd=-1;
	char *devname=NULL;
	struct gpt_data *gpt=NULL;
	ssize_t len;
	int OP, data, i;
	const char *actstr;
	char *buf=NULL;
	char *secon=NULL;

	enum {
		UNSPEC=0,
		ENABLE=1,
		DISABLE=2,
		DELETE=4,
		MERGEDATA=8,
	} mode=UNSPEC;

	while((i=getopt(argc, argv, "edFvqhH?"))>=0) {
		switch(i) {

		case 'v':
			if(verbose!=((unsigned int)-1>>1)) ++verbose;
			break;

		case 'q':
			if(verbose!=~((int)-1>>1)) --verbose;
			break;

		case 'e':
			mode|=ENABLE;
			goto check_mode;
		case 'd':
			mode|=DISABLE;
			goto check_mode;
#if 0
		case 'D':
			mode|=DELETE;
			goto check_mode;
#endif
		case 'F':
			mode|=MERGEDATA;
			goto check_mode;

		check_mode:
			if(!(mode&(mode-1))) break;

			fprintf(stderr, "Multiple incompatible modes have been selected, cannot continue!\n");
			return 1;


		case 'h':
		case 'H':
		case '?':
			ret=0;
			do {
		default:
				ret=1;
			} while(0);
			goto usage;
		}
	}

        if(argc-optind!=0) {
                ret=1;
        usage:
                fprintf(stderr,
"Copyright (C) 2018 Elliott Mitchell, distributed under GPLv3\n"
"Version: $Id$\n" "\n"
"Usage: %s [-edDfvqh] <KDZ file>\n"
"  -h  Help, this message\n" "  -v  Verbose, increase verbosity\n"
"  -q  Quiet, decrease verbosity\n"
"  -e  Reenable /OP, if only temporarily disabled\n"
"  -d  Temporarily disable /OP, can still be reenabled later\n"
#if 0
"  -D  PERMANENTLY disable /OP, `kdzwriter` required to reenable\n"
#endif
"  -F  PERMANENTLY disable /OP, and add space to userdata, REQUIRES clearing\n"
"      device data!\n", argv[0]);

		return ret;
	}

	libselinux_start();


	/* get the device */
	if(!(devname=malloc(PATH_MAX+1))) {
		fprintf(stderr, "%s: Memory allocation failure, sorry.",
argv[0]);
		ret=1;
		goto abort;
	}
	len=readlink("/dev/block/bootdevice/by-name/OP", devname, PATH_MAX+1);
	if(len<=0) {
		fprintf(stderr, "%s: Could not find /OP's device, "
"unable to continue\n", argv[0]);
		ret=1;
		goto abort;
	}

	while(isdigit(devname[--len]));
	++len;

	devname=realloc(devname, len+1);
	devname[len]='\0';

	if(verbose>=3) fprintf(stderr, "OP is a slice of %s\n", devname);

	if((dev=open(devname, O_RDWR|O_SYNC|O_LARGEFILE))<0) {
		fprintf(stderr, "%s: Failed to open %s: %s\n", argv[0],
devname, strerror(errno));
		ret=1;
		goto abort;
	}

	if(!(gpt=readgpt(dev, GPT_ANY))) {
		fprintf(stderr, "%s: Failed to read GPT of %s, aborting\n",
argv[0], devname);
		ret=1;
		goto abort;
	}

#if defined(DEBUG)||defined(DISABLE_WRITES)
	close(dev);
	fprintf(stderr, "Replacing %s with ", devname);

	for(i=len; i>=0; --i) if(devname[i]=='/') break;
	strcpy(devname, devname+i);
	fprintf(stderr, "%s\n", devname);
	if((dev=open(devname, O_RDWR|O_LARGEFILE|O_CREAT, 0644))<0) {
		fprintf(stderr, "%s: Failed to open %s: %s\n", argv[0], devname,
strerror(errno));
		ret=1;
		goto abort;
	}
#endif


	OP=0;
	while(strcmp(gpt->entry[OP].name, "OP")&&
strcmp(gpt->entry[OP].name, "XOPX"))
		if(++OP>=gpt->head.entryCount) {
			fprintf(stderr, "%s: Failed to find OP area\n",
argv[0]);
			ret=1;
			goto abort;
		}

	data=0;
	while(strcmp(gpt->entry[data].name, "userdata"))
		if(++data>=gpt->head.entryCount)
			data=-1;


	switch(mode) {
	case MERGEDATA:
		actstr="freed";

		if(data<0) {
			fprintf(stderr,
"%s: userdata is on different device from OP, unable to free\n", argv[0]);
			ret=1;
			goto abort;
		}

		if(gpt->entry[data].startLBA>gpt->entry[OP].startLBA) {
			if(gpt->entry[data].startLBA!=gpt->entry[OP].endLBA+1)
				if((ret=check_intervene(argv[0], gpt,
gpt->entry[OP].startLBA, gpt->entry[data].endLBA, data, OP))) goto abort;

			gpt->entry[data].startLBA=gpt->entry[OP].startLBA;

		} else if(gpt->entry[data].startLBA<gpt->entry[OP].startLBA) {
			if(gpt->entry[data].endLBA+1!=gpt->entry[OP].startLBA)
				if((ret=check_intervene(argv[0], gpt,
gpt->entry[data].startLBA, gpt->entry[OP].endLBA, data, OP))) goto abort;

			gpt->entry[data].endLBA=gpt->entry[OP].endLBA;

		/* zero-sized, but exists; restored GPT? */
		} else if(gpt->entry[OP].startLBA==gpt->entry[OP].endLBA)

		{ /* Already merged in? */
			fprintf(stderr,
"%s: userdata and OP at same point, already merged with userdata?\n", argv[0]);
			ret=128;
			goto abort;
		}

		do {
	case DELETE:
			actstr="deleted";
		} while(0);

		memset(gpt->entry+OP, 0, sizeof(gpt->entry[OP]));


		printf("The /OP area is about to removed.  "
"This will make additional space available\n"
"on the device, but is difficult to undo.  "
"Additionally WIPING USERDATA WILL\n"
"BE REQUIRED!\n"
"\n" "\aAre you sure?  (y/N)\n");

		{
			char ch;
			tcflag_t lflag;
			cc_t save0, save1;
			struct termios termios;
			tcgetattr(0, &termios);
			lflag=termios.c_lflag;
			save0=termios.c_cc[VMIN];
			save1=termios.c_cc[VTIME];
			termios.c_lflag&=~ICANON;
			termios.c_cc[VMIN]=1;
			termios.c_cc[VTIME]=0;
			tcsetattr(fileno(stdin), TCSANOW, &termios);
			ch=getchar();
			puts("\n\n");
			termios.c_lflag=lflag;
			termios.c_cc[VMIN]=save0;
			termios.c_cc[VTIME]=save1;
			tcsetattr(fileno(stdin), TCSANOW, &termios);
			if(tolower(ch)!='y') {
				fprintf(stderr,
"No user confirmation, aborting.\n");
				ret=64;
				goto abort;
			}
		}
		break;

	case ENABLE:
		actstr="enabled";

		strcpy(gpt->entry[OP].name, "OP");
		break;

	case DISABLE:
		actstr="disabled";

		strcpy(gpt->entry[OP].name, "XOPX");
		break;


	case UNSPEC:
	default:
		fprintf(stderr, "%s: No operational mode specified.\n",
argv[0]);
		ret=1;
		goto abort;
	}



	if(mode==MERGEDATA) {
		int bufcnt, cur;
		uint64_t range[2];

		if(mkdir("/cust", 0777)<0) {
			struct stat buf;
			if(errno!=EEXIST) {
				fprintf(stderr,
"%s: Failed to create /cust mount point: %s\n", argv[0], strerror(errno));
				ret=1;
				goto abort;
			}
			if(stat("/cust", &buf)<0||!S_ISDIR(buf.st_mode)) {
				fprintf(stderr,
"%s: Failed when creating /cust mount point, unable to continue\n", argv[0]);
				ret=1;
				goto abort;
			}
		}

		if(mount("/dev/block/bootdevice/by-name/cust", "/cust",
"ext4", MS_RDONLY, "discard")) {
			fprintf(stderr,
"%s: Failed OP resize data saving: %s\n", argv[0], strerror(errno));
			ret=1;
			goto abort;
		}

		if((fd=open("/cust/official_op_resize.cfg",
O_RDONLY))<0) {
			fprintf(stderr,
"%s: Unable to open official_op_resize.cfg: %s\n", argv[0], strerror(errno));
			ret=1;
			goto abort;
		}

		if(fgetfilecon(fd, &secon)<0) {
			fprintf(stderr,
"%s: Unable to get SE Linux context for official_op_resize.cfg: %s\n", argv[0],
strerror(errno));
			goto abort;
		}


		if((bufcnt=lseek(fd, 0, SEEK_END))<0) {
			fprintf(stderr,
"%s: Failed to get size of official_op_resize.cfg: %s\n", argv[0],
strerror(errno));
			ret=1;
			goto abort;
		}

		if(lseek(fd, 0, SEEK_SET)!=0) {
			fprintf(stderr, "%s: lseek() failed: %s\n", argv[0],
strerror(errno));
			ret=1;
			goto abort;
		}

		buf=malloc(bufcnt+1);

		if(read(fd, buf, bufcnt)!=bufcnt) {
			fprintf(stderr,
"%s: Failed during read of official_op_resize.cfg: %s\n", argv[0],
strerror(errno));
			ret=1;
			goto abort;
		}

		close(fd);
		fd=-1;

		/* this loop could overflow, but not beyond bufcnt+1 */
		buf[bufcnt]='\0';

		for(i=0, cur=0; i<bufcnt; ++i, ++cur) {
			buf[cur]=buf[i];

			if(buf[i]=='=') {
				while(isdigit(buf[++i])) ;
				buf[++cur]='0'; /* buf has an extra */

				/* line with no number???  overflow time! */
				if(cur>--i) {
					fprintf(stderr,
"%s: official_op_resize.cfg line did not include size, unable to handle!\n",
argv[0]);
					ret=128;
					goto abort;
				}
			}
		}

#ifndef DEBUG
		if(cur!=bufcnt)
#endif
		{
			if(mount("/dev/block/bootdevice/by-name/cust", "/cust",
"ext4",
#if defined(DISABLE_WRITES)||defined(DEBUG)
MS_RDONLY|
#endif
MS_REMOUNT|MS_NOATIME, "discard")) {
				fprintf(stderr,
"%s: Failed OP resize data saving: %s\n", argv[0], strerror(errno));
				ret=1;
				goto abort;
			}

			if((fd=creat(CUST"/official_op_resize.cfg.rmOP",
0644))<0) {
				fprintf(stderr,
"%s: Unable to open official_op_resize.cfg RW: %s\n", argv[0], strerror(errno));
				ret=1;
				goto abort;
			}

			if(fsetfilecon(fd, secon)<0) {
				fprintf(stderr,
"%s: Unable to set SE Linux context for official_op_resize.cfg.rmOP: %s\n",
argv[0], strerror(errno));
				ret=1;
				goto abort;
			}

			freecon(secon);
			secon=NULL;

			if(write(fd, buf, cur)!=cur) {
				fprintf(stderr,
"%s: Failed while writing official_op_resize.cfg: %s\n", argv[0],
strerror(errno));
				ret=1;
				goto abort;
			}

			if(close(fd)<0) {
				fprintf(stderr,
"%s: Failure during close() of official_op_resize.cfg.rmOP: %s\n", argv[0],
strerror(errno));
				ret=1;
				goto abort;
			}
			fd=-1;

#if !defined(DISABLE_WRITES)&&!defined(DEBUG)
			if(rename("/cust/official_op_resize.cfg",
"/cust/official_op_resize.cfg.orig")<0) {
				fprintf(stderr,
"%s: Failed to rename old official_op_resize.cfg: %s\n", argv[0],
strerror(errno));
				ret=1;
				goto abort;
			}
#endif

			if(link(CUST"/official_op_resize.cfg.rmOP",
CUST"/official_op_resize.cfg")<0) {
				fprintf(stderr,
"%s: Failed to link() new official_op_resize.cfg: %s\n", argv[0],
strerror(errno));
				ret=1;
				goto abort;
			}
		}

		umount("/cust");

		free(buf);
		buf=NULL;


		range[0]=0;
		range[1]=(gpt->entry[OP].endLBA-gpt->entry[OP].startLBA)*gpt->blocksz;
#if !defined(DISABLE_WRITES)&&!defined(DEBUG)
		ioctl(dev, BLKDISCARD, range);
#endif
	}


	if(!writegpt(dev, gpt)) {
		fprintf(stderr,
"%s: Failed during writing new GPT, state uncertain: PANIC!\a\a\a\n", argv[0]);
		ret=1;

	} else {
		printf("Successfully %s OP area.\n", actstr);

		ret=0;

		if(ioctl(dev, BLKRRPART, NULL))
			printf(
"\nAttempt to reload kernel table failed, kernel is still using old table.\n"
"Restarting phone before ANY other actions is STRONGLY RECOMMENDED!\n");
	}


abort:
	umount("/cust");

	if(buf) free(buf);

	if(gpt) free(gpt);
	if(devname) free(devname);

	if(dev>=0) close(dev);

	if(secon) freecon(secon);

	libselinux_stop();

	return ret;
}


static int check_intervene(const char *const argv0, const struct gpt_data *gpt,
uint64_t lo, uint64_t hi, int skip0, int skip1)
{
	int i;

	for(i=0; i<=gpt->head.entryCount; ++i) {
		if(i==skip0||i==skip1) continue;
		if((gpt->entry[i].startLBA>=lo&&gpt->entry[i].startLBA<=hi)||
(gpt->entry[i].endLBA>=lo&&gpt->entry[i].endLBA<=hi)) {
			fprintf(stderr,
"%s: An intervening slice is present, cannot join OP to data slice\n", argv0);
			return 1;
		}
	}

	return 0;
}


void libselinux_start(void)
{
#ifndef BUILDTIME_LINK_LIBS
	int i;
	struct {
		void **psym;
		const char name[16];
	} syms[]={
		{(void **)&p_fgetfilecon, "fgetfilecon"},
		{(void **)&p_fsetfilecon, "fsetfilecon"},
		{(void **)&p_freecon, "freecon"},
	};

	if(libselinux) return;

	if(!(libselinux=dlopen("libselinux.so", RTLD_GLOBAL))) {
		fprintf(stderr, "Failed to dlopen() libselinux.so: %s\n",
dlerror());
		exit(-1);
	}

	for(i=0; i<sizeof(syms)/sizeof(syms[0]); ++i) {
		if(!(*(syms[i].psym)=dlsym(libselinux, syms[i].name))) {
			fprintf(stderr, "Failed to resolve \"%s\": %s\n",
syms[i].name, dlerror());
			exit(-1);
		}
	}
#endif
}

void libselinux_stop(void)
{
#ifndef BUILDTIME_LINK_LIBS
	if(!libselinux) return;

	dlclose(libselinux);
	p_fgetfilecon=NULL;
	p_fsetfilecon=NULL;
	p_freecon=NULL;
#endif
}

