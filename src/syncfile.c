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


static off_t copyfile(const char *src, const char *dst);


int main(int argc, char **argv, char **envp)
{
	if(argc!=3) {
		fprintf(stderr, "Usage: %s <src> <dst>\n"
"Copies src to dst, trying to keep number of writes low\n", argv[0]);
		exit(1);
	}

	if(copyfile(argv[1], argv[2])<0) {
		fprintf(stderr, "Copy failed!\n");
	}

	return 0;
}


static off_t copyfile(const char *src, const char *dst)
{
	int srcfd, dstfd;
	off_t size, dstsize;
	off64_t blksz;
	char *buf=NULL, *dstbuf=NULL;
	ssize_t count;

	if((srcfd=open(src, O_RDONLY|O_LARGEFILE))<0) {
		fprintf(stderr, "Failed while opening \"%s\": %s\n", src,
strerror(errno));
		return -1;
	}
	if((dstfd=open(dst, O_RDWR|O_LARGEFILE|O_CREAT, 0666))<0) {
		fprintf(stderr, "Failed while opening \"%s\": %s\n", dst,
strerror(errno));
		return -1;
	}
	size=lseek(srcfd, 0, SEEK_END);
	if((buf=mmap(NULL, size, PROT_READ, MAP_SHARED, srcfd, 0))==MAP_FAILED) {
		fprintf(stderr, "Failed to map \"%s\": %s\n", src, strerror(errno));
		return -1;
	}

	dstsize=lseek(dstfd, 0, SEEK_END);
	if(dstsize<size) {
		dstsize=size;
		ftruncate(dstfd, dstsize);
	}
	if((dstbuf=mmap(NULL, dstsize, PROT_READ, MAP_SHARED, dstfd, 0))==MAP_FAILED) {
		fprintf(stderr, "Failed to map \"%s\": %s\n", dst, strerror(errno));
		return -1;
	}

	if(ioctl(dstfd, BLKSSZGET, &blksz)) {
		if(!memcmp(buf, dstbuf, size)) {
			munmap(dstbuf, dstsize);
			munmap(buf, size);
			close(srcfd);
			if(dstsize!=size) ftruncate(dstfd, size);
			if(close(dstfd)<0) return -1;

			printf("Skipping copy of \"%s\" to \"%s\", already identical\n", src, dst);

			return size;
		} else {
			if(lseek(dstfd, 0, SEEK_SET)!=0) {
				fprintf(stderr, "lseek() to begining of \"%s\" failed?! error: %s\n", dst, strerror(errno));
				goto abort;
			}

			if((count=write(dstfd, buf, size))<size) {
				fprintf(stderr, "Failed while write()ing \"%s\", cause: %s\n", dst, strerror(errno));
				goto abort;
			}
		}
	} else {
		/* target is an actual (flash?) device */
		uint64_t range[2];
		uint64_t page;

		count=0;

		for(page=0; page<(size+blksz-1)/blksz; ++page) {
			/* Try NOT to fatigue the flash */
			if(memcmp(buf+blksz*page, dstbuf+blksz*page, blksz)) {
				memcpy(dstbuf+blksz*page, buf+blksz*page, blksz);
				++count;
			}
		}

		/* round UP to nearest integer page */
		range[0]=size;

		/* end seems more sensible, but [1] is a count */
		range[1]=dstsize-size;

		/* we don't care if this fails */
		if(dstsize>size) ioctl(dstfd, BLKDISCARD, range);

		printf("Needed to rewrite %ld pages of %ld, sized %ld bytes\n",
count, (size+blksz-1)/blksz, blksz);
	}

	munmap(dstbuf, dstsize);
	munmap(buf, size);
	close(srcfd);
	if((count=close(dstfd))<0) {
		fprintf(stderr, "Failed during close() \"%s\", cause: %s", dst, strerror(errno));
		return -100;
	}

	return size;


abort:
	if(dstbuf) munmap(dstbuf, dstsize);
	if(buf) munmap(buf, size);
	close(srcfd);
	close(dstfd);

	return -1;
}

