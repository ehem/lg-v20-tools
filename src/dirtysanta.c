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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LOGV(...) { __android_log_print(ANDROID_LOG_INFO, "dirtysanta", __VA_ARGS__); }

static off64_t copyfile(const char *src, const char *dst);

int main ()
{
	LOGV("Starting Backup");
	if(copyfile("/dev/block/sde1", "/storage/emulated/0/bootbackup.img")<=0) return -1;

	if(copyfile("/dev/block/sde6", "/storage/emulated/0/abootbackup.img")<=0) return -1;

	LOGV("Backup Complete.");
	sleep(5);

	LOGV("Starting flash of Aboot!");
	if(copyfile("/storage/emulated/0/aboot.img", "/dev/block/sde6")<=0) return -1;

	LOGV("Finished. Please run Step 2 now.");
	sleep(999999);
	return(0);
}

static off64_t copyfile(const char *src, const char *dst)
{
	int srcfd, dstfd;
	off64_t size;
	char *buf;
	if((srcfd=open(src, O_RDONLY|O_LARGEFILE))<0) return -1;
	if((dstfd=open(dst, O_WRONLY|O_LARGEFILE|O_TRUNC|O_CREAT, 0666))<0) return -1;
	size=lseek64(srcfd, 0, SEEK_END);
	if(!(buf=mmap64(NULL, size, PROT_READ, MAP_PRIVATE, srcfd, 0))) return -1;
	if(write(dstfd, buf, size)<size) return -1;
	munmap64(buf, size);
	close(srcfd);
	if(close(dstfd)<0) return -1;
	return size;
}

