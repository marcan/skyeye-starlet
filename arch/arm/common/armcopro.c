/*  armcopro.c -- co-processor interface:  ARM6 Instruction Emulator.
    Copyright (C) 1994, 2000 Advanced RISC Machines Ltd.
 
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
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


#include "armdefs.h"
#include "armos.h"
#include "armemu.h"

//chy 2005-07-08
#include "ansidecl.h"

/* Dummy Co-processors.  */

static unsigned
NoCoPro3R (ARMul_State * state ATTRIBUTE_UNUSED,
	   unsigned a ATTRIBUTE_UNUSED, ARMword b ATTRIBUTE_UNUSED)
{
	return ARMul_CANT;
}

static unsigned
NoCoPro4R (ARMul_State * state ATTRIBUTE_UNUSED,
	   unsigned a ATTRIBUTE_UNUSED,
	   ARMword b ATTRIBUTE_UNUSED, ARMword c ATTRIBUTE_UNUSED)
{
	return ARMul_CANT;
}

static unsigned
NoCoPro4W (ARMul_State * state ATTRIBUTE_UNUSED,
	   unsigned a ATTRIBUTE_UNUSED,
	   ARMword b ATTRIBUTE_UNUSED, ARMword * c ATTRIBUTE_UNUSED)
{
	return ARMul_CANT;
}

/* The XScale Co-processors.  */

/* Coprocessor 14:  Performance Monitoring,  Clock and Power management,
   Software Debug.  */

/* Here's ARMulator's MMU definition.  A few things to note:
   1) It has eight registers, but only two are defined.
   2) You can only access its registers with MCR and MRC.
   3) MMU Register 0 (ID) returns 0x41440110
   4) Register 1 only has 4 bits defined.  Bits 0 to 3 are unused, bit 4
      controls 32/26 bit program space, bit 5 controls 32/26 bit data space,
      bit 6 controls late abort timimg and bit 7 controls big/little endian.  */

static ARMword MMUReg[8];

static unsigned
MMUInit (ARMul_State * state)
{
/* 2004-05-09 chy
-------------------------------------------------------------
read ARM Architecture Reference Manual
2.6.5 Data Abort
There are three Abort Model in ARM arch.

Early Abort Model: used in some ARMv3 and earlier implementations. In this
model, base register wirteback occurred for LDC,LDM,STC,STM instructions, and
the base register was unchanged for all other instructions. (oldest)

Base Restored Abort Model: If a Data Abort occurs in an instruction which
specifies base register writeback, the value in the base register is
	unchanged. (strongarm, xscale)

Base Updated Abort Model: If a Data Abort occurs in an instruction which
specifies base register writeback, the base register writeback still occurs.
(arm720T)

read PART B
chap2 The System Control Coprocessor  CP15
2.4 Register1:control register
L(bit 6): in some ARMv3 and earlier implementations, the abort model of the
processor could be configured:
0=early Abort Model Selected(now obsolete)
1=Late Abort Model selceted(same as Base Updated Abort Model)

on later processors, this bit reads as 1 and ignores writes.
-------------------------------------------------------------
So, if lateabtSig=1, then it means Late Abort Model(Base Updated Abort Model)
    if lateabtSig=0, then it means Base Restored Abort Model
because the ARMs which skyeye simulates are all belonged to  ARMv4,
so I think MMUReg[1]'s bit 6 should always be 1

*/

	MMUReg[1] = state->prog32Sig << 4 |
		state->data32Sig << 5 | 1 << 6 | state->bigendSig << 7;
	//state->data32Sig << 5 | state->lateabtSig << 6 | state->bigendSig << 7;


	ARMul_ConsolePrint (state, ", MMU present");

	return TRUE;
}

static unsigned
MMUMRC (ARMul_State * state ATTRIBUTE_UNUSED, unsigned type ATTRIBUTE_UNUSED,
	ARMword instr, ARMword * value)
{
	mmu_mrc (state, instr, value);
	return (ARMul_DONE);
}

static unsigned
MMUMCR (ARMul_State * state, unsigned type, ARMword instr, ARMword value)
{
	mmu_mcr (state, instr, value);
	return (ARMul_DONE);
}

/* What follows is the Validation Suite Coprocessor.  It uses two
   co-processor numbers (4 and 5) and has the follwing functionality.
   Sixteen registers.  Both co-processor nuimbers can be used in an MCR
   and MRC to access these registers.  CP 4 can LDC and STC to and from
   the registers.  CP 4 and CP 5 CDP 0 will busy wait for the number of
   cycles specified by a CP register.  CP 5 CDP 1 issues a FIQ after a
   number of cycles (specified in a CP register), CDP 2 issues an IRQW
   in the same way, CDP 3 and 4 turn of the FIQ and IRQ source, and CDP 5
   stores a 32 bit time value in a CP register (actually it's the total
   number of N, S, I, C and F cyles).  */

static ARMword ValReg[16];

static unsigned
ValLDC (ARMul_State * state ATTRIBUTE_UNUSED,
	unsigned type, ARMword instr, ARMword data)
{
	static unsigned words;

	if (type != ARMul_DATA)
		words = 0;
	else {
		ValReg[BITS (12, 15)] = data;

		if (BIT (22))
			/* It's a long access, get two words.  */
			if (words++ != 4)
				return ARMul_INC;
	}

	return ARMul_DONE;
}

static unsigned
ValSTC (ARMul_State * state ATTRIBUTE_UNUSED,
	unsigned type, ARMword instr, ARMword * data)
{
	static unsigned words;

	if (type != ARMul_DATA)
		words = 0;
	else {
		*data = ValReg[BITS (12, 15)];

		if (BIT (22))
			/* It's a long access, get two words.  */
			if (words++ != 4)
				return ARMul_INC;
	}

	return ARMul_DONE;
}

static unsigned
ValMRC (ARMul_State * state ATTRIBUTE_UNUSED,
	unsigned type ATTRIBUTE_UNUSED, ARMword instr, ARMword * value)
{
	*value = ValReg[BITS (16, 19)];

	return ARMul_DONE;
}

static unsigned
ValMCR (ARMul_State * state ATTRIBUTE_UNUSED,
	unsigned type ATTRIBUTE_UNUSED, ARMword instr, ARMword value)
{
	ValReg[BITS (16, 19)] = value;

	return ARMul_DONE;
}

static unsigned
ValCDP (ARMul_State * state, unsigned type, ARMword instr)
{
	static unsigned int finish = 0;

	if (BITS (20, 23) != 0)
		return ARMul_CANT;

	if (type == ARMul_FIRST) {
		ARMword howlong;

		howlong = ValReg[BITS (0, 3)];

		/* First cycle of a busy wait.  */
		finish = ARMul_Time (state) + howlong;

		return howlong == 0 ? ARMul_DONE : ARMul_BUSY;
	}
	else if (type == ARMul_BUSY) {
		if (ARMul_Time (state) >= finish)
			return ARMul_DONE;
		else
			return ARMul_BUSY;
	}

	return ARMul_CANT;
}

static unsigned
DoAFIQ (ARMul_State * state)
{
	state->NfiqSig = LOW;
	return 0;
}

static unsigned
DoAIRQ (ARMul_State * state)
{
	state->NirqSig = LOW;
	return 0;
}

static unsigned
IntCDP (ARMul_State * state, unsigned type, ARMword instr)
{
	static unsigned int finish;
	ARMword howlong;

	howlong = ValReg[BITS (0, 3)];

	switch ((int) BITS (20, 23)) {
	case 0:
		if (type == ARMul_FIRST) {
			/* First cycle of a busy wait.  */
			finish = ARMul_Time (state) + howlong;

			return howlong == 0 ? ARMul_DONE : ARMul_BUSY;
		}
		else if (type == ARMul_BUSY) {
			if (ARMul_Time (state) >= finish)
				return ARMul_DONE;
			else
				return ARMul_BUSY;
		}
		return ARMul_DONE;

	case 1:
		if (howlong == 0)
			ARMul_Abort (state, ARMul_FIQV);
		else
			ARMul_ScheduleEvent (state, howlong, DoAFIQ);
		return ARMul_DONE;

	case 2:
		if (howlong == 0)
			ARMul_Abort (state, ARMul_IRQV);
		else
			ARMul_ScheduleEvent (state, howlong, DoAIRQ);
		return ARMul_DONE;

	case 3:
		state->NfiqSig = HIGH;
		return ARMul_DONE;

	case 4:
		state->NirqSig = HIGH;
		return ARMul_DONE;

	case 5:
		ValReg[BITS (0, 3)] = ARMul_Time (state);
		return ARMul_DONE;
	}

	return ARMul_CANT;
}

/* Install co-processor instruction handlers in this routine.  */

unsigned
ARMul_CoProInit (ARMul_State * state)
{
	unsigned int i;

	/* Initialise tham all first.  */
	for (i = 0; i < 16; i++)
		ARMul_CoProDetach (state, i);

	/* Install CoPro Instruction handlers here.
	   The format is:
	   ARMul_CoProAttach (state, CP Number, Init routine, Exit routine
	   LDC routine, STC routine, MRC routine, MCR routine,
	   CDP routine, Read Reg routine, Write Reg routine).  */
		ARMul_CoProAttach (state, 4, NULL, NULL, ValLDC, ValSTC,
				   ValMRC, ValMCR, ValCDP, NULL, NULL);

		ARMul_CoProAttach (state, 5, NULL, NULL, NULL, NULL,
				   ValMRC, ValMCR, IntCDP, NULL, NULL);

		ARMul_CoProAttach (state, 15, MMUInit, NULL, NULL, NULL,
				   //                  MMUMRC, MMUMCR, NULL, MMURead, MMUWrite);
				   MMUMRC, MMUMCR, NULL, NULL, NULL);
//chy 2003-09-03 do it in future!!!!????
#if 0
	if (state->is_iWMMXt) {
		ARMul_CoProAttach (state, 0, NULL, NULL, IwmmxtLDC, IwmmxtSTC,
				   NULL, NULL, IwmmxtCDP, NULL, NULL);

		ARMul_CoProAttach (state, 1, NULL, NULL, NULL, NULL,
				   IwmmxtMRC, IwmmxtMCR, IwmmxtCDP, NULL,
				   NULL);
	}
#endif
	//-----------------------------------------------------------------------------
	//chy 2004-05-25, found the user/system code visit CP 1,2, so I add below code.
	ARMul_CoProAttach (state, 1, NULL, NULL, NULL, NULL,
			   ValMRC, ValMCR, NULL, NULL, NULL);
	ARMul_CoProAttach (state, 2, NULL, NULL, ValLDC, ValSTC,
			   NULL, NULL, NULL, NULL, NULL);
	//------------------------------------------------------------------------------
	/* No handlers below here.  */

	/* Call all the initialisation routines.  */
	for (i = 0; i < 16; i++)
		if (state->CPInit[i])
			(state->CPInit[i]) (state);

	return TRUE;
}

/* Install co-processor finalisation routines in this routine.  */

void
ARMul_CoProExit (ARMul_State * state)
{
	register unsigned i;

	for (i = 0; i < 16; i++)
		if (state->CPExit[i])
			(state->CPExit[i]) (state);

	for (i = 0; i < 16; i++)	/* Detach all handlers.  */
		ARMul_CoProDetach (state, i);
}

/* Routines to hook Co-processors into ARMulator.  */

void
ARMul_CoProAttach (ARMul_State * state,
		   unsigned number,
		   ARMul_CPInits * init,
		   ARMul_CPExits * exit,
		   ARMul_LDCs * ldc,
		   ARMul_STCs * stc,
		   ARMul_MRCs * mrc,
		   ARMul_MCRs * mcr,
		   ARMul_CDPs * cdp,
		   ARMul_CPReads * read, ARMul_CPWrites * write)
{
	if (init != NULL)
		state->CPInit[number] = init;
	if (exit != NULL)
		state->CPExit[number] = exit;
	if (ldc != NULL)
		state->LDC[number] = ldc;
	if (stc != NULL)
		state->STC[number] = stc;
	if (mrc != NULL)
		state->MRC[number] = mrc;
	if (mcr != NULL)
		state->MCR[number] = mcr;
	if (cdp != NULL)
		state->CDP[number] = cdp;
	if (read != NULL)
		state->CPRead[number] = read;
	if (write != NULL)
		state->CPWrite[number] = write;
}

void
ARMul_CoProDetach (ARMul_State * state, unsigned number)
{
	ARMul_CoProAttach (state, number, NULL, NULL,
			   NoCoPro4R, NoCoPro4W, NoCoPro4W, NoCoPro4R,
			   NoCoPro3R, NULL, NULL);

	state->CPInit[number] = NULL;
	state->CPExit[number] = NULL;
	state->CPRead[number] = NULL;
	state->CPWrite[number] = NULL;
}
