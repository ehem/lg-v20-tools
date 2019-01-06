#!/usr/bin/env python

"""
Copyright (C) 2019 Elliott Mitchell

	This program is free software: you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation, either version 3 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program.  If not, see
	<http://www.gnu.org/licenses/>.

$Id$
"""

from __future__ import print_function

import sys
import io
import struct


fmt1x1 = struct.Struct('<B')

def showbyte(str, file, prefix=u''):
	vals = fmt1x2.unpack(str)
	file.write(u"{0}Value as integer byte: {1:02X}/{1:03d}\n".format(prefix, vals[0]))

fmt1x2 = struct.Struct('<BB')
fmt2x1 = struct.Struct('<H')

def showbytes2le(str, file, prefix=u''):
	vals = fmt2x1.unpack(str)
	file.write(u"{0}Value as 2-byte: {1:04X}/{1:05d}\n".format(prefix, vals[0]))
	vals = fmt1x2.unpack(str)
	file.write(u"{0}Value as integer bytes: {1:02X}/{1:03d} {2:02X}/{2:03d}\n".format(prefix, vals[0], vals[1]))

fmt1x4 = struct.Struct('<BBBB')
fmt2x2 = struct.Struct('<HH')
fmt4x1 = struct.Struct('<L')

def showbytes4le(str, file, prefix=u''):
	vals = fmt4x1.unpack(str)
	file.write(u"{0}Value as 4-byte: {1:08X}/{1:010d}\n".format(prefix, vals[0]))
	vals = fmt2x2.unpack(str)
	file.write(u"{0}Value as pair 2-byte: {1:04X}/{1:05d} {2:04X}/{2:05d}\n".format(prefix, vals[0], vals[1]))
	vals = fmt1x4.unpack(str)
	file.write(u"{0}Value as integer bytes: {1:02X}/{1:03d} {2:02X}/{2:03d} {3:02X}/{3:03d} {4:02X}/{4:03d}\n".format(prefix, vals[0], vals[1], vals[2], vals[3]))

fmt1x8 = struct.Struct('<BBBBBBBB')
fmt2x4 = struct.Struct('<HHHH')
fmt4x2 = struct.Struct('<LL')
fmt8x1 = struct.Struct('<Q')

def showbytes8le(str, file, prefix=u''):
	vals = fmt8x1.unpack(str)
	file.write(u"{0}Value as 8-byte: {1:016X}/{1:020d}\n".format(prefix, vals[0]))
	vals = fmt4x2.unpack(str)
	file.write(u"{0}Value as pair 4-byte: {1:08X}/{1:010d} {2:08X}/{2:010d}\n".format(prefix, vals[0], vals[1]))
	vals = fmt2x4.unpack(str)
	file.write(u"{0}Value as quad 2-byte: {1:04X}/{1:05d} {2:04X}/{2:05d} {3:04X}/{3:05d} {4:04X}/{4:05d}\n".format(prefix, vals[0], vals[1], vals[2], vals[3]))
	vals = fmt1x8.unpack(str)
	file.write(u"{0}Value as integer bytes: {1:02X}/{1:03d} {2:02X}/{2:03d} {3:02X}/{3:03d} {4:02X}/{4:03d} {5:02X}/{5:03d} {6:02X}/{6:03d} {7:02X}/{7:03d}\n".format(prefix, vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7]))



def dumpimage(file, offset):

	file.seek(offset)

	header = file.read(0x40)

	if len(header) != 0x40:
		print("Failed while attempting to read header at offset 0x{:04X}".format(offset))
		sys.exit(1)

	name = header[0:0x28].rstrip(b'\x00').decode("ascii")
	params = header[0x28:]

	if len(name)<=0:
		return False

	try:
		image = io.open(name+".ppm", "w")
	except IOError as err:
		print('Failed while opening image file "{}": {}'.format(name+".ppm", str(err)), file=sys.stderr)
		sys.exit(1)

	image.write(u"P3\n")
	image.write(u'# data file "{}" entry at 0x{:0X}'.format(name, offset))
	offset = fmt4x1.unpack(params[0:4])[0]
	image.write(u" image data starts at 0x{:0X}\n".format(offset))
	expect = fmt4x1.unpack(params[4:8])[0]
	params = params[8:]
	width = fmt4x1.unpack(params[0:4])[0]
	height = fmt4x1.unpack(params[4:8])[0]
	image.write(u"# unknown region: (8 bytes)\n")
	showbytes8le(params[8:16], image, u"# ")
	image.write(u"#\n")

	image.write(u"# expecting 0x{0:08X}/0d{0:010d} bytes encoded\n".format(expect))
	image.write(u"# width height\n")
	image.write(u"{:d} {:d}\n".format(width, height))
	image.write(u"# maximum value (single byte, so 2^8-1)\n"+u"255\n")

	print('Image name "{}"'.format(name))

	file.seek(offset)
	count = 0
	pixels = 0

	while pixels<width*height:
		try:
			pixel = file.read(4)
		except IOError as err:
			# likely EOF
			image.close()
			return True
		if len(pixel)<4:
			image.close()
			return True
		pixel = bytearray(pixel)
		for ign in range(pixel[0]):
			image.write(u"{:3d} {:3d} {:3d}\n".format(pixel[3], pixel[2], pixel[1]))
		pixels += pixel[0]
		count += 1

	if count<<2 == expect:
		image.write(u"# took expected 0x{0:08X}/0x{0:010d} bytes encoded\n".format(expect))
	else:
		image.write(u"# took unexpected 0x{0:X}/0d{0:d} chunks (0x{1:X}/0d{1:d} bytes) to account for needed data\n".format(count, count<<2))

	image.close()

	return True


def dumpimages(file):
	for offset in range(0x1000, 0x2000, 0x40):
		if not dumpimage(file, offset):
			return


if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("Wrong number of arguments (need single file)", file=sys.stderr)
		sys.exit(1)
	try:
		file = io.open(sys.argv[1], "rb")
	except IOError as err:
		print('Failed while opening resources file "{}": {}'.format(sys.argv[1], str(err)), file=sys.stderr)
		sys.exit(1)

	magic = file.read(0x10)
	if magic != b'BOOT_IMAGE_RLE\x00\x00':
		print('Bad magic number (unknown format): {}'.format(magic.rstrip('\x00')), file=sys.stderr)
		sys.exit(1)

	try:
		notes = io.open(sys.argv[1]+".notes", "w")
	except IOError as err:
		print('Failed while opening notes file "{}": {}'.format(sys.argv[1]+".notes", str(err)), file=sys.stderr)
		sys.exit(1)

	notes.write(u'Found magic string "BOOT_IMAGE_RLE"\n\n')

	str = file.read(0x08)

	dev = file.read(0x10)

	notes.write(u'Target device: "{}"\n\n'.format(dev.rstrip(b'\x00').decode("ascii")))

	notes.write(u"Lead unknown values (suspect pair of 4-byte values):\n")
	showbytes8le(str, notes)

	str = file.read(0x04)

	if len(str) != 0x04:
		print('Got too few bytes while reading resources file!', file=sys.stderr)
		sys.exit(1)

	notes.write(u"\nSecond unknown value (suspect length):\n")
	showbytes4le(str, notes)

	dumpimages(file)

