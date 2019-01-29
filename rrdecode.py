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


imageheaderfmt = struct.Struct("<40s6L")

def dumpimage(file, offset):

	file.seek(offset)

	header = file.read(imageheaderfmt.size)

	if len(header) != imageheaderfmt.size:
		print("Failed while attempting to read header at offset 0x{:04X}".format(offset))
		sys.exit(1)

	name, dataoffset, expect, width, height, offsetX, offsetY = imageheaderfmt.unpack(header)
	name = name.rstrip(b'\x00').decode("ascii")

	if len(name)<=0:
		return False

	try:
		image = io.open(name+".ppm", "w")
	except IOError as err:
		print('Failed while opening image file "{}": {}'.format(name+".ppm", str(err)), file=sys.stderr)
		sys.exit(1)

	image.write(u"P3\n")

	image.write(u'# data file "{}" entry at 0x{:0X}'.format(name, offset))
	image.write(u" image data starts at 0x{:0X}\n".format(dataoffset))

	image.write(u"# expecting 0x{0:08X}/0d{0:010d} bytes encoded\n".format(expect))
	image.write(u"# width height\n")
	image.write(u"{:d} {:d}\n".format(width, height))
	image.write(u"# displayed {:d} columns from left, {:d} lines from top\n".format(offsetX, offsetY))
	image.write(u'# NOTE: For V10/V20 the vertical offset includes +160 for "second screen"\n')
	image.write(u"# maximum value (single byte, so 2^8-1)\n"+u"255\n")

	image.write(u"# raw pixel data as decimal values, 0 = black, 255 = maximum intensity\n")
	image.write(u"# {:d} pixels, one per line; red green blue order\n".format(width*height))

	print('Image name "{}"'.format(name))

	file.seek(dataoffset)
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


headerfmt = struct.Struct("<16s2L16s1L")

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("Wrong number of arguments (need single file)", file=sys.stderr)
		sys.exit(1)
	try:
		file = io.open(sys.argv[1], "rb")
	except IOError as err:
		print('Failed while opening resources file "{}": {}'.format(sys.argv[1], str(err)), file=sys.stderr)
		sys.exit(1)

	header = file.read(headerfmt.size)
	if len(header) != headerfmt.size:
		print("Failed while attempting to read starting raw_resources header", file=sys.stderr)
		sys.exit(1)

	magic, count, unknown, dev, dataend = headerfmt.unpack(header)



	if magic != b'BOOT_IMAGE_RLE\x00\x00':
		print('Bad magic number (unknown format): {}'.format(magic.rstrip('\x00')), file=sys.stderr)
		sys.exit(1)

	try:
		notes = io.open(sys.argv[1]+".notes", "w")
	except IOError as err:
		print('Failed while opening notes file "{}": {}'.format(sys.argv[1]+".notes", str(err)), file=sys.stderr)
		sys.exit(1)

	notes.write(u'Found magic string "BOOT_IMAGE_RLE"\n\n')


	notes.write(u'Target device: "{}"\n'.format(dev.rstrip(b'\x00').decode("ascii")))

	notes.write(u'Have {:d} image entries\n\n'.format(count))

	notes.write(u"Data ends at address: 0x{0:08X}/{0:d}\n".format(dataend))

	notes.write(u"\nLead unknown value: 0x{0:08X}/0d{0:010d}\n".format(unknown))

	# probe the size
	for shift in range(9,20):
		blocksize = 1<<shift
		file.seek(blocksize)

		header = file.read(imageheaderfmt.size)
		if len(header) != imageheaderfmt.size:
			print("Failed while attempting to probe at shift {:d} (blocksize {:d})".format(shift, blocksize), file=sys.stderr)
			sys.exit(1)

		name = imageheaderfmt.unpack(header)[0].rstrip(b'\x00').decode("ascii")

		if len(name)>0:
			break
	else:
		print("Probing failed to find image headers/blocksize", file=sys.stderr)
		sys.exit(1)

	print("Probe found a blocksize of {:d} (shift={:d})".format(blocksize, shift))

	for offset in range(blocksize, blocksize+count*imageheaderfmt.size, imageheaderfmt.size):
		if not dumpimage(file, offset):
			print("Found too few images (expected {:d} got {:d})\n".format(count, (offset-blocksize)/imageheaderfmt.size), file=sys.stderr)
			sys.exit(1)

