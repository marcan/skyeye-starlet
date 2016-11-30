#	       Makefile for SkyEye-V1.x
#------------------------------------------------------------------------	       
#    Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.
#    Written by Cygnus Support.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
# -------------------------------------------------------------------------
# Author Chen Yu <yuchen@tsinghua.edu.cn>

# Cross Compile:
# --------------
# Developers can try cross-compile to check whether the changes
# provide SkyEye to work on other platforms. For example:
#
# 1. MinGW cross-compile on Linux
#	$ OSTYPE=msys make CROSS_COMPILE=i586-mingw32msvc- NO_BFD=1
#
# 2. CygWin cross-compile on Linux
#	$ OSTYPE=cygwin make CROSS_COMPILE=i586-cygwin- \
#		EXTRA_CFLAGS="-I /opt/cygwin/include/w32api" \
#		EXTRA_LIBS="-L /opt/cygwin/lib/w32api" \
#		NO_BFD=1
#
# Extra CFLAGS/LIBS:
# ------------------
# Developers can specify the extra CFLAGS/LIBS as using nonstandard platform.
# For example:
#
#	$ make CROSS_COMPILE=i386-linux-uclibc- \
#		EXTRA_LIBS="-L /usr/local/uclibc-0.9.28-3/i386/lib -lgcc_s"
#

NO_DBCT = 1
NO_BFD = 1
NO_NET = 1
NO_LCD = 1

CROSS_COMPILE ?=
EXTRA_CFLAGS ?=
EXTRA_LIBS ?= -lcrypto

CC ?= $(CROSS_COMPILE)gcc
CXX ?= $(CROSS_COMPILE)g++
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
SUFFIX =

AR_FLAGS = rc

# Anthony Lee 2007-04-02: no $(OSTYPE), we should find it for native compilation: Ideas by Y.Wang
ifndef OSTYPE
	ifeq ($(findstring CYGWIN,$(shell uname -a)),CYGWIN)
		OSTYPE=cygwin
	endif

	ifeq ($(findstring MINGW32,$(shell uname -a)),MINGW32)
		OSTYPE=msys
	endif

	ifeq ($(shell uname -s),BeOS)
		OSTYPE=beos
	else
		ifeq ($(shell uname -m),BePC)
			OSTYPE=beos
		endif
	endif
endif # !OSTYPE

ifneq ($(OSTYPE),beos)
	EXTRA_LIBS += -lm
endif

# Anthony Lee 2006-09-18 : LCD defines
HAS_LCD = 0
ifndef NO_LCD
	ifneq ($(shell pkg-config --atleast-version=2.0.0 gtk+-2.0 > /dev/null 2>&1 || echo "false"),false)
			GTK_LCD = 1
			HAS_LCD = 1
	endif
	
	ifeq ($(OSTYPE),msys)
		WIN32_LCD = 1
		GTK_LCD =0 # WIN32_LCD can work with itself
		HAS_LCD = 1
	endif
		
	ifeq ($(OSTYPE),cygwin)
		WIN32_LCD = 1
		GTK_LCD = 0  # WIN32_LCD can work with itself
		HAS_LCD = 1
	endif
	
	ifeq ($(OSTYPE),beos)
		BEOS_LCD = 1
		HAS_LCD = 1
	endif
	ifeq ($(HAS_LCD),0)
		NO_LCD = 1
	endif
endif # !NO_LCD

SUPPORT_ARCH_DEF = -DARM 

ARCH_ARM_CFLAGS = -I arch/arm -I arch/arm/common -I arch/arm/common/mmu -I arch/arm/mach
ARCH_BLACKFIN_CFLAGS = -I arch/bfin/common -I arch/bfin/mach
ARCH_COLDFIRE_CFLAGS = -I arch/coldfire/common
ARCH_MIPS_CFLAGS = -I arch/mips/common
ARCH_PPC_CFLAGS = -I arch/ppc/common
DEVICE_CFLAGS = -I device -I device/net -I device/lcd -I device/flash -I device/uart -I device/nandflash
UTILS_CFLAGS = -I utils -I utils/share -I utils/main -I utils/config -I utils/debugger

SIM_EXTRA_CFLAGS = -DMODET $(ARCH_ARM_CFLAGS) $(ARCH_BLACKFIN_CFLAGS) $(ARCH_COLDFIRE_CFLAGS) $(ARCH_PPC_CFLAGS) $(DEVICE_CFLAGS) $(UTILS_CFLAGS) 
SIM_EXTRA_LIBS = $(EXTRA_LIBS)

# Determine extra flags/libs on system
ifeq ($(OSTYPE),msys)
	SUFFIX = .exe
	# for MinGW: winsock2, msvcrt, winmm
	SIM_EXTRA_LIBS += -lmsvcrt -lws2_32 -lwinmm
	# for GTK+
	EXTRA_CFLAGS += -mms-bitfields
else
ifeq ($(OSTYPE),cygwin)
	SUFFIX = .exe
	# for CygWin: Win32API
	ifeq ($(CROSS_COMPILE),)
		SIM_EXTRA_LIBS += -L /usr/lib/w32api
	endif
	SIM_EXTRA_LIBS += -lwinmm
	# for GTK+
	EXTRA_CFLAGS += -mms-bitfields
else
ifeq ($(OSTYPE),beos)
	# We still use gcc-2.95 to compile c++ files to provide linking,
	# because there are different symbol between gcc-2.x and gcc-3.x.
	ifeq ($(shell test \"`uname -r`\" == \"5.0\" > /dev/null 2>&1 || echo "true"),true)
		# BeOS Bone/Dano or ZETA OS
		SIM_EXTRA_LIBS += -L /boot/beos/system/lib -lbe -lroot -lsocket
		EXTRA_CFLAGS += -I /boot/develop/headers/be/bone
		CXX = /boot/develop/bin/g++
	else
		# BeOS R5.0.x
		SIM_EXTRA_LIBS += -L /boot/beos/system/lib -lbe -lroot -lnet
		CXX = /boot/develop/tools/gnupro/bin/g++
	endif
	EXTRA_CFLAGS += -I '$(shell pwd)/utils/portable/beos'
	EXTRA_CFLAGS += $(foreach path, $(subst ;, , $(BEINCLUDES)), $(addprefix -I , $(path)))
endif
endif
endif

CFLAGS = -fdiagnostics-show-option -Wall -Wno-error=unused-function -Wno-error=unused-label -Wno-error=unused-variable -Wno-error=format -g -O2 -D_FILE_OFFSET_BITS=64 -DSTANDALONE -DDEFAULT_INLINE=0 -DMODET $(EXTRA_CFLAGS) $(SIM_EXTRA_CFLAGS) $(SUPPORT_ARCH_DEF) -I.

ARM_COMMON_PATH= arch/arm/common
ARM_DBCT_PATH= arch/arm/dbct
ARM_MACH_PATH= arch/arm/mach

BFIN_COMMON_PATH= arch/bfin/common
BFIN_MACH_PATH=arch/bfin/mach
BFIN_DBCT_PATH=arch/bfin/dbct

#Shi yang 2006-08-23
MIPS_COMMON_PATH= arch/mips/common
MIPS_MACH_PATH= arch/mips/mach

UTILS_PATH =utils
DEVICE_PATH=device
#ARM2X86_C_FILES = arm2x86.c arm2x86_dp.c arm2x86_mem.c arm2x86_movl.c arm2x86_mul.c arm2x86_other.c arm2x86_psr.c arm2x86_shift.c arm2x86_test.c arm2x86_coproc.c tb.c
ARM2X86_H_FILES = $(ARM_DBCT_PATH)/arm2x86.h $(ARM_DBCT_PATH)/arm2x86_dp.h $(ARM_DBCT_PATH)/arm2x86_mem.h $(ARM_DBCT_PATH)/arm2x86_movl.h $(ARM_DBCT_PATH)/arm2x86_mul.h $(ARM_DBCT_PATH)/arm2x86_other.h $(ARM_DBCT_PATH)/arm2x86_psr.h $(ARM_DBCT_PATH)/arm2x86_shift.h $(ARM_DBCT_PATH)/arm2x86_test.h $(ARM_DBCT_PATH)/arm2x86_coproc.h $(ARM_DBCT_PATH)/arm2x86_self.h $(ARM_DBCT_PATH)/tb.h

ifdef NO_DBCT
	ARM2X86_O_FILES = 
	CFLAGS += -DNO_DBCT
else
	ARM2X86_O_FILES = binary/arm2x86.o \
			  binary/arm2x86_dp.o \
			  binary/arm2x86_mem.o \
			  binary/arm2x86_movl.o \
			  binary/arm2x86_mul.o \
			  binary/arm2x86_other.o \
			  binary/arm2x86_psr.o \
			  binary/arm2x86_shift.o \
			  binary/arm2x86_test.o \
			  binary/arm2x86_coproc.o \
			  binary/tb.o
	ARM2X86_CFLAGS = -DDEFAULT_INLINE=0 -DMODET -I. \
			 $(EXTRA_CFLAGS) $(ARCH_ARM_CFLAGS) \
			 $(DEVICE_CFLAGS) $(UTILS_CFLAGS) -g -O
endif

SIM_ARM_OBJS =  binary/armcopro.o binary/armemu26.o binary/armemu32.o binary/arminit.o  binary/armsupp.o binary/armos.o binary/thumbemu.o binary/armvirt.o  binary/armmmu.o binary/armmem.o binary/armio.o  binary/arm_arch_interface.o binary/armengr.o

# Disabled "armsym.o" by Lee 20071012, it seems like the "skyeye/utils/main/symbol.c"
#
#SIM_ARM_OBJS += binary/armsym.o
#

BFIN_DBCT_OBJS = binary/bfin_tb.o binary/dbct_step.o binary/bfin2x86_load_store.o binary/bfin2x86_move.o binary/bfin2x86_arith.o
BFIN_COMMON_OBJS = binary/bfin_arch_interface.o binary/iomem.o binary/bfin-dis.o
BFIN_MACH_OBJS = binary/bf533_io.o binary/bf537_io.o
SIM_BFIN_OBJS =  $(BFIN_COMMON_OBJS) $(BFIN_MACH_OBJS) \
		#$(BFIN_DBCT_OBJS)


SIM_MMU_OBJS =  binary/tlb.o binary/cache.o binary/rb.o binary/wb.o \
		binary/arm926ejs_mmu.o\

SIM_MACH_OBJS = binary/skyeye_mach_starlet.o binary/sha1.o

#Shi yang 2006-08-23
SIM_MIPS_OBJS=  binary/mips_cache.o  binary/mips_cp0.o  binary/mips_dcache.o  binary/mips_decoder.o   binary/mips_exception.o   binary/mips_interrupt.o  binary/mips_icache.o  binary/mips_arch_interface.o  binary/mips_multiply.o binary/mips_emul.o binary/mips_tlb.o  binary/mipsmem.o binary/mipsio.o binary/skyeye_mach_nedved.o binary/skyeye_mach_au1100.o

ifdef NO_NET
	SIM_NET_OBJS =
	CFLAGS += -DNO_NET
else
	SIM_NET_OBJS =  binary/skyeye_net.o \
			binary/skyeye_net_tuntap.o \
			binary/skyeye_net_vnet.o \
			binary/dev_net_rtl8019.o \
			binary/dev_net_cs8900a.o \
			binary/dev_net_s3c4510b.o
	ifeq ($(OSTYPE),msys)
		SIM_EXTRA_LIBS += -ladvapi32
	else
	ifeq ($(OSTYPE),cygwin)
		SIM_EXTRA_LIBS += -ladvapi32
	endif
	endif
endif

BFD_LIBS =

ifdef NO_BFD
	CFLAGS += -DNO_BFD
else
	# Anthony Lee 2006-09-18 : for getopt/bfd on Cygwin
	# BFD_LIBS -------------------------------- START
	ifeq ($(OSTYPE),cygwin)
	BFD_LIBS += -lc
	endif

	BFD_LIBS += -lbfd -liberty

	ifeq ($(OSTYPE),cygwin)
	BFD_LIBS += -lintl
	endif
	# BFD_LIBS -------------------------------- END
endif

ifdef STATIC
	CFLAGS += -static
endif

ifdef NO_LCD
	SIM_LCD_OBJS =  
	CFLAGS += -DNO_LCD
else
	SIM_LCD_OBJS =  binary/skyeye_lcd.o \
			binary/dev_lcd_ep7312.o \
			binary/dev_lcd_pxa.o \
			binary/dev_lcd_s3c2410.o \
			binary/dev_lcd_s3c44b0x.o \
			binary/dev_lcd_au1100.o
	ifeq ($(GTK_LCD),1)
		SIM_LCD_OBJS += binary/skyeye_lcd_gtk.o
		CFLAGS += -DGTK_LCD
		SIM_EXTRA_LIBS += $(shell pkg-config --libs gtk+-2.0)
	endif #GTK_LCD
	ifdef WIN32_LCD
		SIM_LCD_OBJS += binary/skyeye_lcd_win32.o
		CFLAGS += -DWIN32_LCD
		SIM_EXTRA_LIBS += -lgdi32 -lkernel32 -luser32
	endif #WIN32_LCD
	ifdef BEOS_LCD
		SIM_LCD_OBJS += binary/skyeye_lcd_beos.o binary/skyeye_lcd_beos_cxx.o
		CFLAGS += -DBEOS_LCD
	endif #BEOS_LCD
endif

SIM_UART_OBJS = binary/skyeye_uart.o \
		binary/skyeye_uart_stdio.o \
		binary/skyeye_uart_pipe.o \
		binary/skyeye_uart_net.o \
		binary/skyeye_uart_cvt_dcc.o

SIM_FLASH_OBJS= 
SIM_NANDFLASH_OBJS= binary/skyeye_nandflash.o \
		binary/dev_nandflash_starlet.o\
		binary/nandflash_smallblock.o\
		binary/nandflash_largeblock.o

SIM_TOUCHSCREEN_OBJS = binary/skyeye_touchscreen.o binary/dev_touchscreen_skyeye.o

SIM_DEVICE_OBJS = binary/skyeye_device.o $(SIM_NANDFLASH_OBJS)

SIM_UTILS_OBJS =	binary/skyeye2gdb.o \
			binary/gdbserver.o \
			binary/gdb_tracepoint.o \
			binary/arch_regdefs.o \
			binary/arm_regdefs.o \
			binary/skyeye_config.o \
			binary/skyeye_options.o \
			binary/skyeye.o \
			binary/skyeye_arch.o \
			binary/skyeye_portable_mman.o \
			binary/skyeye_portable_gettimeofday.o \
			binary/skyeye_portable_usleep.o
ifndef NO_BFD
ifneq ($(OSTYPE),msys)
SIM_UTILS_OBJS +=	binary/symbol.o 
endif
endif


SIM_ARM = $(SIM_ARM_OBJS) \
	$(SIM_MMU_OBJS) \
	$(ARM2X86_O_FILES) \
	$(SIM_MACH_OBJS)  \

SIM_DEV_OBJS = $(SIM_DEVICE_OBJS)

SIM_UTILS = $(SIM_UTILS_OBJS)

SIM_MIPS =$(SIM_MIPS_OBJS)


ALL_CFLAGS = $(CFLAGS)

# Anthony Lee 2007-03-28 : check scripts
CHECK_SCRIPTS =
CHECK_ENV =					\
	BINARY_DIR="$(shell pwd)/binary/"	\
	CROSS_COMPILE="$(CROSS_COMPILE)"	\
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)"		\
	EXTRA_LIBS="$(EXTRA_LIBS)"		\
	BFD_LIBS="$(BFD_LIBS)"			\
	CC=$(CC) CXX=$(CC) SUFFIX=$(SUFFIX)

ifndef NO_CHECK
	ifndef NO_GCC_CHECK
	CHECK_SCRIPTS += ./utils/scripts/check-gcc.sh
	endif

	ifndef NO_DBCT
	CHECK_SCRIPTS += ./utils/scripts/check-x86-asm.sh
	endif

	ifndef NO_BFD
	CHECK_SCRIPTS += ./utils/scripts/check-bfd.sh
	endif

	ENDIAN_CHECK_RESULT = $(shell $(CHECK_ENV) sh ./utils/scripts/check-bigendian.sh)
	ifeq ($(ENDIAN_CHECK_RESULT),yes)
		EXTRA_CFLAGS += -DHOST_IS_BIG_ENDIAN
	endif
endif


all: check binary/skyeye$(SUFFIX)

check:
ifndef NO_CHECK
	@echo "--------------------------- NOTICE ------------------------------"
	@echo "If you always get error, please run \"make NO_CHECK=1\" instead."
	@echo "-----------------------------------------------------------------"
	@echo "Checking whether host is big endian ... $(ENDIAN_CHECK_RESULT)"
	@for f in $(CHECK_SCRIPTS) .none; do \
		test "$$f" = ".none" || $(CHECK_ENV) sh $$f || exit 1; \
	done
	@echo "-----------------------------------------------------------------"
endif

clean: $(SIM_EXTRA_CLEAN)
	rm -f binary/*.o
	rm -f binary/*.a
	rm -f binary/skyeye$(SUFFIX)
	make -C arch/coldfire clean
ifeq ($(OSTYPE),beos)
	make -C utils/portable/beos/tap_driver clean EXTRA_CFLAGS="$(EXTRA_CFLAGS)"
endif

distclean mostlyclean maintainer-clean realclean: clean
	rm -f TAGS tags

binary/libdev.a: $(SIM_DEV_OBJS)
	rm -f binary/libdev.a
	$(AR) $(AR_FLAGS) binary/libdev.a $(SIM_DEV_OBJS)
	$(RANLIB) binary/libdev.a

binary/libutils.a: $(SIM_UTILS)
	rm -f binary/libutils.a
	$(AR) $(AR_FLAGS) binary/libutils.a $(SIM_UTILS)
	$(RANLIB) binary/libutils.a

binary/libarm.a: $(SIM_ARM)
	rm -f binary/libarm.a
	$(AR) $(AR_FLAGS) binary/libarm.a $(SIM_ARM)
	$(RANLIB) binary/libarm.a

#Shi yang 2006-08-23
binary/libmips.a: $(SIM_MIPS)
	rm -f binary/libmips.a
	$(AR) $(AR_FLAGS) binary/libmips.a $(SIM_MIPS)
	$(RANLIB) binary/libmips.a

#generate lib for bfin architecture
binary/libbfin.a: $(SIM_BFIN_OBJS)
	rm -f binary/libbfin.a
	$(AR) $(AR_FLAGS) binary/libbfin.a $(SIM_BFIN_OBJS)
	$(RANLIB) binary/libbfin.a

#generate lib for coldfire architecture
SIM_CF_OBJS = binary/cf_arch_interface.o binary/skyeye_mach_mcf5249.o binary/skyeye_mach_mcf5272.o
CF_PATH = arch/coldfire/
CF_COMMON_PATH = arch/coldfire/common/
CF_MACH_PATH = $(CF_PATH)/mach
COLDFIRE_FLAG = -I arch/coldire/tracer
binary/libcoldfire.a:$(SIM_CF_OBJS) 
	make -C arch/coldfire EXTRA_CFLAGS="$(EXTRA_CFLAGS)" CROSS_COMPILE=$(CROSS_COMPILE)
	$(AR) $(AR_FLAGS) binary/libcoldfire.a $(SIM_CF_OBJS) $(CF_COMMON_PATH)/*.o $(CF_PATH)/tracer/tracer.o $(CF_PATH)/i_5206/i.o
	$(RANLIB) binary/libcoldfire.a

SKYEYE_LIB=-Lbinary -larm -ldev -lutils
SKYEYE_DEPS =	binary \
		binary/libarm.a \
		binary/libdev.a \
		binary/libutils.a \
		binary/skyeye.o

# Anthony Lee 2007-03-20
ifeq ($(OSTYPE),beos)
	CC_ALL = $(CROSS_COMPILE)g++
else
	CC_ALL = $(CC)
endif

#Shi yang 2006-08-23
binary/skyeye$(SUFFIX): $(SKYEYE_DEPS)
	$(CC_ALL) $(ALL_CFLAGS) -o $@ binary/skyeye.o \
		$(SKYEYE_LIB) $(BFD_LIBS) $(SIM_EXTRA_LIBS)
	@echo "****" && \
	echo "**** The binary file located at '$@', enjoy it." && \
	echo "****"; \

binary:
	mkdir $@

binary/skyeye.o: utils/main/skyeye.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/symbol.o: utils/main/symbol.c utils/main/symbol.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/gdb_tracepoint.o: utils/debugger/gdb_tracepoint.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye2gdb.o: utils/debugger/skyeye2gdb.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/gdbserver.o: utils/debugger/gdbserver.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/arch_regdefs.o: utils/debugger/arch_regdefs.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/arm_regdefs.o: utils/debugger/arm_regdefs.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/bfin_regdefs.o: utils/debugger/bfin_regdefs.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/cf_regdefs.o: utils/debugger/cf_regdefs.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_regdefs.o: utils/debugger/ppc_regdefs.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS) $(POWERPC_FLAG)
binary/mips_regdefs.o: utils/debugger/mips_regdefs.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS) $(ARCH_MIPS_CFLAGS)

binary/skyeye_portable_%.o: $(UTILS_PATH)/portable/%.c $(UTILS_PATH)/portable/%.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@

.c.o:
	$(CC) -c $(ALL_CFLAGS) $<

binary/arm2x86.o: $(ARM_DBCT_PATH)/arm2x86.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_dp.o: $(ARM_DBCT_PATH)/arm2x86_dp.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_mem.o: $(ARM_DBCT_PATH)/arm2x86_mem.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_movl.o: $(ARM_DBCT_PATH)/arm2x86_movl.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_mul.o: $(ARM_DBCT_PATH)/arm2x86_mul.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_other.o: $(ARM_DBCT_PATH)/arm2x86_other.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_psr.o: $(ARM_DBCT_PATH)/arm2x86_psr.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_shift.o: $(ARM_DBCT_PATH)/arm2x86_shift.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_test.o: $(ARM_DBCT_PATH)/arm2x86_test.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/arm2x86_coproc.o: $(ARM_DBCT_PATH)/arm2x86_coproc.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ARM2X86_CFLAGS)
binary/tb.o: $(ARM_DBCT_PATH)/tb.c $(ARM_COMMON_PATH)/armdefs.h $(ARM2X86_H_FILES)
	$(CC) -c $< -o $@ $(ALL_CFLAGS)

binary/armemu26.o: $(ARM_COMMON_PATH)/armemu.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h $(ARM_COMMON_PATH)/armemu.h 
	$(CC) -c $< -o binary/armemu26.o  $(ALL_CFLAGS)
binary/armemu32.o: $(ARM_COMMON_PATH)/armemu.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h $(ARM_COMMON_PATH)/armemu.h
	$(CC) -c $< -o binary/armemu32.o -DMODE32 $(ALL_CFLAGS)
binary/armos.o: $(ARM_COMMON_PATH)/armos.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h $(ARM_COMMON_PATH)/armos.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armcopro.o: $(ARM_COMMON_PATH)/armcopro.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/arminit.o: $(ARM_COMMON_PATH)/arminit.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h $(ARM_COMMON_PATH)/armemu.h $(ARM_DBCT_PATH)/arm2x86.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armsupp.o: $(ARM_COMMON_PATH)/armsupp.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h $(ARM_COMMON_PATH)/armemu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/thumbemu.o: $(ARM_COMMON_PATH)/thumbemu.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h $(ARM_COMMON_PATH)/armemu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armio.o:	$(ARM_COMMON_PATH)/armio.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h 
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armmem.o:	$(ARM_COMMON_PATH)/armmem.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h $(ARM_DBCT_PATH)/tb.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armmmu.o:	$(ARM_COMMON_PATH)/armmmu.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h 
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armvirt.o:	$(ARM_COMMON_PATH)/armvirt.c $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmmu.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h 
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye_config.o: $(UTILS_PATH)/config/skyeye_config.c $(UTILS_PATH)/config/skyeye_config.h $(ARM_COMMON_PATH)/armdefs.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye_options.o: $(UTILS_PATH)/config/skyeye_options.c $(UTILS_PATH)/config/skyeye_config.h \
			$(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armmem.h $(ARM_COMMON_PATH)/armio.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye_arch.o:$(UTILS_PATH)/config/skyeye_arch.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/arm_arch_interface.o:	$(ARM_COMMON_PATH)/arm_arch_interface.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armengr.o:	$(ARM_COMMON_PATH)/armengr.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/armsym.o:	$(ARM_COMMON_PATH)/armsym.c $(ARM_COMMON_PATH)/armsym.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
#SIM_MMU_OBJS
binary/arm7100_mmu.o: $(ARM_COMMON_PATH)/mmu/arm7100_mmu.c $(ARM_COMMON_PATH)/mmu/arm7100_mmu.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/arm920t_mmu.o: $(ARM_COMMON_PATH)/mmu/arm920t_mmu.c $(ARM_COMMON_PATH)/mmu/arm920t_mmu.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/arm926ejs_mmu.o: $(ARM_COMMON_PATH)/mmu/arm926ejs_mmu.c $(ARM_COMMON_PATH)/mmu/arm926ejs_mmu.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/sa_mmu.o: $(ARM_COMMON_PATH)/mmu/sa_mmu.c $(ARM_COMMON_PATH)/mmu/sa_mmu.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/xscale_copro.o: $(ARM_COMMON_PATH)/mmu/xscale_copro.c 
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/maverick.o: $(ARM_COMMON_PATH)/mmu/maverick.c $(ARM_COMMON_PATH)/armdefs.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/tlb.o:  $(ARM_COMMON_PATH)/mmu/tlb.c $(ARM_COMMON_PATH)/mmu/tlb.h
	$(CC)  -c $< -o $@ $(ALL_CFLAGS)
binary/cache.o:        $(ARM_COMMON_PATH)/mmu/cache.c $(ARM_COMMON_PATH)/mmu/cache.h
	$(CC)  -c $< -o $@ $(ALL_CFLAGS)
binary/rb.o:   $(ARM_COMMON_PATH)/mmu/rb.c $(ARM_COMMON_PATH)/mmu/rb.h
	$(CC)  -c $< -o $@ $(ALL_CFLAGS)
binary/wb.o:   $(ARM_COMMON_PATH)/mmu/wb.c $(ARM_COMMON_PATH)/mmu/wb.h
	$(CC)  -c $< -o $@ $(ALL_CFLAGS)



#SIM_MACH_OBJS

binary/skyeye_mach_at91.o: $(ARM_MACH_PATH)/skyeye_mach_at91.c  $(UTILS_PATH)/config/skyeye_config.h \
			$(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/at91.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_ep7312.o: $(ARM_MACH_PATH)/skyeye_mach_ep7312.c $(UTILS_PATH)/config/skyeye_config.h \
			$(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/clps7110.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_lh79520.o: $(ARM_MACH_PATH)/skyeye_mach_lh79520.c $(UTILS_PATH)/config/skyeye_config.h \
			$(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/lh79520.h $(ARM_MACH_PATH)/lh79520-hardware.h \
			$(ARM_MACH_PATH)/lh79520_irq.h $(ARM_MACH_PATH)/serial_amba_pl011.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@

binary/skyeye_mach_cs89712.o: $(ARM_MACH_PATH)/skyeye_mach_cs89712.c $(UTILS_PATH)/config/skyeye_config.h \
                        $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/clps7110.h $(ARM_MACH_PATH)/ep7212.h $(ARM_MACH_PATH)/cs89712.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@

binary/skyeye_mach_ep9312.o: $(ARM_MACH_PATH)/skyeye_mach_ep9312.c $(UTILS_PATH)/config/skyeye_config.h \
			$(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/clps9312.h $(ARM_MACH_PATH)/ep9312.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_s3c4510b.o: $(ARM_MACH_PATH)/skyeye_mach_s3c4510b.c $(UTILS_PATH)/config/skyeye_config.h \
			$(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/s3c4510b.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_s3c44b0.o: $(ARM_MACH_PATH)/skyeye_mach_s3c44b0.c $(UTILS_PATH)/config/skyeye_config.h \
			$(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/s3c44b0.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_s3c44b0x.o: $(ARM_MACH_PATH)/skyeye_mach_s3c44b0x.c $(ARM_MACH_PATH)/s3c44b0.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_s3c3410x.o: $(ARM_MACH_PATH)/skyeye_mach_s3c3410x.c $(ARM_MACH_PATH)/s3c3410x.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_starlet.o: $(ARM_MACH_PATH)/skyeye_mach_starlet.c $(ARM_MACH_PATH)/starlet.h $(ARM_MACH_PATH)/sha1.h $(ARM_MACH_PATH)/sha1.c
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/sha1.o: $(ARM_MACH_PATH)/sha1.c $(ARM_MACH_PATH)/sha1.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_sa.o: $(ARM_MACH_PATH)/skyeye_mach_sa.c $(ARM_MACH_PATH)/sa1100.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_at91rm92.o: $(ARM_MACH_PATH)/skyeye_mach_at91rm92.c $(ARM_MACH_PATH)/at91rm92.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_s3c2410x.o: $(ARM_MACH_PATH)/skyeye_mach_s3c2410x.c $(ARM_MACH_PATH)/s3c2410x.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_s3c2440.o: $(ARM_MACH_PATH)/skyeye_mach_s3c2440.c $(ARM_MACH_PATH)/s3c2440.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_sharp.o: $(ARM_MACH_PATH)/skyeye_mach_sharp.c $(ARM_MACH_PATH)/sharp.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_pxa250.o: $(ARM_MACH_PATH)/skyeye_mach_pxa250.c $(ARM_MACH_PATH)/pxa.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_pxa270.o: $(ARM_MACH_PATH)/skyeye_mach_pxa270.c $(ARM_MACH_PATH)/pxa.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_lpc.o: $(ARM_MACH_PATH)/skyeye_mach_lpc.c $(ARM_MACH_PATH)/lpc.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_ns9750.o: $(ARM_MACH_PATH)/skyeye_mach_ns9750.c $(ARM_MACH_PATH)/ns9750.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_lpc2210.o: $(ARM_MACH_PATH)/skyeye_mach_lpc2210.c $(ARM_MACH_PATH)/lpc.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_integrator.o: $(ARM_MACH_PATH)/skyeye_mach_integrator.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_9238imx.o:$(ARM_MACH_PATH)/skyeye_mach_9238imx.c $(ARM_MACH_PATH)/imx-regs.h
	$(CC) $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_mach_ps7500.o: $(ARM_MACH_PATH)/skyeye_mach_ps7500.c $(UTILS_PATH)/config/skyeye_config.h \
                        $(ARM_COMMON_PATH)/armdefs.h $(ARM_COMMON_PATH)/armio.h $(ARM_MACH_PATH)/ps7500.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@

#SIM_DEVICE_OBJS
binary/skyeye_device.o: $(DEVICE_PATH)/skyeye_device.c $(DEVICE_PATH)/skyeye_device.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)

#SIM_FLASH_OBJS	
binary/armflash.o:     $(DEVICE_PATH)/flash/armflash.c $(DEVICE_PATH)/flash/armflash.h $(ARM_COMMON_PATH)/armdefs.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye_flash.o: $(DEVICE_PATH)/flash/skyeye_flash.c $(DEVICE_PATH)/flash/skyeye_flash.h $(ARM_COMMON_PATH)/armdefs.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/dev_flash_%.o:$(DEVICE_PATH)/flash/dev_flash_%.c $(DEVICE_PATH)/flash/dev_flash_%.h $(DEVICE_PATH)/flash/skyeye_flash.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)

#SIM_NANDFLASH_OBJS	
binary/skyeye_nandflash.o: $(DEVICE_PATH)/nandflash/skyeye_nandflash.c $(DEVICE_PATH)/nandflash/skyeye_nandflash.h $(ARM_COMMON_PATH)/armdefs.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/nandflash_smallblock.o: $(DEVICE_PATH)/nandflash/nandflash_smallblock.c $(DEVICE_PATH)/nandflash/nandflash_smallblock.h $(ARM_COMMON_PATH)/armdefs.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/nandflash_largeblock.o: $(DEVICE_PATH)/nandflash/nandflash_largeblock.c $(DEVICE_PATH)/nandflash/nandflash_largeblock.h $(ARM_COMMON_PATH)/armdefs.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/dev_nandflash_%.o:$(DEVICE_PATH)/nandflash/dev_nandflash_%.c $(DEVICE_PATH)/nandflash/dev_nandflash_%.h $(DEVICE_PATH)/nandflash/skyeye_nandflash.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)

#SIM_NET_OBJS
binary/dev_net_%.o: $(DEVICE_PATH)/net/dev_net_%.c $(DEVICE_PATH)/net/dev_net_%.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_net.o: $(DEVICE_PATH)/net/skyeye_net.c $(DEVICE_PATH)/net/skyeye_net.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_net_%.o: $(DEVICE_PATH)/net/skyeye_net_%.c $(DEVICE_PATH)/net/skyeye_net.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_net_tuntap.o: $(DEVICE_PATH)/net/skyeye_net_tuntap.c \
			    $(DEVICE_PATH)/net/skyeye_net_tap_win32.c \
			    $(DEVICE_PATH)/net/skyeye_net_tap_beos.c \
			    $(DEVICE_PATH)/net/skyeye_net.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@

#SIM_LCD_OBJS
binary/dev_lcd_%.o: $(DEVICE_PATH)/lcd/dev_lcd_%.c $(DEVICE_PATH)/lcd/dev_lcd_%.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_lcd.o: $(DEVICE_PATH)/lcd/skyeye_lcd.c $(DEVICE_PATH)/lcd/skyeye_lcd.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_lcd_gtk.o: $(DEVICE_PATH)/lcd/skyeye_lcd_gtk.c $(DEVICE_PATH)/lcd/skyeye_lcd.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@ `pkg-config gtk+-2.0 --cflags`
binary/skyeye_lcd_win32.o: $(DEVICE_PATH)/lcd/skyeye_lcd_win32.c $(DEVICE_PATH)/lcd/skyeye_lcd.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_lcd_beos.o: $(DEVICE_PATH)/lcd/skyeye_lcd_beos.c $(DEVICE_PATH)/lcd/skyeye_lcd_beos.h $(DEVICE_PATH)/lcd/skyeye_lcd.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_lcd_beos_cxx.o: $(DEVICE_PATH)/lcd/skyeye_lcd_beos.cpp $(DEVICE_PATH)/lcd/skyeye_lcd_beos.h $(DEVICE_PATH)/lcd/skyeye_lcd.h
	$(CXX) -g -O2 $(ALL_CFLAGS) -c $< -o $@

#SIM_UART_OBJS
binary/skyeye_uart.o: $(DEVICE_PATH)/uart/skyeye_uart.c $(DEVICE_PATH)/uart/skyeye_uart.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_uart_stdio.o: $(DEVICE_PATH)/uart/skyeye_uart_stdio.c $(DEVICE_PATH)/uart/skyeye_uart.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_uart_pipe.o: $(DEVICE_PATH)/uart/skyeye_uart_pipe.c $(DEVICE_PATH)/uart/skyeye_uart.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_uart_net.o: $(DEVICE_PATH)/uart/skyeye_uart_net.c $(DEVICE_PATH)/uart/skyeye_uart.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_uart_cvt_%.o : $(DEVICE_PATH)/uart/skyeye_uart_cvt_%.c $(DEVICE_PATH)/uart/skyeye_uart.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@

#SIM_TOUCHSCREEN_OBJS
binary/skyeye_touchscreen.o : $(DEVICE_PATH)/touchscreen/skyeye_touchscreen.c $(DEVICE_PATH)/touchscreen/skyeye_touchscreen.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/dev_touchscreen_%.o : $(DEVICE_PATH)/touchscreen/dev_touchscreen_%.c $(DEVICE_PATH)/touchscreen/skyeye_touchscreen.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@

#SIM_SOUND_OBJS
binary/skyeye_sound.o : $(DEVICE_PATH)/sound/skyeye_sound.c $(DEVICE_PATH)/sound/skyeye_sound.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/dev_sound_%.o : $(DEVICE_PATH)/sound/dev_sound_%.c $(DEVICE_PATH)/sound/skyeye_sound.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@
binary/skyeye_sound_pcm.o : $(DEVICE_PATH)/sound/skyeye_sound_pcm.c $(DEVICE_PATH)/sound/skyeye_sound.h
	$(CC) -g -O2 $(ALL_CFLAGS) -c $< -o $@

#SIM_BFIN_OBJS
binary/bf533_io.o: $(BFIN_MACH_PATH)/bf533_io.c $(BFIN_MACH_PATH)/bf533_io.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/bf537_io.o: $(BFIN_MACH_PATH)/bf537_io.c $(BFIN_MACH_PATH)/bf533_io.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@

binary/bfin-dis.o: $(BFIN_COMMON_PATH)/bfin-dis.c $(BFIN_COMMON_PATH)/bfin-sim.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/iomem.o: $(BFIN_COMMON_PATH)/iomem.c $(BFIN_COMMON_PATH)/bfin-sim.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/bfin_arch_interface.o: $(BFIN_COMMON_PATH)/bfin_arch_interface.c $(BFIN_COMMON_PATH)/bfin-sim.h
	$(CC)  $(ALL_CFLAGS) -c $< -o $@
binary/bfin_tb.o: $(BFIN_DBCT_PATH)/bfin_tb.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@
binary/dbct_step.o: $(BFIN_DBCT_PATH)/dbct_step.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@
binary/bfin2x86_load_store.o:$(BFIN_DBCT_PATH)/bfin2x86_load_store.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@
binary/bfin2x86_move.o:$(BFIN_DBCT_PATH)/bfin2x86_move.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@
binary/bfin2x86_arith.o:$(BFIN_DBCT_PATH)/bfin2x86_arith.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@
binary/cf_arch_interface.o:$(CF_COMMON_PATH)/cf_arch_interface.c
	$(CC) $(ALL_CFLAGS) $(COLDFIRE_FLAG) -c $< -o $@
binary/skyeye_mach_mcf5249.o:$(CF_MACH_PATH)/skyeye_mach_mcf5249.c
	$(CC) $(ALL_CFLAGS) $(COLDFIRE_FLAG) -c $< -o $@
binary/skyeye_mach_mcf5272.o:$(CF_MACH_PATH)/skyeye_mach_mcf5272.c
	$(CC) $(ALL_CFLAGS) $(COLDFIRE_FLAG) -c $< -o $@

binary/ppc_alu.o:$(PPC_COMMON_PATH)/ppc_alu.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_dec.o:$(PPC_COMMON_PATH)/ppc_dec.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_cpu.o:$(PPC_COMMON_PATH)/ppc_cpu.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_arch_interface.o:$(PPC_COMMON_PATH)/ppc_arch_interface.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_e500_exc.o:$(PPC_COMMON_PATH)/ppc_e500_exc.c $(PPC_COMMON_PATH)/ppc_cpu.h $(PPC_COMMON_PATH)/ppc_mmu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_fpu.o:$(PPC_COMMON_PATH)/ppc_fpu.c $(PPC_COMMON_PATH)/ppc_cpu.h 
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_mmu.o:$(PPC_COMMON_PATH)/ppc_mmu.c $(PPC_COMMON_PATH)/ppc_cpu.h $(PPC_COMMON_PATH)/ppc_mmu.h $(PPC_COMMON_PATH)/ppc_memory.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_vec.o:$(PPC_COMMON_PATH)/ppc_vec.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_opc.o:$(PPC_COMMON_PATH)/ppc_opc.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_tools.o:$(PPC_COMMON_PATH)/ppc_tools.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/ppc_io.o:$(PPC_COMMON_PATH)/ppc_io.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye_mach_mpc8560.o:$(PPC_MACH_PATH)/skyeye_mach_mpc8560.c $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS) -I$(PPC_COMMON_PATH)
binary/ppc_boot.o:$(PPC_COMMON_PATH)/ppc_boot.c $(PPC_COMMON_PATH)/ppc_boot.h $(PPC_COMMON_PATH)/ppc_cpu.h
	$(CC) -c $< -o $@ $(ALL_CFLAGS) -I$(PPC_COMMON_PATH)


#SIM_MIPS_OBJS Shi yang 2006-08-23
binary/mips_cache.o:$(MIPS_COMMON_PATH)/cache.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_cp0.o:$(MIPS_COMMON_PATH)/cp0.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_dcache.o:$(MIPS_COMMON_PATH)/dcache.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_decoder.o:$(MIPS_COMMON_PATH)/decoder.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_exception.o:$(MIPS_COMMON_PATH)/exception.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_icache.o:$(MIPS_COMMON_PATH)/icache.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_interrupt.o:$(MIPS_COMMON_PATH)/interrupt.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_arch_interface.o:$(MIPS_COMMON_PATH)/mips_arch_interface.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_multiply.o:$(MIPS_COMMON_PATH)/multiply.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_tlb.o:$(MIPS_COMMON_PATH)/tlb.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mips_emul.o:$(MIPS_COMMON_PATH)/emul.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mipsmem.o:$(MIPS_COMMON_PATH)/mipsmem.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/mipsio.o:$(MIPS_COMMON_PATH)/mipsio.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye_mach_nedved.o:$(MIPS_MACH_PATH)/skyeye_mach_nedved.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
binary/skyeye_mach_au1100.o:$(MIPS_MACH_PATH)/skyeye_mach_au1100.c
	$(CC) -c $< -o $@ $(ALL_CFLAGS)
