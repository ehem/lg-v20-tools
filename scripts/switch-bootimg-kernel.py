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
	if len(sys.argv) != 3:
		print("{:s} <bootimg> <new kernel>".format(sys.argv[0]))
		sys.exit(1)

	try:
		bootimg = io.open(sys.argv[1], "r+b")
	except IOError as err:
		print(str(err), file=sys.stderr)
		sys.exit(1)

	try:
		kernel = io.open(sys.argv[2], "rb")
	except IOError as err:
		print(str(err), file=sys.stderr)
		sys.exit(1)

	bootimg.seek(0, io.SEEK_SET)
	newsize = kernel.seek(0, io.SEEK_END)
	kernel.seek(0, io.SEEK_SET)
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
	oldsize = vals[KERNELSZ]

	oldspace = (oldsize + pagesize - 1) // pagesize * pagesize
	newspace = (newsize + pagesize - 1) // pagesize * pagesize

	vals = list(vals)
	vals[KERNELSZ] = newsize


	if newspace > oldspace:
		# New kernel is larger, need to move everything towards end
		delta = newspace - oldspace

		bootimg.seek(delta+pagesize, io.SEEK_END)

		while bootimg.seek(-delta-(pagesize<<1), io.SEEK_CUR) > pagesize:
			buf = bootimg.read(pagesize)
			bootimg.seek(delta-len(buf), io.SEEK_CUR)
			bootimg.write(buf)

		buf = b" "


	bootimg.seek(pagesize, io.SEEK_SET)
	while len(buf) > 0:
		buf = kernel.read(pagesize)
		if len(buf) > 0:
			buf = buf.ljust(pagesize, b'\x00')
		bootimg.write(buf)


	if newspace < oldspace:
		# New kernel is same size or smaller
		delta = oldspace - newspace
		bootimg.seek(oldspace + pagesize, io.SEEK_SET)
		buf = b" "
		while len(buf) > 0:
			buf = bootimg.read(pagesize)
			bootimg.seek(-delta-len(buf), io.SEEK_CUR)
			bootimg.write(buf)
			bootimg.seek(delta, io.SEEK_CUR)


	# figure out the total size of the actual image
	totalsize  = (vals[RAMDISKSZ]	+ pagesize - 1) // pagesize
	totalsize += (vals[SECONDSZ]	+ pagesize - 1) // pagesize
	totalsize += (vals[DTSIZE]	+ pagesize - 1) // pagesize
	totalsize *= pagesize
	# header and the new kernel
	totalsize += pagesize + newspace

	bootimg.truncate(totalsize)


	# finally write the new header
	bootimg.seek(0, io.SEEK_SET)
	bootimg.write(BootImgHead.pack(*vals))

