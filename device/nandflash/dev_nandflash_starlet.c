/* "Starlet" NAND flash hardware interface emulation
    Copyright 2008, 2009	Haxx Enterprises
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "armdefs.h"
#include "skyeye_device.h"
#include "skyeye_nandflash.h"
#include "dev_nandflash_starlet.h"

void starlet_arm2host (ARMul_State *state, void *dst, ARMword src, ARMword len);
void starlet_host2arm (ARMul_State *state, ARMword dst, void *src, ARMword len);

//#define NADNFLASH_VERBOSE printf
#define NADNFLASH_VERBOSE(...)

// ecc code based on segher's unecc.c
static u8
parity(u8 x) {
	u8 y = 0;

	while (x) {
		y ^= (x & 1);
		x >>= 1;
	}

	return y;
}

static void 
calc_ecc(u8 *data, u8 *ecc) {
	u8 a[12][2];
	int i, j;
	u32 a0, a1;
	u8 x;

	memset(a, 0, 12*2);
	for (i = 0; i < 512; i++) {
        x = data[i];
        for (j = 0; j < 9; j++)
			a[3+j][(i >> j) & 1] ^= x;
	}

	x = a[3][0] ^ a[3][1];
	a[0][0] = x & 0x55;
	a[0][1] = x & 0xaa;
	a[1][0] = x & 0x33;
	a[1][1] = x & 0xcc;
	a[2][0] = x & 0x0f;
	a[2][1] = x & 0xf0;

	for (j = 0; j < 12; j++) {
		a[j][0] = parity(a[j][0]);
		a[j][1] = parity(a[j][1]);
	}

	a0 = a1 = 0;
	for (j = 0; j < 12; j++) {
		a0 |= a[j][0] << j;
		a1 |= a[j][1] << j;
	}

	ecc[0] = a0;
	ecc[1] = a0 >> 8;
	ecc[2] = a1;
	ecc[3] = a1 >> 8;
}

static void nandflash_starlet_interrupt(struct device_desc *dev)
{
	struct device_interrupt *intr = &dev->intr;
	struct machine_config *mc = (struct machine_config*)dev->mach;

	if (mc->mach_set_intr == NULL) return;
	mc->mach_set_intr(intr->interrupts[0]);

	if (mc->mach_update_intr != NULL) mc->mach_update_intr(mc);
}

static void
nandflash_starlet_fini (struct device_desc *dev)
{
	struct nandflash_device *nandflashdev=(struct nandflash_device*)dev->dev;
	struct nandflash_starlet_io *io = (struct nandflash_starlet_io *) dev->data;

	nandflashdev->uinstall(nandflashdev);
	if (!dev->dev)
		free (dev->dev);
	if (!io)
		free (io);
}

static void
nandflash_starlet_reset (struct device_desc *dev)
{
	struct nandflash_device *nandflash_dev = (struct nandflash_device *) dev->dev;
	struct nandflash_starlet_io *io = (struct nandflash_starlet_io *) dev->data;

	memset(io->address, 0, 6);
	memset(io->buf, 0, sizeof io->buf);
	io->nfcmd=0;
	io->nfconf=0;
	io->nf_data=0;
	io->nf_ecc=0;
	io->nfstat=0;
	nandflash_dev->setCE(nandflash_dev,NF_LOW);
}

static void
nandflash_starlet_update (struct device_desc *dev)
{
//	struct nandflash_device *nandflash_dev = (struct nandflash_device *) dev->dev;
//	printf("update: dev->devicesize=%x\n", nandflash_dev->devicesize);
//	struct nandflash_starlet_io *io = (struct nandflash_starlet_io *) dev->data;
//	struct machine_config *mc = (struct machine_config *) dev->mach;
// ? do nothing?
}

int iomach(u32 addr)
{
	if((addr>=NFCMD)&&(addr<=NF_ECC))
		return 1;
	else
		return 0;
}

int
nandflash_starlet_read_word (struct device_desc *dev, u32 addr, u32 * data)
{
	struct nandflash_device *nandflash_dev = (struct nandflash_device *) dev->dev;

	struct nandflash_starlet_io *io = (struct nandflash_starlet_io *) dev->data;
	int ret = ADDR_HIT;
	if (!iomach(addr)) return ADDR_NOHIT;
	
	*data = 0;
	switch (addr) {
	case NFCONF: *data = io->nfconf; break;
	case NFADDR0: *data = io->nf_addr0; break;
	case NFADDR1: *data = io->nf_addr1; break;
	case NF_DATA: *data= io->nf_data; break;
	case NFSTAT: *data=nandflash_dev->readRB(nandflash_dev); break;
	case NF_ECC: *data= io->nf_ecc; break;
	case NF_UNK1: NANDFLASH_DBG("starlet NAND: R unk1=%08x\n", *data); break;
	case NF_UNK2: NANDFLASH_DBG("starlet NAND: R unk1=%08x\n", *data); break;
	default:
		NANDFLASH_DBG("%s: read from unknown addr %x\n", __FUNCTION__, addr);
		ret = ADDR_NOHIT;
		break;
	}
	return ret;
}

int
nandflash_starlet_read_byte(struct device_desc *dev, u32 addr, u8 * data)
{
	return nandflash_starlet_read_word(dev,addr,(u32*)data);
}

#define NAND_FLAGS_WAIT 0x8
#define NAND_FLAGS_WR	0x4
#define NAND_FLAGS_RD	0x2
#define NAND_FLAGS_ECC	0x1

//#define printf(...) ((void)0)
void starlet_hexdump(void *d, int len);

int
nandflash_starlet_write_word (struct device_desc *dev, u32 addr, u32 data)
{
	struct nandflash_device *nandflash_dev = (struct nandflash_device *) dev->dev;
	struct nandflash_starlet_io *io = (struct nandflash_starlet_io *) dev->data;
	ARMul_State *state = (ARMul_State*)skyeye_config.mach->state;
	int ret = ADDR_HIT;
	int i;
	if (!iomach(addr)) return ADDR_NOHIT;
	switch (addr) {
	case NFCONF:
		io->nfconf=data;
		NANDFLASH_DBG("starlet NAND: W conf=%08x\n", data);
		
		break;
	case NFCMD:	
		if (data == 0x7FFFFFFF) {
			NANDFLASH_DBG("starlet NAND: ignoring 0x7FFFFFFF\n");
			return ADDR_HIT;
		}
		if (data == 0) {
			NANDFLASH_DBG("starlet NAND: ignoring 0\n");
			return ADDR_HIT;
		}
		unsigned int bitmask=(data & 0x1f000000) >> 24;
		unsigned int command=(data & 0x00ff0000) >> 16;
		unsigned int datasize=(data & 0x00000fff);
		unsigned int flags=(data & 0x0000f000)>>12;
		NANDFLASH_DBG("sending bitmask=%x command %02x datasize=%x\n", bitmask, command, datasize);
		int pageno = io->address[4] << 16 | io->address[3] << 8 | io->address[2];
		switch (command) {
			case 0: break;
			case NAND_CMD_READID: NADNFLASH_VERBOSE("[NAND] READID\n"); break;
			case NAND_CMD_RESET: NADNFLASH_VERBOSE("[NAND] reset\n"); break;
			case NAND_CMD_READ0b:
					/*NADNFLASH_VERBOSE("[NAND] READ1 address %02x%02x%02x%02x %02x%02x datasize=%x address=%08x spareaddr=%08x\n", 
						   io->address[5], io->address[4], io->address[3], io->address[2], io->address[1], io->address[0], datasize, io->nf_data, io->nf_ecc)*/;
				break;
			case NAND_CMD_ERASE1: NADNFLASH_VERBOSE("[NAND] ERASE PREPARE\n"); break;
			case NAND_CMD_ERASE2:
				NADNFLASH_VERBOSE("[NAND] ERASE pages %08x-%08x\n", pageno, pageno+0x3f);
				// todo: implement erase
			break;
			case 0x80: NADNFLASH_VERBOSE("[NAND] PAGE PROGRAM START (addr = %02x%02x%02x%02x %02x%02x) (size = %x)\n",
				io->address[5], io->address[4], io->address[3], io->address[2], io->address[1], io->address[0], datasize);
			break;
			case 0x85: NADNFLASH_VERBOSE("[NAND] RANDOM DATA IN (offset = %02x%02x) (size = %x)\n", io->address[1], io->address[0], datasize); break;
			case 0x10: NADNFLASH_VERBOSE("[NAND] PAGE PROGRAM CONFIRM\n"); break;
			case NAND_CMD_STATUS: /*printf("[NAND] GETSTATUS\n");*/ break;
			default:
				printf("[NAND] unknown command %02x address %02x%02x%02x%02x %02x%02x datasize=%x address=%08x spareaddr=%08x flags=%x bitmask=%2x\n", 
						command, io->address[5], io->address[4], io->address[3], io->address[2], io->address[1], io->address[0], datasize, io->nf_data, io->nf_ecc, flags, bitmask);
		}
		/*printf("[NAND] command %02x address %02x%02x%02x%02x %02x%02x datasize=%x address=%08x spareaddr=%08x flags=%x bitmask=%2x\n", 
			command, io->address[5], io->address[4], io->address[3], io->address[2], io->address[1], io->address[0], datasize, io->nf_data, io->nf_ecc, flags, bitmask);*/
		nandflash_dev->sendcmd(nandflash_dev,(u8)command);
		for (i=0; i<6; i++) if (bitmask & (1 << i)) {
				nandflash_dev->sendaddr(nandflash_dev,io->address[i]);
			}
		if(datasize>0) {
			if(flags&NAND_FLAGS_RD) {
				for (i=0; i<datasize; i++) {
					u32 tmp = nandflash_dev->readdata(nandflash_dev);
					io->buf[i] = tmp & 0xff;
				}
				if(datasize <= 0x800) {
					starlet_host2arm(state, io->nf_data, io->buf, datasize);
				} else if(datasize == 0x840) {
					starlet_host2arm(state, io->nf_data, io->buf, 0x800);
					starlet_host2arm(state, io->nf_ecc, &io->buf[0x800], 0x40);
					if(flags&NAND_FLAGS_ECC) {
						u8 bytes[4], i;
						//printf("nf_ecc is at %08x, writing to %08x\n", io->nf_ecc, io->nf_ecc ^ 0x40);
						for (i = 0; i < 4; i++) {
							/*if (!memcmp(io->buf + 0x830 + i*4, "\xff\xff\xff\xff", 4)) memcpy(bytes, "\xff\xff\xff\xff", 4);
							else if (!memcmp(io->buf + 0x830 + i*4, "\x00\x00\x00\x00", 4)) memcpy(bytes, "\x00\x00\x00\x00", 4);
							else */calc_ecc(io->buf + i*512, bytes);
							/*if (memcmp(io->buf + 0x830 + i*4, bytes, 4)) {
								printf("[NAND] mismatch ecc%d=%02x%02x%02x%02x calc=%02x%02x%02x%02x\n", i,
									io->buf[0x830+i*4], io->buf[0x831+i*4], io->buf[0x832+i*4], io->buf[0x833+i*4], 
									bytes[0], bytes[1], bytes[2], bytes[3]);
								if (i==0) {
									int j;
									for (j = 0; j < 512; j++) {
										printf("%02x ", io->buf[j]);
										if ((j%16) == 15) printf("\n");
									}
								}
								memcpy(bytes, io->buf + 0x830 + i*4, 4);
							}*/
							starlet_host2arm(state, (io->nf_ecc ^ 0x40) + i * 4, bytes, 4);
						}
					}
				} else {
					printf("Bad NAND data size: 0x%x\n",datasize);
					exit(0);
				}
			}
			if(flags&NAND_FLAGS_WR) {
				if(datasize <= 0x800) {
					starlet_arm2host(state, io->buf, io->nf_data, datasize);
				} else if(datasize == 0x840) {
					u8 i;
					starlet_arm2host(state, io->buf, io->nf_data, 0x800);
					starlet_arm2host(state, &io->buf[0x800], io->nf_ecc, 0x40);
					for (i = 0; i < 4; i++) {
						//printf("Writing ecc%d: %02x%02x%02x%02x\n", i, io->buf[0x830+4*i], io->buf[0x831+4*i], io->buf[0x832+4*i], io->buf[0x833+4*i]);
					}
				} else {
					printf("Bad NAND data size: 0x%x\n",datasize);
					exit(0);
				}
				if(flags&NAND_FLAGS_ECC) {
					u8 bytes[4], i;
					if(datasize != 0x800) {
						printf("Bad NAND data size for ECC: 0x%x\n",datasize);
						exit(0);
					}
					//starlet_hexdump(io->buf, 2048);
					for (i = 0; i < 4; i++) {
						calc_ecc(io->buf + i*512, bytes);
						//printf("Calc'd ecc%d: %02x%02x%02x%02x\n", i, bytes[0],bytes[1],bytes[2],bytes[3]);
						starlet_host2arm(state, (io->nf_ecc ^ 0x40) + i * 4, bytes, 4);
					}
				}
				for (i=0; i<datasize; i++) {
					nandflash_dev->senddata(nandflash_dev, io->buf[i]);
				}
			}
		}
		nandflash_starlet_interrupt(dev);
		break;
	case NFADDR0:
		NANDFLASH_DBG("starlet NAND: W addr0=%08x\n", data);
		io->address[0] = data & 0xff;
		io->address[1] = (data >> 8) & 0xff;
		io->nf_addr0 = data;
		break;
	case NFADDR1:
		NANDFLASH_DBG("starlet NAND: W addr1=%08x\n", data);	
		io->address[2] = data & 0xff;
		io->address[3] = (data >> 8) & 0xff;
		io->address[4] = (data >> 16) & 0xff;
		io->address[5] = (data >> 24) & 0xff;
		io->nf_addr1 = data;
		break;		
	case NF_DATA:
		NANDFLASH_DBG("starlet NAND: W data=%08x\n", data);	
		io->nf_data = data;
		break;
	case NF_ECC:
		NANDFLASH_DBG("starlet NAND: W ecc=%08x\n", data);	
		io->nf_ecc = data;
		break;
	default:
		NANDFLASH_DBG("%s:mach:%x,data:%x\n", __FUNCTION__, addr,data);
	
		ret = ADDR_NOHIT;
		break;
	}

	return ret;
}

int
nandflash_starlet_write_byte (struct device_desc *dev, u32 addr, u8 data)
{
	return nandflash_starlet_write_word(dev,addr,(u32)data);
}

static int
nandflash_starlet_setup (struct device_desc *dev)
{
//	int i;
	struct nandflash_starlet_io *io;
//	struct device_interrupt *intr = &dev->intr;
	struct nandflash_device *nandflashdev=(struct nandflash_device*)dev->dev;
	dev->fini = nandflash_starlet_fini;
	dev->reset = nandflash_starlet_reset;
	dev->update = nandflash_starlet_update;
	dev->read_word = nandflash_starlet_read_word;
	dev->write_word = nandflash_starlet_write_word;
	dev->read_byte= nandflash_starlet_read_byte;
	dev->write_byte= nandflash_starlet_write_byte;
	nandflash_lb_setup(nandflashdev);
	if (nandflash_module_setup(nandflashdev,dev->name)==-1)
		return 1;
	nandflashdev->install(nandflashdev);
	io = (struct nandflash_starlet_io *)
		malloc (sizeof (struct nandflash_starlet_io));
	if (io == NULL)
		return 1;
	memset (io, 0, sizeof (struct nandflash_starlet_io));
	dev->data = (void *) io;
	nandflash_starlet_reset (dev);
	return 0;
}

void
nandflash_starlet_init (struct device_module_set *mod_set)
{
	register_device_module ("starlet_nand", mod_set, &nandflash_starlet_setup);
}

