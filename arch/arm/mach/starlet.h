/************************************************************************/
/*		STARLET Special Function Register Definition		*/
/************************************************************************/

#ifndef __STARLET_H__
#define __STARLET_H__

#define REGBASE         0x0D800000
#define REGL(addr)      (REGBASE+addr)
#define REGW(addr)      (REGBASE+addr)
#define REGB(addr)      (REGBASE+addr)

#define INT_NAND 0x01
#define INT_POWER 0x0b
#define INT_RESET 0x11

#endif /*__STARLET_H___*/

