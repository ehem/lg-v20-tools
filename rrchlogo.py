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

headerfmt = struct.Struct("<16s2L16s1L")


class RRImage:

	# where our source material comes from
	input = None

	# the destination
	output = None

	# kind of important factor
	blocksize = None

	# for images which need delayed handling (merge candidates)
	delayed = []

	# table of potential merging candidates
	mergetab = {}

	# end of area used so far
	used = None

	def __init__(self, offset, name, dataoffset, expect, width, height, offsetX, offsetY):
		self.offset=offset
		self.name=name
		self.dataoffset=dataoffset
		self.expect=expect
		self.width=width
		self.height=height
		self.offsetX=offsetX
		self.offsetY=offsetY

	# initialized shared values
	@staticmethod
	def startup(input, output, blocksize):
		RRImage.input = input
		RRImage.output = output
		RRImage.blocksize = blocksize
		RRImage.used = blocksize<<1

	@staticmethod
	def entry(offset):
		RRImage.input.seek(offset)

		header = RRImage.input.read(imageheaderfmt.size)

		if len(header) != imageheaderfmt.size:
			print("Failed while attempting to read header at offset 0x{:04X}".format(offset))
			sys.exit(1)

		name, dataoffset, expect, width, height, offsetX, offsetY = imageheaderfmt.unpack(header)
		name = name.rstrip(b'\x00').decode("ascii")

		if len(name)<=0:
			return False

		later = False

		if "lglogo" in name:
			later = True
			key = None
		elif "1st" in name:
			later = True
			part = name.partition("1st")
			key = part[0]+part[2]
		elif "2nd" in name:
			later = True
			part = name.partition("2nd")
			key = part[0]+part[2]

		entry = RRImage(offset, name, dataoffset, expect, width, height, offsetX, offsetY)

		if later:
			RRImage.delayed.append(entry)
			if key in RRImage.mergetab:
				RRImage.mergetab[key].merge(entry)
			else:
				RRImage.mergetab[key] = entry

		else:
			entry.shrink()
			entry.finish()


	@staticmethod
	def dolate():
		for entr in RRImage.delayed:
			entr.payload = b''
			entr.height = 1

		for entr in RRImage.delayed:
			entr.finish()

	def merge(self, other):
		print("Entry in merge table, implement merge")
		pass

	def shrink(self):
		self.input.seek(self.dataoffset)
		self.payload = self.input.read(self.expect)

	def finish(self):
		# align to blocksize
		RRImage.used += self.blocksize-1
		RRImage.used &= ~(self.blocksize-1)

		header = imageheaderfmt.pack(self.name.encode("ascii"), self.used, len(self.payload), self.width, self.height, self.offsetX, self.offsetY)

		self.output.seek(self.used)
		self.output.write(self.payload)

		RRImage.used += len(self.payload)

		self.output.seek(self.offset)
		self.output.write(header)


if __name__ == "__main__":
	if len(sys.argv) == 3:
		try:
			output = io.open(sys.argv[2], "wb")
		except IOError as err:
			print('Failed while opening output file "{:s}": {:s}'.format(sys.argv[2], str(err)), file=sys.stderr)
			sys.exit(1)
	elif len(sys.argv) != 2:
		print("Usage: {:s} <input file> [<output file>]".format(sys.argv[0]), file=sys.stderr)
		sys.exit(1)
	else:
		try:
			output = io.open(sys.argv[1]+".out", "wb")
		except IOError as err:
			print('Failed while opening output file "{:s}": {:s}'.format(sys.argv[1]+".out", str(err)), file=sys.stderr)
	try:
		input = io.open(sys.argv[1], "rb")
	except IOError as err:
		print('Failed while opening input file "{}": {}'.format(sys.argv[1], str(err)), file=sys.stderr)
		sys.exit(1)

	header = input.read(headerfmt.size)
	if len(header) != headerfmt.size:
		print("Failed while attempting to read starting raw_resources header", file=sys.stderr)
		sys.exit(1)

	magic, count, unknown, dev, dataend = headerfmt.unpack(header)



	if magic != b'BOOT_IMAGE_RLE\x00\x00':
		print('Bad magic number (unknown format): {}'.format(magic.rstrip('\x00')), file=sys.stderr)
		sys.exit(1)

	print('Found magic string "BOOT_IMAGE_RLE"\n')


	print('Target device: "{}"'.format(dev.rstrip(b'\x00').decode("ascii")))


	# probe the size
	for shift in range(9,20):
		blocksize = 1<<shift
		input.seek(blocksize)

		header = input.read(imageheaderfmt.size)
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

	RRImage.startup(input, output, blocksize)


	for offset in range(blocksize, blocksize+count*imageheaderfmt.size, imageheaderfmt.size):
		RRImage.entry(offset)

	RRImage.dolate()


	header = headerfmt.pack(magic, count, unknown, dev, RRImage.used)
	output.seek(0)
	output.write(header)
	output.close()

