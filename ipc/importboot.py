#!/usr/bin/env python

import sys
from ipc import *
sys.path.append(os.path.realpath(os.path.dirname(sys.argv[0]))+"/../../pywii/Common")
import pywii as wii
import sha

wii.loadkeys()

def getcerts(signed,certs):
	data = ""
	for certname in signed.issuer[::-1]:
		if certname == "Root":
			continue
		cert = certs[certname]
		data += cert.data
	return data

wad = wii.WiiWad(sys.argv[1])

print "Going to import boot2 version",wad.tmd.title_version

tmd = wad.tmd.data
tik = wad.tik.data
wad.showinfo()
tikcerts = getcerts(wad.tik, wad.certs)
tmdcerts = getcerts(wad.tmd, wad.certs)
content = wad.getcontent(0, encrypted=True)
content += "\x00" * 0
content2 = wad.getcontent(0)

wii.chexdump(wad.tmd.get_content_records()[0].sha)
wii.chexdump(sha.new(content2).digest())

print "Waiting for IPC to start up..."
ipc = SkyeyeIPC()
ipc.init()
print "IPC ready"

fd = ipc.IOSOpen("/dev/es")
print "ES fd: %d"%fd
if fd < 0:
	print "Error opening ES"
	sys.exit(1)

res = ipc.IOSIoctlv(fd, 0x1f, "dddddd", tik, tikcerts, tmd, tmdcerts, None, content)
ipc.IOSClose(fd)

if res < 0:
	print "Error %d importing boot2"%res
else:
	print "boot2 import succeeded"

