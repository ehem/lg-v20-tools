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
from binascii import a2b_hex, b2a_hex

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


CPIOMagic = b"070701"
CPIOHead = struct.Struct(">IIIIIIIIIIIII")

# fields of note in the CPIO header
CPIOINODE	= 0
CPIOMODE	= 1
CPIOUID		= 2
CPIOGID		= 3
CPIONLINK	= 4
CPIOMTIME	= 5
CPIOFILESZ	= 6
CPIODEVMAJ	= 7
CPIODEVMIN	= 8
CPIORDEVMAJ	= 9
CPIORDEVMIN	= 10
CPIONAMESZ	= 11
CPIOCHECK	= 12


targets = {
#	# Modifications to system properties
#	'default.prop': (
#		# Lets `adb shell` be root
#		(b"ro.secure=1",		b"ro.secure=0"),
#
#		# Enable debugging property
#		(b"ro.debuggable=0",		b"ro.debuggable=1"),
#
#		# Get it turned on during boot for debugging!
#		(b"persist.sys.usb.config=boot", b"persist.sys.usb.config=boot,adb"),
#	),

	# Modifications to filesystem table
	'fstab.elsa': (
		# Remove dm-verify from fstab, enable fsck (add "check"?)
		(b"wait,verify",		b"wait"),
		# Disables forced encryption (a Bad Thing(tm), really)
		(b"wait,check,forceencrypt",	b"wait,check,encryptable"),
	),

	# Delete the dm-verity key
	'verity_key': None,
}



def cpiowrite(file, head, name, buf):
	# set the length of the file
	head[CPIOFILESZ] = len(buf)

	file.write(CPIOMagic)
	count = len(CPIOMagic)

	head = b2a_hex(CPIOHead.pack(*head))
	file.write(head)
	count += len(head)

	name = name.encode("utf8") + b'\x00'
	file.write(name)
	count += len(name)

	file.write(b"\x00\x00\x00\x00"[:(4-(count&3))&3:])

	file.write(buf)
	count = len(buf)

	file.write(b"\x00\x00\x00\x00"[:(4-(count&3))&3:])


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

	name = ""

	while name != "TRAILER!!!":
		magic = ramin.read(len(CPIOMagic))
		count = 6

		if magic != CPIOMagic:
			print("ERROR: Missing magic number in ramdisk image!", file=sys.stderr)
			sys.exit(1)

		head = list(CPIOHead.unpack(a2b_hex(ramin.read(CPIOHead.size<<1))))
		count += CPIOHead.size<<1

		# names include an extra null
		name = (ramin.read(head[CPIONAMESZ])[:-1]).decode("utf8")
		count += head[CPIONAMESZ]

		# Grab any padding
		ramin.read((4-(count&3))&3)

		count = head[CPIOFILESZ]
		buf = ramin.read(count)

		# Grab any padding
		ramin.read((4-(count&3))&3)

		if name in targets:
			if targets[name] == None:
				# mark as done
				del targets[name]

				# delete the file!
				continue

			idx = 0
			for target in targets[name]:
				# used for adding lines
				if target[0] == None:
					buf += target[1]
					idx = len(buf)
					continue
				idx = buf.find(target[0], idx)
				if idx < 0:
					print("Failed to find string \"{:s}\" in ramdisk file \"{:s}\"!".format(name, target[0]), file=sys.stderr)
					sys.exit(1)
				buf = buf[:idx] + target[1] + buf[idx+len(target[0]):]
				idx += len(target[1])

			# mark as done
			del targets[name]

		cpiowrite(ramout, head, name, buf)


	ramin.close()

	ramout.close()

	if len(targets.keys()) != 0:
		print("ERROR: one or more targetted files were missed!", file=sys.stderr)
		sys.exit(1)


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

