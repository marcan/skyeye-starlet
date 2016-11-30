#!/usr/bin/env python

import sys
from ipc import *

ios = int(sys.argv[1])
titleID = 0x100000000 | ios

print "Waiting for IPC to start up..."
ipc = SkyeyeIPC()
ipc.init()
print "IPC ready"
print "Going to launch IOS%d"%ios

fd = ipc.IOSOpen("/dev/es")
print "ES fd: %d"%fd
if fd < 0:
	print "Error opening ES"
	sys.exit(1)

tikbuf = ipc.makebuf(216)
res = ipc.IOSIoctlv(fd, 0x13, "qi:d", titleID, 1, tikbuf)
if res < 0:
	print "Error %d getting ticket views"%res
	ipc.IOSClose(fd)
	sys.exit(1)

print "Launching..."

# this special sequence is specific for IOS reboots
# issue an async request, then wait for EITHER a reply OR a ack
# ack means IOS rebooted, reply means some kind of fail
# send an ack after a successful reboot to reinit IPC
launchres = IOSIoctlv(fd, 0x08, "qd:", titleID, tikbuf)
ipc.clearack()
ipc.async(launchres)
while ipc.acks == 0 and not launchres.done:
	ipc.processmsg()

tikbuf.free()
if ipc.acks != 0:
	ipc.sendack()
	launchres.free()
	print "IOS%d launched"%ios
else:
	print "Error %d while launching"%launchres.result
	ipc.IOSClose(fd)
