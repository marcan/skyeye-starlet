#!/usr/bin/python

import sys, time
import os, struct, code, traceback, readline
from ipc import *
import __main__
import __builtin__

ipc = SkyeyeIPC()

saved_display = sys.displayhook

def display(val):
	global saved_display
	if isinstance(val, int) or isinstance(val, long):
		__builtin__._ = val
		print hex(val)
	else:
		saved_display(val)

sys.displayhook = display

# convenience
h = hex

SCRATCH = 0x11000000

locals = __main__.__dict__

for attr in dir(ipc):
	locals[attr] = getattr(ipc,attr)
del attr

class ConsoleMod(code.InteractiveConsole):
	def showtraceback(self):
		type, value, tb = sys.exc_info()
		self.write(traceback.format_exception_only(type, value)[0])

ConsoleMod(locals).interact("Have fun!")

