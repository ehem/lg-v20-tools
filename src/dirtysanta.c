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
		{"/dev/block/sde1", "boot.img"},
		{"/dev/block/sde6", "aboot.img"},
		{"/dev/block/sde2", "recovery.img"},
		{NULL, NULL}
	};
	int i;

	LOGV("Starting Backup");

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

	LOGV("Starting flash of Aboot!");
	if(copyfile("../aboot.img", "/dev/block/sde6")<=0) {
		LOGV("Flash of Aboot failed!  Trying to revert!");
		if(copyfile("aboot.img", "/dev/block/sde6")<=0)
			LOGV("Reinstallation of Aboot failed, phone state unknown/unsafe, PANIC!");
		else
			LOGV("Reinstallation of Aboot succeeded, but Dirty Santa failed.");
		return -1;
	}

	LOGV("Finished. Please run Step 2 now.");
	sleep(999999);
	return(0);
}

static off_t copyfile(const char *src, const char *dst)
{
	int srcfd, dstfd;
	off_t size;
	char *buf;
	if((srcfd=open(src, O_RDONLY|O_LARGEFILE))<0) return -1;
	if((dstfd=open(dst, O_WRONLY|O_LARGEFILE|O_TRUNC|O_CREAT, 0666))<0) return -1;
	size=lseek(srcfd, 0, SEEK_END);
	if(!(buf=mmap(NULL, size, PROT_READ, MAP_PRIVATE, srcfd, 0))) return -1;
	if(write(dstfd, buf, size)<size) return -1;
	munmap(buf, size);
	close(srcfd);
	if(close(dstfd)<0) return -1;
	return size;
}

