/* **********************************************************************
* Copyright (C) 2016 "me2151"						*
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

/* **********************************************************************
* full original source not publically accessible, only author contact	*
* is via XDA Developers:						*
* https://forum.xda-developers.com/v20/development/ls997vs995h910-dirtysanta-bootloader-t3519410 *
************************************************************************/


#include <android/log.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


static const char *const log_tag="dirtysanta";
#define LOGV(...) do { __android_log_print(ANDROID_LOG_INFO, log_tag, __VA_ARGS__); } while(0)

static off_t copyfile(const char *src, const char *dst);

int main ()
{
	const char *const backupdir="/storage/emulated/0/dirtysanta_backups";
	const char *const backuplist[][2]={
		{"/dev/block/sde1",	"boot.img"},
		{"/dev/block/sde6",	"aboot.img"},
		{"/dev/block/sde2",	"recovery.img"},
#if 0
		{"/dev/block/sda17",	"OP"},
		{"/dev/block/sde26",	"apdp"},
		{"/dev/block/sdd3",	"cdt"},
		{"/dev/block/sde22",	"cmnlib"},
		{"/dev/block/sde24",	"cmnlib64"},
#if 0
		{"/dev/block/sde25",	"cmnlib64bak"},
		{"/dev/block/sde23",	"cmnlibbak"},
#endif
		{"/dev/block/sda16",	"cust"},
		{"/dev/block/sdd1",	"ddr"},
		{"/dev/block/sde16",	"devcfg"},
#if 0
		{"/dev/block/sde17",	"devcfgbak"},
#endif
		{"/dev/block/sdb6",	"devinfo"},
		{"/dev/block/sdb5",	"dip"},
		{"/dev/block/sde28",	"dpo"},
		{"/dev/block/sda3",	"drm"},
		{"/dev/block/sda8",	"eksst"},
		{"/dev/block/sda7",	"encrypt"},
		{"/dev/block/sda6",	"factory"},
		{"/dev/block/sdb3",	"fota"},
		{"/dev/block/sdf3",	"fsc"},
		{"/dev/block/sdb4",	"fsg"},
		{"/dev/block/sda19",	"grow"},
		{"/dev/block/sdb7",	"grow2"},
		{"/dev/block/sdc3",	"grow3"},
		{"/dev/block/sdd5",	"grow4"},
		{"/dev/block/sde29",	"grow5"},
		{"/dev/block/sdf5",	"grow6"},
		{"/dev/block/sdg2",	"grow7"},
		{"/dev/block/sde20",	"keymaster"},
#if 0
		{"/dev/block/sde21",	"keymasterbak"},
#endif
		{"/dev/block/sda11",	"keystore"},
		{"/dev/block/sda1",	"laf"},
#if 0
		{"/dev/block/sda13",	"lafbak"},
#endif
		{"/dev/block/sda5",	"misc"},
		{"/dev/block/sda2",	"mpt"},
		{"/dev/block/sde27",	"msadp"},
		{"/dev/block/sda12",	"persist"},
		{"/dev/block/sdg1",	"persistent"},
		{"/dev/block/sda9",	"rct"},
		{"/dev/block/sdd2",	"reserve"},
		{"/dev/block/sde10",	"rpm"},
#if 0
		{"/dev/block/sde11",	"rpmbak"},
#endif
		{"/dev/block/sde19",	"sec"},
		{"/dev/block/sda4",	"sns"},
		{"/dev/block/sdd4",	"spare1"},
		{"/dev/block/sdf4",	"spare2"},
		{"/dev/block/sda10",	"ssd"},
		{"/dev/block/sda14",	"system"},
		{"/dev/block/sdb1",	"xbl"},
		{"/dev/block/sdc1",	"xbl2"},
#if 0
		{"/dev/block/sdc2",	"xbl2bak"},
		{"/dev/block/sdb2",	"xblbak"},
#endif
#endif
		{NULL,			NULL}
	};
	int i;


	if(mkdir(backupdir, 0777)<0&&errno!=EEXIST) {
		fprintf(stderr, "Failed to make backup directory\n");
		LOGV("mkdir() failed!");
		return -1;
	}

	if(chdir(backupdir)) {
		fprintf(stderr, "Failed to change directory to /storage/emulated/0/dirtysanta_backups\n");
		LOGV("chdir() failed!");
		return -1;
	}

	LOGV("Starting Backup");

	for(i=0; backuplist[i][0]; ++i) {
		__android_log_print(ANDROID_LOG_DEBUG, log_tag, "Backing up %s", backuplist[i][1]);
		if(copyfile(backuplist[i][0], backuplist[i][1])<=0) {
			fprintf(stderr, "Failed during backup of %s\n", backuplist[i][1]);
			LOGV("Backup of %s failed, aborting!", backuplist[i][1]);
			return -1;
		}
	}

	LOGV("Backup Complete.");
	sleep(5);

	if((i=open("/storage/emulated/0/aboot.img", O_RDONLY|O_LARGEFILE))) {
		close(i);

		LOGV("Starting flash of Aboot!");
		if(copyfile("/storage/emulated/0/aboot.img", "/dev/block/sde6")<=0) {
			LOGV("Flash of Aboot failed!  Trying to revert!");
			if(copyfile("aboot.img", "/dev/block/sde6")<=0)
				LOGV("Reinstallation of Aboot failed, phone state unknown/unsafe, PANIC!");
			else
				LOGV("Reinstallation of Aboot succeeded, but Dirty Santa failed.");
			return -1;
		}

		LOGV("Finished. Please run Step 2 now.");

	} else {
		LOGV("aboot.img absent, skipping flash of aboot");

		return 0;
	}

	sleep(999999);
	return(0);
}

static off_t copyfile(const char *src, const char *dst)
{
	int srcfd, dstfd;
	off_t size;
	off64_t blksz;
	char *buf;
	if((srcfd=open(src, O_RDONLY|O_LARGEFILE))<0) return -1;
	if((dstfd=open(dst, O_WRONLY|O_LARGEFILE|O_TRUNC|O_CREAT, 0666))<0) return -1;
	size=lseek(srcfd, 0, SEEK_END);
	if(!(buf=mmap(NULL, size, PROT_READ, MAP_PRIVATE, srcfd, 0))) return -1;
	if(write(dstfd, buf, size)<size) return -1;
	munmap(buf, size);
	close(srcfd);

	if(!ioctl(dstfd, BLKSSZGET, &blksz)) {
		/* target is an actual (flash) device */
		uint64_t range[2];

		if((range[1]=lseek(dstfd, 0, SEEK_END))<0) goto done;

		/* round UP to nearest integer page */
		range[0]=size+blksz-1;
		range[0]-=range[0]%blksz;

		/* end seems more sensible, but [1] is a count */
		range[1]-=range[0];

		/* we don't care if this fails */
		ioctl(dstfd, BLKDISCARD, range);
	}

done:
	if(close(dstfd)<0) return -1;
	return size;
}

