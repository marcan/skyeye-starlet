#!/usr/bin/env python

import sys
from ipc import *

sys.path.append('../pywii/Common')
from pywii import *

CNTDATA_SIZE = 0x10000

def install_tik(ipc, es, tik, certs):
	tikbuf = ipc.makebuf(tik)
	certbuf = ipc.makebuf(certs)
	res = ipc.IOSIoctlv(es, 0x01, "ddd", tikbuf, certbuf, None)
	print "%d" % res,
	return res

def install_tmd(ipc, es, tmd, certs):
	tmdbuf = ipc.makebuf(tmd)
	certbuf = ipc.makebuf(certs)
	res = ipc.IOSIoctlv(es, 0x02, "dddi", tmdbuf, certbuf, None, 1)
	print "%d" % res,
	return res

def install_content(ipc, es, wad, tid, cr):
	cntbuf = wad.getcontent(cr.index, True)

	id = res = ipc.IOSIoctlv(es, 0x03, "qi", tid, cr.cid)
	print "%d -" % res,
	if res < 0:
		return res

	size = cr.size
	offset = 0
	while size > 0:
		if size < CNTDATA_SIZE:
			b = ipc.makebuf(cntbuf[offset:])
			res = ipc.IOSIoctlv(es, 0x04, "id", id, b)
			b.free()
			size = 0
		else:
			b = ipc.makebuf(cntbuf[offset:offset+CNTDATA_SIZE])
			res = ipc.IOSIoctlv(es, 0x04, "id", id, b)
			b.free()
			offset = offset + CNTDATA_SIZE
			size = size - CNTDATA_SIZE

		print "%d" % res,
		if res < 0:
			return res

	res = ipc.IOSIoctlv(es, 0x05, "i", id)
	print "- %d" % res,
	return res

def install_finish(ipc, es):
	return ipc.IOSIoctlv(es, 0x06, "")

wad = WiiWad(sys.argv[1])
tid = unpack(">Q", wad.tmd.title_id)[0]

print "title id 0x%016X" % tid

print "Waiting for IPC to start up..."
ipc = SkyeyeIPC()
ipc.init()
print "IPC ready"

for i in range(16):
	ipc.IOSClose(i)

fd = ipc.IOSOpen("/dev/es")
print "ES fd: %d"%fd
if fd < 0:
	print "Error opening ES"
	sys.exit(1)

certs = ""
for cert in wad.certlist:
	certs += cert.data

print "\nInstalling tik...",
res = install_tik(ipc, fd, wad.tik.data, certs)
if res < 0:
	sys.exit(1)

print "\nInstalling tmd...",
res = install_tmd(ipc, fd, wad.tmd.data, certs)
if res < 0:
	sys.exit(1)

for cr in wad.tmd.get_content_records():
	print "\nInstalling content %d (%d bytes)... " % (cr.index, cr.size),
	res = install_content(ipc, fd, wad, tid, cr)
	if res < 0:
		sys.exit(1)

print "\nFinishing install...",
res = install_finish(ipc, fd)
print "%d" % res
if res < 0:
	sys.exit(1)

print "All done!"

