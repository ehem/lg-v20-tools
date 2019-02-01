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

from collections import deque


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


		self.splitpayload()

		self.removetop()

		self.removebottom()

		self.removeleft()

		self.removeright()

		self.joinpayload()


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


	def splitpayload(self):

		payload = deque()

		while self.payload:
			count = ord(self.payload[0])
			current = 0
			while count < self.width and len(self.payload) > current+4:
				current += 4
				count += ord(self.payload[current])
			payload.append(self.payload[:current])
			if count == self.width:
				payload[-1] += self.payload[current:current+4]
				self.payload = self.payload[current+4:]
			elif count > self.width:
				count -= self.width
				payload[-1] += chr(ord(self.payload[current])-count)+self.payload[current+1:current+4]
				self.payload = chr(count)+self.payload[current+1:]
			else:
				payload.pop()
				self.payload = b''
				print('Warning, payload for "{:s}" was wrong length!'.format(self.name), file=sys.stderr)

		# Potentially caused by other tools screwing up
		if len(payload) < self.height:
			print('Found insufficient payload data for "{:s}"'.format(self.name), file=sys.stderr)
			self.height = len(payload)
		# Yes, this really has been seen in nature
		if len(payload) > self.height:
			print('Found excess payload data for "{:s}"'.format(self.name), file=sys.stderr)
			while len(payload) > self.height:
				payload.pop()

		self.payload = payload


	def joinpayload(self):

		payload = self.payload

		pixel = payload[0][-3:]
		count = ord(payload[0][-4])
		self.payload = payload.popleft()[:-4]

		while payload:
			if count >= 255:
				self.payload += '\xFF' + pixel
				count -= 255
			elif payload[0][1:3] == pixel:
				count += ord(payload[0][0])
				payload[0] = payload[0][4:]
				if not payload[0]:
					payload.popleft()
			else:
				self.payload += chr(count) + pixel
				pixel = payload[0][-3:]
				count = ord(payload[0][-4])
				self.payload += payload.popleft()[:-4]

		if count:
			self.payload += chr(count) + pixel


	def removetop(self):

		self.removedtop = 0

		payload = self.payload

		pixel = payload[0][1:4]
		for x in range(5, len(payload[0]), 4):
			if payload[0][x:x+3] != pixel:
				break
		else:
			while pixel:
				for x in range(1, len(payload[1]), 4):
					if payload[1][x:x+3] != pixel:
						pixel = None
						break
				else:
					payload.popleft()
					self.offsetY += 1
					self.height -= 1
					self.removedtop += 1

		self.payload = payload


	def removebottom(self):

		self.removedbottom = 0

		payload = self.payload

		pixel = payload[-1][1:4]
		for x in range(5, len(payload[-1]), 4):
			if payload[-1][x:x+3] != pixel:
				break
		else:
			while pixel:
				for x in range(1, len(payload[-2]), 4):
					if payload[-2][x:x+3] != pixel:
						pixel = None
						break
				else:
					payload.pop()
					self.height -= 1
					self.removedbottom += 1

		self.payload = payload


	def removeleft(self):

		self.removedleft = 0

		payload = self.payload

		pixel = payload[0][1:4]
		max = ord(payload[0][0])
		for x in range(4, len(payload[0]), 4):
			if payload[0][x+1:x+4] != pixel:
				max -= 1
				break
			max += ord(payload[0][x])

		for y in range(1, len(payload)):
			cur = 0
			for x in range(0, len(payload[y]), 4):
				if payload[y][x+1:x+4] != pixel:
					max = cur - 1
					break
				cur += ord(payload[y][x])
				if cur >= max:
					break

		if max < 0:
			max = 0

		for y in range(0, len(payload)):
			cur = max
			for x in range(0, len(payload[y]), 4):
				if ord(payload[y][x]) == cur:
					payload[y] = payload[y][x+4:]
					break
				elif ord(payload[y][x]) > cur:
					payload[y] = chr(ord(payload[y][x])-cur) + payload[y][x+1:]
					break
				cur -= ord(payload[y][x])

		self.offsetX += max
		self.width -= max
		self.removedleft += max

		self.payload = payload


	def removeright(self):

		self.removedright = 0

		payload = self.payload

		pixel = payload[0][-3:]
		max = ord(payload[0][-4])
		for x in range(len(payload[0])-8, -1, -4):
			if payload[0][x+1:x+4] != pixel:
				max -= 1
				break
			max += ord(payload[0][x])

		for y in range(1, len(payload)):
			cur = 0
			for x in range(len(payload[y])-4, -1, -4):
				if payload[y][x+1:x+4] != pixel:
					max = cur - 1
					break
				cur += ord(payload[y][x])
				if cur >= max:
					break

		if max < 0:
			max = 0

		for y in range(0, len(payload)):
			cur = max
			for x in range(len(payload[y])-4, -1, -4):
				if ord(payload[y][x]) == cur:
					payload[y] = payload[y][:x]
					break
				elif ord(payload[y][x]) > cur:
					payload[y] = payload[y][:x] + chr(ord(payload[y][x])-cur) + payload[y][x+1:x+4]
					break
				cur -= ord(payload[y][x])

		self.width -= max
		self.removedright += max

		self.payload = payload


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

