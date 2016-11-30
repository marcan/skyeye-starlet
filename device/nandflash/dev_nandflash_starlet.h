/* 
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/*
 * author bushing
 */
#ifndef __DEV_NANDFLASH_STARLET_H_
#define __DEV_NANDFLASH_STARLET_H_

//#define STARLET_NFCONF_EN          (1<<15)
//#define STARLET_NFCONF_512BYTE     (1<<14)
//#define STARLET_NFCONF_4STEP       (1<<13)
//#define STARLET_NFCONF_INITECC     (1<<12)
//#define STARLET_NFCONF_nFCE        (1<<11)
//#define STARLET_NFCONF_TACLS(x)    ((x)<<8)
//#define STARLET_NFCONF_TWRPH0(x)   ((x)<<4)
//#define STARLET_NFCONF_TWRPH1(x)   ((x)<<0)
#define NFCMD 0x0D010000	/* NAND flash configuration */
#define NFCONF 0x0D010004	/* NAND flash command set register */
#define NFADDR0   0x0D010008  /*NAND flash address set register*/
#define NFADDR1  0x0D01000C	/* NAND flash data register */
#define NFSTAT   0x0D010000    /*NAND flash operation status*/
#define NF_DATA  0x0D010010 	/* NAND flash ECC (Error Correction Code) register */
#define NF_ECC   0x0D010014
#define NF_UNK1  0x0D010018
#define NF_UNK2  0x0D01001C


typedef struct nandflash_starlet_io
{
	u32 nfconf;
	u32 nfcmd;
	u8 address[6];
	u32 nf_addr0, nf_addr1;
	u32 nf_data;
	u32 nfstat;
	u32 nf_ecc;
	u32 nf_datasize;
	u8 buf[4096];
} nandflash_starlet_io_t;


#endif //_DEV_NANDFLASH_STARLET_H_

