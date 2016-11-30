#!/usr/bin/python

import sys, time
from ipc import *

ipc = SkyeyeIPC()

state = ipc.getstate()
cr = state.cr
ttbr = state.ttbr

if cr & 1 == 0:
	print "MMU disabled!"
	sys.exit(1)

if ttbr == 0:
	print "TTBR not set!"
	sys.exit(1)

def pap(b):
	if b == 0:
		if cr & 0x10:
			if cr & 0x20:
				return "???"
			else:
				return "R/_"
		else:
			if cr & 0x20:
				return "R/R"
			else:
				return "_/_"
	elif b == 1:
		return "W/_"
	elif b == 2:
		return "W/R"
	elif b == 3:
		return "W/W"

def pcb(b):
	if b == 0:
		return "NC"
	elif b == 1:
		return "B"
	elif b == 2:
		return "WT"
	elif b == 3:
		return "WB"

def psiz(s):
	if s % (1024*1024) == 0:
		return "%3dM"%(s/1024/1024)
	elif s % (1024) == 0:
		return "%3dK"%(s/1024)
	return "%d"%s

def docoarse(addr, base, domain):
	level2 = struct.unpack(">256I",ipc.readmem(base, 1024))
	pfrom = None
	pto = None
	pinfo = None
	psize = None
	for i,pd in enumerate(level2):
		subaddr = (i<<12) | addr
		t = pd & 3
		if t == 0:
			continue
		cb = (pd>>2)&3
		ap0 = (pd>>4)&3
		ap1 = (pd>>6)&3
		ap2 = (pd>>8)&3
		ap3 = (pd>>10)&3
		if ap0 == ap1 == ap2 == ap3:
			aps = pap(ap0)
		else:
			aps = ','.join(map(pap,[ap0,ap1,ap2,ap3]))
		info = "[%s] [AP:%s] [D:%d]"%(pcb(cb), aps, domain)
		if t == 1 and ((i&3) == 0):
			base = (pd&0xFFFF0000)
			size = 64 * 1024
			info = "L " + info
		elif t == 2:
			base = (pd&0xFFFFF000)
			size = 4 * 1024
			info = "S " + info
		elif t == 3:
			raise Exception("WTF")
		
		if pfrom is not None:
			if pinfo == info and (pto + psize) == base:
				psize += size
				continue
			else:
				print "%08x -> %08x..%08x [%s] %s"%(pfrom, pto, pto+psize, psiz(psize), pinfo)
		
		pfrom = subaddr
		pto = base
		pinfo = info
		psize = size

	if pfrom is not None:
		print "%08x -> %08x..%08x [%s] %s"%(pfrom, pto, pto+psize, psiz(psize), pinfo)

print "Page tables (TTBR %08x):"%ttbr

level1 = struct.unpack(">4096I",ipc.readmem(ttbr, 16384))

pfrom = None
pto = None
pinfo = None
psize = None

for i,pd in enumerate(level1):
	addr = i<<20
	t = pd & 3
	if t != 2:
		if pfrom is not None:
			print "%08x -> %08x..%08x [%s] %s"%(pfrom, pto, pto+psize, psiz(psize), pinfo)
			pfrom = None

	if t == 0:
		continue
	domain = (pd >> 5) & 0xf
	if t == 1:
		base = (pd&0xFFFFFC00)
		#print "Coarse Subtable [D:%2d]"%(domain)
		docoarse(addr, base, domain)
		continue
	if t == 3:
		print "%08x Fine [D:%d] (not implemented)"%(addr, domain)
		continue
	
	cb = (pd>>2)&3
	ap = (pd>>10)&3
	base = (pd&0xFFF00000)
	info = "G [%s] [AP:%s] [D:%d]"%(pcb(cb), pap(ap), domain)
	size = 1024*1024
	if pfrom is not None:
		if pinfo == info and (pto + psize) == base:
			psize += size
			continue
		else:
			print "%08x -> %08x..%08x [%s] %s"%(pfrom, pto, pto+psize, psiz(psize), pinfo)

	pfrom = addr
	pto = base
	pinfo = info
	psize = size

if pfrom is not None:
	print "%08x -> %08x..%08x [%s] %s"%(pfrom, pto, (pto+psize)&0xffffffff, psiz(psize), pinfo)

