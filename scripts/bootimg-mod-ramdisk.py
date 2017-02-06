#!/usr/bin/env python

"""
Copyright (C) 2017 Elliott Mitchell <ehem+lg-v20-toolsd@m5p.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

from __future__ import print_function

import struct
import argparse
import sys
import io

import gzip
import tempfile


# See Android's bootimg.h for detail about these
BootImgHead = struct.Struct("<8sLLLLLLLLL")
BootImgMagic = b"ANDROID!"

# fields of note in the header
MAGICNUM	= 0
KERNELSZ	= 1
KERNELADDR	= 2
RAMDISKSZ	= 3
RAMDISKADDR	= 4
SECONDSZ	= 5
SECONDADDR	= 6
TAGSADDR	= 7
PAGESIZE	= 8
DTSIZE		= 9


if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("{:s} <bootimg>".format(sys.argv[0]))
		sys.exit(1)

	try:
		bootimg = io.open(sys.argv[1], "r+b")
	except IOError as err:
		print(str(err), file=sys.stderr)
		sys.exit(1)

	bootimg.seek(0, io.SEEK_SET)

	try:
		buf = bootimg.read(BootImgHead.size)

		if len(buf) != BootImgHead.size:
			raise IOError("read() failure while reading \"{:s}\", damaged file?".format(bootimg.name))
	except IOError as err:
		print(str(err), file=sys.stderr)
		sys.exit(1)

	vals = BootImgHead.unpack(buf)

	if vals[MAGICNUM] != BootImgMagic:
		print("Magic string \"{:s}\" missing from \"{:s}\", damaged file?".format(BootImgMagic, bootimg.name))
		sys.exit(1)

	pagesize = vals[PAGESIZE]

	oldsize = vals[RAMDISKSZ]
	oldspace = (oldsize + pagesize - 1) // pagesize * pagesize

	vals = list(vals)

	# ramdisk is after the kernel
	start = ((vals[KERNELSZ] + pagesize - 1) // pagesize + 1) * pagesize

	# seek to begining of ramdisk
	bootimg.seek(start, io.SEEK_SET)

	tmpfile = tempfile.TemporaryFile()
	size = oldsize

	while size > 0:
		buf = bootimg.read(pagesize if size >= pagesize else size)
		size -= len(buf)
		tmpfile.write(buf)

	tmpfile.seek(0, io.SEEK_SET)
	ramin = gzip.GzipFile(fileobj=tmpfile, mode="rb")


	newfile = tempfile.TemporaryFile()
	ramout = gzip.GzipFile(fileobj=newfile, mode="wb", compresslevel=9, mtime=0)

	targets = [
		[b"fstab.elsa",			b"fstab.elsa"],
		[b"wait,verify",		b"wait"],
		[b"wait,check,forceencrypt",	b"wait,check,encryptable"],
	]


	buf = ramin.read(pagesize)
	next = ramin.read(pagesize)

	buf += next[:40]

	for target in targets:
		target[1] = target[1].rjust(len(target[0]), b' ')

		while not target[0] in buf:
			ramout.write(buf[:pagesize])
			buf = next
			next = ramin.read(pagesize)
			buf += next[:40]
			if len(buf) <=40:
				raise("Internal error!")

		idx = buf.find(target[0])

		buf = buf[:idx] + target[1] + buf[idx+len(target[0]):]

		if idx+len(target[0]) >= pagesize:
			ramout.write(buf[:pagesize])
			buf = next
			next = ramin.read(pagesize)
			buf += next[:40]

			buf = target[1][pagesize-(idx+len(target[1])):] + buf[idx+len(target[0])-pagesize:]

	ramout.write(buf[:pagesize])
	buf = next
	while buf:
		ramout.write(buf)
		buf = ramin.read(pagesize)

	ramout.close()

	newsize = newfile.tell()
	newfile.seek(0, io.SEEK_SET)

	# new image is done, now just need to modify the boot image
	vals[RAMDISKSZ] = newsize
	newspace = (newsize + pagesize -1) // pagesize * pagesize

	buf = b" "



	if newspace > oldspace:
		# New image is larger, need to move everything towards end
		delta = newspace - oldspace

		bootimg.seek(delta+pagesize, io.SEEK_END)

		while bootimg.seek(-delta-(pagesize<<1), io.SEEK_CUR) > pagesize:
			buf = bootimg.read(pagesize)
			bootimg.seek(delta-len(buf), io.SEEK_CUR)
			bootimg.write(buf)

		buf = b" "


	bootimg.seek(start, io.SEEK_SET)
	while len(buf) > 0:
		buf = newfile.read(pagesize)
		if len(buf) > 0:
			buf = buf.ljust(pagesize, b'\x00')
		bootimg.write(buf)


	if newspace < oldspace:
		# New image is same size or smaller
		delta = oldspace - newspace
		bootimg.seek(oldspace + start, io.SEEK_SET)
		buf = b" "
		while len(buf) > 0:
			buf = bootimg.read(pagesize)
			bootimg.seek(-delta-len(buf), io.SEEK_CUR)
			bootimg.write(buf)
			bootimg.seek(delta, io.SEEK_CUR)


	# figure out the total size of the actual image
	totalsize  = (vals[SECONDSZ]	+ pagesize - 1) // pagesize
	totalsize += (vals[DTSIZE]	+ pagesize - 1) // pagesize
	totalsize *= pagesize
	# header and the new kernel
	totalsize += start + newspace

	bootimg.truncate(totalsize)


	# finally write the new header
	bootimg.seek(0, io.SEEK_SET)
	bootimg.write(BootImgHead.pack(*vals))

