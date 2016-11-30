#include "armdefs.h"
#include "skyeye_types.h"
#include "armemu.h"
#include "armmem.h"
ARMul_State *state;
static int verbosity;
extern int big_endian;
static int mem_size = (1 << 21);
static mem_config_t arm_mem;
static cpu_config_t *p_arm_cpu;
extern ARMword skyeye_cachetype;

int ARMul_ICE_WriteByte (ARMul_State * state, ARMword address, ARMword data);
int ARMul_ICE_ReadByte(ARMul_State * state, ARMword address, ARMword *presult);
void register_arch (arch_config_t * arch);
int split_param (const char *param, char *name, char *value);

//chy 2005-08-01, borrow from wlm's 2005-07-26's change


ARMword
ARMul_Debug (ARMul_State * state, ARMword pc, ARMword instr)
{
	return instr;
}

void
ARMul_ConsolePrint (ARMul_State * stafte, const char *format, ...)
{
}
void
ARMul_CallCheck (ARMul_State * state, ARMword cur_pc, ARMword to_pc,
		 ARMword instr)
{
}

static void
arm_reset_state ()
{
	//chy 2003-01-14 seems another ARMul_Reset, the first is in ARMul_NewState
	//chy 2003-08-19 mach_init should call ARMul_SelectProcess
	//skyeye_config.mach->mach_init (state, skyeye_config.mach);
	//chy:2003-08-19, after mach_init, because ARMul_Reset should after ARMul_SelectProcess
	ARMul_Reset (state);
	state->NextInstr = 0;
	state->Emulate = 3;

	// add step disassemble code here :teawater
	state->disassemble = skyeye_config.can_step_disassemble;
	io_reset (state);	/*from ARMul_Reset. */
}
static void
arm_init_state ()
{
	ARMul_EmulateInit ();
	state = ARMul_NewState ();
	state->cpu = p_arm_cpu;
	state->mem_bank = &arm_mem;
//chy 2005-08-01, borrow from wlm's 2005-07-26's change

	/*
	 * 2007-01-24 removed the term-io functions by Anthony Lee,
	 * moved to "device/uart/skyeye_uart_stdio.c".
	 */

	//it will be set in  skyeye_config.mach->mach_init(state, skyeye_config.mach);
	state->abort_model = 0;
//chy 2005-08-01 ---------------------------------------------

	state->bigendSig = (big_endian ? HIGH : LOW);
	ARMul_MemoryInit (state, mem_size);
	ARMul_OSInit (state);
	state->verbose = verbosity;
	/*some option should init before read config. e.g. uart option. */
//chy 2005-08-01 ---------------------------------------------
//      skyeye_option_init (&skyeye_config);
	//    skyeye_read_config ();
	//chy 2005-08-01 commit and disable ksh's energy estimantion, will be recover in the future
	/*added by ksh for energy estimation,in 2004-11-26 */
	state->energy.energy_prof = skyeye_config.energy.energy_prof;
	/*mach init */
	skyeye_config.mach->mach_init (state, skyeye_config.mach);
}
static void
arm_step_once ()
{
	//ARMul_DoInstr(state);
	state->NextInstr = RESUME;      /* treat as PC change */
	state->Reg[15] = ARMul_DoProg(state);
	FLUSHPIPE;
}
static void
arm_set_pc (WORD pc)
{
	state->Reg[15] = pc;
}
static WORD
arm_get_pc(){
	return (WORD)state->Reg[15];
}
static int
arm_ICE_write_byte (WORD addr, uint8_t v)
{
	return (ARMul_ICE_WriteByte (state, (ARMword) addr, (ARMword) v));
}
static int arm_ICE_read_byte (WORD addr, uint8_t *pv){
	ARMword data;
	int ret;
	ret = ARMul_ICE_ReadByte (state, (ARMword) addr, &data);
	*pv = (uint8_t)data ;
	return (ret);
}
extern void starlet_mach_init();

//chy 2003-08-11: the cpu_id can be found in linux/arch/arm/boot/compressed/head.S
cpu_config_t arm_cpus[] = {
	{"armv5", "arm926ejs", 0x41069260, 0xff0ffff0, INSTCACHE},
};


static int
arm_parse_cpu (const char *params[])
{
	int i;
	for (i = 0; i < (sizeof (arm_cpus) / sizeof (cpu_config_t)); i++) {
		if (!strncmp
		    (params[0], arm_cpus[i].cpu_name, MAX_PARAM_NAME)) {

			p_arm_cpu = &arm_cpus[i];
			SKYEYE_INFO ("cpu info: %s, %s, %x, %x, %x \n",
				     p_arm_cpu->cpu_arch_name,
				     p_arm_cpu->cpu_name,
				     p_arm_cpu->cpu_val,
				     p_arm_cpu->cpu_mask,
				     p_arm_cpu->cachetype);
			skyeye_cachetype = p_arm_cpu->cachetype;
			return 0;

		}
	}
	SKYEYE_ERR ("Error: Unkonw cpu name \"%s\"\n", params[0]);
	return -1;

}

machine_config_t arm_machines[] = {
	{"starlet", starlet_mach_init, NULL, NULL, NULL},	/* Nintendo Starlet */
};


static int
arm_parse_mach (machine_config_t * mach, const char *params[])
{
	int i;
	for (i = 0; i < (sizeof (arm_machines) / sizeof (machine_config_t));
	     i++) {
		if (!strncmp
		    (params[0], arm_machines[i].machine_name,
		     MAX_PARAM_NAME)) {
			skyeye_config.mach = &arm_machines[i];
			SKYEYE_INFO
				("mach info: name %s, mach_init addr %p\n",
				 skyeye_config.mach->machine_name,
				 skyeye_config.mach->mach_init);
			return 0;
		}
	}
	SKYEYE_ERR ("Error: Unkonw mach name \"%s\"\n", params[0]);

	return -1;

}

/*ywc 2005-03-30*/
extern ARMword flash_read_byte (ARMul_State * state, ARMword addr);
extern void flash_write_byte (ARMul_State * state, ARMword addr,
			      ARMword data);
extern ARMword flash_read_halfword (ARMul_State * state, ARMword addr);
extern void flash_write_halfword (ARMul_State * state, ARMword addr,
				  ARMword data);
extern ARMword flash_read_word (ARMul_State * state, ARMword addr);
extern void flash_write_word (ARMul_State * state, ARMword addr,
			      ARMword data);

static int
arm_parse_mem (int num_params, const char *params[])
{
	char name[MAX_PARAM_NAME], value[MAX_PARAM_NAME];
	int i, num;
//chy 2003-09-12, now support more io bank
//      static int io_bank_num=0;
	//state->mem_bank = (mem_config_t *)malloc(sizeof(mem_config_t));
	mem_config_t *mc = &arm_mem;
	mem_bank_t *mb = mc->mem_banks;

	mc->bank_num = mc->current_num++;

	num = mc->current_num - 1;	/*mem_banks should begin from 0. */
	mb[num].filename[0] = '\0';
	mb[num].mmap = -1;
	for (i = 0; i < num_params; i++) {
		if (split_param (params[i], name, value) < 0)
			SKYEYE_ERR
				("Error: mem_bank %d has wrong parameter \"%s\".\n",
				 num, name);

		if (!strncmp ("map", name, strlen (name))) {
			if (!strncmp ("M", value, strlen (value))) {
				mb[num].read_byte = real_read_byte;
				mb[num].write_byte = real_write_byte;
				mb[num].read_halfword = real_read_halfword;
				mb[num].write_halfword = real_write_halfword;
				mb[num].read_word = real_read_word;
				mb[num].write_word = real_write_word;
				mb[num].type = MEMTYPE_RAM;
			}
			else if (!strncmp ("A", value, strlen (value))) {
				mb[num].read_byte = alias_read_byte;
				mb[num].write_byte = alias_write_byte;
				mb[num].read_halfword = alias_read_halfword;
				mb[num].write_halfword = alias_write_halfword;
				mb[num].read_word = alias_read_word;
				mb[num].write_word = alias_write_word;
				mb[num].type = MEMTYPE_ALIAS;
			}
			else if (!strncmp ("I", value, strlen (value))) {
				mb[num].read_byte = io_read_byte;
				mb[num].write_byte = io_write_byte;
				mb[num].read_halfword = io_read_halfword;
				mb[num].write_halfword = io_write_halfword;
				mb[num].read_word = io_read_word;
				mb[num].write_word = io_write_word;
				mb[num].type = MEMTYPE_IO;

				/*ywc 2005-03-30 */
			}
			else {
				SKYEYE_ERR
					("Error: mem_bank %d \"%s\" parameter has wrong value \"%s\"\n",
					 num, name, value);
			}
		}
		else if (!strncmp ("type", name, strlen (name))) {
			//chy 2003-09-21: process type
			if (!strncmp ("R", value, strlen (value))) {
				if (mb[num].type == MEMTYPE_RAM)
					mb[num].type = MEMTYPE_ROM;
				mb[num].write_byte = warn_write_byte;
				mb[num].write_halfword = warn_write_halfword;
				mb[num].write_word = warn_write_word;
			}
		}
		else if (!strncmp ("addr", name, strlen (name))) {

			if (value[0] == '0' && value[1] == 'x')
				mb[num].addr = strtoul (value, NULL, 16);
			else
				mb[num].addr = strtoul (value, NULL, 10);

		}
		else if (!strncmp ("size", name, strlen (name))) {

			if (value[0] == '0' && value[1] == 'x')
				mb[num].len = strtoul (value, NULL, 16);
			else
				mb[num].len = strtoul (value, NULL, 10);

		}
		else if (!strncmp ("dest", name, strlen (name))) {

			if (value[0] == '0' && value[1] == 'x')
				mb[num].dest = strtoul (value, NULL, 16);
			else
				mb[num].dest = strtoul (value, NULL, 10);

		}
		else if (!strncmp ("file", name, strlen (name))) {
			strncpy (mb[num].filename, value, strlen (value) + 1);
		}
		else if (!strncmp ("mmap", name, strlen (name))) {
			if (value[0] == '0' && value[1] == 'x')
				mb[num].mmap = strtoul (value, NULL, 16);
			else
				mb[num].mmap = strtoul (value, NULL, 10);
		}
		else if (!strncmp ("boot", name, strlen (name))) {
			/*this must be the last parameter. */
			if (!strncmp ("yes", value, strlen (value)))
				skyeye_config.start_address = mb[num].addr;
		}
		else {
			SKYEYE_ERR
				("Error: mem_bank %d has unknow parameter \"%s\".\n",
				 num, name);
		}
	}

	return 0;
}

void
init_arm_arch ()
{

	static arch_config_t arm_arch;

	arm_arch.arch_name = "arm";
	arm_arch.init = arm_init_state;
	arm_arch.reset = arm_reset_state;
	arm_arch.set_pc = arm_set_pc;
	arm_arch.get_pc = arm_get_pc;
	arm_arch.step_once = arm_step_once;
	arm_arch.ICE_write_byte = arm_ICE_write_byte;
	arm_arch.ICE_read_byte = arm_ICE_read_byte;
	arm_arch.parse_cpu = arm_parse_cpu;
	arm_arch.parse_mach = arm_parse_mach;
	arm_arch.parse_mem = arm_parse_mem;

	register_arch (&arm_arch);
}
