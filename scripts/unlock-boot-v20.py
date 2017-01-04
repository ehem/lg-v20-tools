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

import argparse
import io
import sys
import os
import subprocess
import threading
import re

# debugging
import time


if sys.version_info >= (3, 0):
	from queue import Queue
	_dev_null = subprocess.DEVNULL
	if sys.version_info > (3.4): print("Note: Your version of Python is more recent than the developer's main system\nfunctionality may be impaired.", file=sys.stderr)
else:
	from Queue import Queue
	_dev_null = open(os.devnull, "wb")
	if sys.version_info < (2, 7):
		print("Warning: Your version of Python is rather old, this program may not function!\a", file=sys.stderr)

logEvents = Queue()

class Logger(threading.Thread):
	"""
	"""
	def __init__(self, device):
		"""
		"""
		super(Logger, self).__init__()

		try:
			Logger._logLineRE
			Logger._prio
		except AttributeError:
			Logger._logLineRE = re.compile("^([SFEWIDV])/(.*)\s*\(\s*(\d+)\):\s+(.*?)\s*$")
			Logger._prio = {
				'S': 0,
				'F': 1,
				'E': 2,
				'W': 3,
				'I': 4,
				'D': 5,
				'V': 6,
			}

		self.daemon = True

		try: self.proc = subprocess.Popen(["adb", "-s", device, "logcat", "-v", "brief"], stdout=subprocess.PIPE, universal_newlines=True)
		except OSError:
			print("ERROR: Failed starting logcat thread, cannot continue", file=sys.stderr)
			sys.exit(1)

		self.start()

	def run(self):
		"""
		"""
		self.lines = -1
		for line in self.proc.stdout:
			self.lines += 1
			vals = self._logLineRE.match(line)
			if not vals: continue

			log = {
				'prio':	self._prio[vals.group(1)],
				'pid':	int(vals.group(3)),
				'prog':	vals.group(2),
				'msg':	vals.group(4),
			}

			if "dirtysanta" in log['msg']:
				print("DEBUG: Dirty Santa event, prio={:d}, program={:s}, PID={:d}, message=\"{:s}\" after {:d} ignored".format(log['prio'], log['prog'], log['pid'], log['msg'], self.lines))
				self.lines = -1
# DEBUG: disabled while debugging
#				logEvents.put(log)

# Most likely indicates script has restarted the phone, but need to mark this
#		logEvents.put("adb exited!")

	def getLineCount(self):
		"""
		"""

		return self.lines



def startAdbServer():
	"""
	Start the ADB server so the output doesn't clutter up elsewhere
	"""
	try: proc = subprocess.Popen(["adb", "start-server"], stdout=_dev_null)
	except OSError:
		print("ERROR: Failed when trying to start adb server process, is adb installed?", file=sys.stderr)
		sys.exit(1)

	if proc.wait() != 0:
		print("ERROR: adb returned non-zero, aborting!", file=sys.stderr)
		sys.exit(1)


def commandPopenDevices(prog):
	"""
	Retrieve real device serial numbers available to prog
	"""
	try: proc = subprocess.Popen([prog, "devices"], stdout=subprocess.PIPE, universal_newlines=True)
	except OSError:
		print("ERROR: Failed when trying to retrieve devices from {:s}, is {:s} installed?".format(prog, prog), file=sys.stderr)
		print("", file=sys.stderr)
		print("Suggested resource: http://www.xda-developers.com/easily-get-binaries-needed-to-work-with-kernels/", file=sys.stderr)
		if os.name != "nt":
			print("", file=sys.stderr)
			print("Debian/Linux variants, install the package android-tools-fsutils", file=sys.stderr)
		sys.exit(1)

	return proc


def getAdbDevice():
	"""
	Retrieve real device serial numbers available to `adb`
	"""
	proc = commandPopenDevices("adb")

	proc.stdout.readline()
	devices = []
	for line in proc.stdout:
		if len(line) > 1:
			dev = line.partition('\t')[0]
			if not "emulator-" in dev: devices.append(dev)

	if proc.wait() != 0:
		print("ERROR: adb returned non-zero, aborting!", file=sys.stderr)
		sys.exit(1)

	return devices


def getFastbootDevice():
	"""
	Retrieve real device serial numbers available to `fastboot`
	"""
	proc = commandPopenDevices("fastboot")

	devices = []
	for line in proc.stdout:
		if "<waiting for device>" in line:
			print("ERROR: fastboot reports waiting for device", file=sys.stderr)
			if os.name == "nt": print("This likely means you need to install the fastboot drivers", file=sys.stderr)
			sys.exit(1)
		dev = line.partition('\t')[0]
		devices.append(dev)

	if proc.wait() !=0:
		print("ERROR: fastboot returned non-zero, aborting!", file=sys.stderr)
		sys.exit(1)

	return devices


# continue work...
if __name__ == "__main__":
	# sigh, some versions report server status to standard output
	startAdbServer()

	devsAdb = getAdbDevice()
	devsFastboot = getFastbootDevice()
	print("Found {:d} real Android devices".format(len(devsAdb)))
	print("Found {:d} real fastbooting Android devices".format(len(devsFastboot)))

	if (len(devsAdb) + len(devsFastboot)) != 1:
		if (len(devsAdb) + len(devsFastboot)) > 1: print("Multiple Android devices found, cannot identify target", file=sys.stderr)
		else: print("No Android devices found, cannot continue", file=sys.stderr)
		sys.exit(1)

# sigh, then there is an issue if device is in Fastboot, instead of Adb
	log = Logger(devsAdb[0])

	time.sleep(600)

	print("Logger had ignored {:d} lines".format(log.getLineCount()))

