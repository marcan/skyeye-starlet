#include "instr.h"
#include "emul.h"
#include <stdio.h>

// Access to the System Coprocessor registers.

UInt32 
read_cp0(MIPS_State* mstate, int n) 
{
    	switch (n) {
    		case Random:
    		{
			return get_random(mstate);
    		}
	    	case Count:
    		{
			return mstate->cp0[Count] + ((mstate->now - mstate->count_seed) / 2);
	    	}
    		case Cause:
    		{
			return mstate->cp0[Cause] | (mstate->events & bitsmask(Cause_IP_Last, Cause_IP_First));
    		}
	    	default:
			return mstate->cp0[n];
    }
}


void 
write_cp0(MIPS_State* mstate, int n, UInt32 x)
{
    	switch (n) {
		case Index:
    		{
			mstate->cp0[Index] = clear_bits(x, 30, 17); //Shi yang 2006-08-11
			mstate->cp0[Index] = clear_bits(x, 7, 0);
			break;
    		}
	    	case Random: //Random register is a read-only register
    		{
			break;
    		}
	    	case EntryLo0:
    		{
			mstate->cp0[EntryLo0] = clear_bits(x, 7, 0); //Shi yang 2006-08-11
			break;
    		}
	    	case Context:
    		{
			mstate->cp0[Context] = clear_bits(x, 1, 0); //Shi yang 2006-08-11
			break;
    		}
	    	case BadVAddr: //BadVAddr register is a read-only register
    		{
			break;
    		}
	    	case Count:
    		{
			mstate->count_seed = mstate->now;
			mstate->cp0[Count] = x;
			mstate->now = mstate->now + (mstate->cp0[Compare] - (mstate->cp0[Count] + ((mstate->now - mstate->count_seed) / 2))) * 2;
			break;
    		}
	    	case EntryHi:
    		{
			mstate->asid = bits(x, 11, 6); //Shi yang 2006-08-11
			mstate->cp0[EntryHi] = x & (bitsmask(5, 0) |
				    	       bitsmask(vaddr_width - 1, 12) |
				    	       bitsmask(11, 6));
			break;
    		}
    		case Compare:
    		{
			//fprintf(stderr, "KSDBG: in %s,write 0x%x to compare\n", __FUNCTION__, x);
			mstate->cp0[Compare] = x;
			mstate->events = clear_bit(mstate->events, 7 + Cause_IP_First);
			mstate->now = mstate->now + (mstate->cp0[Compare] - (mstate->cp0[Count] + ((mstate->now - mstate->count_seed) / 2))) * 2;
			mstate->cp0[Cause] &= 0xFFFF7FFF; /* clear IP bit in cause register for timer */
			break;
    		}
	    	case SR:
    		{
			mstate->cp0[SR] = x & ~(bitsmask(27, 26) | bitsmask(24, 23) | bitsmask(7, 6)); //Shi yang 2006-08-11
			//leave_kernel_mode(mstate);
			break;
    		}
	    	case Cause:
    		{
			mstate->events |= x & bitsmask(Cause_IP1, Cause_IP0);
			break;
    		}
	    	case EPC:
    		{
			mstate->cp0[EPC] = x;
			break;
    		}
	    	case PRId: //PRId register is a read-only register
    		{
			break;
    		}
    		default:
			break;
    		}
}


int 
decode_cop0(MIPS_State* mstate, Instr instr)
{
	// CP0 is usable in kernel more or when the CU bit in SR is set.
    	if (!(mstate->mode & kmode) && !bit(mstate->cp0[SR], SR_CU0))
		process_coprocessor_unusable(mstate, 0);

    	/* Only COP0, MFC0 and MTC0 make sense, although the R3K
    	 * manuals say nothing about handling the others.
         */

    	if (bit(instr, 25)) {
		switch (funct(instr)) {
			case TLBR:
			{
			    	// Read Indexed TLB Entry
				TLBEntry* e = &mstate->tlb[bits(mstate->cp0[Index], 13, 6)]; //Shi yang 2006-08-11
			    	mstate->cp0[EntryHi] = e->hi | (bits(e->asid, 5, 0) << 6);
			    	mstate->cp0[EntryLo0] = e->lo;
			    	return nothing_special;
			}
			case TLBWI:
			{
			    	// Write Indexed TLB Entry
			    	set_tlb_entry(mstate, bits(mstate->cp0[Index], 13, 8)); //Shi yang 2006-08-11
			    	return nothing_special;
			}
			case TLBWR:
			{
		    		// Write Random TLB Entry
			    	set_tlb_entry(mstate, get_random(mstate));
			    	return nothing_special;
			}
			case TLBP:
			{
		    		// Probe TLB For Matching Entry
			    	VA va = mstate->cp0[EntryHi];
		    		TLBEntry* e = probe_tlb(mstate, va);
			    	mstate->cp0[Index] = (e) ? e->index : bitmask(31);
			    	return nothing_special;
			}
			case RFE: //Shi yang 2006-08-11
			{
				// Exception Return
				leave_kernel_mode(mstate);
				return nothing_special;
			}
			case ERET:
			{
				mstate->cp0[SR] &= 0xFFFFFFFD; /* Set Exl bit to zero */ 
				mstate->branch_target =  mstate->cp0[EPC];
				return branch_delay;
			}
			default:
				process_reserved_instruction(mstate);
		    		return nothing_special;
		}
    	} else {
		switch (rs(instr)) {
			case MFCz:
			{
		    		// Move From System Control Coprocessor
				mstate->gpr[rt(instr)] = read_cp0(mstate, rd(instr));
			    	return nothing_special;
			}
			case DMFCz:
			{
		    		// Doubleword Move From System Control Coprocessor
				process_reserved_instruction(mstate);
				return nothing_special;
			}
			case CFCz:
			{
		    		// Move Control From Coprocessor
			    	return nothing_special;
			}
			case MTCz:
			{
		    		// Move To System Control Coprocessor
			    	write_cp0(mstate, rd(instr), mstate->gpr[rt(instr)]);
			    	return nothing_special;
			}
			case DMTCz:
			{
			    	// Doubleword Move To System Control Coprocessor
				process_reserved_instruction(mstate);
			    	return nothing_special;
			}
			case CTCz:
			{
				process_reserved_instruction(mstate);

		    		// Move Control To Coprocessor
			    	return nothing_special;
			}
			case BCz:
			{
			    	// Branch On Coprocessor Condition
	    			switch (rt(instr)) {
				    	case BCzF:
				    	case BCzT:
				    	case BCzFL:
				    	case BCzTL:
						process_reserved_instruction(mstate);
						return nothing_special;
	    				default:
						process_reserved_instruction(mstate);
						return nothing_special;
	    			}
			}
			default:
				    	process_reserved_instruction(mstate);
					return nothing_special;
		}
    	}
    	return nothing_special;
}
int 
decode_cop1(MIPS_State* mstate, Instr instr)
{
	 fprintf(stderr, "in %s\n", __FUNCTION__); 


	// CP0 is usable in kernel more or when the CU bit in SR is set.
    	if (!(mstate->mode & kmode) && !bit(mstate->cp0[SR], SR_CU0))
		process_coprocessor_unusable(mstate, 0);

    	/* Only COP0, MFC0 and MTC0 make sense, although the R3K
    	 * manuals say nothing about handling the others.
         */
	switch (function(instr)){
				case WAIT:
			return nothing_special;
		default:
			//process_reserved_instruction(mstate);
                        break;

	}
	switch (fmt(instr)){
		case CF:		
			if(fs(instr) == 0)
				mstate->gpr[ft(instr)] = mstate->fir;
			else
				fprintf(stderr, "Not implementation for CFC1 instruction\n");
			break;
								
		default:
			process_reserved_instruction(mstate);
			break;
	}
	return nothing_special;
}

