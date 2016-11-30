/*
        ppc_io.c - necessary arm definition for skyeye debugger
        Copyright (C) 2003-2007 Skyeye Develop Group
        for help please send mail to <skyeye-developer@lists.sf.linuxforum.net>

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
 * 04/26/2006   Michael.Kang  <blackfin.kang@gmail.com>
 */

#include <stdint.h>
uint32_t ppc_read_byte(void * state, uint32_t addr){
	return 0;
}
uint32_t ppc_read_halfword(void * state,uint32_t addr){
	return 0;
}
uint32_t ppc_read_word(void * state, uint32_t addr){
	return 0;
}
void ppc_write_byte(void * state, uint32_t addr, uint32_t data){}
void ppc_write_halfword(void * state, uint32_t addr,uint32_t data){}
void ppc_write_word(void * state,uint32_t addr,uint32_t data){}
