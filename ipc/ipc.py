#!/usr/bin/env python

import sys, os, socket, struct, time

class EmuState:
	USERBANK=0
	FIQBANK=1
	IRQBANK=2
	SVCBANK=3
	ABORTBANK=4
	UNDEFBANK=5
	DUMMYBANK=6
	SYSTEMBANK=USERBANK

	MODE2NAME={
		0x10: "User",
		0x11: "FIQ",
		0x12: "IRQ",
		0x13: "SVC",
		0x17: "Abort",
		0x1B: "Undefined",
		0x1F: "System",
		}
	MODE2BANK={
		0x10: USERBANK,
		0x11: FIQBANK,
		0x12: IRQBANK,
		0x13: SVCBANK,
		0x17: ABORTBANK,
		0x1B: UNDEFBANK,
		0x1F: SYSTEMBANK,
	}
	def __init__(self, buf):
		self.regs = struct.unpack("16I", buf[0:4*16])
		buf = buf[4*16:]
		self.banks = []
		for i in range(7):
			self.banks.append(struct.unpack("16I", buf[:4*16]))
			buf = buf[4*16:]
		self.cpsr = struct.unpack("I", buf[0:4])[0]
		self.spsrs = struct.unpack("7I", buf[4:8*4])
		buf = buf[8*4:]
		self.cr, self.ttbr, self.dacr, self.fsr, self.far = struct.unpack("5I", buf)

		self.mode = self.cpsr & 0x1f
		self.bank = self.MODE2BANK[self.mode]
		self.spsr = self.spsrs[self.bank]
		self.banked_regs = self.banks[self.bank]

	def show(self):
		print "Emulation state:"
		for i in range(0,16,4):
			tr = "%3s"%("R%d"%i)
			print " %s: %08x %08x %08x %08x"%(tr,self.regs[i], self.regs[i+1], self.regs[i+2], self.regs[i+3])
		print " CPSR: %08x (mode %s)"%(self.cpsr, self.MODE2NAME[self.mode])
		print " SPSR: %08x"%self.spsr
		print " CR:   %08x"%self.cr
		print " TTBR: %08x"%self.ttbr
		print " DACR: %08x"%self.dacr
		print " FSR:  %08x"%self.fsr
		print " FAR:  %08x"%self.far

class SkyeyeSocket:
	def __init__(self, file="ipcsock"):
		self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.sock.connect(file)
	def send(self, msg):
		totalsent = 0
		while totalsent < len(msg):
			sent = self.sock.send(msg[totalsent:])
			if sent == 0:
				raise Exception("socket connection broken")
			totalsent = totalsent + sent
	def recv(self, size):
		msg = ''
		while len(msg) < size:
			chunk = self.sock.recv(size-len(msg))
			if chunk == '':
				raise Exception("socket connection broken")
			msg = msg + chunk
		return msg

class SkyeyeProtocol(object):
	MSG_MESSAGE = 1
	MSG_ACK = 2
	MSG_STATUS = 3
	MSG_READ = 4
	MSG_WRITE = 5
	MSG_STATE = 6
	MSG_EXIT = 7
	def __init__(self, file="ipcsock"):
		self.s = SkyeyeSocket(file)
		self.readbuf = None
		self.ppcstat = None
		self.acks = 0
	
	def sendmsg(self, msg, arg=0, buf=""):
		self.s.send(struct.pack("III", msg, arg, len(buf)) + buf)
	def recvmsg(self):
		msg, arg, size = struct.unpack("III", self.s.recv(12))
		if size != 0:
			buf = self.s.recv(size)
		else:
			buf = None
		return msg, arg, buf
	def processmsg(self):
		msg, arg, buf = self.recvmsg()
		#print "Got message: %08x %08x %s"%(msg, arg, repr(buf))
		if msg == self.MSG_STATUS:
			self.ppcstat = arg
		elif msg == self.MSG_READ:
			self.readbuf = buf
		elif msg == self.MSG_ACK:
			self.acks += 1
		elif msg == self.MSG_MESSAGE:
			self.ipcreply(arg)
		elif msg == self.MSG_STATE:
			self.statebuf = buf

	def waitppc(self, status=1):
		while self.ppcstat != status:
			self.processmsg()
	def readmem(self, addr, size):
		self.readbuf = None
		self.sendmsg(self.MSG_READ, addr, struct.pack("I", size))
		while self.readbuf is None:
			self.processmsg()
		return self.readbuf
	def writemem(self, addr, buf):
		self.sendmsg(self.MSG_WRITE, addr, buf)
	def ipcrequest(self, addr):
		self.sendmsg(self.MSG_MESSAGE, addr)
	
	def ipcreply(self, addr):
		print "Unhandled IPC reply: %08x"%addr
	
	def clearack(self):
		self.acks = 0
	def sendack(self):
		self.sendmsg(self.MSG_ACK)
	def waitack(self):
		while self.acks == 0:
			self.processmsg()

	def getstate(self):
		self.statebuf = None
		self.sendmsg(self.MSG_STATE)
		while self.statebuf is None:
			self.processmsg()
		
		return EmuState(self.statebuf)
	
	def read32(self, addr):
		return struct.unpack(">I", self.readmem(addr, 4))[0]
	def read16(self, addr):
		return struct.unpack(">H", self.readmem(addr, 2))[0]
	def read8(self, addr):
		return struct.unpack(">B", self.readmem(addr, 1))[0]

	def write32(self, addr, data):
		self.writemem(addr, struct.pack(">I", data))
	def write16(self, addr, data):
		self.writemem(addr, struct.pack(">H", data))
	def write8(self, addr, data):
		self.writemem(addr, struct.pack(">B", data))
	def exit(self):
		self.sendmsg(self.MSG_EXIT)

class MemMgr(object):
	def __init__(self, start, end, block=64):
		if start%block:
			raise ValueError("heap start not aligned")
		if end%block:
			raise ValueError("heap end not aligned")
		self.offset = start
		self.count = (end - start) / block
		self.blocks = [(self.count,False)]
		self.block = block
	def malloc(self, size):
		size = (size + self.block - 1) / self.block
		pos = 0
		for i, (bsize, full) in enumerate(self.blocks):
			if not full:
				if bsize == size:
					self.blocks[i] = (bsize,True)
					return self.offset + self.block * pos
				if bsize > size:
					self.blocks[i] = (size,True)
					self.blocks.insert(i+1, (bsize-size, False))
					return self.offset + self.block * pos
			pos += bsize
		raise Exception("Out of memory")
	def free(self, addr):
		if addr%self.block:
			raise ValueError("free address not aligned")
		if addr<self.offset:
			raise ValueError("free address before heap")
		addr -= self.offset
		addr /= self.block
		if addr>=self.count:
			raise ValueError("free address after heap")
		pos = 0
		for i, (bsize, used) in enumerate(self.blocks):
			if pos > addr:
				raise ValueError("bad free address")
			if pos == addr:
				if used == False:
					raise ValueError("block already free")
				if i!=0 and self.blocks[i-1][1] == False:
					bsize += self.blocks[i-1][0]
					del self.blocks[i]
					i -= 1
				if i!=(len(self.blocks)-1) and self.blocks[i+1][1] == False:
					bsize += self.blocks[i+1][0]
					del self.blocks[i]
				self.blocks[i] = (bsize, False)
				return
			pos += bsize
		raise ValueError("bad free address")
	def check(self):
		free = 0
		inuse = 0
		for i, (bsize, used) in enumerate(self.blocks):
			if used:
				inuse += bsize
			else:
				free += bsize
		if free + inuse != self.count:
			raise Exception("Total block size is inconsistent")
		print "Heap stats:"
		print " In use: %8dkB"%(inuse * self.block / 1024)
		print " Free:   %8dkB"%(free * self.block / 1024)

class IPCBuffer(object):
	def __init__(self, ipcdev, arg):
		self.ipcdev = ipcdev
		if isinstance(arg, str):
			self.size = len(arg)
			self.addr = self.ipcdev.mallwrite(arg)
		elif isinstance(arg, int) or isinstance(arg, long):
			self.size = arg
			self.addr = self.ipcdev.malloc(arg)
	def read(self):
		return self.ipcdev.readmem(self.addr, self.size)
	def write(self, val):
		if len(val) > self.size:
			raise ValueError("buffer overflow")
		self.ipcdev.writemem(self.addr, val)
	def free(self):
		if self.addr is not None:
			self.ipcdev.free(self.addr)
			self.addr = None
	def __del__(self):
		self.free()

class IPCRequest(object):
	def __init__(self, cmd, fd=0):
		self.ipcdev = None
		self.freebufs = []
		self.cmd = cmd
		self.fd = fd
		self.args = []
		self.done = False
		self.addr = None

	def getbuf(self, buf, ipcdev):
		if buf is None:
			return (0,0)
		if isinstance(buf, IPCBuffer):
			return buf.addr, buf.size
		elif isinstance(buf, str):
			ibuf = ipcdev.makebuf(buf)
			self.freebufs.append(ibuf)
			return ibuf.addr, ibuf.size
		elif isinstance(buf, tuple):
			return buf
		else:
			raise ValueError("invalid buffer type")

	def prepare(self, ipcdev):
		while len(self.args) < 5:
			self.args.append(0)
		self.ipcdev = ipcdev
		req = struct.pack(">Iii5I", self.cmd, 0, self.fd, *self.args)
		self.addr = self.ipcdev.mallwrite(req)
		return self.addr

	def reply(self):
		self.result = struct.unpack(">i", self.ipcdev.readmem(self.addr+4, 4))[0]
		self.free()
		self.done = True
	
	def free(self):
		for buf in self.freebufs:
			buf.free()
		self.freebufs = []
		if self.addr is not None:
			self.ipcdev.free(self.addr)
			self.ipcdev.delreq(self.addr)
			self.addr = None

	def __del__(self):
		self.free()

	def wait(self):
		while not self.done:
			self.ipcdev.processmsg()
		return self.result

class IOSOpen(IPCRequest):
	def __init__(self, path, mode=0):
		self.path = path
		self.mode = mode
		IPCRequest.__init__(self, 1, 0)
	def prepare(self, ipcdev):
		pathbuf = ipcdev.makebuf(self.path+"\0")
		self.freebufs.append(pathbuf)
		self.args = [pathbuf.addr, self.mode]
		return IPCRequest.prepare(self,ipcdev)
	
class IOSClose(IPCRequest):
	def __init__(self, fd):
		IPCRequest.__init__(self, 2, fd)

class IOSReadWrite(IPCRequest):
	def __init__(self, cmd, fd, buf, size=None):
		self.buf = buf
		self.size = size
		IPCRequest.__init__(self, cmd, fd)
	def prepare(self, ipcdev):
		bufaddr, bufsize = self.getbuf(self.buf, ipcdev)
		if self.size is None:
			self.args = [bufaddr, bufsize]
		else:
			self.args = [bufaddr, self.size]
		return IPCRequest.prepare(self,ipcdev)

class IOSRead(IOSReadWrite):
	def __init__(self, fd, buf, size=None):
		IOSReadWrite.__init__(self, 3, fd, buf, size)

class IOSWrite(IOSReadWrite):
	def __init__(self, fd, buf, size=None):
		IOSReadWrite.__init__(self, 4, fd, buf, size)

class IOSSeek(IPCRequest):
	def __init__(self, fd, where, whence):
		self.where = where
		self.whence = whence
		IPCRequest.__init__(self, 5, fd)
	def prepare(self, ipcdev):
		self.args = [self.where & 0xFFFFFFFF, self.whence]
		return IPCRequest.prepare(self,ipcdev)
	
class IOSIoctl(IPCRequest):
	def __init__(self, fd, ioctl, bufi=None, bufo=None):
		self.ioctl = ioctl
		self.bufi = bufi
		self.bufo = bufo
		IPCRequest.__init__(self, 6, fd)
	def prepare(self, ipcdev):
		bufiaddr, bufisize = self.getbuf(self.bufi, ipcdev)
		bufoaddr, bufosize = self.getbuf(self.bufo, ipcdev)
		self.args = [self.ioctl, bufiaddr, bufisize, bufoaddr, bufosize]
		return IPCRequest.prepare(self,ipcdev)

class IOSIoctlv(IPCRequest):
	def __init__(self, fd, ioctl, format, *args):
		self.ioctl = ioctl
		self.format = format
		self.ioargs = args
		IPCRequest.__init__(self, 7, fd)
	def parse(self, ipcdev, fmt, args):
		left = list(args)
		bufs = []
		for c in fmt:
			if c == 'b':
				bufs.append(self.getbuf(struct.pack(">B", left[0] & 0xFF), ipcdev))
			elif c == 'h':
				bufs.append(self.getbuf(struct.pack(">H", left[0] & 0xFFFF), ipcdev))
			elif c == 'i':
				bufs.append(self.getbuf(struct.pack(">I", left[0] & 0xFFFFFFFF), ipcdev))
			elif c == 'q':
				bufs.append(self.getbuf(struct.pack(">Q", left[0] & 0xFFFFFFFFFFFFFFFF), ipcdev))
			elif c == 'd':
				bufs.append(self.getbuf(left[0], ipcdev))
			else:
				raise ValueError("bad ioctlv format")
			left = left[1:]
		return bufs, left
	
	def prepare(self, ipcdev):
		fmt = self.format
		if ':' not in self.format:
			ifmt = fmt
			ofmt = ''
		else:
			ifmt, ofmt = fmt.split(':')
		ibufs, left = self.parse(ipcdev, ifmt, self.ioargs)
		obufs, left = self.parse(ipcdev, ofmt, left)
		if left:
			raise ValueError("Too many ioctlv arguments")
		blist = ""
		for addr, size in ibufs + obufs:
			blist += struct.pack(">II", addr, size)
		
		blbuf = ipcdev.makebuf(blist)
		self.freebufs.append(blbuf)
		self.args = [self.ioctl, len(ibufs), len(obufs), blbuf.addr]
		return IPCRequest.prepare(self,ipcdev)

class SkyeyeIPC(SkyeyeProtocol):
	def __init__(self, file="ipcsock", heaplo=0x11000000, heaphi=0x12000000):
		SkyeyeProtocol.__init__(self, file)
		self.mem = MemMgr(heaplo, heaphi)
		self.requests = {}

	def malloc(self, size):
		return self.mem.malloc(size)
	def mallwrite(self, buf):
		addr = self.mem.malloc(len(buf))
		self.writemem(addr, buf)
		return addr
	def free(self, size):
		return self.mem.free(size)
	
	def async(self, req):
		addr = req.prepare(self)
		self.requests[addr] = req
		self.ipcrequest(addr)
	def sync(self, req):
		self.async(req)
		return req.wait()

	def ipcreply(self, addr):
		if addr in self.requests:
			self.requests[addr].reply()
			return
		print "Unhandled IPC reply: %08x"%addr
	
	def delreq(self, addr):
		del self.requests[addr]
	
	def makebuf(self, arg):
		return IPCBuffer(self, arg)
	
	def IOSOpen(self, path, mode=0):
		return self.sync(IOSOpen(path, mode))
	def IOSClose(self, fd):
		return self.sync(IOSClose(fd))
	def IOSRead(self, fd, buf, size=None):
		return self.sync(IOSRead(fd, buf, size))
	def IOSWrite(self, fd, buf, size=None):
		return self.sync(IOSRead(fd, buf, size))
	def IOSSeek(self, where, whence):
		return self.sync(IOSSeek(where, whence))
	def IOSIoctl(self, fd, ioctl, bufi=None, bufo=None):
		return self.sync(IOSIoctl(fd, ioctl, bufi, bufo))
	def IOSIoctlv(self, fd, ioctl, format, *args):
		return self.sync(IOSIoctlv(fd, ioctl, format, *args))

	def init(self):
		self.waitppc()
		self.sendack()

	def initreload(self):
		self.waitack()
		self.sendack()

	def initpoll(self):
		# send a random ACK and wait for IOS to clear the bit
		self.writemem(0xd800004, struct.pack(">I",0x38))
		dr = struct.unpack(">I",self.readmem(0xd800004, 4))[0]
		while dr & 8 == 8:
			time.sleep(0.1)
			dr = struct.unpack(">I",self.readmem(0xd800004, 4))[0]

if __name__=="__main__":
	ipc = SkyeyeIPC()
	print "Initializing IPC..."
	ipc.init()
	print "IPC is initialized"
