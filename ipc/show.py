#!/usr/bin/python

import sys, time
from ipc import *

ipc = SkyeyeIPC()

while True:
	print "============================================================================"
	ipc.getstate().show()
	time.sleep(0.01)
