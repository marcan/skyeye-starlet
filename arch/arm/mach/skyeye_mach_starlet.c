/*
	skyeye_mach_starlet.c - Nintendo starlet simulation for skyeye
	Copyright (C) 2007 Skyeye Develop Group
	Copyright (C) 2007 Hector Martin
	for help please send mail to <hector@marcansoft.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

/*
 * 03/04/2007	Written by Anthony Lee
 */
/*
 * 01/28/2008	Modified by Hector Martin
 * thru (wtf a year already?)
 * 04/04/2009   (and counting)
 */

/*
 * COMPLETED: nothing (okay, a bit more)
 * UNIMPLEMENTED: everything else
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "armdefs.h"
#include "armemu.h"
#include "starlet.h"
#include <openssl/aes.h>
//#include <openssl/sha.h>
#include "sha1.h"

#define STARLET_DEBUG			1

#define PRINT(x...)			printf("[STARLET]: " x)

#if STARLET_DEBUG
#define DEBUG(x...)			do { starlet_print_context(state); printf("[STARLET]: " x); } while(0)
#else
#define DEBUG(x...)			(void)0
#endif
#define SOMETIMES_DIVISOR (500000)

static void starlet_print_context(ARMul_State *state);

char ascii(char s) {
	if(s < 0x20) return '.';
	if(s > 0x7E) return '.';
	return s;
}

void starlet_hexdump(void *d, int len) {
	u8 *data;
	int i, off;
	data = (u8*)d;
	for (off=0; off<len; off += 16) {
		printf("%08x  ",off);
		for(i=0; i<16; i++)
			if((i+off)>=len) printf("   ");
		else printf("%02x ",data[off+i]);

		printf(" ");
		for(i=0; i<16; i++)
			if((i+off)>=len) printf(" ");
		else printf("%c",ascii(data[off+i]));
		printf("\n");
	}
}

void starlet_emu_hexdump(ARMul_State *state, ARMword d, int len) {
	int i, off;
	for (off=0; off<len; off += 16) {
		printf("%08x  ",off);
		for(i=0; i<16; i++)
			if((i+off)>=len) printf("   ");
		else printf("%02x ",ARMul_ReadByte(state, d+off+i));

		printf(" ");
		for(i=0; i<16; i++)
			if((i+off)>=len) printf(" ");
		else printf("%c",ascii(ARMul_ReadByte(state, d+off+i)));
		printf("\n");
	}
}

static inline u16 be16(const u8 *p)
{
	return (p[0] << 8) | p[1];
}

static inline u32 be32(const u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static inline void wbe16(u8 *p, u16 x)
{
	p[0] = x >> 8;
	p[1] = x;
}

static inline void wbe32(u8 *p, u32 x)
{
	wbe16(p, x >> 16);
	wbe16(p + 2, x);
}

struct starlet_io_t {

	ARMword disr, dicvr;
	ARMword dicmdbuf[3];
	ARMword dimar, dilength;
	ARMword dicr, diimmbuf, dicfg;
	
	ARMword gpio1_port, gpio1_dir, gpio1_latch;
	ARMword gpio1_enable;
	ARMword gpio1_owner;
	
	ARMword irq_active, irq_enabled;
	ARMword irq_active_ppc, irq_enabled_ppc;
	ARMword timer, alarm;
	
	ARMword mirror_flags;
	ARMword boot0_flags;
	ARMword ahb_flush_flags;
	ARMword reset_flags;
	ARMword clocks;
	
	int ppc_running;
	
	ARMword boot_code[16];
	
	ARMword ipc_ppcmsg, ipc_ppcctrl;
	ARMword ipc_armmsg, ipc_armctrl;
	
	struct {
		ARMword csr;
		ARMword mar;
		ARMword len;
		ARMword cr;
		ARMword data;
	} exi0, exi1, exi2;
	
	struct {
		int cs;
		int state;
		u8 cmd;
	} gecko;

	struct {
		int state;
		unsigned int clock, count;
		u32 bits_out;
		u32 bits_in;
		u32 address;
		u32 opcode;
		u16 *data;
		u32 wren;
	} seeprom;
	
	ARMword ahb_flushcmd;

	ARMword *otp;
	ARMword otp_index;
	u8 *srama;
	u8 *sramb;
	u8 *boot0;
	ARMword dummy;

	struct {
		ARMword dma_addr;
		ARMword blk_size, blk_cnt;
		ARMword argument;
		ARMword mode;
		ARMword command;
		ARMword response[4];
		ARMword present_state;
		ARMbyte host_control, pwr_control, bgap_control, wakeup_control;
		ARMword clock_control;
		ARMbyte timeout_control;
		ARMword irq_enable, irq_signal, irq_status;
		ARMword eirq_enable, eirq_signal, eirq_status;
		ARMword acmd12_err_status;
		ARMword capabilities, max_capabilities;
		ARMword force_event_eirq, force_event_acmd12;
	} sdhc_reg;
	
	struct {
		ARMword blah;
	} ohci[2];
	
	int internal;
};

FILE *sd = NULL;
uint64_t sd_card_size;
int sd_card_is_sdhc;
int sd_card_inserted;

#define BITMASK(x) (1<<(x))

#define CVR			BITMASK(0)
#define CVRINTMASK	BITMASK(1)
#define CVRINT		BITMASK(2)

#define BRK			BITMASK(0)
#define DEINTMASK	BITMASK(1)
#define DEINT		BITMASK(2)
#define TCINTMASK	BITMASK(3)
#define TCINT		BITMASK(4)
#define BRKINTMASK	BITMASK(5)
#define BRKINT		BITMASK(6)

static struct starlet_io_t starlet_io;
#define io starlet_io

extern ARMword mem_read_byte(ARMul_State*, ARMword);
extern ARMword mem_read_halfword(ARMul_State*, ARMword);
extern ARMword mem_read_word(ARMul_State*, ARMword);
extern void mem_write_byte(ARMul_State*, ARMword, ARMword);
extern void mem_write_halfword(ARMul_State*, ARMword, ARMword);
extern void mem_write_word(ARMul_State*, ARMword, ARMword);

void starlet_arm2host32 (ARMul_State *state, void *dst, ARMword src, ARMword len)
{
	uint8_t *xdst = (uint8_t *)dst;
	io.internal++;
	len /= 4;
	while(len--) {
		wbe32(xdst, mem_read_word(state, src));
		xdst += 4;
		src += 4;
	}
	io.internal--;
}

void starlet_arm2host16 (ARMul_State *state, void *dst, ARMword src, ARMword len)
{
	uint8_t *xdst = (uint8_t *)dst;
	io.internal++;
	len /= 2;
	while(len--) {
		wbe16(xdst, mem_read_halfword(state, src));
		xdst += 2;
		src += 2;
	}
	io.internal--;
}

void starlet_arm2host (ARMul_State *state, void *dst, ARMword src, ARMword len)
{
	uint8_t *xdst = (uint8_t *)dst;
	io.internal++;
	while(len--) {
		*xdst++ = mem_read_byte(state, src++);
	}
	io.internal--;
}

void starlet_host2arm32 (ARMul_State *state, ARMword dst, void *src, ARMword len)
{
	uint8_t *xsrc = (uint8_t *)src;
	io.internal++;
	len /= 4;
	while(len--) {
		mem_write_word(state, dst, be32(xsrc));
		xsrc += 4;
		dst += 4;
	}
	io.internal--;
}

void starlet_host2arm16 (ARMul_State *state, ARMword dst, void *src, ARMword len)
{
	uint8_t *xsrc = (uint8_t *)src;
	io.internal++;
	len /= 2;
	while(len--) {
		mem_write_halfword(state, dst, be16(xsrc));
		xsrc += 2;
		dst += 2;
	}
	io.internal--;
}

void starlet_host2arm (ARMul_State *state, ARMword dst, void *src, ARMword len)
{
	uint8_t *xsrc = (uint8_t *)src;
	io.internal++;
	while(len--) {
		mem_write_byte(state, dst++, *xsrc++);
	}
	io.internal--;
}


struct patchset_header {
	ARMword magic;
//	ARMword table_start;
	u8 *table_start;
	ARMword num_patches;
	ARMword patch_size;
	ARMword search_start;
	ARMword search_end;
	ARMword entrypoint;
	ARMword reserved;
} patch_header_boot1, patch_header_boot2_fs, patch_header_boot2_kernel;

u8 boot1_patches[] = {
/*	// disable _ahbMemFlush
	0xB5,0xF0,0x46,0x5F,0x46,0x56,0x46,0x4D,0x46,0x44,0xB4,0xF0,0x1c,0x04,0x28,0x0c,
	0x47,0x70,0x46,0x5F,0x46,0x56,0x46,0x4D,0x46,0x44,0xB4,0xF0,0x1c,0x04,0x28,0x0c,
	
	// ditto (for bc)
	0xB5,0x70,0x25,0x00,0x28,0x0C,0xD8,0x03,0x4A,0x20,0x00,0x83,0x58,0x9B,0x46,0x9F,
	0x47,0x70,0x25,0x00,0x28,0x0C,0xD8,0x03,0x4A,0x20,0x00,0x83,0x58,0x9B,0x46,0x9F,
	
	// make ahbMemFlush always ack (redundant?)
	0x04,0x1B,0x2B,0x00,0xD0,0x23,0x20,0x01,0xF0,0x00,0xFE,0xB2,0x4B,0x18,0x34,0x01,
	0x23,0x00,0x2B,0x00,0xD0,0x23,0x20,0x01,0xF0,0x00,0xFE,0xB2,0x4B,0x18,0x34,0x01,
*/
	// disable delay()
	0xBC,0x02,0x47,0x08,0xB5,0x00,0x23,0x00,0x38,0x04,0x1C,0x02,0x42,0x83,0xD2,0x02,
	0xBC,0x02,0x47,0x08,0x47,0x70,0x23,0x00,0x38,0x04,0x1C,0x02,0x42,0x83,0xD2,0x02,
};

u8 boot2_fs_patches[] = {
	// disable jj_virt_to_phys
	0x47,0x78,0x46,0xC0,0xEA,0xFF,0xFF,0xA9,0x47,0x78,0x46,0xC0,0xEA,0xFF,0xFF,0x1D,
	0x47,0x70,0x46,0xC0,0xEA,0xFF,0xFF,0xA9,0x47,0x78,0x46,0xC0,0xEA,0xFF,0xFF,0x1D,
	
};

u8 boot2_kernel_patches[] = {
	// disable _ahbMemFlush
	0xB5,0xF0,0x46,0x5F,0x46,0x56,0x46,0x4D,0x46,0x44,0xB4,0xF0,0x1c,0x04,0x28,0x0c,
	0x47,0x70,0x46,0x5F,0x46,0x56,0x46,0x4D,0x46,0x44,0xB4,0xF0,0x1c,0x04,0x28,0x0c,
	
	// defang call_ahbMemFlush
	0x00,0x0D,0x8B,0x40,0x00,0xB5,0x70,0x25,0x00,0x28,0x0C,0xD8,0x03,0x4A,0x20,0x00,
	0x00,0x0D,0x8B,0x40,0x00,0x47,0x70,0x25,0x00,0x28,0x0C,0xD8,0x03,0x4A,0x20,0x00,
	
	// disable j_cache_something_0
	0x02,0xFE,0xC3,0xF0,0x02,0xFE,0xC5,0xE0,0x01,0x24,0x16,0x42,0x64,0x1C,0x20,0xBC,
	0x02,0xFE,0xC3,0x00,0x00,0x00,0x00,0xE0,0x01,0x24,0x16,0x42,0x64,0x1C,0x20,0xBC,
	
	// disable delay
	0xB5,0x00,0x4B,0x16,0x68,0x1B,0x2B,0x00,0xD1,0x10,0x08,0x83,0x18,0x1B,0x09,0x82,
	0x47,0x70,0x4B,0x16,0x68,0x1B,0x2B,0x00,0xD1,0x10,0x08,0x83,0x18,0x1B,0x09,0x82,

};

void patchit_init(void) {
	patch_header_boot1.magic=0x12345678;
	patch_header_boot1.table_start = boot1_patches;
	patch_header_boot1.num_patches=1; //4;
	patch_header_boot1.patch_size = 16;
	patch_header_boot1.search_start = 0x0d400000;
	patch_header_boot1.search_end = 0x0d417000;
	
	patch_header_boot2_fs.magic=0x12345678;
	patch_header_boot2_fs.table_start = boot2_fs_patches;
	patch_header_boot2_fs.num_patches=1;
	patch_header_boot2_fs.patch_size = 16;
	patch_header_boot2_fs.search_start = 0x20000000;
	patch_header_boot2_fs.search_end = 0x2000ffff;

	patch_header_boot2_kernel.magic=0x12345678;
	patch_header_boot2_kernel.table_start = boot2_kernel_patches;
	patch_header_boot2_kernel.num_patches=4;
	patch_header_boot2_kernel.patch_size = 16;
	patch_header_boot2_kernel.search_start = 0xffff0000;
	patch_header_boot2_kernel.search_end = 0xffffffff;
	
}

void patchit(ARMul_State *state, struct patchset_header *h) {
	u32 patchnum, offset;
	u8 *buf = malloc(h->search_end - h->search_start);
	
	starlet_arm2host(state, buf, h->search_start, h->search_end - h->search_start);
	for (patchnum = 0; patchnum < h->num_patches; patchnum++) {
		printf("Checking patch %d\n", patchnum);
		u8 *patchptr = h->table_start + patchnum * 2 * h->patch_size;
		for (offset = 0; offset < (h->search_end - h->search_start - h->patch_size); offset++) {
			if (!memcmp(buf + offset, patchptr, h->patch_size)) {
				printf("Patching @%x\n", offset);
				memcpy(buf + offset, patchptr + h->patch_size, h->patch_size);
			}
		}
	}
	starlet_host2arm(state, h->search_start, buf, h->search_end - h->search_start);	
	free(buf);
}

static void starlet_io_reset(ARMul_State *state)
{
	int bytes_read;

	printf("STARLET: IO RESET\n");

	memset(&io, 0, sizeof(io));
	
	io.srama = malloc(0x10000);
	io.sramb = malloc(0x8000);
	io.boot0 = malloc(0x2000);
	if (!io.srama || !io.sramb || !io.boot0) {
		printf("malloc error in %s\n", __FUNCTION__);
		exit(-1);
	}
	
	FILE *f = fopen("boot0.bin","rb");
	if (!f) {
		perror("Error opening boot0.bin: ");
		exit(-1);
	}
	
	bytes_read = fread(io.boot0, 1, 0x2000, f);
	if (bytes_read != 0x2000) {
		printf("Short read on boot0: expected %d bytes, read %d bytes\n", 0x2000, bytes_read);
		exit(-1);
	}
	fclose(f);
	
	state->mmu.control |= CONTROL_VECTOR;
	ARMul_Abort(state, ARMul_ResetV);
	
	io.gpio1_latch = 0x20;
}

/* Interrupt Routine */
static void starlet_update_int(ARMul_State *state)
{
	//DEBUG("starlet_update_int()\n");
	//DEBUG("IRQs: Active = %08x, enabled=%08x\n", io.irq_active, io.irq_enabled);
	state->NfiqSig = HIGH;
	state->NirqSig = (io.irq_active & io.irq_enabled) ? LOW : HIGH;
//	state->NirqSig = LOW;
}

static void
starlet_set_intr (u32 interrupt)
{
	io.irq_active |= (1 << interrupt);
	io.irq_active_ppc |= (1 << interrupt);
//	SET_IRQ(interrupt);
}
static int
starlet_pending_intr (u32 interrupt)
{
	//DEBUG("starlet_pending_intr(%d)\n", interrupt);

	return io.irq_active & (1 << interrupt);
}

static int
starlet_pending_intr_ppc (u32 interrupt)
{
	//DEBUG("starlet_pending_intr(%d)\n", interrupt);

	return io.irq_active_ppc & (1 << interrupt);
}


static void
starlet_update_intr (void *mach)
{
//	DEBUG("starlet_update_intr(%p)\n", mach);

	struct machine_config *mc = (struct machine_config *) mach;
	ARMul_State *state = (ARMul_State *) mc->state;
	//DEBUG("IRQs: Active = %08x, enabled=%08x\n", io.irq_active, io.irq_enabled);
	state->NirqSig = (io.irq_active & io.irq_enabled) ? LOW : HIGH;
}

enum context
{
	C_UNKNOWN = -1,
	C_BOOT0 = 0,
	C_BOOT1,
	C_BOOT2L,
	C_IOSLDR,
	C_KERNEL,
	C_CRYPTO,
	C_FS,
	C_ES,
	C_DIP,
	C_STM,
	C_SDI,
	C_OH0,
	C_OH1,
	C_SO,
	C_KD,
	C_WD,
	C_WL,
	C_NCD,
	C_ETH,
	C_KBD,
	C_SSL,
	C_BOOTMII,
};

static const char *ctx_names[] = {
	"BOOT0",
	"BOOT1",
	"BOOT2L",
	"IOSLDR",
	"KERNEL",
	"CRYPTO",
	"FS",
	"ES",
	"DIP",
	"STM",
	"SDI",
	"OH0",
	"OH1",
	"SO",
	"KD",
	"WD",
	"WL",
	"NCD",
	"ETH",
	"KBD",
	"SSL",
	"BOOTMII",
};

static int get_context_id(ARMul_State *state) {
	ARMword pc = ARMul_GetPC(state);
	if((pc>>17) == 0x7ff8)
		pc = (pc&0x1ffff)|0x0d400000;

	switch(pc>>16) {
		case 0x13a7:
			return C_CRYPTO;
		case 0x2000:
			return C_FS;
		case 0x2010:
			return C_ES;
		case 0x2020:
			return C_DIP;
		case 0x2030:
			return C_STM;
		case 0x2040:
			return C_SDI;
		case 0x138a:
			return C_OH0;
		case 0x138b:
			return C_OH1;
		case 0x13b4:
		case 0x13b5:
		case 0x13b6:
			return C_SO;
		case 0x13db:
		case 0x13dc:
		case 0x13dd:
		case 0x13de:
		case 0x13df:
		case 0x13e0:
			return C_KD;
		case 0x13eb:
			return C_WD;
		case 0x13ed:
		case 0x13ee:
		case 0x13ef:
		case 0x13f0:
		case 0x13f1:
		case 0x13f2:
		case 0x13f3:
			return C_WL;
		case 0x13d9:
			return C_NCD;
		case 0x13aa:
			return C_ETH;
		case 0x1365:
			return C_KBD;
		case 0x13cc:
		case 0x13cd:
		case 0x13ce:
		case 0x13cf:
		case 0x13d0:
		case 0x13d1:
			return C_SSL;
		case 0xFFFF:
			if(io.boot0_flags & 0x1000 || (io.mirror_flags&0x20))
				return C_KERNEL;
			else
				return C_BOOT0;
		case 0x0002:
			return C_BOOT2L;
		case 0x1010:
			return C_IOSLDR;
		case 0x0d40:
			return C_BOOT1;
		case 0x1040:
			return C_BOOTMII;
		default:
			return C_UNKNOWN;
	}
}

static const char *get_context(ARMul_State *state) {
	static char buf[10];
	ARMword pc = ARMul_GetPC(state);
	int ctx;
	ctx = get_context_id(state);
	if(ctx==C_UNKNOWN) {
		sprintf(buf, "0x%04x", pc>>16);
	} else {
		strcpy(buf, "      ");
		memcpy(buf, ctx_names[ctx], strlen(ctx_names[ctx]));
	}
	return buf;
}

static void starlet_print_context(ARMul_State *state) {
	static ARMword lpc = 0, patch_step=0;
	static int ctx, lctx = C_UNKNOWN;
	/*if(patch_step==0) {
		patchit_init();
		//patchit(state, &patch_header_boot2_kernel);
		//patchit(state, &patch_header_boot2_fs);
		patch_step++;

	}*/
	//return;
	ARMword pc = ARMul_GetPC(state);
	if((pc>>17) == 0x7ff8)
		pc = (pc&0x1ffff)|0x0d400000;
	if((pc & 0xFFFF0000) != (lpc & 0xFFFF0000)) {
		ctx = get_context_id(state);
		if(ctx == C_UNKNOWN) {
			printf("[STARLET]: ========= NOW RUNNING IN   0x%04x CONTEXT (pc:%08x) =========\n", pc>>16, ARMul_GetPC(state));
		} else if(ctx != lctx) {
			printf("[STARLET]: ========= NOW RUNNING IN %8s CONTEXT (pc:%08x) =========\n", ctx_names[ctx], ARMul_GetPC(state));
		}
		lctx = ctx;
		lpc = pc;
	}
}

static ARMword starlet_di_read(ARMul_State *state, ARMword offset)
{
	ARMword data = 0;
	
	//printf("[STARLET] DIread offset:0x%x", offset);
	
	switch(offset&0xFF) {
		case 0x0:
			data = io.disr;
			break;
		case 0x4:
			data = io.dicvr;
			break;
		case 0x8:
			data = io.dicmdbuf[0];
			break;
		case 0xc:
			data = io.dicmdbuf[1];
			break;
		case 0x10:
			data = io.dicmdbuf[2];
			break;
		case 0x14:
			data = io.dimar;
			break;
		case 0x18:
			data = io.dilength;
			break;
 		case 0x20:
			data = io.dicr;
			break;
		case 0x22:
			data = io.diimmbuf;
			break;
		case 0x24:
			data = io.dicfg;
			break;
	
	}
	
	//printf(" = 0x%08x\n", data);
	return data;
}

void starlet_do_di_command(ARMul_State *state) {
	
	ARMword command;
	off_t offset;
	ARMword len;
	ARMword buf;
	int blah;
	
	unsigned char *data = NULL;
	
	static FILE *f = NULL;
	
	printf("[STARLET] DICOMMAND %08x %08x %08x -> %08x len %08x\n",
		   io.dicmdbuf[0], io.dicmdbuf[1], io.dicmdbuf[2],
	 io.dimar, io.dilength);

	command = io.dicmdbuf[0];
	
	if(!f) {
		f = fopen("disc.iso","rb");
		if(!f) {
			printf("[STARLET] Unable to open disc.iso\n");
			skyeye_exit(1);
		}
	}
			
	if(!data) {
		data = malloc(32768);
	}
	
	switch(command) {
	
		case 0xa8000040:
		case 0xa8000000:
			
			offset = io.dicmdbuf[1];
			offset <<=2;
			len = io.dicmdbuf[2];
			buf = io.dimar;
			
			printf("[STARLET] Disc or ID read [ 0x%09x 0x%08x ]\n", (unsigned int)offset, len);
			
			if(len > 32768) {
				printf("[STARLET] DI length too large\n");
				break;
			}
			
			fseek(f, offset, SEEK_SET);
			blah=fread(data, 1, len, f);
			if (blah != len) printf("short read: %d != %d\n", blah, len);
			
			starlet_host2arm(state, buf, data, len);
			
			//TODO interrupt here
			
			io.dicr &= ~1;
			io.disr |= TCINT;
			
			break;
	
		default:
			printf("[STARLET] Unknown DI command 0x%08x\n",command);
	}
	
}

static void starlet_di_write(ARMul_State *state, ARMword offset, ARMword data)
{
	//printf("[STARLET] DIwrite offset:0x%x, data:0x%x\n", offset, data);

	switch(offset&0xFF) {
		case 0x0:
			io.disr &= ~(DEINTMASK | TCINTMASK | BRKINTMASK);
			io.disr |= data & (DEINTMASK | TCINTMASK | BRKINTMASK);
			if(data & BRK) {
				printf("[STARLET] DI Break requested\n");
				io.disr &= ~BRK; //it's complete before it even started
			}
			// clear interrupt flags if requested
			if(data & DEINT)
				io.disr &= ~DEINT;
			if(data & TCINT)
				io.disr &= ~TCINT;
			if(data & BRKINT)
				io.disr &= ~BRKINT;
			break;
		case 0x4:
			// keep CVRINT bit but clear if requested
			io.dicvr = io.dicvr & CVRINT;
			if(data & CVRINT) io.dicvr = 0;
			// update CVRINTMASK
			io.dicvr |= data & CVRINTMASK;
			// pretend cover is always closed (CVR=0)
			break;
		case 0x8:
			io.dicmdbuf[0] = data;
			break;
		case 0xc:
			io.dicmdbuf[1] = data;
			break;
		case 0x10:
			io.dicmdbuf[2] = data;
			break;
		case 0x14:
			io.dimar = data;
			break;
		case 0x18:
			io.dilength = data;
			break;
		case 0x1c:
			io.dicr = data;
			if(io.dicr & 1) {
				starlet_do_di_command(state);
			}
			break;
		case 0x20:
			io.diimmbuf = data;
			break;
		case 0x24:
			io.dicfg = data;
			break;
	}	
}

static ARMword starlet_otp_getdata(ARMul_State *state) {
	ARMword retval = be32((u8*)&io.otp[io.otp_index]);
//	DEBUG("OTP: read OTP[%d]=%08x\n", io.otp_index, retval);
	return retval;
}

static void starlet_otp_setaddr(ARMul_State *state, unsigned int index) {
	if (!io.otp) {
		FILE *f;
		io.otp = malloc(128);
		DEBUG("OTP: Initializing...\n");
		f = fopen("otp.bin","rb");
		if(f) {
			if(fread(io.otp, 1, 128, f) != 128) {
				DEBUG("OTP: File open failed, initializing with 0x0000...\n");
				memset(io.otp, 0, 128);
			} else {
				io.otp_index=9;
				DEBUG("OTP: Successfully read data from file for NG ID %08x\n", starlet_otp_getdata(state));
			}
			fclose(f);
		} else {
			DEBUG("OTP: File open seeprom.bin failed, initializing with 0x0000...\n");
			memset(io.otp, 0, 128);
		}
	}
	index &= 0xFF;
	if (index > (128/4)) {
		DEBUG("%s: invalid index 0x%08x\n", __FUNCTION__, index);
	}
	io.otp_index = index;
}

static void gpio_seeprom(ARMul_State *state, ARMword data) {
	if(!io.seeprom.data) {
		FILE *f;
		io.seeprom.data = malloc(0x100);
		DEBUG("SEEPROM: Initializing...\n");
		f = fopen("seeprom.bin","rb");
		if(f) {
			if(fread(io.seeprom.data, 1, 0x100, f) != 0x100) {
				DEBUG("SEEPROM: File read failed, initializing with 0x0000...\n");
				memset(io.seeprom.data, 0, 0x100);
			} else {
				DEBUG("SEEPROM: Successfully read data from file\n");
			}
		} else {
			DEBUG("SEEPROM: File open seeprom.bin failed, initializing with 0x0000...\n");
			memset(io.seeprom.data, 0, 0x100);
		}
		fclose(f);
		io.seeprom.clock = 0;
		io.seeprom.state = 0;
		io.seeprom.bits_out = 0;
		io.seeprom.bits_in = 0;
		io.seeprom.count = 1;
		io.seeprom.address = 0;
	}
	if(!(data & 0x00000400)) { //CS down
		io.seeprom.clock = 0;
		io.seeprom.state = 0;
		io.seeprom.bits_out = 0;
		io.seeprom.bits_in = 0;
		io.seeprom.count = 1;
		io.seeprom.address = 0;
	} else { //CS up
		if(io.gpio1_dir & 0x00002000) {
			DEBUG("SEEPROM: Short Circuit! Wii went up in smoke. %08x %08x\n",data, io.gpio1_dir);
			skyeye_exit(1);
		}
		if((data & 0x00000800) && !io.seeprom.clock) { //clock rise
			io.seeprom.count--;
			io.seeprom.bits_in = (io.seeprom.bits_in << 1) | ((data & 0x00001000)?1:0);
			if(io.seeprom.bits_out & (1<<io.seeprom.count))
				io.gpio1_port |= 0x00002000;
			else
				io.gpio1_port &= ~0x00002000;
			if(io.seeprom.count == 0) {
				switch(io.seeprom.state) {
					case -1: //dead / loop
						io.seeprom.count = 1;
						break;
					case 0: //start-bit
						if(io.seeprom.bits_in != 1) {
							DEBUG("SEEPROM: Busy check\n");
							io.seeprom.count = 1;
							io.seeprom.bits_out = 1;
							io.seeprom.state = -1;
						} else {
							io.seeprom.count = 2;
							io.seeprom.state = 1;
						}
						break;
					case 1: //opcode
						io.seeprom.opcode = io.seeprom.bits_in;
						io.seeprom.count = 8;
						io.seeprom.state = 2;
						break;
					case 2: //address
						io.seeprom.address = io.seeprom.bits_in;
						switch(io.seeprom.opcode) {
							case 0: //special
								switch(io.seeprom.address >> 6) {
									case 0: //wr disable
										DEBUG("SEEPROM: Write Disable\n");
										io.seeprom.wren = 0;
										io.seeprom.count = 1;
										io.seeprom.state = -1;
										break;
									case 1: //wr all
										if(!io.seeprom.wren) {
											printf("SEEPROM: ERROR: Tried to Write All while writes are disabled!\n");
											io.seeprom.count = 1;
											io.seeprom.state = -1;
										} else {
											io.seeprom.count = 16;
											io.seeprom.state = 3;
										}
										break;
									case 2: //erase all
										if(!io.seeprom.wren) {
											printf("SEEPROM: ERROR: Tried to Erase All while writes are disabled!\n");
										} else {
											DEBUG("SEEPROM: Erase All\n");
											memset(io.seeprom.data,0,0x100);
										}
										io.seeprom.count = 1;
										io.seeprom.state = -1;
										break;
									case 3: //wr enable
										DEBUG("SEEPROM: Write Enable\n");
										io.seeprom.wren = 1;
										io.seeprom.count = 1;
										io.seeprom.state = -1;
										break;
								}
								break;
							case 1:
								if(!io.seeprom.wren) {
									printf("SEEPROM: ERROR: Tried to Write 0x%02x while writes are disabled!\n",io.seeprom.address);
									io.seeprom.count = 1;
									io.seeprom.state = -1;
								} else {
									io.seeprom.count = 16;
									io.seeprom.state = 3;
								}
								break;
							case 2:
								DEBUG("SEEPROM: Read 0x%02x, data=0x%04x\n", io.seeprom.address, be16((u8*)&io.seeprom.data[io.seeprom.address&0x7f]));
								io.seeprom.count = 16;
								io.seeprom.bits_out = be16((u8*)&io.seeprom.data[io.seeprom.address&0x7f]);
								io.seeprom.state = 3;
								break;
							case 3:
								if(!io.seeprom.wren) {
									printf("SEEPROM: ERROR: Tried to Erase 0x%02x while writes are disabled!\n", io.seeprom.address);
								} else {
									DEBUG("SEEPROM: Erase x%02x\n", io.seeprom.address);
									io.seeprom.data[io.seeprom.address&0x7f] = 0x0000;
								}
								io.seeprom.count = 1;
								io.seeprom.state = -1;
								break;
						}
						break;
					case 3:
						switch(io.seeprom.opcode) {
							case 0: // has to be wr all
								printf("SEEPROM: Write All, data=0x%04x\n", io.seeprom.bits_in);
								int i;
								for(i=0;i<0x80;i++)
									io.seeprom.data[i] = io.seeprom.bits_in;
								break;
							case 1: // write
								printf("SEEPROM: Write 0x%02x, data=0x%04x\n", io.seeprom.address, io.seeprom.bits_in);
								wbe16((u8*)&io.seeprom.data[io.seeprom.address&0x7f],io.seeprom.bits_in);
								break;
						}
						io.seeprom.count = 1;
						io.seeprom.state = -1;
				}
				io.seeprom.bits_in = 0;
			}
		}
		io.seeprom.clock = data & 0x00000800;
	}
}

static void starlet_gpio1_latch_write(ARMul_State *state, ARMword data) {
	ARMword changes = io.gpio1_latch ^ data;
	if(changes & 0x00FF0000) {
// uncomment if LCD debug spew is enabled
//		if((ARMul_GetPC(state) & 0xFFFF0000) != 0x10400000)
			DEBUG("DEBUG[%02hhx]\n", (data >> 16) & 0xFF);
	}
	if(changes & 0x00000020) {
		DEBUG("Slot LED: %d\n", (data&0x20)?1:0);
	}
	if(changes & 0x00000004) {
		DEBUG("Fan: %d\n", (data&0x4)?1:0);
	}
	if(changes & 0x00000008) {
		DEBUG("DC-DC: %d\n", (data&0x8)?1:0);
	}
	if(changes & 0x00000002) {
		DEBUG("Shutdown: %d\n", (data&0x2)?1:0);
	}
	if(changes & 0x00000010) {
		DEBUG("DI Spin: %d\n", (data&0x10)?1:0);
	}
	if(changes & 0x00000200) {
		DEBUG("Eject force: %d\n", (data&0x200)?1:0);
	}
	if(changes & 0x00000100) {
		DEBUG("Sensor bar: %d\n", (data&0x100)?1:0);
	}
	if(changes & 0x00001c00) {
		gpio_seeprom(state, data);
	}
	if(changes & 0x0000E0C1) {
		DEBUG("@ %08x: Unknown GPIO PORT write %08x -> %08x\n", ARMul_GetPC(state), io.gpio1_latch, data);
	}
	io.gpio1_latch = data;
}

static void starlet_gpio1_port_write(ARMul_State *state, ARMword data) {
	DEBUG("Tried to write GPIO1 PORT register! value=%08x\n",data);
}

static void starlet_gpio1_dir_write(ARMul_State *state, ARMword data) {
	if(io.gpio1_dir != data)
		DEBUG("GPIO1 direction update: %08x -> %08x\n",io.gpio1_dir,data);
	io.gpio1_dir = data;
}

static void starlet_gpio1_enable_write(ARMul_State *state, ARMword data) {
	if(io.gpio1_enable != data)
		DEBUG("GPIO1 ENABLE update: %08x -> %08x\n",io.gpio1_enable,data);
	io.gpio1_enable = data;
}

static void starlet_gpio1_owner_write(ARMul_State *state, ARMword data) {
	if(io.gpio1_owner != data)
		DEBUG("GPIO1 owner update: %08x -> %08x\n",io.gpio1_owner,data);
	io.gpio1_owner = data;
}

static void starlet_ipc_write(ARMul_State *state, ARMword addr, ARMword data)
{
	switch(addr) {
		case 0:
			//DEBUG("PPC IPC MSG write: %08x\n", data);
			io.ipc_ppcmsg = data;
			break;
		case 4:
			//DEBUG("PPC IPC CTRL write: %08x\n", data);
			io.ipc_ppcctrl &= 0x06;
			io.ipc_armctrl &= 0x39;
			io.ipc_ppcctrl |= data & 0x39;
			io.ipc_ppcctrl &= ~(data & 0x06);
			if(data & 0x01)
				io.ipc_armctrl |= 0x04;
			if(data & 0x08)
				io.ipc_armctrl |= 0x02;
			if(data & 0x02)
				io.ipc_armctrl &= ~0x08;
			if(data & 0x04)
				io.ipc_armctrl &= ~0x01;
			break;
		case 8:
			//DEBUG("ARM IPC MSG write: %08x\n", data);
			io.ipc_armmsg = data;
			break;
		case 12:
			//DEBUG("ARM IPC CTRL write: %08x\n", data);
			io.ipc_armctrl &= 0x06;
			io.ipc_ppcctrl &= 0x39;
			io.ipc_armctrl |= data & 0x39;
			io.ipc_armctrl &= ~(data & 0x06);
			if(data & 0x01)
				io.ipc_ppcctrl |= 0x04;
			if(data & 0x08)
				io.ipc_ppcctrl |= 0x02;
			if(data & 0x02)
				io.ipc_ppcctrl &= ~0x08;
			if(data & 0x04)
				io.ipc_ppcctrl &= ~0x01;
			break;
	}
}

static void ipc_update_int(ARMul_State *state)
{
	if(((io.ipc_ppcctrl & 0x14) == 0x14) || ((io.ipc_ppcctrl & 0x22) == 0x22)) {
		if(!starlet_pending_intr_ppc(30))
			//printf("IPC: PPC IRQ is set\n");
		starlet_set_intr(30);
		starlet_update_int(state);
	}
	if(((io.ipc_armctrl & 0x14) == 0x14) || ((io.ipc_armctrl & 0x22) == 0x22)) {
		if(!starlet_pending_intr(31))
			//printf("IPC: ARM IRQ is set\n");
		starlet_set_intr(31);
		starlet_update_int(state);
	}
}

static void gecko_select(int selected)
{
	selected = !!selected;
	if(selected != io.gecko.cs) {
		io.gecko.state = 0;
		io.gecko.cs = selected;
	}
}

static void gecko_sendbyte(u8 byte)
{
	printf("%c", byte);
}

static u8 gecko_exchange(u8 byte)
{
	u8 b;
	if(!io.gecko.cs)
		return 0;
	
	switch(io.gecko.state) {
		case -1:
			return 0;
		case 0:
			io.gecko.cmd = byte;
			switch(byte>>4) {
				default:
					io.gecko.state = -1;
					return 0;
				case 0x9:
					io.gecko.state = 1;
					return 0x04;
				case 0xC:
					io.gecko.state = -1;
					return 0x04;
				case 0xB:
					io.gecko.state = 2;
					return 0x04;
			}
		case 1:
			io.gecko.state = -1;
			return 0x70;
		case 2:
			b = (io.gecko.cmd << 4) | (byte >> 4);
			gecko_sendbyte(b);
			io.gecko.state = -1;
			return 0;
	}
	return 0;
}

static ARMword starlet_exi_read(ARMul_State *state, ARMword addr)
{
	ARMword retval = 0;
	switch(addr & 0xff) {
		case 0x00: return io.exi0.csr;
		case 0x04: return io.exi0.mar;
		case 0x08: return io.exi0.len;
		case 0x0c: return io.exi0.cr;
		case 0x10: return io.exi0.data;
		case 0x14: return io.exi1.csr;
		case 0x18: return io.exi1.mar;
		case 0x1c: return io.exi1.len;
		case 0x20: return io.exi1.cr;
		case 0x24: return io.exi1.data;
		case 0x28: return io.exi2.csr;
		case 0x2c: return io.exi2.mar;
		case 0x30: return io.exi2.len;
		case 0x34: return io.exi2.cr;
		case 0x38: return io.exi2.data;
		
		case 0x40:
		case 0x44:
		case 0x48:
		case 0x4c:
		case 0x50:
		case 0x54:
		case 0x58:
		case 0x5c:
		case 0x60:
		case 0x64:
		case 0x68:
		case 0x6c:
		case 0x70:
		case 0x74:
		case 0x78:
		case 0x7c:
			return io.boot_code[(addr&0x3f)>>2];
		default:
		DEBUG("UNIMPLEMENTED: %s(addr:0x%08x), returning %08x\n", __FUNCTION__, addr, retval);
	}
	return 0;
}

static void starlet_exi_write(ARMul_State *state, ARMword addr, ARMword data)
{
	switch(addr & 0xff) {
		case 0x00: io.exi0.csr = data; return;
		case 0x04: io.exi0.mar = data; return;
		case 0x08: io.exi0.len = data; return;
		case 0x0c: io.exi0.cr = data; return;
		case 0x10: io.exi0.data = data; return;
		case 0x14:
			gecko_select(data & 0x80);
			io.exi1.csr = data;
			return;
		case 0x18: io.exi1.mar = data; return;
		case 0x1c: io.exi1.len = data; return;
		case 0x20:
			if(data & 0x01) {
				int len = ((data >> 4) & 3) + 1;
				int i;
				for(i=0; i<4; i++) {
					u8 b = 0;
					if(i < len)
						b = gecko_exchange(io.exi1.data >> 24);
					io.exi1.data <<= 8;
					io.exi1.data |= b;
				}
				data &= ~1;
			}
			io.exi1.cr = data;
			return;
		case 0x24: io.exi1.data = data; return;
		case 0x28: io.exi2.csr = data; return;
		case 0x2c: io.exi2.mar = data; return;
		case 0x30: io.exi2.len = data; return;
		case 0x34: io.exi2.cr = data; return;
		case 0x38: io.exi2.data = data; return;

		case 0x40:
		case 0x44:
		case 0x48:
		case 0x4c:
		case 0x50:
		case 0x54:
		case 0x58:
		case 0x5c:
		case 0x60:
		case 0x64:
		case 0x68:
		case 0x6c:
		case 0x70:
		case 0x74:
		case 0x78:
		case 0x7c:
			io.boot_code[(addr&0x3f)>>2] = data;
			if((addr & 0x3f) == 0x3c) {
				DEBUG("PowerPC Boot Code:\n");
				DEBUG(" %08x %08x %08x %08x %08x %08x %08x %08x\n", io.boot_code[0], io.boot_code[1], io.boot_code[2], io.boot_code[3], io.boot_code[4], io.boot_code[5], io.boot_code[6], io.boot_code[7]);
				DEBUG(" %08x %08x %08x %08x %08x %08x %08x %08x\n", io.boot_code[8], io.boot_code[9], io.boot_code[10], io.boot_code[11], io.boot_code[12], io.boot_code[13], io.boot_code[14], io.boot_code[15]);
			}
			break;
		default:
		DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%08x)\n", __FUNCTION__, addr, data);
	}
}

u8 hollywood_port_state[0x10000];

static ARMword starlet_hollywood_read(ARMul_State *state, ARMword addr)
{
	ARMword retval=0;
	retval = be32(hollywood_port_state+(addr & 0xFFFF));

	switch(addr & 0xFFFF) {
		case 0x0000: return io.ipc_ppcmsg;
		case 0x0004: return io.ipc_ppcctrl;
		case 0x0008: return io.ipc_armmsg;
		case 0x000C: return io.ipc_armctrl;
		case 0x0010: return io.timer;
		case 0x0014: return io.alarm;
		case 0x0030: return io.irq_active_ppc;
		case 0x0034: return io.irq_enabled_ppc;
		case 0x0038: return io.irq_active;
		case 0x003C: return io.irq_enabled;
		case 0x0060: return io.mirror_flags;
		case 0x0070: break;
		case 0x00C0: return io.gpio1_latch & io.gpio1_owner;
		case 0x00C4: return io.gpio1_dir & io.gpio1_owner;
		case 0x00C8: return io.gpio1_port & io.gpio1_owner;
		case 0x00DC: return io.gpio1_enable;
		case 0x00E0: return io.gpio1_latch;
		case 0x00E4: return io.gpio1_dir;
		case 0x00E8: return io.gpio1_port;
		case 0x00F0: return 0; // gpio irq
		case 0x00FC: return io.gpio1_owner;
		case 0x0130: return 0x400;
		case 0x0138: return 0x400;
		case 0x0180: break;
		case 0x0188: return io.ahb_flush_flags;
		case 0x018c: return io.boot0_flags;
		case 0x0190: return io.clocks;
		case 0x0194: return io.reset_flags;
		case 0x01b0: return 0x4011c0;
		case 0x01b4: return 0x18000018;
		case 0x01d8: break;		
		case 0x01f0: return starlet_otp_getdata(state);
//		case 0x0214: return 0xf0;
		case 0x0214: break;
		case 0x100: break;
		case 0x104: break;
		case 0x108: break;
		case 0x10c: break;
		case 0x110: break;
		case 0x114: break;
		case 0x118: break;
		case 0x11c: break;
		case 0x120: break;
		case 0x124: break;
		case 0x134: break;
		default:
		DEBUG("UNIMPLEMENTED: %s(addr:0x%08x), returning %08x\n", __FUNCTION__, addr, retval);
		break;
	}
	return retval;
}

static void starlet_hollywood_write(ARMul_State *state, ARMword addr, ARMword data)
{
	u32 old = be32(hollywood_port_state+(addr & 0xFFFF));
	switch(addr & 0xFFFF) {
		case 0x000:
		case 0x004:
		case 0x008:
		case 0x00C:
			starlet_ipc_write(state, addr&0xFFFF, data);
		break;
		case 0x010: io.timer=data; break;
		case 0x014: /*DEBUG("Set alarm=%x\n", data);*/ io.alarm=data; break;
		case 0x30: 
			io.irq_active_ppc &= ~data;
		break;
		case 0x34: 
			io.irq_enabled_ppc=data;
		break;
		case 0x38: 
			io.irq_active &= ~data;
			//DEBUG("@%08x: Set irq_active=%08x\n", ARMul_GetPC(state), io.irq_active); 
			starlet_update_int(state);
		break;
		case 0x3C: 
			io.irq_enabled=data;
			//DEBUG("@%08x: Set irq_enabled=%08x (pending=%08x)\n", ARMul_GetPC(state), io.irq_enabled, io.irq_active); 
			starlet_update_int(state);
		break;
		case 0x60:
			io.mirror_flags = data;
			DEBUG("Set memory mirror flags = %08x\n", io.mirror_flags);
			break;
		case 0x70:
		DEBUG("Set Legacy DI=%d, EXI Boot Buf Enable=%d\n", (data & 0x10)>>4, data & 1);
		break;
		case 0x88:  DEBUG("%x: %08x -> %08x\n", addr & 0xFFFF, old, data); break;
		
		case 0x0c0: starlet_gpio1_latch_write(state,(data & io.gpio1_owner) | (io.gpio1_latch & (io.gpio1_owner ^ 0xFFFFFF)));  return;
		case 0x0c4: starlet_gpio1_dir_write(state,(data & io.gpio1_owner) | (io.gpio1_dir & (io.gpio1_owner ^ 0xFFFFFF))); return;
		case 0x0c8: starlet_gpio1_port_write(state,(data & io.gpio1_owner) | (io.gpio1_port & (io.gpio1_owner ^ 0xFFFFFF))); return;
		case 0x0dc: starlet_gpio1_enable_write(state, data); return;
		case 0x0e0: starlet_gpio1_latch_write(state,(data & (io.gpio1_owner ^ 0xFFFFFF)) | (io.gpio1_latch & io.gpio1_owner));  return;
		case 0x0e4: starlet_gpio1_dir_write(state,(data & (io.gpio1_owner ^ 0xFFFFFF)) | (io.gpio1_dir & io.gpio1_owner)); return;
		case 0x0e8: starlet_gpio1_port_write(state,(data & (io.gpio1_owner ^ 0xFFFFFF)) | (io.gpio1_port & io.gpio1_owner)); return;
		case 0x0fc: starlet_gpio1_owner_write(state, data); return;
		case 0x180: break;
		case 0x188:
			io.ahb_flush_flags = data;
			io.boot0_flags &= 0xfffffff0;
			if(!(data & 0x10000))
				io.boot0_flags |= 9;
				
				
			break;
		case 0x18c: io.boot0_flags = data; break;
		case 0x190:
			DEBUG("CLOCKS: %08x -> %08x\n", io.clocks, data);
			io.clocks = data;
			break;
		case 0x194:
			DEBUG("RESETS: %08x\n",data);
			if((io.reset_flags ^ data)&0x30) {
				DEBUG("PowerPC Boot Flags: %d %d\n", !!(data&0x20), !!(data&0x10));
				if((data&0x20) && (data&0x10))
					io.ppc_running = 1;
				else
					io.ppc_running = 0;
			}
			io.reset_flags = data;
			break;
		case 0x1d8:
			DEBUG("%x: %08x -> %08x\n", addr & 0xFFFF, old, data); break;
		case 0x1ec: starlet_otp_setaddr(state, data); return;
		case 0x130: break;
		
		case 0x100: break;
		case 0x104: break;
		case 0x108: break;
		case 0x10c: break;
		case 0x110: break;
		case 0x114: break;
		case 0x118: break;
		case 0x11c: break;
		case 0x120: break;
		case 0x124: break;
		case 0x134: break;
		default:
		DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%08x)\n", __FUNCTION__, addr, data);
		break;
	}
	wbe32(hollywood_port_state+(addr & 0xFFFF), data);
	
	
}

ARMword dram_4074=0, dram_4076=0;

static ARMword starlet_dram_read(ARMul_State *state, ARMword addr)
{
	ARMword retval=0;
//	retval = be32(hollywood_port_state+(addr & 0xFFFF));

	switch(addr & 0xFFFF) {
		case 0x4074: return dram_4074;
		case 0x4076: return dram_4076;
		default:
//		DEBUG("UNIMPLEMENTED: %s(addr:0x%08x), returning %08x\n", __FUNCTION__, addr, retval);
		break;
	}
	return retval;
}

static void starlet_dram_write(ARMul_State *state, ARMword addr, ARMword data)
{
	switch(addr & 0xFFFF) {
		case 0x4074: dram_4074 = data; return;
		case 0x4076: dram_4076 = data; return;
		default:
//		DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%08x)\n", __FUNCTION__, addr, data);
		break;
	}
//	wbe32(hollywood_port_state+(addr & 0xFFFF), data);
	
	
}

unsigned char AES_key[16];
unsigned char AES_iv[16];
u8 temp_iv[16];

ARMword AES_src_ptr, AES_dest_ptr;
u8 AES_src_buf[0x10000], AES_dest_buf[0x10000];

char * keytostr(unsigned char *key) {
	static char keybuf[33];
	int i;
	for(i=0;i<16;i++) sprintf(keybuf+i*2, "%02hhx", key[i]);
	keybuf[32]='\0';
	return keybuf;
}

static void starlet_aes_command(ARMul_State *state, ARMword command) {
	AES_KEY aeskey;
	
	if (command & 0x80000000) {
		int iv_reset_flag = (command & 0x1000)?0:1;
		int num_bytes = ((command & 0xFFF) +1) * 0x10;
		//DEBUG("AES: key=%s %08x\n", keytostr(AES_key), command);
		//DEBUG("AES: iv=%s\n", keytostr(AES_iv));
		//DEBUG("AES: src_ptr=%08x, dst_ptr=%08x, iv_reset=%d, num_bytes=%d\n", AES_src_ptr, AES_dest_ptr, iv_reset_flag, num_bytes);

		starlet_arm2host(state, AES_src_buf, AES_src_ptr, num_bytes);

		if (command & 0x10000000) {
			if (command & 0x08000000) {
				AES_set_decrypt_key(AES_key, 128, &aeskey);
				AES_cbc_encrypt(AES_src_buf, AES_dest_buf, num_bytes,
				                &aeskey, iv_reset_flag?AES_iv:temp_iv, AES_DECRYPT);
			} else {
				AES_set_encrypt_key(AES_key, 128, &aeskey);
				AES_cbc_encrypt(AES_src_buf, AES_dest_buf, num_bytes,
				                &aeskey, iv_reset_flag?AES_iv:temp_iv, AES_ENCRYPT);
			}
		} else {
			memcpy(AES_dest_buf, AES_src_buf, num_bytes);
		}
		
		starlet_host2arm(state, AES_dest_ptr, AES_dest_buf, num_bytes);
		memcpy(temp_iv, AES_src_buf + num_bytes - 0x10, 0x10);
		AES_src_ptr += num_bytes;
		AES_dest_ptr += num_bytes;
		if (command & 0x40000000) {
			starlet_set_intr(2);
			starlet_update_int(state);
		}
		return;
	}
}

static ARMword starlet_aes_read(ARMul_State *state, ARMword addr)
{
	switch(addr & 0xFFFF) {
		case 0x0000: 
//			DEBUG("%x: starlet_aes_read(status)=0\n",ARMul_GetPC(state)); 
			return 0;
		default:
		break;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x)\n", __FUNCTION__, addr);
	return 0;
}


static void starlet_aes_write(ARMul_State *state, ARMword addr, ARMword data)
{
	//DEBUG("%x: %s(addr:0x%08x, data=%08x)\n", 
	//	ARMul_GetPC(state), __FUNCTION__, addr, data);	
	switch(addr & 0xFFFF) {
		case 0x0000: 
		return starlet_aes_command(state, data);
		case 0x0004: //DEBUG("AES: source addr = %08x)\n", data); 
			AES_src_ptr = data;
			return;
		case 0x0008: 
			AES_dest_ptr = data;
			//DEBUG("AES: dest addr = %08x\n", data);
			return;
		case 0x000C:
			//DEBUG("AES: key = %08x\n", data);
			memmove(AES_key,AES_key+4,12);
			AES_key[12] = data >> 24;
			AES_key[13] = (data >> 16) & 0xff;
			AES_key[14] = (data >> 8) & 0xff;
			AES_key[15] = data & 0xff;
		
			return;
		case 0x0010:
			//DEBUG("AES: iv = %08x\n", data);
			memmove(AES_iv,AES_iv+4,12);
			AES_iv[12] = data >> 24;
			AES_iv[13] = (data >> 16) & 0xff;
			AES_iv[14] = (data >> 8) & 0xff;
			AES_iv[15] = data & 0xff;
			return;
		
		default:
			break;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data=%08x)\n", __FUNCTION__, addr, data);
}

SHA1Context SHA_ctx;
ARMword SHA_src_ptr;
ARMword SHA_len;
u8 SHA_buf[0x10000];
u8 SHA_result[20];

static ARMword starlet_sha_read(ARMul_State *state, ARMword addr)
{
	ARMword retval;
	u8 *hash;
	switch(addr & 0xFFFF) {
	case 0x0000: 
//		DEBUG("%x: starlet_sha_read(status)=0\n",ARMul_GetPC(state)); 
		return 0;
	case 0x0008: //DEBUG("SHA: Finalizing\n");
	case 0x000C:
	case 0x0010: 
	case 0x0014: 
	case 0x0018: 
		// don't look at this...
		hash = SHA_result;
		u32 i;
		for(i = 0; i < 5; i++)
		{
			*hash++ = SHA_ctx.Message_Digest[i] >> 24 & 0xff;
			*hash++ = SHA_ctx.Message_Digest[i] >> 16 & 0xff;
			*hash++ = SHA_ctx.Message_Digest[i] >> 8 & 0xff;
			*hash++ = SHA_ctx.Message_Digest[i] & 0xff;
		}
		retval= SHA_result[(addr & 0xFFFF)-8] << 24 |
			   SHA_result[(addr & 0xFFFF)-7] << 16 |
			   SHA_result[(addr & 0xFFFF)-6] << 8 |
			   SHA_result[(addr & 0xFFFF)-5];
//		DEBUG("%x: returning %08x\n", addr, retval);
		return retval;
		default:
		break;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x)\n", __FUNCTION__, addr);
	return 0;
}

static void starlet_sha_write(ARMul_State *state, ARMword addr, ARMword data)
{
//	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data=%08x)\n", __FUNCTION__, addr, data);

	switch(addr & 0xFFFF) {
		case 0x0000:
			if (data & 0x80000000) {
				int num_bytes = ((data & 0xFFF) +1) * 0x40;
				starlet_arm2host(state, SHA_buf, SHA_src_ptr, num_bytes);
				//DEBUG("SHA: Update(%x, %x)\n", SHA_src_ptr, num_bytes);
				SHA1Input(&SHA_ctx,SHA_buf,num_bytes);
				SHA_src_ptr +=  num_bytes;
				if (data & 0x40000000) {
					starlet_set_intr(3);
					starlet_update_int(state);
				}
			}
			return;
		case 0x0004: 
			//DEBUG("SHA: address=%08x\n", data);
			SHA_src_ptr = data;
			return;
		/* We ignore these next writes because SHA1_Init takes care of setting them */
		case 0x0008:
			//if (data != 0x67452301)
			//	DEBUG("starlet_sha_write(h1,%08x)\n", data); 
			SHA_ctx.Message_Digest[0] = data;
			return;
		case 0x000C: 
			//if (data != 0xEFCDAB89)
			//	DEBUG("starlet_sha_write(h2,%08x)\n", data);
			SHA_ctx.Message_Digest[1] = data;
			return;
		case 0x0010: 
			//if (data != 0x98BADCFE)
			//	DEBUG("starlet_sha_write(h3,%08x)\n", data);
			SHA_ctx.Message_Digest[2] = data;
			return;
		case 0x0014: 
			//if (data != 0x10325476)
			//	DEBUG("starlet_sha_write(h4,%08x)\n", data);
			SHA_ctx.Message_Digest[3] = data;
			return;
		case 0x0018: 
			//if (data != 0xC3D2E1F0)
			//	DEBUG("starlet_sha_write(h5,%08x)\n", data);
			SHA_ctx.Message_Digest[4] = data;
			return;
		default:
		break;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data=%08x)\n", __FUNCTION__, addr, data);
}

static void starlet_ahb_write_halfword(ARMul_State *state, ARMword addr, ARMword data)
{
	switch(addr & 0xffff) {
		case 0x4228: io.ahb_flushcmd = data; break;
	}
}

static ARMword starlet_ahb_read_halfword(ARMul_State *state, ARMword addr)
{
	switch(addr & 0xffff) {
		case 0x4228: return io.ahb_flushcmd; break;
		case 0x422a: return io.ahb_flushcmd; break;
	}
	return 0;
}


static u8 *starlet_sram_decode(ARMul_State *state, ARMword addr)
{
	static int first_fail = 1;
	if((io.boot0_flags & 0x1000) == 0) {
		if(io.mirror_flags & 0x20) {
			if(addr<0xffff0000) {
				DEBUG("Illegal memory situation: boot0 enabled and mirror enabled and access it not to 0xffff0000 area, addr=%08x, PC=%08x\n", addr, ARMul_GetPC(state));
				skyeye_exit(1);
			}
		} else if(addr >= 0xffff0000) {
			return &io.boot0[addr&0x1fff];
		}
	}
	addr &= 0x1ffff;
	if(io.mirror_flags & 0x20 && !io.internal)
		addr ^= 0x10000;
	if(addr & 0x10000) {
		if(addr & 0x8000) {
			if (first_fail) {
				DEBUG("BAD SRAM ACCESS: %08x @ %08x \n", addr, ARMul_GetPC(state));
				first_fail = 0;
			}
			io.dummy = 0;
			return (u8*)&io.dummy;
		}
		return &io.sramb[addr&0x7fff];
	}
	return &io.srama[addr&0xffff];
}

static ARMword starlet_sram_read_word(ARMul_State *state, ARMword addr)
{
	return be32(starlet_sram_decode(state, addr));
}
static ARMword starlet_sram_read_halfword(ARMul_State *state, ARMword addr)
{
	return be16(starlet_sram_decode(state, addr));
}
static ARMword starlet_sram_read_byte(ARMul_State *state, ARMword addr)
{
	return *starlet_sram_decode(state, addr);
}

static void starlet_sram_write_word(ARMul_State *state, ARMword addr, ARMword data)
{
	wbe32(starlet_sram_decode(state, addr),data);
}
static void starlet_sram_write_halfword(ARMul_State *state, ARMword addr, ARMword data)
{
	wbe16(starlet_sram_decode(state, addr),data);
}
static void starlet_sram_write_byte(ARMul_State *state, ARMword addr, ARMword data)
{
	*starlet_sram_decode(state, addr) = data;
}

static void starlet_sdio_write(ARMul_State *state, ARMword addr, ARMword data)
{
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%08x)\n", __FUNCTION__, addr, data);
}
static ARMword starlet_sdio_read(ARMul_State *state, ARMword addr)
{
	printf("PC: %08x\n",ARMul_GetPC(state));
	switch(addr & 0xFFFF) {
		case 0x0024: 	return 0x01ff0000;
		case 0x002C: 	return 0x00000002;
		case 0x0040: 	return 0x01e130b0;
		default:
		break;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x)\n", __FUNCTION__, addr);
	return 0;
}

static void starlet_ohc_write(ARMul_State *state, int id, ARMword addr, ARMword data)
{
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%08x)\n", __FUNCTION__, addr, data);
}
static ARMword starlet_ohc_read(ARMul_State *state, int id, ARMword addr)
{
	switch(addr & 0xFFFF) {
		case 0x00:	return 0x110;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x)\n", __FUNCTION__, addr);
	return 0;
}


//
// SD Host Controller
//
#include "sdhcreg.h"
static enum { 
	SD_STATE_IDLE,
} sd_state;
static int sd_app_cmd = 0;


static void starlet_sdhc_fire_irq(ARMul_State *state, u16 irq)
{
	if (io.sdhc_reg.irq_enable & irq) {
		//DEBUG("SDHC: enabled bit for nirq %04x\n", irq);
		io.sdhc_reg.irq_status |= irq;
	} else {
		//DEBUG("SDHC: nirq %04x is disabled\n", irq);
	}
	if (io.sdhc_reg.irq_signal & irq) {
		//DEBUG("SDHC: fired nirq %04x.\n", irq);
		starlet_set_intr(7);
	} else {
		//DEBUG("SDHC: signal for nirq %04x is disabled\n", irq);
	}

}

#define OCR_POWERUP_STATUS	(1<<31)
static void starlet_sdhc_exec(ARMul_State *state)
{
	u16 command;
	u16 mode;

	// TODO: check actual card state

	command = (io.sdhc_reg.command>>SDHC_COMMAND_INDEX_SHIFT) &
		SDHC_COMMAND_INDEX_MASK;
	mode = io.sdhc_reg.mode;
	/*DEBUG("SDHC: executing command %04x; mode %04x\n", command, mode);
	if (mode & SDHC_MULTI_BLOCK_MODE)
		DEBUG("      -> [x] multi block mode.\n");
	else
		DEBUG("      -> [ ] multi block mode.\n");
	if (mode & SDHC_READ_MODE)
		DEBUG("      -> [x] read mode.\n");
	else
		DEBUG("      -> [ ] read mode.\n");
	if (mode & SDHC_AUTO_CMD12_ENABLE)
		DEBUG("      -> [x] ACMD12 enable.\n");
	else
		DEBUG("      -> [ ] ACMD12 enable.\n");
	if (mode & SDHC_BLOCK_COUNT_ENABLE)
		DEBUG("      -> [x] block count enable.\n");
	else
		DEBUG("      -> [ ] block count enable.\n");
	if (mode & SDHC_DMA_ENABLE)
		DEBUG("      -> [x] DMA enable.\n");
	else
		DEBUG("      -> [ ] DMA enable.\n");*/

	if (sd_app_cmd) {
		sd_app_cmd = 0;
		DEBUG("      -> in APP_CMD state.\n");

		if (command == 41) {
			DEBUG("      -> SD_SEND_OP_COND\n");
			io.sdhc_reg.response[0] = OCR_POWERUP_STATUS | io.sdhc_reg.argument; // OCR!!
			starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
			goto done;
		}
		DEBUG("   UNIMPLEMENTED!!!\n");
		goto unimplemented;
	}

	if (command == 0) {
		DEBUG("      -> GO_IDLE_STATE\n");
		sd_state = SD_STATE_IDLE;
		starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
		goto done;
	}

	if (command == 55) {
		DEBUG("      -> APP_CMD\n");
		sd_app_cmd = 1;
		starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
		goto done;
	}

	if (command == 8) {
		DEBUG("      -> SEND_IF_COND\n");
		io.sdhc_reg.response[0] = io.sdhc_reg.argument;
		starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
		goto done;
	}
	
	if (command == 9) {
		DEBUG("      -> SEND_CSD\n");
		// only SDHC mode for now because meh
		// this is enough to make BootMii happy
		int blocks = sd_card_size / 1024 / 512;
		io.sdhc_reg.response[0] = 0x00000000;
		io.sdhc_reg.response[1] = (blocks-1) << 8;
		io.sdhc_reg.response[2] = 0x00000000;
		io.sdhc_reg.response[3] = 0x00000e00;
		starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
		goto done;
	}
	
	if (command == 2) {
		DEBUG("      -> SEND_CID\n");
		// only SDHC mode for now because meh
		// this is enough to make BootMii happy
		int blocks = sd_card_size / 1024 / 512;
		io.sdhc_reg.response[0] = 0;
		io.sdhc_reg.response[1] = 0x30000000;
		io.sdhc_reg.response[2] = 0x30303030;
		io.sdhc_reg.response[3] = 0x00014130;
		starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
		goto done;
	}
	
	if (command == 18) {
		/*DEBUG("      -> READ_MULTIPLE 0x%x %04x %d -> %08x\n",
			  io.sdhc_reg.argument, io.sdhc_reg.blk_size, io.sdhc_reg.blk_cnt,
			  io.sdhc_reg.dma_addr);*/
		int block_size = io.sdhc_reg.blk_size & 0xfff;
		void *buf = malloc(block_size * io.sdhc_reg.blk_cnt);
		fseek(sd, io.sdhc_reg.argument * 512L, SEEK_SET);
		if (fread(buf, block_size, io.sdhc_reg.blk_cnt, sd) != io.sdhc_reg.blk_cnt) {
			DEBUG("SDHC: Failed to read from SD file!\n");
		}
		starlet_host2arm(state, io.sdhc_reg.dma_addr, buf, block_size * io.sdhc_reg.blk_cnt);
		//starlet_hexdump(buf, 512);
		free(buf);
		//starlet_emu_hexdump(state, io.sdhc_reg.dma_addr, 512);
		starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
		starlet_sdhc_fire_irq(state, SDHC_TRANSFER_COMPLETE);
		goto done;
	}
	if (command == 25) {
		DEBUG("      -> WRITE_MULTIPLE 0x%x %04x %d -> %08x\n",
			  io.sdhc_reg.argument, io.sdhc_reg.blk_size, io.sdhc_reg.blk_cnt,
			  io.sdhc_reg.dma_addr);
		int block_size = io.sdhc_reg.blk_size & 0xfff;
		void *buf = malloc(block_size * io.sdhc_reg.blk_cnt);
		starlet_arm2host(state, buf, io.sdhc_reg.dma_addr, block_size * io.sdhc_reg.blk_cnt);
		fseek(sd, io.sdhc_reg.argument * 512L, SEEK_SET);
		if (fwrite(buf, block_size, io.sdhc_reg.blk_cnt, sd) != io.sdhc_reg.blk_cnt) {
			DEBUG("SDHC: Failed to write to SD file!\n");
		}
		//starlet_hexdump(buf, 512);
		free(buf);
		//starlet_emu_hexdump(state, io.sdhc_reg.dma_addr, 512);
		starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
		starlet_sdhc_fire_irq(state, SDHC_TRANSFER_COMPLETE);
		goto done;
	}

unimplemented:
	starlet_sdhc_fire_irq(state, SDHC_COMMAND_COMPLETE);
	DEBUG("SDHC: command %02x not implemented!\n", command);

done:
	io.sdhc_reg.command = 0xffff0000; // detect every change in the cmd register

}

static void starlet_sdhc_reset(ARMul_State *state, int cmd, int dat, int all)
{
	if (!sd) {
		sd = fopen("sd.bin", "r+b");
		if (sd == NULL) {
			DEBUG("SDHC: unable to open sd.bin; emulating empty slot.\n");
			sd_card_inserted = 0;
		} else {
			fseek(sd, 0, SEEK_END);
			sd_card_size = ftell(sd);
			sd_card_inserted = 1;
			sd_card_is_sdhc = sd_card_size > 0x80000000UL;
		}
	}
	if (all) {
		DEBUG("SDHC: resetting controller\n");
		io.sdhc_reg.dma_addr = 0;
		io.sdhc_reg.blk_size = 0;
		io.sdhc_reg.blk_cnt = 0;
		io.sdhc_reg.argument = 0;
		io.sdhc_reg.mode = 0;
		io.sdhc_reg.command = 0xffff0000;
		io.sdhc_reg.response[0] = 0;
		io.sdhc_reg.response[1] = 0;
		io.sdhc_reg.response[2] = 0;
		io.sdhc_reg.response[3] = 0;
		io.sdhc_reg.present_state = 0;
		io.sdhc_reg.host_control = 0;
		io.sdhc_reg.pwr_control = 0;
		io.sdhc_reg.bgap_control = 0;
		io.sdhc_reg.wakeup_control = 0;
		io.sdhc_reg.clock_control = 0;
		io.sdhc_reg.timeout_control = 0;
		io.sdhc_reg.irq_enable = 0;
		io.sdhc_reg.irq_signal = 0;
		io.sdhc_reg.irq_status = 0;
		io.sdhc_reg.eirq_enable = 0;
		io.sdhc_reg.eirq_signal = 0;
		io.sdhc_reg.eirq_status = 0;
		io.sdhc_reg.acmd12_err_status = 0;
		io.sdhc_reg.capabilities = 0;
		io.sdhc_reg.max_capabilities = 0;
		io.sdhc_reg.force_event_eirq = 0;
		io.sdhc_reg.force_event_acmd12 = 0;

		io.sdhc_reg.capabilities |= SDHC_VOLTAGE_SUPP_3_3V |
			SDHC_DMA_SUPPORT | (10<<SDHC_BASE_FREQ_SHIFT);
		io.sdhc_reg.max_capabilities = io.sdhc_reg.capabilities;

	}
	if (cmd) {
		DEBUG("SDHC: resetting CMD line\n");
//		io.sdhc_reg.present_state &= ~SDHC_CMD_INHIBIT_CMD;
		io.sdhc_reg.irq_status &= ~SDHC_COMMAND_COMPLETE;
	}
	if (dat) {
		DEBUG("SDHC: resetting DAT line\n");
/*		io.sdhc_reg.present_state &= ~(SDHC_CMD_INHIBIT_DAT |
				SDHC_DAT_ACTIVE | SDHC_READ_TRANSFER_ACTIVE |
				SDHC_WRITE_TRANSFER_ACTIVE |
				SDHC_BUFFER_READ_ENABLE |
				SDHC_BUFFER_WRITE_ENABLE);*/
		io.sdhc_reg.irq_status &= ~(SDHC_TRANSFER_COMPLETE |
				SDHC_DMA_INTERRUPT | SDHC_BLOCK_GAP_EVENT |
				SDHC_BUFFER_READ_READY |
				SDHC_BUFFER_WRITE_READY);
	}
		printf("pres: %08x\n", io.sdhc_reg.present_state);
}

static void starlet_sdhc_init(ARMul_State *state)
{
	starlet_sdhc_reset(state, 1, 1, 1);
}

static void starlet_sdhc_write(ARMul_State *state, ARMword addr, ARMword data)
{
	ARMword diff, reg;

	//DEBUG("SDHC: %s(addr:0x%08x, data:0x%x)\n", __FUNCTION__, addr, data);

	switch(addr & 0xFFFF) {
	case SDHC_DMA_ADDR:
		//DEBUG("SDHC: new SDMA addr: %08x\n", data);
		io.sdhc_reg.dma_addr = data;
		break;
	case SDHC_BLOCK_SIZE: /* and SDHC_BLOCK_COUNT */ //FIXED
		diff = io.sdhc_reg.blk_size ^ (data&0xffff);
		if (diff) {
			io.sdhc_reg.blk_size = data&0xffff;
			//DEBUG("SDHC: new block size: %04x\n", data&0xffff);
		}
		diff = io.sdhc_reg.blk_cnt ^ (data>>16);
		if (diff) {
			io.sdhc_reg.blk_cnt = data>>16;
			//DEBUG("SDHC: new block count: %04x\n", data>>16);
		}
		break;
	case SDHC_ARGUMENT:
		io.sdhc_reg.argument = data;
		//DEBUG("SDHC: new argument: %08x\n", io.sdhc_reg.argument);
		break;
	case SDHC_TRANSFER_MODE: /* and command */ // FIXED
		diff = io.sdhc_reg.mode ^ (data&0xffff);
		if (diff) {
			io.sdhc_reg.mode = data&0xffff;
			/*DEBUG("SDHC: new transfer mode: %04x\n",
					io.sdhc_reg.mode);*/
		}
		diff = io.sdhc_reg.command ^ (data>>16);
		if (diff) {
			io.sdhc_reg.command = data>>16;
			/*DEBUG("SDHC: new command: %08x\n",
					io.sdhc_reg.command);*/
			starlet_sdhc_exec(state);
		}
		break;
	case SDHC_RESPONSE:
	case SDHC_RESPONSE+4:
	case SDHC_RESPONSE+8:
	case SDHC_RESPONSE+12:
		reg = (addr&0xffff) - SDHC_RESPONSE;
		DEBUG("SDHC: WTF?: ARM changes reponse register %d to %08x\n",
				reg, data);
		printf("PC: %08x\n",ARMul_GetPC(state));
		io.sdhc_reg.response[reg] = data;
	case SDHC_PRESENT_STATE:
		DEBUG("SDHC: WTF: ARM wants to write to SDHC_PRESENT_STATE.\n");
		printf("PC: %08x\n",ARMul_GetPC(state));
		break;
	case SDHC_HOST_CTL: /* and power, bgap and wakeup ctl */ // FIXED
		diff = io.sdhc_reg.host_control ^ (data&0xf);
		if (diff) {
			io.sdhc_reg.host_control = data&0xf;
			/*DEBUG("SDHC: new hostcontrol: %02x\n",
					io.sdhc_reg.host_control);*/
		}
		diff = io.sdhc_reg.pwr_control ^ ((data>>8)&0xf);
		if (diff) {
			io.sdhc_reg.pwr_control = (data>>8)&0xf;
			DEBUG("SDHC: new powercontrol: %02x\n",
					io.sdhc_reg.pwr_control);
		}
		diff = io.sdhc_reg.bgap_control ^ ((data>>16)&0xf);
		if (diff) {
			io.sdhc_reg.bgap_control = (data>>16)&0xf;
			DEBUG("SDHC: new bgapcontrol: %02x\n",
					io.sdhc_reg.bgap_control);
		}
		diff = io.sdhc_reg.wakeup_control ^ ((data>>24)&0xf);
		if (diff) {
			io.sdhc_reg.wakeup_control = (data>>24)&0xf;
			DEBUG("SDHC: new wakeupcontrol: %02x\n",
					io.sdhc_reg.wakeup_control);
		}
		break;
	case SDHC_CLOCK_CTL: /* and timeout control and reset */ // FIXED
		diff = io.sdhc_reg.clock_control ^ (data&0xffff);
		if (diff) {
			io.sdhc_reg.clock_control = data&0xffff;
			DEBUG("SDHC: new clock control: %04x\n", data&0xffff);
			if (io.sdhc_reg.clock_control & SDHC_INTCLK_ENABLE)
				io.sdhc_reg.clock_control |= SDHC_INTCLK_STABLE;
		}
		diff = (io.sdhc_reg.timeout_control ^ (data>>16)) & 0xf;
		if (diff) {
			io.sdhc_reg.timeout_control = (data>>16)&0xf;
			DEBUG("SDHC: new timeout control: %02x\n",
					io.sdhc_reg.timeout_control);
		}
		diff = (data>>24)&0xf;
		if (diff) {
			DEBUG("SDHC: reset: ALL: %d CMD: %d DAT: %d\n",
					(diff & SDHC_RESET_ALL ? 1 : 0),
					(diff & SDHC_RESET_CMD ? 1 : 0),
					(diff & SDHC_RESET_DAT ? 1 : 0));
			starlet_sdhc_reset(state,
					   diff & SDHC_RESET_CMD,
					   diff & SDHC_RESET_DAT,
					   diff & SDHC_RESET_ALL);
		}
		break;
	case SDHC_NINTR_STATUS: // FIXED
		diff = data&0xffff;
		if (diff) {
			//DEBUG("SDHC: NINTR status write: %04x\n", diff);
			io.sdhc_reg.irq_status &= ~diff;
		}
		diff = (data>>16);
		if (diff) {
			//DEBUG("SDHC: EINTR status write: %04x\n", diff);
			io.sdhc_reg.eirq_status &= ~diff;
		}
		break;
	case SDHC_NINTR_STATUS_EN: // FIXED
		diff = (data&0xffff) ^ io.sdhc_reg.irq_enable;
		if (diff) {
			io.sdhc_reg.irq_enable = data&0xffff;
//			io.sdhc_reg.irq_status &= data&0xffff;
			/*DEBUG("SDHC: NINTR status enable: %04x\n",
					io.sdhc_reg.irq_enable);*/
		}
		diff = (data>>16) ^ io.sdhc_reg.eirq_enable;
		if (diff) {
			io.sdhc_reg.eirq_enable = data>>16;
//			io.sdhc_reg.eirq_status &= data>>16;
			/*DEBUG("SDHC: EINTR status enable: %04x\n",
					io.sdhc_reg.eirq_enable);*/
		}
		break;
	case SDHC_NINTR_SIGNAL_EN:
		diff = (data&0xffff) ^ io.sdhc_reg.irq_signal;
		if (diff) {
			io.sdhc_reg.irq_signal = data&0xffff;
			/*DEBUG("SDHC: NINTR signal enable: %04x\n",
					io.sdhc_reg.irq_signal);*/
		}
		diff = (data>>16) ^ io.sdhc_reg.eirq_signal;
		if (diff) {
			io.sdhc_reg.eirq_signal = data>>16;
			/*DEBUG("SDHC: EINTR signal enable: %04x\n",
					io.sdhc_reg.eirq_signal);*/
		}
		break;
	case SDHC_CAPABILITIES:
		DEBUG("SDHC: WTF: write to SDHC_CAPABILITIES!\n");
		printf("PC: %08x\n",ARMul_GetPC(state));
		break;
	case SDHC_MAX_CAPABILITIES:
		DEBUG("SDHC: WTF: write to SDHC_MAX_CAPABILITIES!\n");
		printf("PC: %08x\n",ARMul_GetPC(state));
		break;
	default:
		DEBUG("SDHC UNIMPLEMENTED: %s(addr:0x%08x, value: %08x)\n",
				__FUNCTION__, addr, data);
		printf("PC: %08x\n",ARMul_GetPC(state));
		break;
	}
}

static ARMword starlet_sdhc_read(ARMul_State *state, ARMword addr)
{
	//DEBUG("SDHC: %s(addr:0x%08x)\n", __FUNCTION__, addr);
	switch(addr & 0xFFFF) {
	case SDHC_DMA_ADDR:
		return io.sdhc_reg.dma_addr;
	case SDHC_BLOCK_SIZE: /* and SDHC_BLOCK_COUNT */
		return io.sdhc_reg.blk_size | io.sdhc_reg.blk_cnt<<16;
	case SDHC_ARGUMENT:
		return io.sdhc_reg.argument;
	case SDHC_TRANSFER_MODE: /* and command */
		return io.sdhc_reg.mode | io.sdhc_reg.command<<16;
	case SDHC_RESPONSE:
		return io.sdhc_reg.response[0];
	case SDHC_RESPONSE+4:
		return io.sdhc_reg.response[1];
	case SDHC_RESPONSE+8:
		return io.sdhc_reg.response[2];
	case SDHC_RESPONSE+12:
		return io.sdhc_reg.response[3];
	case SDHC_PRESENT_STATE:
		if (sd_card_inserted)
			return io.sdhc_reg.present_state | SDHC_CARD_INSERTED;
		else
			return io.sdhc_reg.present_state;
	case SDHC_HOST_CTL: /* and power, bgap and wakeup ctl */ // FIXED
		return io.sdhc_reg.host_control |
		       io.sdhc_reg.pwr_control<<8  |
		       io.sdhc_reg.bgap_control<<16  |
		       io.sdhc_reg.wakeup_control<<24;
	case SDHC_CLOCK_CTL: /* and timeout control */ // FIXED
		return io.sdhc_reg.clock_control |
		       io.sdhc_reg.timeout_control<<16;
	case SDHC_NINTR_STATUS: // FIXED
		return io.sdhc_reg.irq_status |
		       io.sdhc_reg.eirq_status<<16;
	case SDHC_NINTR_STATUS_EN: // FIXED
		return io.sdhc_reg.irq_enable |
		       io.sdhc_reg.eirq_enable<<16;
	case SDHC_NINTR_SIGNAL_EN: // FIXED
		return io.sdhc_reg.irq_signal |
		       io.sdhc_reg.eirq_signal<<16;
	case SDHC_CAPABILITIES:
		return io.sdhc_reg.capabilities;
	case SDHC_MAX_CAPABILITIES:
		return io.sdhc_reg.max_capabilities;
	case SDHC_SLOT_INTR_STATUS:
		return io.sdhc_reg.irq_status == 0 ? 0 : 1;
	default:
		DEBUG("UNIMPLEMENTED: %s(addr:0x%08x)\n", __FUNCTION__, addr);
		printf("PC: %08x\n",ARMul_GetPC(state));
		return 0;
		break;
	}
}

/* IO Read Routine */
static ARMword starlet_io_read_word(ARMul_State *state, ARMword addr)
{
	switch (addr >> 20) {
		case 0x0d0:
			switch (addr >> 16) {
				case 0x0D00:
					switch(addr & 0xFC00) {
						case 0x0000: return starlet_hollywood_read(state, addr);
						case 0x6000: return starlet_di_read(state, addr);
						case 0x6800: return starlet_exi_read(state, addr);
					}
		//		case 0x0D01: nand flash, handled elsewhere
				case 0x0D02: return starlet_aes_read(state, addr);
				case 0x0D03: return starlet_sha_read(state, addr);
				case 0x0D05: return starlet_ohc_read(state, 0, addr);
				case 0x0D06: return starlet_ohc_read(state, 1, addr);
				case 0x0D07: return starlet_sdhc_read(state, addr);
				case 0x0D08: return starlet_sdio_read(state, addr);
			}
			break;
		case 0x0d8:
			switch (addr >> 16) {
				case 0x0D80:
					switch(addr & 0xFC00) {
						case 0x0000: return starlet_hollywood_read(state, addr);
						case 0x6000: return starlet_di_read(state, addr);
						case 0x6800: return starlet_exi_read(state, addr);
					}
				case 0x0D8B: return starlet_dram_read(state, addr);
			}
			break;
		case 0x0d4:
		case 0x0dc:
		case 0xfff:
			return starlet_sram_read_word(state, addr);
			break;
	}
	DEBUG("@%08x: UNIMPLEMENTED: %s(addr:0x%08x)\n", ARMul_GetPC(state), __FUNCTION__, addr);
	
	return 0;
}

/* IO Write Routine */
static void starlet_io_write_word(ARMul_State *state, ARMword addr, ARMword data)
{
	switch (addr >> 20) {
		case 0x0d0:
			switch (addr >> 16) {
				case 0x0D00:
					switch(addr & 0xFC00) {
						case 0x0000: return starlet_hollywood_write(state, addr, data);
						case 0x6000: return starlet_di_write(state, addr, data);
						case 0x6800: return starlet_exi_write(state, addr, data);
					}
		//		case 0x0D01: nand flash, handled elsewhere
				case 0x0D02: return starlet_aes_write(state, addr, data);
				case 0x0D03: return starlet_sha_write(state, addr, data);
				case 0x0D05: return starlet_ohc_write(state, 0, addr, data);
				case 0x0D06: return starlet_ohc_write(state, 1, addr, data);
				case 0x0D07: return starlet_sdhc_write(state, addr, data);
				case 0x0D08: return starlet_sdio_write(state, addr, data);
			}
			break;
		case 0x0d8:
			switch (addr >> 16) {
				case 0x0D80:
					switch(addr & 0xFC00) {
						case 0x0000: return starlet_hollywood_write(state, addr, data);
						case 0x6000: return starlet_di_write(state, addr, data);
						case 0x6800: return starlet_exi_write(state, addr, data);
					}
				case 0x0D8B: return starlet_dram_write(state, addr, data);
			}
			break;
		case 0x0d4:
		case 0x0dc:
		case 0xfff:
			return starlet_sram_write_word(state, addr, data);
			break;
		default:
			break;
	}
	
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%x)\n", __FUNCTION__, addr, data);
}

static ARMword starlet_io_read_byte(ARMul_State *state, ARMword addr)
{
	switch (addr >> 20) {
		case 0x0d4:
		case 0x0dc:
		case 0xfff:
			return starlet_sram_read_byte(state, addr);
	}
	return starlet_io_read_word(state, addr);
}


static ARMword starlet_io_read_halfword(ARMul_State *state, ARMword addr)
{
	switch (addr >> 20) {
		case 0x0d8:
			switch(addr >> 16) {
				case 0x0d8b:
					return starlet_ahb_read_halfword(state, addr);
			}
			break;
		case 0x0d4:
		case 0x0dc:
		case 0xfff:
			return starlet_sram_read_halfword(state, addr);
	}
	return starlet_io_read_word(state, addr);
}

static void starlet_io_write_byte(ARMul_State *state, ARMword addr, ARMword data)
{
	switch (addr >> 20) {
		case 0x0d4:
		case 0x0dc:
		case 0xfff:
			starlet_sram_write_byte(state, addr, data);
			return;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%x)\n", __FUNCTION__, addr, data);
	starlet_io_write_word(state, addr, data);
}


static void starlet_io_write_halfword(ARMul_State *state, ARMword addr, ARMword data)
{
	switch (addr >> 20) {
		case 0x0d8:
			switch(addr >> 16) {
				case 0x0d8b:
					starlet_ahb_write_halfword(state, addr, data);
					return;
			}
			break;
		case 0x0d4:
		case 0x0dc:
		case 0xfff:
			starlet_sram_write_halfword(state, addr, data);
			return;
	}
	DEBUG("UNIMPLEMENTED: %s(addr:0x%08x, data:0x%x)\n", __FUNCTION__, addr, data);
	starlet_io_write_word(state, addr, data);
}

enum ipc_state {
	IPC_INIT,
	IPC_ACCEPT,
	IPC_IDLE,
	IPC_SNDACK,
};

#define SOCK_PATH "ipcsock"

int sendall(int s, void *buf, int len)
{
	int total = 0;        // how many bytes we've sent
	int bytesleft = len; // how many we have left to send
	int n = 0;

	while(total < len) {
		n = send(s, ((char*)buf)+total, bytesleft, 0);
		if (n == -1) break;
		total += n;
		bytesleft -= n;
	}
	
	return n==-1?-1:0; // return -1 on failure, 0 on success
}

int recvall(int s, void *buf, int len)
{
	int total = 0;        // how many bytes we've received
	int bytesleft = len; // how many we have left to receive
	int n = 0;
	while(total < len) {
		n = recv(s, ((char*)buf)+total, bytesleft, 0);
		if (n == -1) break;
		if (n == 0) {
			n = -1;
			break;
		}
		total += n;
		bytesleft -= n;
	}
	
	return n==-1?-1:0; // return -1 on failure, 0 on success
}

#define MSG_MESSAGE 1
#define MSG_ACK 2
#define MSG_STATUS 3
#define MSG_READ 4
#define MSG_WRITE 5
#define MSG_STATE 6
#define MSG_EXIT 7

int sendipcmsg(int s, unsigned int msg, unsigned int arg, void *buf, unsigned int len)
{
	if(!buf)
		len = 0;

	if(sendall(s, &msg, 4) == -1)
		return -1;
	if(sendall(s, &arg, 4) == -1)
		return -1;
	if(sendall(s, &len, 4) == -1)
		return -1;
	if(len) {
		if(sendall(s, buf, len) == -1)
			return -1;
	}
	return 0;
}

int msgpending(int s)
{
	int res;
	struct timeval tv;
	fd_set readfds;
	fd_set excfds;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&readfds);
	FD_SET(s, &readfds);
	FD_ZERO(&excfds);
	FD_SET(s, &excfds);

	// don't care about writefds and exceptfds:
	res = select(s+1, &readfds, NULL, &excfds, &tv);
	if(res == 0)
		return 0;
	if(res == -1)
		return -1;

	if (FD_ISSET(s, &excfds))
		return -1;
	if (FD_ISSET(s, &readfds))
		return 1;
	return -1;

}

int recvipcmsg(int s, unsigned int *msg, unsigned int *arg, void **buf, unsigned int *len)
{
	if(recvall(s, msg, 4) == -1)
		return -1;
	if(recvall(s, arg, 4) == -1)
		return -1;
	if(recvall(s, len, 4) == -1)
		return -1;
	*buf = NULL;
	if(*len) {
		*buf = malloc(*len);
	}
	if(recvall(s, *buf, *len) == -1) {
		free(buf);
		return -1;
	}
	return 0;
}

static void ipc_set(ARMul_State *state, ARMword bits)
{
	starlet_ipc_write(state, 4, (io.ipc_ppcctrl & 0x3C) | bits);
}

struct cooked_state {
	ARMword regs[16];
	ARMword regs_banked[7][16];
	ARMword cpsr, spsrs[7];
	ARMword cr;
	ARMword ttbr;
	ARMword dacr;
	ARMword fsr, far;
};

static void starlet_do_ipc(ARMul_State *state)
{
	static enum ipc_state ipcs = IPC_INIT;
	static int asock, bsock;
	static struct sockaddr_un local, remote;
	static int last_ppc_state = 0;
	int len;
	unsigned int t;
	int res;
	
	switch(ipcs)
	{
		case IPC_INIT:
			starlet_ipc_write(state, 4, 0x36);
			if ((asock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
				perror("socket");
				skyeye_exit(1);
			}
			local.sun_family = AF_UNIX;
			strcpy(local.sun_path, SOCK_PATH);
			unlink(local.sun_path);
			len = strlen(local.sun_path) + sizeof(local.sun_family);
			fcntl(asock, F_SETFL, O_NONBLOCK);
			if (bind(asock, (struct sockaddr *)&local, len) == -1) {
				perror("bind");
				skyeye_exit(1);
			}
			if (listen(asock, 1) == -1) {
				perror("listen");
				skyeye_exit(1);
			}
			printf("IPC: listening for connection\n");
			ipcs = IPC_ACCEPT;
			break;
		case IPC_ACCEPT:
			t = sizeof(remote);
			if ((bsock = accept(asock, (struct sockaddr *)&remote, &t)) == -1) {
				if(errno == EAGAIN)
					break;
				perror("accept");
				skyeye_exit(1);
			}
			if(sendipcmsg(bsock, MSG_STATUS, io.ppc_running, NULL, 0) == -1) {
				perror("sendmsg");
				close(bsock);
				ipcs = IPC_ACCEPT;
				break;
			}
			last_ppc_state = io.ppc_running;
			ipcs = IPC_IDLE;
			break;
		case IPC_IDLE:
			if(starlet_pending_intr_ppc(30)) {
				if(io.ipc_ppcctrl & 0x02) {
					printf("IPC: received extra ACK\n");
					ipc_set(state, 0x02);
					if(sendipcmsg(bsock, MSG_ACK, 0, NULL, 0) == -1) {
						perror("sendmsg");
						close(bsock);
						ipcs = IPC_ACCEPT;
						break;
					}
				}
				if(io.ipc_ppcctrl & 0x04) {
					printf("IPC: received message %08x\n", io.ipc_armmsg);
					ipc_set(state, 0x0c);
					if(sendipcmsg(bsock, MSG_MESSAGE, io.ipc_armmsg, NULL, 0) == -1) {
						perror("sendmsg");
						close(bsock);
						ipcs = IPC_ACCEPT;
						break;
					}
				}
				io.irq_active_ppc &= ~(1<<30);
			}
			res = msgpending(bsock);
			if(res == -1) {
				perror("msgpending");
				close(bsock);
				ipcs = IPC_ACCEPT;
				break;
			}
			if(res == 1) {
				unsigned int msg, arg;
				unsigned int len;
				void *buf;
				void *buf2;
				struct cooked_state cstate;
				errno = 0;
				if(recvipcmsg(bsock, &msg, &arg, &buf, &len) == -1)
				{
					if(errno != 0)
						perror("recvmsg");
					close(bsock);
					ipcs = IPC_ACCEPT;
					break;
				}
				switch(msg) {
					case MSG_MESSAGE:
						io.ipc_ppcmsg = arg;
						ipc_set(state, 0x01);
						printf("IPC: sent message %08x\n", arg);
						ipcs = IPC_SNDACK;
						break;
					case MSG_ACK:
						ipc_set(state, 0x08);
						printf("IPC: sent extra ack\n");
						break;
					case MSG_READ:
						printf("IPC: READ addr 0x%08x len 0x%x\n", arg, *(unsigned int*)buf);
						buf2 = malloc(*(unsigned int*)buf);
						if((arg&3) == 0 && (*(unsigned int*)buf&3) == 0)
							starlet_arm2host32(state, buf2, arg, *(unsigned int*)buf);
						else if((arg&1) == 0 && (*(unsigned int*)buf&1) == 0)
							starlet_arm2host16(state, buf2, arg, *(unsigned int*)buf);
						else
							starlet_arm2host(state, buf2, arg, *(unsigned int*)buf);
						if(sendipcmsg(bsock, MSG_READ, arg, buf2, *(unsigned int*)buf) == -1) {
							perror("sendmsg");
							close(bsock);
							ipcs = IPC_ACCEPT;
							free(buf2);
							break;
						}
						free(buf2);
						break;
					case MSG_WRITE:
						printf("IPC: WRITE addr 0x%08x len 0x%x\n", arg, len);
						if((arg&3) == 0 && (len&3) == 0)
							starlet_host2arm32(state, arg, buf, len);
						else if((arg&1) == 0 && (len&1) == 0)
							starlet_host2arm16(state, arg, buf, len);
						else
							starlet_host2arm(state, arg, buf, len);
						break;
					case MSG_STATE:
						printf("IPC: STATE\n");
						memcpy(cstate.regs, state->Reg, sizeof(state->Reg));
						memcpy(cstate.regs_banked, state->RegBank, sizeof(state->RegBank));
						cstate.cpsr = state->Cpsr;
						memcpy(cstate.spsrs, state->Spsr, sizeof(state->Spsr));
						cstate.cr = state->mmu.control;
						cstate.ttbr = state->mmu.translation_table_base;
						cstate.dacr = state->mmu.domain_access_control;
						cstate.fsr = state->mmu.fault_status;
						cstate.far = state->mmu.fault_address;
						if(sendipcmsg(bsock, MSG_STATE, 0, &cstate, sizeof(cstate)) == -1) {
							perror("sendmsg");
							close(bsock);
							ipcs = IPC_ACCEPT;
							break;
						}
						break;
					case MSG_EXIT:
						printf("IPC: EXIT requested by peer\n");
						skyeye_exit(0);
					default:
						printf("IPC: unknown message 0x%08x\n", msg);
				}
				if(buf)
					free(buf);
			}
			if(io.ppc_running != last_ppc_state) {
				printf("IPC: PPC state change %d->%d\n", last_ppc_state, io.ppc_running);
				if(sendipcmsg(bsock, MSG_STATUS, io.ppc_running, NULL, 0) == -1) {
					perror("sendmsg");
					close(bsock);
					ipcs = IPC_ACCEPT;
					break;
				}
				last_ppc_state = io.ppc_running;
			}
			break;
		case IPC_SNDACK:
			if(starlet_pending_intr_ppc(30)) {
				if(io.ipc_ppcctrl & 0x02) {
					printf("IPC: got ACK\n");
					ipc_set(state, 0x02);
					ipcs = IPC_IDLE;
				}
				io.irq_active_ppc &= ~(1<<30);
			}
			break;
	}
}

static void starlet_io_do_cycle(ARMul_State *state)
{
//	printf("io cycle\n");
	io.timer++;
	if (io.timer == io.alarm) {
		starlet_set_intr(0);
		starlet_update_int(state);
		//DEBUG("Timer IRQ\n");
	}
	//starlet_print_context(state);
	ipc_update_int(state);
	starlet_do_ipc(state);
	ipc_update_int(state);
}



///// OS CALLS /////

char *starlet_get_string(ARMul_State *state, ARMword pointer)
{
	static char buf[0x1000];
	char *ptr;
	int i;
	
	ptr = buf;
	
	for(i=0; i<0x1000; i++) {
		int byte = ARMul_LoadByte(state, pointer++);
		*ptr++ = byte;
		if(byte == 0) break;
	}
	buf[0xfff]=0;
	return buf;
}

void starlet_IOS_ExitThread(ARMul_State *state, ARMword threadid, ARMword code)
{
	printf("[%s] IOS_ExitThread(%d,%d)\n", get_context(state), threadid, code);
}

void starlet_IOS_CreateThread(ARMul_State *state, ARMword r0, ARMword r1, ARMword r2, ARMword r3)
{
	printf("[%s] IOS_CreateThread(0x%08x,0x%x,0x%08x,0x%x)\n", get_context(state), r0, r1, r2, r3);
}

void starlet_IOS_JoinThread(ARMul_State *state, ARMword r0)
{
	printf("[%s] IOS_JoinThread(0x%08x)\n", get_context(state), r0);
}

void starlet_IOS_StopThread(ARMul_State *state, ARMword r0)
{
	printf("[%s] IOS_StopThread(0x%08x)\n", get_context(state), r0);
}

void starlet_IOS_YieldThread(ARMul_State *state, ARMword r0)
{
	printf("[%s] IOS_YieldThread(%d)\n", get_context(state), r0);
}

void starlet_IOS_ContinueThread(ARMul_State *state, ARMword r0)
{
	printf("[%s] IOS_ContinueThread(%d)\n", get_context(state), r0);
}

void starlet_IOS_GetTID(ARMul_State *state)
{
	printf("[%s] IOS_GetTID()\n", get_context(state));
}

void starlet_IOS_GetPID(ARMul_State *state)
{
	printf("[%s] IOS_GetPID()\n", get_context(state));
}

void starlet_IOS_ThreadSetPriority(ARMul_State *state, ARMword tid, ARMword prio)
{
	ARMword scaddr = ARMul_GetPC(state)-8;
	const char *ctx = get_context(state);
	printf("[%s] IOS_ThreadSetPriority(%d, %d)\n", ctx,tid, prio);
	if(!strncmp(ctx, "WL", 2) || !strncmp(ctx, "WD", 2) || !strncmp(ctx, "KD", 2) || !strncmp(ctx, "NCD", 3)) {
		printf("[%s] Killing thread at 0x%08x : 0x%08x -> ", ctx, scaddr, ARMul_ReadWord(state, scaddr));
		mem_write_word(state, scaddr, 0xe6000050);
		printf("0x%08x\n", ARMul_ReadWord(state, scaddr) );
	}
	
}


void starlet_IOS_Allocate(ARMul_State *state, ARMword heap, ARMword size)
{
	printf("[%s] IOS_Allocate(%d,0x%x)\n", get_context(state), heap, size);
}

void starlet_IOS_CreateHeap(ARMul_State *state, ARMword address, ARMword size)
{
	printf("[%s] IOS_CreateHeap(0x%08x,0x%x)\n", get_context(state), address, size);
}

void starlet_IOS_AllocateAligned(ARMul_State *state, ARMword heap, ARMword size, ARMword alignment)
{
	printf("[%s] IOS_AllocateAligned(%d,0x%x,%d)\n", get_context(state), heap, size, alignment);
}

void starlet_IOS_Free(ARMul_State *state, ARMword heap, ARMword addr)
{
	printf("[%s] IOS_Free(%d,0x%0x)\n", get_context(state), heap, addr);
}

void starlet_IOS_CreateQueue(ARMul_State *state, ARMword address, ARMword nelem)
{
	printf("[%s] IOS_CreateQueue(0x%08x,%d)\n", get_context(state), address, nelem);
}

void starlet_IOS_DestroyQueue(ARMul_State *state, ARMword address, ARMword nelem)
{
	printf("[%s] IOS_DestroyQueue(0x%08x,%d)\n", get_context(state), address, nelem);
}

void starlet_IOS_ReadQueue(ARMul_State *state, ARMword queue, ARMword message, ARMword nonblock)
{
	if(!strncmp(get_context(state), "WL", 2))
		printf("[%s] IOS_ReadQueue(%d,0x%08x,%d)\n", get_context(state), queue, message, nonblock);
}


void starlet_IOS_RegisterEvent(ARMul_State *state, ARMword event, ARMword queue, ARMword pmessage)
{
	printf("[%s] IOS_RegisterEvent(%d,%d,0x%08x)\n", get_context(state), event, queue, pmessage);
}

void starlet_IOS_CreateTimer(ARMul_State *state, ARMword r0, ARMword interval, ARMword queue, ARMword message)
{
	printf("[%s] IOS_CreateTimer(%d,%d,%d,0x%08x)\n", get_context(state), r0, interval, queue, message);
}


void starlet_IOS_RestartTimer(ARMul_State *state, ARMword timer, ARMword delay, ARMword interval)
{
	printf("[%s] IOS_RestartTimer(%d,%d,%d)\n", get_context(state), timer, delay, interval);
}

void starlet_IOS_StopTimer(ARMul_State *state, ARMword timer)
{
	printf("[%s] IOS_StopTimer(%d)\n", get_context(state), timer);
}

void starlet_IOS_DestroyTimer(ARMul_State *state, ARMword timer)
{
	printf("[%s] IOS_DestroyTimer(%d)\n", get_context(state), timer);
}

void starlet_IOS_DeviceRegister(ARMul_State *state, ARMword name, ARMword queue)
{

	char *dev;
	
	dev = starlet_get_string(state, name);
	
	printf("[%s] IOS_DeviceRegister(\"%s\", %d)\n", get_context(state), dev, queue);
}

void starlet_IOS_AckMessage(ARMul_State *state, ARMword qaddr, ARMword value)
{
	printf("[%s] IOS_AckMessage(0x%08x, 0x%x (%d)\n", get_context(state),qaddr,value,value);
}

void starlet_IOS_DeviceOpen(ARMul_State *state, ARMword pname, ARMword mode)
{
	char *name;
	
	name = starlet_get_string(state, pname);
	printf("[%s] IOS_Open(\"%s\", %d)\n", get_context(state), name, mode);
}

void starlet_IOS_DeviceClose(ARMul_State *state, ARMword fd)
{
	printf("[%s] IOS_Close(%d)\n", get_context(state), fd);
}

void starlet_IOS_DeviceRead(ARMul_State *state, ARMword fd, ARMword buf, ARMword len)
{
	printf("[%s] IOS_Read(%d, 0x%08x, 0x%x)\n", get_context(state), fd, buf, len);
}

void starlet_IOS_DeviceWrite(ARMul_State *state, ARMword fd, ARMword buf, ARMword len)
{
	printf("[%s] IOS_Write(%d, 0x%08x, 0x%x)\n", get_context(state), fd, buf, len);
}

void starlet_IOS_DeviceSeek(ARMul_State *state, ARMword fd, ARMword where, ARMword whence)
{
	printf("[%s] IOS_Seek(%d, %d, %d)\n", get_context(state), fd, where, whence);
}

void starlet_IOS_DeviceIoctl(ARMul_State *state, ARMword fd, ARMword num, ARMword inbuf, ARMword inlen, ARMword outbuf, ARMword outlen)
{
	printf("[%s] IOS_Ioctl(%d, 0x%x, 0x%08x, 0x%x, 0x%08x, 0x%x)\n", get_context(state), fd, num, inbuf, inlen, outbuf, outlen);
}

void starlet_IOS_DeviceIoctlv(ARMul_State *state, ARMword fd, ARMword num, ARMword ins, ARMword outs, ARMword pvecs)
{
	struct {
		ARMword offset, len;
	} vecs[256];
	int i;
	
	printf("[%s] IOS_Ioctlv(%d, 0x%x, %d, %d, 0x%08x)\n", get_context(state), fd, num, ins, outs, pvecs);
	if((ins+outs) < 256) {
		for(i=0;i<(ins+outs);i++) {
			vecs[i].offset = ARMul_ReadWord(state, pvecs+8*i);
			vecs[i].len = ARMul_ReadWord(state, pvecs+8*i+4);
			printf("             %3d [%c]: 0x%08x [0x%x]\n", i, (i<ins)?'I':'O', vecs[i].offset, vecs[i].len);
		}
	}
}

void starlet_IOS_DeviceOpenAsync(ARMul_State *state, ARMword pname, ARMword mode, ARMword queue, ARMword message)
{
	char *name;
	
	name = starlet_get_string(state, pname);
	printf("[%s] IOS_OpenAsync(\"%s\", %d, %08x, %08x)\n", get_context(state), name, mode, queue, message);
}

void starlet_IOS_DeviceCloseAsync(ARMul_State *state, ARMword fd, ARMword queue, ARMword message)
{
	printf("[%s] IOS_CloseAsync(%d, %08x, %08x)\n", get_context(state), fd, queue, message);
}

void starlet_IOS_DeviceReadAsync(ARMul_State *state, ARMword fd, ARMword buf, ARMword len, ARMword queue, ARMword message)
{
	printf("[%s] IOS_ReadAsync(%d, 0x%08x, 0x%x, %08x, %08x)\n", get_context(state), fd, buf, len, queue, message);
}

void starlet_IOS_DeviceWriteAsync(ARMul_State *state, ARMword fd, ARMword buf, ARMword len, ARMword queue, ARMword message)
{
	printf("[%s] IOS_WriteAsync(%d, 0x%08x, 0x%x, %08x, %08x)\n", get_context(state), fd, buf, len, queue, message);
}

void starlet_IOS_DeviceSeekAsync(ARMul_State *state, ARMword fd, ARMword where, ARMword whence, ARMword queue, ARMword message)
{
	printf("[%s] IOS_SeekAsync(%d, %d, %d, %08x, %08x)\n", get_context(state), fd, where, whence, queue, message);
}

void starlet_IOS_DeviceIoctlAsync(ARMul_State *state, ARMword fd, ARMword num, ARMword inbuf, ARMword inlen, ARMword outbuf, ARMword outlen, ARMword queue, ARMword message)
{
	printf("[%s] IOS_IoctlAsync(%d, 0x%x, 0x%08x, 0x%x, 0x%08x, 0x%x, %08x, %08x)\n", get_context(state), fd, num, inbuf, inlen, outbuf, outlen, queue, message);
}

void starlet_IOS_DeviceIoctlvAsync(ARMul_State *state, ARMword fd, ARMword num, ARMword ins, ARMword outs, ARMword pvecs, ARMword queue, ARMword message)
{
	struct {
		ARMword offset, len;
	} vecs[256];
	int i;
	
	printf("[%s] IOS_IoctlvAsync(%d, 0x%x, %d, %d, 0x%08x, %08x, %08x)\n", 
		get_context(state), fd, num, ins, outs, pvecs, queue, message);
	if((ins+outs) < 256) {
		for(i=0;i<(ins+outs);i++) {
			vecs[i].offset = ARMul_ReadWord(state, pvecs+8*i);
			vecs[i].len = ARMul_ReadWord(state, pvecs+8*i+4);
			printf("             %3d [%c]: 0x%08x [0x%x]\n", i, (i<ins)?'I':'O', vecs[i].offset, vecs[i].len);
		}
	}
}

void starlet_IOS_SetUID(ARMul_State *state, ARMword uid)
{
	printf("[%s] IOS_SetUID(%d)\n", get_context(state), uid);
}

void starlet_IOS_SetGID(ARMul_State *state, ARMword gid)
{
	printf("[%s] IOS_SetGID(%d)\n", get_context(state), gid);
}

/* key type sizes
	0-0  0x10
	0-1  0x14
	0-4  0x1E
	1-2  0x100
	1-3  0x200
	1-4  0x3C
	2-4  0x5A
	3-5  0 (imm 32bit keyid only)
	3-6  0 (imm 32bit keyid only)
	
*/

int get_key_size(ARMword type, ARMword subtype)
{
	switch((type<<4) | subtype) {
		case 0x00: return 0x10;
		case 0x01: return 0x14;
		case 0x04: return 0x1e;
		case 0x12: return 0x100;
		case 0x13: return 0x200;
		case 0x14: return 0x3c;
		case 0x24: return 0x5a;
		case 0x35: return 0;
		case 0x36: return 0;
		default: return -1;
	}
}

void starlet_IOS_CreateKey(ARMul_State *state, ARMword keyid_p, ARMword type, ARMword subtype)
{
	printf("[%s] IOS_MakeKey(0x%08x,%d,%d) (size:0x%x)\n", get_context(state), keyid_p, type, subtype, get_key_size(type, subtype));
}

void starlet_IOS_RemoveKey(ARMul_State *state, ARMword keyid)
{
	printf("[%s] IOS_RemoveKey(%d)\n", get_context(state), keyid);
}

void starlet_IOS_LoadDecryptKey(ARMul_State *state, ARMword keyid, ARMword r1, ARMword deckey, ARMword r3, ARMword r4, ARMword r5, ARMword r6)
{
	printf("[%s] IOS_LoadDecryptKey(%d, 0x%08x, %d, %d, 0x%08x, 0x%08x, 0x%08x)\n", get_context(state), keyid, r1, deckey, r3, r4, r5, r6);
}

void starlet_IOS_VerifyLoadCert(ARMul_State *state, ARMword addr, ARMword cakey, ARMword destkey)
{
	if(cakey == 0xfffffff)
		printf("[%s] IOS_VerifyLoadCert(0x%08x,ROOT,%d)\n", get_context(state), addr, destkey);
	else
		printf("[%s] IOS_VerifyLoadCert(0x%08x,%d,%d)\n", get_context(state), addr, cakey, destkey);
}

void starlet_IOS_VerifyHash(ARMul_State *state, ARMword hash, ARMword hashlen, ARMword certkey, ARMword signature)
{
	printf("[%s] IOS_VerifyHash(0x%08x,0x%x,%d,0x%08x)\n", get_context(state), hash, hashlen, certkey, signature);
}


void starlet_IOS_GetKeyID(ARMul_State *state, ARMword id, ARMword ptr)
{
	printf("[%s] IOS_GetKeyID(%d,0x%x)\n", get_context(state), id, ptr);
}

void starlet_IOS_SHA1(ARMul_State *state, ARMword pctxt, ARMword pdata, ARMword len, ARMword mode, ARMword phash)
{
	char *smodes[] = {"INITIALIZE", "CONTINUE", "FINALIZE"};
	char *smode;
	
	if(mode<3) smode = smodes[mode];
	else smode = "UNKNOWN";
	
	printf("[%s] IOS_SHA1(0x%08x, 0x%08x, 0x%x, %s, 0x%08x)\n", get_context(state),pctxt,pdata,len,smode,phash);

}

void starlet_IOS_SHA1Async(ARMul_State *state, ARMword pctxt, ARMword pdata, ARMword len, ARMword mode, ARMword phash, ARMword queue, ARMword message)
{
	char *smodes[] = {"INITIALIZE", "CONTINUE", "FINALIZE"};
	char *smode;
	
	if(mode<3) smode = smodes[mode];
	else smode = "UNKNOWN";
	
	printf("[%s] IOS_SHA1Async(0x%08x, 0x%08x, 0x%x, %s, 0x%08x, %d, 0x%08x)\n", get_context(state),pctxt,pdata,len,smode,phash,queue,message);

}

void starlet_IOS_AesDecrypt(ARMul_State *state, ARMword key, ARMword piv, ARMword psrc, ARMword len, ARMword pdest)
{
	printf("[%s] IOS_AesDecrypt(%d, 0x%08x, 0x%08x, 0x%x, 0x%08x)\n", get_context(state),
		key,piv,psrc,len,pdest);
	
}
void starlet_IOS_AesEncrypt(ARMul_State *state, ARMword key, ARMword piv, ARMword psrc, ARMword len, ARMword pdest)
{
	printf("[%s] IOS_AesEncrypt(%d, 0x%08x, 0x%08x, 0x%x, 0x%08x)\n", get_context(state),
		key,piv,psrc,len,pdest);
	
}
void starlet_IOS_AesDecryptAsync(ARMul_State *state, ARMword key, ARMword piv, ARMword psrc, ARMword len, ARMword pdest, ARMword queue, ARMword message)
{
	printf("[%s] IOS_AesDecryptAsync(%d, 0x%08x, 0x%08x, 0x%x, 0x%08x, %d, 0x%08x)\n", get_context(state),
		key,piv,psrc,len,pdest,queue,message);
	
}
void starlet_IOS_AesEncryptAsync(ARMul_State *state, ARMword key, ARMword piv, ARMword psrc, ARMword len, ARMword pdest, ARMword queue, ARMword message)
{
	printf("[%s] IOS_AesEncryptAsync(%d, 0x%08x, 0x%08x, 0x%x, 0x%08x, %d, 0x%08x)\n", get_context(state),
		key,piv,psrc,len,pdest,queue,message);
	
}

void starlet_IOS_HMAC(ARMul_State *state, ARMword pctxt, ARMword bufa, ARMword alen, ARMword bufb, ARMword blen, ARMword key, ARMword mode, ARMword hmacout)
{
	char *smodes[] = {"INITIALIZE", "CONTINUE", "FINALIZE"};
	char *smode;
	
	if(mode<3) smode = smodes[mode];
	else smode = "UNKNOWN";
	
	printf("[%s] IOS_HMAC(0x%08x, 0x%08x, 0x%x, 0x%08x, 0x%x, %d, %s, 0x%08x)\n",
		get_context(state), pctxt, bufa, alen, bufb, blen, key, smode, hmacout);
	
}

void starlet_IOS_HMACAsync(ARMul_State *state, ARMword pctxt, ARMword bufa, ARMword alen, ARMword bufb, ARMword blen, ARMword key, ARMword mode, ARMword hmacout, ARMword queue, ARMword message)
{
	char *smodes[] = {"INITIALIZE", "CONTINUE", "FINALIZE"};
	char *smode;
	
	if(mode<3) smode = smodes[mode];
	else smode = "UNKNOWN";
	
	printf("[%s] IOS_HMACAsync(0x%08x, 0x%08x, 0x%x, 0x%08x, 0x%x, %d, %s, %08x, %d, 0x%08x)\n",
		get_context(state), pctxt, bufa, alen, bufb, blen, key, smode, hmacout, queue, message);
		
}


void starlet_IOS_DCacheInval(ARMul_State *state, ARMword base, ARMword length)
{
	//printf("[STARLET] IOS_DCacheInval(0x%08x, 0x%x)\n", base, length);
}

void starlet_IOS_DCacheFlush(ARMul_State *state, ARMword base, ARMword length)
{
	//printf("[STARLET] IOS_DCacheFlush(0x%08x, 0x%x)\n", base, length);
}

void starlet_IOS_AHBMemFlush(ARMul_State *state, ARMword type)
{
	//printf("[STARLET] IOS_AHBMemFlush(%d)\n", type);
}

void starlet_IOS_SomeOtherFlush(ARMul_State *state, ARMword type)
{
	//printf("[STARLET] IOS_SomeOtherFlush(%d)\n", type);
}

void starlet_IOS_unknown(ARMul_State *state, int syscall, int nparms)
{
	switch(nparms)
	{
		case 0:
			printf("[%s] undefined syscall 0x%02x (void) at 0x%08x.\n", get_context(state), syscall, state -> Reg[14]);
			break;
		case 1:
			printf("[%s] undefined syscall 0x%02x (%08x) at 0x%08x.\n", get_context(state), syscall, state -> Reg[0], state -> Reg[14]);
			break;
		case 2:
			printf("[%s] undefined syscall 0x%02x (%08x, %08x) at 0x%08x.\n", get_context(state), syscall,
				   state -> Reg[0], state -> Reg[1], state -> Reg[14]);
			break;
		case 3:
			printf("[%s] undefined syscall 0x%02x (%08x, %08x, %08x) at 0x%08x.\n", get_context(state), syscall,
				   state -> Reg[0], state -> Reg[1], state -> Reg[2], state -> Reg[14]);
			break;
		default:
			printf("[%s] undefined syscall 0x%02x (%08x, %08x, %08x, %08x) at 0x%08x.\n", get_context(state), syscall,
				   state -> Reg[0], state -> Reg[1], state -> Reg[2], state -> Reg[3], state -> Reg[14]);
	}
}

ARMword starlet_stack(ARMul_State *state, int num)
{
	return ARMul_ReadWord(state, state->Reg[13]+num*4);
}

#define SYSCALL_BOUNDARY 0x60

enum high_syscalls {
	SC_IGNORE = 0x100,
	SC_SHA1,
	SC_SHA1ASYNC,
	SC_AESDECRYPT,
	SC_AESENCRYPT,
	SC_AESDECRYPTASYNC,
	SC_AESENCRYPTASYNC,
	SC_HMAC,
	SC_HMACASYNC,
	SC_VERIFYLOADCERT,
	SC_VERIFYHASH,
	SC_CREATEKEY,
	SC_REMOVEKEY,
	SC_LOADDECRYPTKEY,
	SC_GETKEYID,
};

int syscalls_boot2v2[0x100] = {
	// shut these up
	[0x68] SC_AESDECRYPTASYNC,
	[0x6b] SC_HMAC,
	[0x6c] SC_HMACASYNC,
};

int syscalls_IOS30[0x100] = {
	[0x5b] SC_CREATEKEY,
	[0x5c] SC_REMOVEKEY,
	[0x5d] SC_LOADDECRYPTKEY,
	[0x63] SC_GETKEYID,
	[0x66] SC_SHA1ASYNC,
	[0x67] SC_SHA1,
	[0x68] SC_AESENCRYPTASYNC,
	[0x69] SC_AESENCRYPT,
	[0x6a] SC_AESDECRYPTASYNC,
	[0x6b] SC_AESDECRYPT,
	[0x6c] SC_VERIFYHASH,
	[0x6d] SC_HMAC,
	[0x6e] SC_HMACASYNC,
	[0x6f] SC_VERIFYLOADCERT,
	
};

int disabled_syscalls[0x200] = {
	[SC_AESDECRYPTASYNC] = 1,
	[SC_HMAC] = 1,
	[SC_HMACASYNC] = 1,
};

int starlet_undefined_trap (ARMul_State * state, ARMword instr) {

	int syscall;
	char *pstr;
	static char lbp='\n';
	starlet_print_context(state);
	//printf("@%08x: instr = %08x\n", ARMul_GetPC(state), instr);
	if((instr & 0xFFFF001F) == 0xe6000010) {
		syscall = (instr >> 5) & 0xFF;
//		printf("syscall=%x\n", syscall);
		if(lbp != '\n') { //nintendo sometimes forgets the newlines
			printf("\n");
			lbp = '\n';
		}
		int iosversion = mem_read_word(state, 0x3140) >> 16;
		int *sctab = NULL;
		switch(iosversion) {
			case 30:
				sctab = syscalls_IOS30;
				break;
			case 0:
				sctab = syscalls_boot2v2;
				break;
		}
		if(sctab && syscall < 0x100 && sctab[syscall])
			syscall = sctab[syscall];
		if(syscall < 0x200 && disabled_syscalls[syscall])
			syscall = SC_IGNORE;
		switch(syscall) {
		
			case 0x00:
				starlet_IOS_CreateThread(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3]);
				break;
			case 0x01:
				starlet_IOS_JoinThread(state, state->Reg[0]);
				break;
			case 0x02:
				starlet_IOS_ExitThread(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x03:
				starlet_IOS_GetTID(state);
				break;
			case 0x04:
				starlet_IOS_GetPID(state);
				break;
			case 0x05:
				starlet_IOS_ContinueThread(state, state->Reg[0]);
				break;
			case 0x06:
				starlet_IOS_StopThread(state, state->Reg[0]);
				break;
			case 0x07:
				starlet_IOS_YieldThread(state, state->Reg[0]);
				break;
			case 0x08:
				starlet_IOS_ThreadSetPriority(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x09:
				starlet_IOS_ThreadSetPriority(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x0a:
				starlet_IOS_CreateQueue(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x0b:
				starlet_IOS_DestroyQueue(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x0e:
				starlet_IOS_ReadQueue(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x0f:
				starlet_IOS_RegisterEvent(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x11:
				starlet_IOS_CreateTimer(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3]);
				break;
			case 0x12:
				starlet_IOS_RestartTimer(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x13:
				starlet_IOS_StopTimer(state, state->Reg[0]);
				break;
			case 0x14:
				starlet_IOS_DestroyTimer(state, state->Reg[0]);
				break;
			case 0x16:
				starlet_IOS_CreateHeap(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x18:
				starlet_IOS_Allocate(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x19:
				starlet_IOS_AllocateAligned(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x1a:
				starlet_IOS_Free(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x1b:
				starlet_IOS_DeviceRegister(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x1c:
				starlet_IOS_DeviceOpen(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x1d:
				starlet_IOS_DeviceClose(state, state->Reg[0]);
				break;
			case 0x1e:
				starlet_IOS_DeviceRead(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x1f:
				starlet_IOS_DeviceWrite(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x20:
				starlet_IOS_DeviceSeek(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x21:
				starlet_IOS_DeviceIoctl(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3], starlet_stack(state,0), starlet_stack(state,1));
				break;
			case 0x22:
				starlet_IOS_DeviceIoctlv(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
									 starlet_stack(state,0));
				break;
			case 0x23:
				starlet_IOS_DeviceOpenAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3]);
				break;
			case 0x24:
				starlet_IOS_DeviceCloseAsync(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case 0x25:
				starlet_IOS_DeviceReadAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3], starlet_stack(state,0));
				break;
			case 0x26:
				starlet_IOS_DeviceWriteAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3], starlet_stack(state,0));
				break;
			case 0x27:
				starlet_IOS_DeviceSeekAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3], starlet_stack(state,0));
				break;
			case 0x28:
				starlet_IOS_DeviceIoctlAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3], starlet_stack(state,0), starlet_stack(state,1), starlet_stack(state,2), starlet_stack(state,3));
				break;
			case 0x29:
				starlet_IOS_DeviceIoctlvAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
									 starlet_stack(state,0), starlet_stack(state,1), starlet_stack(state,2));
				break;
			case 0x2a:
				starlet_IOS_AckMessage(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x2b:
				starlet_IOS_SetUID(state, state->Reg[0]);
				break;
			case 0x2d:
				starlet_IOS_SetGID(state, state->Reg[0]);
				break;
			case 0x2f:
				starlet_IOS_AHBMemFlush(state, state->Reg[0]);
				break;
			case 0x30:
				starlet_IOS_SomeOtherFlush(state, state->Reg[0]);
				break;
			case 0x3f:
				starlet_IOS_DCacheInval(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x40:
				starlet_IOS_DCacheFlush(state, state->Reg[0], state->Reg[1]);
				break;
			case 0x4f:
				break; //annoying shit
			case SC_CREATEKEY:
				starlet_IOS_CreateKey(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case SC_REMOVEKEY:
				starlet_IOS_RemoveKey(state, state->Reg[0]);
				break;
			case SC_LOADDECRYPTKEY:
				starlet_IOS_LoadDecryptKey(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
										   starlet_stack(state,0), starlet_stack(state,1), starlet_stack(state,2));
				break;
			case SC_GETKEYID:
				starlet_IOS_GetKeyID(state, state->Reg[0], state->Reg[1]);
				break;
			case SC_VERIFYHASH:
				starlet_IOS_VerifyHash(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3]);
				break;
			case SC_VERIFYLOADCERT:
				starlet_IOS_VerifyLoadCert(state, state->Reg[0], state->Reg[1], state->Reg[2]);
				break;
			case SC_SHA1:
				starlet_IOS_SHA1(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
										 starlet_stack(state,0));
				break;
			case SC_SHA1ASYNC:
				starlet_IOS_SHA1(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
										 starlet_stack(state,0));
				break;
			case SC_AESDECRYPTASYNC:
				starlet_IOS_AesDecryptAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
									   starlet_stack(state,0), starlet_stack(state,1), starlet_stack(state,2));
				break;
			case SC_AESDECRYPT:
				starlet_IOS_AesDecrypt(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
									   starlet_stack(state,0));
				break;
			case SC_AESENCRYPTASYNC:
				starlet_IOS_AesEncryptAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
									   starlet_stack(state,0), starlet_stack(state,1), starlet_stack(state,2));
				break;
			case SC_AESENCRYPT:
				starlet_IOS_AesEncrypt(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
									   starlet_stack(state,0));
				break;
			case SC_HMAC:
				starlet_IOS_HMAC(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
								 starlet_stack(state,0), starlet_stack(state,1), starlet_stack(state,2),
								 starlet_stack(state,3));
				break;
			case SC_HMACASYNC:
				starlet_IOS_HMACAsync(state, state->Reg[0], state->Reg[1], state->Reg[2], state->Reg[3],
									  starlet_stack(state,0), starlet_stack(state,1), starlet_stack(state,2),
									  starlet_stack(state,3), starlet_stack(state,4), starlet_stack(state,5));
				break;
			case SC_IGNORE:
				break;
			case 0x32:
			case 0x46:
			case 0x50:
			case 0x52:
				starlet_IOS_unknown(state, syscall, 1);
				break;
			default:
				starlet_IOS_unknown(state, syscall, 4);
				break;
		}
		return 0;
	} else if((instr & 0xFF000000) == 0xEF000000) {
		syscall = instr & 0xFFFFFF;
		switch(syscall) {
			case 0xAB: // semihosting (thumb)
			case 0x12345: // semihosting (arm)
			case 0x123456: // semihosting (arm)
				switch(state->Reg[0]) {
					case 3:
						lbp = ARMul_LoadByte(state, state->Reg[1]);
						putchar(lbp);
						fflush(stdout);
						break;
					case 4:
						pstr = starlet_get_string(state, state->Reg[1]);
						printf("%s",pstr);
						fflush(stdout);
						lbp = pstr[strlen(pstr)-1];
						break;
					default:
						printf("[STARLET] UNIMPLEMENTED: semihosting call %08x (%08x, %08x, %08x)\n",
							   state -> Reg[0], state -> Reg[1], state -> Reg[2], state -> Reg[3]);
				}
				break;
			default:
				printf("[STARLET] UNIMPLEMENTED: swi 0x%06x (%08x, %08x, %08x, %08x)\n", syscall,
					   state -> Reg[0], state -> Reg[1], state -> Reg[2], state -> Reg[3]);
				return 0;
		}
		return 1;
	} else {
		return 0;
	}
}

/* Machine Initialization */
#define MACH_IO_DO_CYCLE_FUNC(f)	((void (*)(void*))(f))
#define MACH_IO_RESET_FUNC(f)		((void (*)(void*))(f))
#define MACH_IO_READ_FUNC(f)		((uint32_t (*)(void*, uint32_t))(f))
#define MACH_IO_WRITE_FUNC(f)		((void (*)(void*, uint32_t, uint32_t))(f))
#define MACH_IO_UPDATE_INT_FUNC(f)	((void (*)(void*))(f))



void starlet_mach_init(ARMul_State *state, machine_config_t *this_mach)
{
	if (state->bigendSig != HIGH) {
		PRINT("*** ERROR: you need -b for big-endian\n");
		skyeye_exit(-1);
	}
	memset(hollywood_port_state, 0, sizeof hollywood_port_state);
	
	ARMul_SelectProcessor(state, ARM_v5_Prop);
	state->lateabtSig = HIGH;

	this_mach->mach_io_do_cycle = MACH_IO_DO_CYCLE_FUNC(starlet_io_do_cycle);
	this_mach->mach_io_reset = MACH_IO_RESET_FUNC(starlet_io_reset);

	this_mach->mach_io_read_word = MACH_IO_READ_FUNC(starlet_io_read_word);
	this_mach->mach_io_read_halfword = MACH_IO_READ_FUNC(starlet_io_read_halfword);
	this_mach->mach_io_read_byte = MACH_IO_READ_FUNC(starlet_io_read_byte);
	this_mach->mach_io_write_word = MACH_IO_WRITE_FUNC(starlet_io_write_word);
	this_mach->mach_io_write_halfword = MACH_IO_WRITE_FUNC(starlet_io_write_halfword);
	this_mach->mach_io_write_byte = MACH_IO_WRITE_FUNC(starlet_io_write_byte);

	this_mach->mach_update_int = MACH_IO_UPDATE_INT_FUNC(starlet_update_int);

	this_mach->mach_set_intr = starlet_set_intr;
	this_mach->mach_pending_intr = starlet_pending_intr;
	this_mach->mach_update_intr = starlet_update_intr;
	
	state->undf_trap = starlet_undefined_trap;
	
	this_mach->state = (void*)state;

	starlet_sdhc_init(state);
	printf("Starlet Init\n");

}

