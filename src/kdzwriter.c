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


#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <selinux/selinux.h>
#include <dlfcn.h>

#include "kdz.h"
#include "md5.h"


int verbose=0;


static struct kmod_file *read_kmods(void);
static int write_kmods(struct kmod_file *kmod, bool simulate);


int main(int argc, char **argv)
{
	struct kdz_file *kdz;
	int ret=0;
	int opt;
	enum {
		TEST	=0x0800,
		READ	=0x4000,
		WRITE	=0x8000,
		EXCL_WRITE=WRITE|0x2000,
		RW_MASK	=0xF000,
		REPORT	=READ|0x1,
		SYSTEM	=WRITE|0x1,
		MODEM	=WRITE|0x2,
		KERNEL	=WRITE|0x4,
		OP	=WRITE|0x8,
		BOOTLOADER=EXCL_WRITE|0x1,
		RESTORE	=EXCL_WRITE|0x2,
		MODE_MASK=0x0F,
	} mode=0;

	while((opt=getopt(argc, argv, "trsmkOabvqBhH?"))>=0) {
		switch(opt) {
		case 'r':
			if(mode) goto badmode;
			mode=REPORT;
			break;
		case 't':
			mode|=TEST;
			break;

		case 's':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=SYSTEM;
			break;
		case 'm':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=MODEM;
			break;
		case 'k':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=KERNEL;
			break;
		case 'O':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=OP;
			break;

		case 'a':
			if(mode) goto badmode;
			mode=SYSTEM|MODEM;
			break;

		case 'b':
			if(mode) goto badmode;
			mode=BOOTLOADER;
			break;

		badmode:
			fprintf(stderr, "Multiple incompatible modes have been selected, cannot continue!\n");
			return 1;

		case 'B':
			/* set blocksize (ever needed?) */
			break;
		case 'v':
			if(verbose!=((int)-1>>1)) ++verbose;
			break;

		case 'q':
			if(verbose!=~((int)-1>>1)) --verbose;
			break;

		default:
			ret=1;
		case 'h':
		case 'H':
		case '?':
			goto usage;
		}
	}

	if(argc-optind!=1) {
		ret=1;
	usage:
		fprintf(stderr,
"Copyright (C) 2017 Elliott Mitchell, distributed under GPLv3\n" "\n"
"Usage: %s [-trsmOabvqB] <KDZ file>\n"
"  -h  Help, this message\n" "  -v  Verbose, increase verbosity\n"
"  -q  Quiet, decrease verbosity\n"
"  -t  Test, does the KDZ file appear applicable, simulates writing\n"
"  -r  Report, list status of KDZ chunks\n"
"  -a  Apply all, write all areas safe to write from KDZ\n"
"  -s  System, write system area from KDZ\n"
"  -m  Modem, write modem area from KDZ\n"
"  -O  OP, write OP area from KDZ\n"
"  -k  Kernel, write kernel/boot area from KDZ\n"
"  -b  Bootloader, write bootloader from KDZ; USED FOR RETURNING TO STOCK!\n"
"Only one of -t, -r, -a, or -b is allowed.  -s, -m, -k, and -O may be used \n"
"together, but they exclude the prior options.\n", argv[0]);
		return ret;
	}

	md5_start();

	if(!(kdz=open_kdzfile(argv[optind]))) {
		fprintf(stderr, "Failed to open KDZ file \"%s\", aborting\n", argv[optind]);
		ret=1;
		goto abort;
	}

	switch(mode) {
	case REPORT:
	case REPORT|TEST:
		ret=report_kdzfile(kdz);
		break;
	case TEST:
		ret=test_kdzfile(kdz);
		{
			const char *res;
			if(ret==0) res="KDZ does not appear applicable to this device\n";
			else if(ret==1) res="KDZ appears applicable to this device\n";
			else if(ret==2) res="KDZ appears applicable to this device and matches original\n";
			else if(ret<0) res="Failure while testing KDZ file\n";
			else res="internal error: test_kdzfile() unknown return code\n";
/* GCC is smart enough to ignore this, but CLANG isn't */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
			printf(res);
#pragma clang diagnostic pop

		}
		break;
	case RESTORE:
		if(test_kdzfile(kdz)<2) {
			fprintf(stderr,
"%s: This KDZ file is an insufficiently good match for this device,\n"
"abandoning operation!\n", argv[0]);
			ret=8;
			goto abort;
		}
		printf("Write everything (to be implemented)\n");
		break;
	case BOOTLOADER:
		if(test_kdzfile(kdz)<2) {
			fprintf(stderr,
"%s: This KDZ file is an insufficiently good match for this device,\n"
"abandoning operation!\n", argv[0]);
			ret=8;
			goto abort;
		}
		printf("Write bootloader (to be implemented)\n");
		break;
	default:
		if((mode&WRITE)==WRITE) {
			if(!test_kdzfile(kdz)) {
				fprintf(stderr,
"%s: This KDZ file does not appear to be applicable to this device,\n"
"abandoning operation!\n", argv[0]);
				ret=8;
				goto abort;
			}

			if((mode&SYSTEM)==SYSTEM) {
				printf("Write system (to be implemented)\n");
			}
			if((mode&MODEM)==MODEM) {
				printf("Write modem (to be implemented)\n");
			}
			if((mode&OP)==OP) {
				printf("Write OP (to be implemented)\n");
			}
			if((mode&KERNEL)==KERNEL) {
				printf("Write kernel/boot (to be implemented)\n");
			}
		} else {
			ret=1;
			fprintf(stderr, "%s: no action specified\n", argv[0]);
		}
	}

abort:
	if(kdz) close_kdzfile(kdz);

	md5_stop();

	return ret;
}


/* wrap the libselinux symbols we need */
static void *libselinux=NULL;

__typeof__(fgetfilecon) *p_fgetfilecon=NULL;
__typeof__(fsetfilecon) *p_fsetfilecon=NULL;
__typeof__(freecon) *p_freecon=NULL;
#define fgetfilecon (*p_fgetfilecon)
#define fsetfilecon (*p_fsetfilecon)
#define freecon (*p_freecon)


void libselinux_start(void)
{
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
}

void libselinux_stop(void)
{
	if(!libselinux) return;

	dlclose(libselinux);
	p_fgetfilecon=NULL;
	p_fsetfilecon=NULL;
	p_freecon=NULL;
}



struct kmod_file {
	struct kmod_file *next;	/* next kmod file */
	struct stat64 stat;	/* our stat */
	char *secon;		/* SE Linux context */
	char *buf;		/* our file contents */
	char name[];		/* filename */
};

static struct kmod_file *read_kmods(void)
{
	struct kmod_file *ret=NULL, **pcur=&ret;
	DIR *dir=NULL;
	struct dirent64 *entry;
	int fd=-1;

	libselinux_start();

	/* hopefully not mounted, but make sure */
	umount2("/system", MNT_DETACH);

	if(mount("/dev/block/bootdevice/by-name/system", "/system",
"ext4", MS_RDONLY, "discard")) {
		fprintf(stderr, "Failed mounting system for kmod saving: %s\n",
strerror(errno));
		goto abort;
	}

	if(!(dir=opendir("/system/lib/modules"))) {
		fprintf(stderr, "Error while opening module directory: %s\n",
strerror(errno));
		goto abort;
	}

	if(fchdir(dirfd(dir))) {
		fprintf(stderr, "Unable to move to module directory: %s\n",
strerror(errno));
		goto abort;
	}

	while((entry=readdir64(dir))) {
		struct kmod_file *cur;

		/* need to skip self and parent directories */
		if(entry->d_name[0]=='.'&&(entry->d_name[1]=='\0'||
(entry->d_name[1]=='.'&&entry->d_name[2]=='\0')))
			continue;

		if(entry->d_type!=DT_REG) {
			fprintf(stderr, "Non-regular file \"%s\" in kernel module directory! (d_type=%d)\n",
entry->d_name, (int)entry->d_type);
			goto abort;
		}

		if(!(cur=malloc(sizeof(struct kmod_file)+strlen(entry->d_name)+1))) {
			fprintf(stderr, "Memory allocation failure\n");
			goto abort;
		}

		*pcur=cur;
		pcur=&cur->next;
		cur->next=NULL;
		cur->buf=NULL;
		cur->secon=NULL;

		strcpy(cur->name, entry->d_name);

		if(!(fd=open(entry->d_name, O_RDONLY|O_LARGEFILE|O_NOATIME))) {
			fprintf(stderr, "Unable to load module \"%s\": %s",
entry->d_name, strerror(errno));
			goto abort;
		}

		if(fstat64(fd, &cur->stat)) {
			fprintf(stderr, "fstat(\"%s\") failed: %s\n",
entry->d_name, strerror(errno));
			goto abort;
		}

		if(fgetfilecon(fd, &cur->secon)<0) {
			fprintf(stderr,
"Unable to get SE Linux context for \"%s\": %s\n", entry->d_name,
strerror(errno));
			goto abort;
		}

		if(!(cur->buf=malloc(cur->stat.st_size))) {
			fprintf(stderr, "Memory allocation failure\n");
			goto abort;
		}

		if(read(fd, cur->buf, cur->stat.st_size)!=cur->stat.st_size) {
			fprintf(stderr, "Failed while read()ing \"%s\": %s\n",
entry->d_name, strerror(errno));
			goto abort;
		}

		if(close(fd)) {
			fprintf(stderr, "Failure while close()ing \"%s\": %s\n",
entry->d_name, strerror(errno));
			goto abort;
		}
	}

	if(closedir(dir)) {
		fprintf(stderr, "Error while closing module directory?\n");
		goto abort;
	}

	chdir("/");

	/* failure here is problematic since we would be writing a mounted FS */
	if(umount("/system")) {
		fprintf(stderr, "Failed during unmount of /system: %s\n",
strerror(errno));
		goto abort;
	}

	return ret;


abort:
	if(fd>=0) close(fd);

	if(dir) closedir(dir);

	while(ret) {
		struct kmod_file *tmp=ret;
		ret=ret->next;
		if(tmp->buf) free(tmp->buf);
		if(tmp->secon) freecon(tmp->secon);
		free(tmp);
	}

	chdir("/");

	/* either this suceeds, or dunno */
	umount("/system");

	return NULL;
}


static int write_kmods(struct kmod_file *kmod, bool simulate)
{
	int fd=-1;

	if(!simulate) {
		if(mount("/dev/block/bootdevice/by-name/system", "/system",
"ext4", MS_PRIVATE|MS_NOATIME, "discard")) {
			fprintf(stderr, "Failed mounting system for kmod restoring: %s\n",
strerror(errno));
			goto abort;
		}

		chdir("/system/lib/modules");
	} else {
		struct stat st;

		if(lstat("/modules", &st)) {
			if(errno!=ENOENT) {
				fprintf(stderr, "Bad return from lstat(\"/modules\"): %s\n",
strerror(errno));
				goto abort;
			}
		} else if(!S_ISDIR(st.st_mode)) {
			fprintf(stderr, "\"/modules\" exists and is not a directory!\n");
			goto abort;
		}

		/* we *really* don't want to be writing anywhere */
		if(umount2("/modules", MNT_DETACH)&&errno!=EINVAL&&
errno!=ENOENT) {
			fprintf(stderr, "Unable to unmount(\"/modules\"): %s\n",
strerror(errno));
			goto abort;
		}

		/* this is okay to fail */
		mkdir("/modules", 0755);

		if(chdir("/modules")) {
			fprintf(stderr, "Failed to chdir(\"/modules\"): %s\n",
strerror(errno));
			goto abort;
		}
	}

	while(kmod) {
		struct kmod_file *next=kmod->next;
		struct timespec times[2];
		bool restperm=simulate;

		/* newer kernel didn't have module, worrisome... */
		if((fd=open(kmod->name, O_WRONLY|O_LARGEFILE|O_NOFOLLOW|O_CREAT|O_EXCL, kmod->stat.st_mode&07777))>=0)
			restperm=1;
		else if(errno!=EEXIST||
(fd=open(kmod->name, O_WRONLY|O_LARGEFILE|O_NOFOLLOW))<0) {
			fprintf(stderr,
"Failed while open()ing \"%s\" for writing: %s\n", kmod->name, strerror(errno));
			goto abort;
		}

		if(ftruncate(fd, kmod->stat.st_size)) {
			fprintf(stderr, "ftruncate(\"%s\") failure: %s\n",
kmod->name, strerror(errno));
			goto abort;
		}

		if(write(fd, kmod->buf, kmod->stat.st_size)!=kmod->stat.st_size) {
			fprintf(stderr, "Failure while write()ing \"%s\": %s\n",
kmod->name, strerror(errno));
			goto abort;
		}

#if 1
if(restperm) {
		/* do we actually want to restore these? */

		if(fchown(fd, kmod->stat.st_uid, kmod->stat.st_gid)) {
			fprintf(stderr, "chmod(\"%s\") failure: %s\n",
kmod->name, strerror(errno));
			goto abort;
		}

		if(fsetfilecon(fd, kmod->secon)) {
			fprintf(stderr, "fsetfilecon(\"%s\") failure: %s\n",
kmod->name, strerror(errno));
			goto abort;
		}

		times[0]=kmod->stat.st_atim;
		times[1]=kmod->stat.st_mtim;

		if(futimens(fd, times)) {
			fprintf(stderr, "futimens(\"%s\") failure: %s\n",
kmod->name, strerror(errno));
			goto abort;
		}
}
#endif

		free(kmod->buf);
		freecon(kmod->secon);
		free(kmod);
		kmod=next;
	}

	if(!simulate) {
		if(umount("/system")) {
			fprintf(stderr, "Failure while unmounting \"/system\": %s\n",
strerror(errno));
			goto abort;
		}
	}

	libselinux_stop();

	return 1;

abort:
	/* not much we can do in case of cascading errors */
	if(!simulate) umount2("/modules", MNT_DETACH);

	if(fd>=0) close(fd);

	while(kmod) {
		struct kmod_file *next=kmod->next;
		free(kmod->buf);
		freecon(kmod->secon);
		free(kmod);
		kmod=next;
	}

	libselinux_stop();

	return 0;
}

