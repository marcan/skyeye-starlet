#!/usr/bin/env python

import sys
from ipc import *

print "Waiting for IPC to start up..."
ipc = SkyeyeIPC()
ipc.init()
print "IPC ready"

fd = ipc.IOSOpen("/dev/es")
print "ES fd: %d"%fd
if fd < 0:
	print "Error opening ES"
	sys.exit(1)

print "Launching..."

launchres = IOSIoctlv(fd, 0x25, "")
ipc.clearack()
ipc.async(launchres)

print "Waiting for PPC..."

ipc.waitppc(0)
print "PPC dead"
ipc.waitppc(1)
print "PPC alive"
ipc.exit()
