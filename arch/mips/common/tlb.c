#include <string.h>
#include <stdio.h>
#include "emul.h"

extern MIPS_State* mstate;

// Reset the Translation Lookaside Buffer.
void 
reset_tlb(MIPS_State* mstate)
{
    	// Clear the TLB map.
    	memset(mstate->tlb_map, 0, sizeof(mstate->tlb_map));

    	// Add all TLB entries to the last (otherwise unused) TLB chain.
	int i;
	for (i = 0; i < tlb_size; ++i) {
		mstate->tlb[i].asid = invalid_asid;
		mstate->tlb[i].index = i;
		mstate->tlb[i].hash  = tlb_map_size;

		if (mstate->tlb_map[tlb_map_size])
	    		mstate->tlb_map[tlb_map_size]->prev = &mstate->tlb[i];
		mstate->tlb[i].next = mstate->tlb_map[tlb_map_size];
		mstate->tlb[i].prev = 0;
		mstate->tlb_map[tlb_map_size] = &mstate->tlb[i];
    	}
}


// Set a TLB entry from EntryHi and EntryLo0.

void 
set_tlb_entry(MIPS_State* mstate, int n)
{
    	int hash = tlb_hash(mstate->cp0[EntryHi]);

    	if ((unsigned int)n >= tlb_size)
		return;

    	// Remove entry from the current TLB hash chain.
    	if (mstate->tlb[n].next)
		mstate->tlb[n].next->prev = mstate->tlb[n].prev;
    	if (mstate->tlb[n].prev)
		mstate->tlb[n].prev->next = mstate->tlb[n].next;
    	else
		mstate->tlb_map[mstate->tlb[n].hash] = mstate->tlb[n].next;

    	// Set the new entry.
    	mstate->tlb[n].hi = (mstate->cp0[EntryHi] & bitsmask(11, 0)); //Shi yang 2006-08-11
	mstate->tlb[n].lo = mstate->cp0[EntryLo0];
    	if (bit(mstate->cp0[EntryLo0], EntryLo_G))
		mstate->tlb[n].asid = global_asid;
    	else
		mstate->tlb[n].asid = bits(mstate->cp0[EntryHi], 11, 6);
    	mstate->tlb[n].index = n;
    	mstate->tlb[n].hash = hash;

    	// Add the entry to the new TLB hash chain.
    	if (mstate->tlb_map[hash])
		mstate->tlb_map[hash]->prev = &mstate->tlb[n];
    	mstate->tlb[n].next = mstate->tlb_map[hash];
    	mstate->tlb[n].prev = 0;
    	mstate->tlb_map[hash] = &mstate->tlb[n];
}


/* The TLB lookup routine. MIPS TLBs are fully-associative, so we use a hash
 * table to look them up. Dynamic allocation of the TLB nodes isn't needed as
 * the number of entries is fixed. This is used only by translate_vaddr() and
 * the TLBP instruction. It returns a pointer to the TLB entry or 0 on a TLB
 * miss.
 */
TLBEntry * 
probe_tlb(MIPS_State* mstate, VA va)
{
    	int hash = tlb_hash(va);
    	TLBEntry* e; 
    	for (e = mstate->tlb_map[hash]; e; e = e->next) {
		if (asid_match(mstate->asid, e->asid) && (bit(e->lo, EntryLo_G) || va_match((va & (bitsmask(11, 0))), e->hi))) //Shi yang 2006-08-11
			return e;
    	}
    	return 0;
}


// Perform a full TLB lookup.
PA 
translate_vaddr(MIPS_State* mstate, VA va, int type)
{
    	UInt32 region_type; // one of SR_UX, SR_SX or SR_KX

    	// Decode the virtual address.
    	if ((vaddr_region(va) >= vaddr_region(kuseg)) && (vaddr_region(va) < vaddr_region(kseg0))) { //KUSEG's most significant three bits are 0xx
		/* The user region is valid in every mode, but we still have
	 	 * to handle the 32/64 bit mode differences.
         	 */
		region_type = KUSEG;
    	} else if (vaddr_region(va) == vaddr_region(kseg0)) { //KSEG0's most significant three bits are 100
		if (mstate->mode & umode) { //User mode access kernel space
	    		process_address_error(type, va);
	    		return 0; // shut up the compiler
		} else {
	    		// Proceed with the TLB lookup.
	    		region_type = KSEG0;
			va = va & ~0x80000000;
			return va;
		}
    	} else if (vaddr_region(va) == vaddr_region(kseg1)) { //KSEG1's most significant three bits are 101 
		if (mstate->mode & umode) {
	    		process_address_error(type, va);
	    		return 0; // shut up the compiler
		} else {
	    		// The physical address is already encoded.
			region_type = KSEG1;
			va = va & ~0xE0000000;
	    		return va;
		}
   	 } else {
		if (mstate->mode & umode) {
	    		process_address_error(type, va);
	    		return 0; // shut up the compiler
		} else {
	    		// Proceed with the TLB lookup.
	    		region_type = KSEG2;
		}
    	}
	  fprintf(stderr, "Warning:Can not find entry for mips tlb in %s,va=0x%x,pc=0x%x\n",__FUNCTION__, va, mstate->pc);

    	// Now, we are ready to probe the TLB.
    	TLBEntry* entry = probe_tlb(mstate, va);
    	if (!entry)
		process_tlb_refill(mstate, type, va); // TLB miss
   	if(!entry){
		fprintf(stderr, "Can not find entry for mips tlb in %s,va=0x%x,pc=0x%x\n",__FUNCTION__, va, mstate->pc);
		skyeye_exit(-1);
	} 
    	// Filter TLB entries marked invalid.
    	UInt32 lo = entry->lo;
    	if (!bit(lo, EntryLo_V))
		process_tlb_invalid(mstate, type, va); // TLB invalid.

    	// Fiter TLB entries marked read-only.
    	if (type == data_store && !bit(lo, EntryLo_D))
		process_tlb_modified(mstate, va); // TLB Modified exception.

    	// Finally, retrieve the mapping.
    	return ((mstate->cp0[EntryLo0] & (bitsmask(11, 0))) | (va & (bitsmask(31, 12)))); //Shi yang 2006-08-11
}
