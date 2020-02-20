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

from PIL import Image


#
# Notes on images:
#
# The code functions as documentation on what I've figured out about the
# format.  The initial string is pretty much a filename.  This is almost
# certainly the key for the bootloader finding a particular image.  The
# numbers are identified by the associated variables on the .unpack line.
#
# The expect value appears to be a specification of number of bytes to
# load.  Normally this matches the full data length encoding the image,
# but in one instance the encoded data length was less than this value
# (an extra zero-length RLE chunk is loaded if you decode the full
# length).
#
# On LGE V10s and LGE V20s the screenoffset is an offset from the
# absolute top of display, including the "second screen".  As such the
# apparent starting line is 160 lines above the obvious interpretation.
#
# This format doesn't absolutely require any alignment.  In real life
# all images were aligned to multiples of 0x1000.  This is notably the
# page size of the flash storage.  I've got two theories as to the
# reason.  First, given the qualities of the encoded images (often
# encoding unnecessarily large borders, sometimes marking data size as
# larger than needed, and in one instance omitting a border) I suspect
# LGE's software for building these images could be very poor quality and
# simply assumes 4KB for internal reasons, and there is no need for
# alignment.  Second, in the limited bootloader environment it is much
# easier to access whole device blocks or memory pages, and using
# misaligned image could crash the bootloader.
#
# I suspect the unknown value are flags.  A must advise against changing
# due to danger of crashing bootloader.
#

imageheaderfmt = struct.Struct("<40s6L")

def dumpimage(file, offset, blocksize):

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
		image = io.open(name+".notes", "w")
	except IOError as err:
		print('Failed while opening notes file "{}": {}'.format(name+".notes", str(err)), file=sys.stderr)
		sys.exit(1)

	image.write(u"P3\n")

	image.write(u'# data file "{}" entry at 0x{:0X}'.format(name, offset))
	image.write(u" image data starts at 0x{:0X}\n".format(dataoffset))
	if dataoffset&(blocksize-1):
		image.write(u"# NOTICE THIS IMAGE IS MISALIGNED! (blocksize={:d})\n".format(blocksize))
		print('Notice: "{:s}" uses misaligned data (never before seen)'.format(name), file=sys.stderr)
	else:
		image.write(u"# data starts at block boundary\n")

	image.write(u"# expecting 0x{0:08X}/0d{0:010d} bytes encoded\n".format(expect))
	image.write(u"# width={:d} height={:d}\n".format(width, height))
	image.write(u"{:d} {:d}\n".format(width, height))
	image.write(u"# displayed {:d} columns from left, {:d} lines from top\n".format(offsetX, offsetY))
	image.write(u'# NOTE: For V10/V20 the vertical offset includes +160 for "second screen"\n')
	image.write(u"# maximum value (single byte, so 2^8-1)\n"+u"255\n")

	image.write(u"# raw pixel data as decimal values, 0 = black, 255 = maximum intensity\n")
	image.write(u"# {:d} pixels, one per line; red green blue order\n".format(width*height))

	print('Image name "{}"'.format(name))

	if dataoffset < dumpimage.previous:
		print('Note: data "{:s}" is before other images'.format(name), file=sys.stderr)
	else:
		dumpimage.previous = dataoffset

	im = Image.frombytes("RGB", (1,1), b"\x00\x00\x00")
	im = im.resize((width, height))

	file.seek(dataoffset)
	count = 0
	pixels = 0

	bins = [0 for undef in range(256)]

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
		bins[pixel[0]] += 1
		# this RLE format allows lines to wrap around!
		next = pixels + pixel[0]
		if pixels//width != next//width:
			im.paste((pixel[3],pixel[2],pixel[1]), (pixels%width,pixels//width,width,pixels//width+1))
			if next//width > height:
				print('Alert: Payload for "{:s}" includes pixels beyond bottom border!'.format(name), file=sys.stderr)
			else:
				im.paste((pixel[3],pixel[2],pixel[1]), (0,next//width,next%width,next//width+1))
		else:
			im.paste((pixel[3],pixel[2],pixel[1]), (pixels%width,pixels//width,next%width,next//width+1))
		pixels = next
		count += 1

	if count<<2 == expect:
		image.write(u"# took expected 0x{0:08X}/0x{0:010d} bytes encoded\n".format(expect))
	else:
		image.write(u"# took unexpected 0x{0:X}/0d{0:d} chunks (0x{1:X}/0d{1:d} bytes) to account for needed data\n".format(count, count<<2))

	image.write(u"# run-length stats: {:d} zero{:s}, {:d} single, {:d} max-length(255)\n".format(bins[0], "" if bins[0]<=0 else "(bug?)", bins[1], bins[255]))

	image.close()

	try:
		im.save(name+".png")
	except IOError as err:
		print('Failed while saving image file "{}": {}'.format(name+".png", str(err)), file=sys.stderr)
		sys.exit(1)

	return True

dumpimage.previous = 0


#
# The fields a pretty much identified by their variable names on the
# .unpack() line.
#
# dataend appears to reliably point to the end of useful data.  This
# omits any padding.
#
# Having looked at a few raw_resources files, I suspect the unknown is
# some flavor of version number.  Likely the value should be preserved
# with any modifications.
#

headerfmt = struct.Struct("<16s2L16sL")

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
		if not dumpimage(file, offset, blocksize):
			print("Found too few images (expected {:d} got {:d})\n".format(count, (offset-blocksize)/imageheaderfmt.size), file=sys.stderr)
			sys.exit(1)

