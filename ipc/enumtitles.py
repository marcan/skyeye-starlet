#!/usr/bin/env python

import sys
from ipc import *
import struct
sys.path.append(os.path.realpath(os.path.dirname(sys.argv[0]))+"/../../pywii/Common")
import pywii as wii

wii.loadkeys()

def decode_title(titleid):
	known_titles = {
						0x0000000100000001 : "boot2",
						0x0000000100000002 : "SystemMenu",
						0x0000000100000100 : "BC",
						0x0000000100000101 : "MIOS",
						0x0001000148415858 : "HBC",
						0x0001000844564458 : "DVDX"
					}

	if titleid in known_titles:
		return known_titles[titleid]

	type = titleid >> 32
	id = titleid & 0xFFFFFFFF

	if type == 0x00000001:
		return "IOS%d" % id
	if type == 0x00010000:
		return "Disc title"
	if type == 0x00010001:
		subtype = id >> 24
		if subtype == 0x43:
			return "VC C64"
		if subtype == 0x45:
			return "VC NeoGeo"
		if subtype == 0x46:
			return "VC NES"
		if subtype == 0x48:
			return "DLC Channel"
		if subtype == 0x4a:
			return "VC SNES"
		if subtype == 0x4e:
			return "VC N64"
		if subtype == 0x57:
			return "WiiWare"
	if type == 0x00010002:
		return "System channel"
	if type == 0x00010004:
		return "Disc channel"
	if type == 0x00010005:
		return "Additional content"
	if type == 0x00010008:
		return "System Channel"
	return "Unknown title type"

print "Waiting for IPC to start up..."
ipc = SkyeyeIPC()
ipc.init()
print "IPC ready"

# lol
ipc.IOSClose(0)
ipc.IOSClose(1)
ipc.IOSClose(2)
ipc.IOSClose(3)

fd = ipc.IOSOpen("/dev/es")
print "ES fd: %d" % fd
if fd < 0:
	print "Error opening ES"
	sys.exit(1)

buf = ipc.makebuf(4)
res = ipc.IOSIoctlv(fd, 0x0e, ":d", buf)
if res < 0:
	print "Error %d" % res
	buf.free()
	ipc.IOSClose(fd)
	sys.exit(1)

numtitles = struct.unpack(">I", buf.read())[0]
print "Number of titles: %d" % numtitles
buf.free()

buf = ipc.makebuf(8 * numtitles)
res = ipc.IOSIoctlv(fd, 0x0f, "i:d", numtitles, buf)
if res < 0:
	print "Error %d" % res
	buf.free()
	ipc.IOSClose(fd)
	sys.exit(1)

titles = list(struct.unpack(">" + "Q" * numtitles, buf.read()))
buf.free()

titles.sort()

buf = ipc.makebuf(4)
for t in titles:
	print "\nTitle 0x%016x (%s)\n" % (t, decode_title(t))

	res = ipc.IOSIoctlv(fd, 0x34, "q:d", t, buf)
	if res < 0:
		print "Error reading the TMD size %d" % res
		continue

	size = struct.unpack(">I", buf.read())[0]

	stmd = ipc.makebuf(size)
	res = ipc.IOSIoctlv(fd, 0x35, "qi:d", t, size, stmd)
	if res < 0:
		stmd.free()
		print "Error reading the TMD %d" % res
		continue

	tmd = wii.WiiTmd(stmd.read())
	tmd.showinfo()
	stmd.free()

buf.free()

ipc.IOSClose(fd)

