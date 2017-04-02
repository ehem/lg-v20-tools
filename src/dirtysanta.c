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
* author contact is via XDA Developers:					*
* https://forum.xda-developers.com/v20/development/ls997vs995h910-dirtysanta-bootloader-t3519410 *
************************************************************************/


#include <android/log.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#ifdef BACKUP_ALL_BACKUPS
#define BACKUP_ALL
#endif
#ifdef BACKUP_ALL
#define BACKUP_ALL_NONKDZ
#endif

static const char *const log_tag="dirtysanta";
#define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, log_tag, __VA_ARGS__); } while(0)
#define LOGW(...) do { __android_log_print(ANDROID_LOG_WARN, log_tag, __VA_ARGS__); } while(0)
#define LOGV(...) do { __android_log_print(ANDROID_LOG_INFO, log_tag, __VA_ARGS__); } while(0)
#define LOGD(...) do { __android_log_print(ANDROID_LOG_DEBUG, log_tag, __VA_ARGS__); } while(0)

static void tryexec(int argc, char **argv, char **envp);

static void dobackups();

static off_t copyfile(const char *src, const char *dst);


int main(int argc, char **argv, char **envp)
{
	off_t res;

	/* ensure everything is as easily accessible as possible */
	umask(0777);

	/* Alas, while an otherwise viable approach, looks like SE Linux... */
	/* this will exit() on failure */
	tryexec(argc, argv, envp);

	/* backup the crucial slices */
	dobackups();

	/* now things get interesting */
	sleep(5);

	LOGV("Starting flash of Aboot!");

	if((res=copyfile("/storage/emulated/0/aboot.img", "/dev/block/sde6"))<=0) {
		if(res==0) LOGV("aboot.img absent, skipped flash of aboot");
		else if(res==-1) LOGV("Failed during opening of Aboot, aborting!");
		else {
			/* VERY VERY BAD!!! */
			LOGW("Flash of Aboot failed!  Trying to revert!");
			if(copyfile("aboot.img", "/dev/block/sde6")<=0)
				LOGE("Reinstallation of Aboot failed, phone state unknown/unsafe, PANIC!");
			else
				LOGV("Reinstallation of Aboot succeeded, but Dirty Santa failed.");
		}

		sleep(999999);
		return -1;
	}

	LOGV("Finished. Please run Step 2 now.");

	sleep(999999);
	return 0;
}


static void tryexec(int argc, char **argv, char **envp)
{
	const char *fullpath=getenv("PATH");
	char command[129];
	int len;
	char *const args[]={"applypatch", "/system/bin/atd", "/storage/emulated/0/dirtysanta"};

	/* We're already in the right position, simply continue the process */
	if(!strcmp(argv[0], "/system/bin/atd")) return;

	/* Alas, while an otherwise viable approach, looks like SE Linux... */
	LOGD("dirtysanta executable invoked as non-atd");

	while(strlen(fullpath)) {
		char *const tmp=strchr(fullpath, ':');
		len=tmp!=NULL?tmp-fullpath:strlen(fullpath);
		fullpath+=len+1;
		if(len<=0) continue;

		snprintf(command, sizeof(command), "%.*s/applypatch", len, fullpath);

		execve(command, args, envp);
		if(errno!=ENOENT) {
			LOGV("Unknown failure trying to invoke dirtycow/applypatch: \"%s\"", strerror(errno));
			exit(1);
		}
	}

	LOGV("Failed to invoke dirtycow/applypatch, unable to continue.");
	exit(1);
}


static void dobackups()
{
	const char backupdir[129]="/storage/emulated/0";
	const char *const backuplist[][2]={
		/* these are standard, mostly for pseudo-brick recovery */
		{"/dev/block/sde6",	"abootbackup.img"},
		{"/dev/block/sde1",	"bootbackup.img"},
		{"/dev/block/sde2",	"recoverybackup.img"},
		{"/sys/firmware/fdt",	"fdt.bin"},

#ifdef BACKUP_ALL_NONKDZ
		/* these are non-standard, but interesting */
		{"/dev/block/sda17",	"OPbackup.img"}, /* KDZ stat unsure */
		{"/dev/block/sdd3",	"cdtbackup.img"},
		{"/dev/block/sdd1",	"ddrbackup.img"},
		{"/dev/block/sdb6",	"devinfobackup.img"},
		{"/dev/block/sdb5",	"dipbackup.img"},
		{"/dev/block/sde28",	"dpobackup.img"},
		{"/dev/block/sda3",	"drmbackup.img"},
		{"/dev/block/sda8",	"eksstbackup.img"},
		{"/dev/block/sda7",	"encryptbackup.img"},
		{"/dev/block/sdb3",	"fotabackup.img"},
		{"/dev/block/sdf3",	"fscbackup.img"},
		{"/dev/block/sdb4",	"fsgbackup.img"},
		{"/dev/block/sda19",	"growbackup.img"},
		{"/dev/block/sdb7",	"grow2backup.img"},
		{"/dev/block/sdc3",	"grow3backup.img"},
		{"/dev/block/sdd5",	"grow4backup.img"},
		{"/dev/block/sde29",	"grow5backup.img"},
		{"/dev/block/sdf5",	"grow6backup.img"},
		{"/dev/block/sdg2",	"grow7backup.img"},
		{"/dev/block/sda11",	"keystorebackup.img"},
		{"/dev/block/sda5",	"miscbackup.img"},
		{"/dev/block/sdf1",	"modemst1backup.img"},
		{"/dev/block/sdf2",	"modemst2backup.img"},
		{"/dev/block/sda2",	"mptbackup.img"},
		{"/dev/block/sdg1",	"persistentbackup.img"},
		{"/dev/block/sdd2",	"reservebackup.img"},
		{"/dev/block/sda4",	"snsbackup.img"},
		{"/dev/block/sdd4",	"spare1backup.img"},
		{"/dev/block/sdf4",	"spare2backup.img"},
		{"/dev/block/sda10",	"ssdbackup.img"},
#endif

#ifdef BACKUP_ALL
		/* these can readily be recreated from KDZ files */
		{"/dev/block/sde26",	"apdpbackup.img"},
		{"/dev/block/sde22",	"cmnlibbackup.img"},
		{"/dev/block/sde24",	"cmnlib64backup.img"},
		{"/dev/block/sda16",	"custbackup.img"},
		{"/dev/block/sde16",	"devcfgbackup.img"},
		{"/dev/block/sda6",	"factorybackup.img"},
		{"/dev/block/sde12",	"hypbackup.img"},
		{"/dev/block/sde20",	"keymasterbackup.img"},
		{"/dev/block/sda1",	"lafbackup.img"},
		{"/dev/block/sde18",	"modembackup.img"},
		{"/dev/block/sde27",	"msadpbackup.img"},
		{"/dev/block/sda12",	"persistbackup.img"},
		{"/dev/block/sde14",	"pmicbackup.img"},
		{"/dev/block/sde8",	"raw_resourcesbackup.img"},
		{"/dev/block/sda9",	"rctbackup.img"},
		{"/dev/block/sde10",	"rpmbackup.img"},
		{"/dev/block/sde11",	"rpmbakbackup.img"},
		{"/dev/block/sde19",	"secbackup.img"},
		/* /system is omitted due to large size */
		{"/dev/block/sdb1",	"xblbackup.img"},
		{"/dev/block/sdc1",	"xbl2backup.img"},
#endif

#if BACKUP_ALL_BACKUPS
		/* these are silly to backup (copies of other portions) */
		{"/dev/block/sde7",	"abootbakbackup.img"},
		{"/dev/block/sde25",	"cmnlib64bakbackup.img"},
		{"/dev/block/sde23",	"cmnlibbakbackup.img"},
		{"/dev/block/sde17",	"devcfgbakbackup.img"},
		{"/dev/block/sde13",	"hypbabackup.img"},
		{"/dev/block/sde21",	"keymasterbakbackup.img"},
		{"/dev/block/sda13",	"lafbakbackup.img"},
		{"/dev/block/sde15",	"pmicbakbackup.img"},
		{"/dev/block/sde9",	"raw_resourcesbakbackup.img"},
		{"/dev/block/sde3",	"recoverybakbackup.img"},
		{"/dev/block/sde11",	"rpmbakbackup.img"},
		{"/dev/block/sde5",	"tzbakbackup.img"},
		{"/dev/block/sdc2",	"xbl2bakbackup.img"},
		{"/dev/block/sdb2",	"xblbakbackup.img"},
#endif

#if 0
		/* this is too big to backup into available space */
		{"/dev/block/sda14",	"systembackup.img"},

		/* you simply don't want to back THIS up */
		{"/dev/block/sda15",	"cachebackup.img"},

		/* the device can't back this up */
		{"/dev/block/sda18",	"userdatabackup.img"},
#endif
		{NULL,			NULL}
	};
	int i;

	/* ensure everything is as easily accessible as possible */
	umask(0777);

	/* this shortens the common strings */
	LOGD("Backup directory is: %s", backupdir);

	if(chdir(backupdir)) {
		LOGV("chdir() failed! (%s)", strerror(errno));
		exit(1);
	}


	LOGV("Starting Backup");

	for(i=0; backuplist[i][0]; ++i) {
		LOGD("Backing up %s", backuplist[i][1]);
		if(copyfile(backuplist[i][0], backuplist[i][1])<=0) {
			LOGV("Backup of %s failed, aborting!", backuplist[i][1]);
			exit(1);
		}
	}

	LOGV("Backup Complete.");
}


static off_t copyfile(const char *src, const char *dst)
{
	int srcfd, dstfd;
	off_t size, dstsize;
	off64_t blksz;
	char *buf, *dstbuf;
	ssize_t count;

	if((srcfd=open(src, O_RDONLY|O_LARGEFILE))<0) return -1;
	if((dstfd=open(dst, O_RDWR|O_LARGEFILE|O_CREAT, 0666))<0) return -1;
	size=lseek(srcfd, 0, SEEK_END);
	if((buf=mmap(NULL, size, PROT_READ, MAP_PRIVATE, srcfd, 0))==MAP_FAILED) return -1;

	if((dstsize=lseek(dstfd, 0, SEEK_END))>=size) {

		if((dstbuf=mmap(NULL, dstsize, PROT_READ, MAP_PRIVATE, dstfd, 0))==MAP_FAILED) goto diff;
		if(!memcmp(buf, dstbuf, size)) {
			munmap(dstbuf, dstsize);
			munmap(buf, size);
			close(srcfd);
			if(dstsize!=size) ftruncate(dstfd, size);
			if(close(dstfd)<0) return -1;

			LOGD("Skipping copy of \"%s\" to \"%s\", already identical", src, dst);

			return size;
		}
		munmap(dstbuf, dstsize);
	diff: ;
	}

	if(lseek(dstfd, 0, SEEK_SET)!=0) {
		munmap(buf, size);
		LOGV("copyfile(): lseek() to begining of %s failed?! error: %s", dst, strerror(errno));
		return -1;
	}
	/* any failure after this point suggests a write failure, BAD */
	if((count=write(dstfd, buf, size))<size) {
		munmap(buf, size);
		LOGW("copyfile(): Failed while write()ing \"%s\", returned %zd cause: %s", dst, count, strerror(errno));
		return -100;
	}
	munmap(buf, size);
	close(srcfd);

	if(!ioctl(dstfd, BLKSSZGET, &blksz)) {
		/* target is an actual (flash) device */
		uint64_t range[2];

		/* round UP to nearest integer page */
		range[0]=size;

		/* end seems more sensible, but [1] is a count */
		range[1]=dstsize-size;

		/* we don't care if this fails */
		if(dstsize>size) ioctl(dstfd, BLKDISCARD, range);
	} else {
		/* doing a backup file */
		if(dstsize!=size) ftruncate(dstfd, size);
	}

	if((count=close(dstfd))<0) {
		LOGW("copyfile(): Failed during close() \"%s\", returned %zd cause: %s", dst, count, strerror(errno));
		return -100;
	}
	return size;
}

