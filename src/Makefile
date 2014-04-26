# DON, a UCI chess playing engine derived from Glaurung 2.1
# Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
# Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad
# Copyright (C) 2008-2014 Ehsan Rashid
#
# DON is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# DON is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.


### ==========================================================================
### Section 1. General Configuration
### ==========================================================================

### Establish the operating system name
UNAME = $(shell uname)

### Executable name
EXE = DON

### Installation dir definitions
PREFIX = /usr/local
# Haiku has a non-standard filesystem layout
ifeq ($(UNAME),Haiku)
	PREFIX = /boot/common
endif
BINDIR = $(PREFIX)/bin

### Built-in benchmark for pgo-builds and signature
PGOBENCH  = ./$(EXE) bench 32 1 1 time default
SIGNBENCH = ./$(EXE) bench

### Object files
OBJS = Benchmark.o BitBases.o BitBoard.o Endgame.o Engine.o Evaluator.o Main.o Material.o \
	MoveGenerator.o MovePicker.o Notation.o Pawns.o PolyglotBook.o Position.o Searcher.o  \
	Thread.o TimeManager.o Transposition.o UCI.o UCI.Option.o Zobrist.o MemoryHandler.o   \
	Debugger.o

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
#
# flag                --- Comp switch      --- Description
# ----------------------------------------------------------------------------
#
# debug   = yes/no    --- -DNDEBUG         --- Enable/Disable debug mode
# optimize= yes/no    --- (-O3/-fast etc.) --- Enable/Disable optimizations
# arch    = (name)    --- (-arch)          --- Target architecture
# os      = (name)    ---                  --- Target operating system
# bits    = 64/32     --- -D_64BIT         --- 64/32-bit operating system
# pfch    = yes/no    --- -DPREFETCH       --- Use prefetch x86 asm-instruction
# bsfq    = yes/no    --- -DBSFQ           --- Use Bitscans x86_64 asm-instruction (only with GCC and ICC 64-bit)
# sse     = yes/no    --- -msse            --- Use Intel Streaming SIMD Extensions instructions
# abm     = yes/no    --- -DABM            --- Use ABM (Adv Bit Manipulation) x86_64 asm-instructions
# bm2     = yes/no    --- -DBM2            --- Use BM2 (Bit Manipulation 2) x86_64 asm-instructions
# lpg     = yes/no    --- -DLPAGES         --- Use Large Pages
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.

### 2.1. General and architecture defaults
debug   = no
optimize= yes

os   = any
bits = 32
pfch = no
sse  = no
bsfq = no
abm  = no
bm2  = no
lpg  = no

### 2.2 Architecture specific

ifeq ($(ARCH),general-32)
	arch = any
endif

ifeq ($(ARCH),x86-32-old)
	arch = i386
endif

ifeq ($(ARCH),x86-32)
	arch = i386
	pfch = yes
	sse  = yes
	lpg  = yes
endif

ifeq ($(ARCH),general-64)
	arch = any
	bits = 64
	lpg  = yes
endif

ifeq ($(ARCH),x86-64)
	arch = x86_64
	bits = 64
	pfch = yes
	sse  = yes
	bsfq = yes
	lpg  = yes
endif

ifeq ($(ARCH),x86-64-abm)
	arch = x86_64
	bits = 64
	pfch = yes
	sse  = yes
	bsfq = yes
	abm  = yes
	lpg  = yes
endif

ifeq ($(ARCH),x86-64-bm2)
	arch = x86_64
	bits = 64
	pfch = yes
	sse  = yes
	bsfq = yes
	abm  = yes
	bm2  = yes
	lpg  = yes
endif

ifeq ($(ARCH),ppc-32)
	arch = ppc
	lpg  = yes
endif

ifeq ($(ARCH),ppc-64)
	arch = ppc64
	bits = 64
	lpg  = yes
endif

ifeq ($(ARCH),armv7)
	arch = armv7
	pfch = yes
	bsfq = yes
endif


### ==========================================================================
### Section 3. Low-level configuration
### ==========================================================================

### 3.1 Selecting compiler (default = gcc)
# -std=c++11 or -std=c++0x or -std=gnu++11 or -std=gnu++0x
# -Wall -fabi-version=6 -fno-access-control -fno-strict-enums -Wno-pragmas -fexceptions

CXXFLAGS += -std=c++11 -Wall -Wcast-qual -Wno-type-limits -fno-exceptions -fno-rtti $(EXTRACXXFLAGS)
LDFLAGS  += $(EXTRALDFLAGS)

# gcc (default)
ifeq ($(COMP),)
	COMP= gcc
endif

ifeq ($(COMP),gcc)
	comp= gcc
	CXX = g++
	CXXFLAGS += -pedantic -Wno-long-long -Wextra -Wshadow
	LDFLAGS += -Wl,--no-as-needed
endif

ifeq ($(COMP),mingw)
	comp= mingw
	CXX = g++
	CXXFLAGS += -Wextra -Wshadow
	LDFLAGS  += -static-libstdc++ -static-libgcc
endif

ifeq ($(COMP),icc)
	comp= icc
	CXX = icpc
	CXXFLAGS += -diag-disable 1476,10120 -Wcheck -Wabi -Wdeprecated -strict-ansi
endif

ifeq ($(COMP),clang)
	comp= clang
	CXX = clang++
	CXXFLAGS += -pedantic -Wno-long-long -Wextra -Wshadow
endif

ifeq ($(comp),icc)
	profile_prepare = icc-profile-prepare
	profile_make    = icc-profile-make
	profile_use     = icc-profile-use
	profile_clean   = icc-profile-clean
else
	profile_prepare = gcc-profile-prepare
	profile_make    = gcc-profile-make
	profile_use     = gcc-profile-use
	profile_clean   = gcc-profile-clean
endif

ifeq ($(UNAME),Darwin)
	CXXFLAGS += -arch $(arch) -mmacosx-version-min=10.6
	LDFLAGS  += -arch $(arch) -mmacosx-version-min=10.6
endif

### On mingw use Windows threads, otherwise POSIX
ifneq ($(comp),mingw)
	# On Android Bionic's C library comes with its own pthread implementation bundled in
	ifneq ($(arch),armv7)
		# Haiku has pthreads in its libroot, so only link it in on other platforms
		ifneq ($(UNAME),Haiku)
			LDFLAGS += -lpthread
		endif
	endif
endif

### 3.4 Debugging
ifeq ($(debug),no)
	CXXFLAGS += -DNDEBUG
else
	CXXFLAGS += -g
endif

### 3.5 Optimization
ifeq ($(optimize),yes)
	ifeq ($(comp),gcc)
		CXXFLAGS += -O3
		ifeq ($(UNAME),Darwin)
			ifeq ($(arch),i386)
				CXXFLAGS += -mdynamic-no-pic
			endif
			ifeq ($(arch),x86_64)
				CXXFLAGS += -mdynamic-no-pic
			endif
		endif
		ifeq ($(arch),armv7)
			CXXFLAGS += -fno-gcse -mthumb -march=armv7-a -mfloat-abi=softfp
		endif
	endif

	ifeq ($(comp),mingw)
		CXXFLAGS += -O3
	endif
	ifeq ($(comp),icc)
		ifeq ($(UNAME),Darwin)
			CXXFLAGS += -fast -mdynamic-no-pic
		else
			CXXFLAGS += -fast
		endif
	endif

	ifeq ($(comp),clang)
		CXXFLAGS += -O3
		ifeq ($(UNAME),Darwin)
			ifeq ($(bm2),no)
				CXXFLAGS += -flto
				LDFLAGS += $(CXXFLAGS)
			endif
			ifeq ($(arch),i386)
				CXXFLAGS += -mdynamic-no-pic
			endif
			ifeq ($(arch),x86_64)
				CXXFLAGS += -mdynamic-no-pic
			endif
		endif
	endif
endif

### 3.6 Bits
ifeq ($(bits),64)
	CXXFLAGS += -D_64BIT
endif

### 3.7 prefetch
ifeq ($(pfch),yes)
	CXXFLAGS += -DPREFETCH
	ifeq ($(sse),yes)
		CXXFLAGS += -msse
		DEPENDFLAGS += -msse
	endif
endif

### 3.8 Bitscans
ifeq ($(bsfq),yes)
	CXXFLAGS += -DBSFQ
endif

### 3.9 ABM (Adv Bit Manipulation)
ifeq ($(abm),yes)
	CXXFLAGS += -DABM -msse3
endif

### 3.10 BM2 (Bit Manipulation 2)
ifeq ($(bm2),yes)
	CXXFLAGS += -DBM2
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mbmi -mbmi2
	endif
endif

### 3.11 Large Pages
ifeq ($(lpg),yes)
	CXXFLAGS += -DLPAGES
endif

### 3.12 Link Time Optimization, it works since gcc 4.5 but not on mingw.
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(comp),gcc)
	ifeq ($(optimize),yes)
	ifeq ($(debug),no)
		GCC_MAJOR := `$(CXX) -dumpversion | cut -f1 -d.`
		GCC_MINOR := `$(CXX) -dumpversion | cut -f2 -d.`
		ifeq (1,$(shell expr \( $(GCC_MAJOR) \> 4 \) \| \( $(GCC_MAJOR) \= 4 \& $(GCC_MINOR) \>= 5 \)))
			CXXFLAGS += -flto
			LDFLAGS  += $(CXXFLAGS)
		endif
	endif
	endif
endif

### 3.13
# CXXFLAGS += -DPOP

### ==========================================================================
### Section 4. Public targets
### ==========================================================================

help:
	@echo ""
	@echo "To compile DON, type: "
	@echo "----------------------"
	@echo "make target ARCH=arch [COMP=comp]"
	@echo ""
	@echo "Supported targets:"
	@echo "------------------"
	@echo "build                   > Standard build"
	@echo "profile-build           > PGO build"
	@echo "strip                   > Strip executable"
	@echo "install                 > Install executable"
	@echo "clean                   > Clean up"
	@echo ""
	@echo "Supported archs:"
	@echo "----------------"
	@echo "general-32              > unspecified 32-bit"
	@echo "x86-32                  > x86 32-bit with SSE support"
	@echo "x86-32-old              > x86 32-bit fall back for old hardware"
	@echo "general-64              > unspecified 64-bit"
	@echo "x86-64                  > x86 64-bit"
	@echo "x86-64-abm              > x86 64-bit with ABM support"
	@echo "x86-64-bm2              > x86 64-bit with BM2 support"
	@echo "ppc-32                  > PPC 32-bit"
	@echo "ppc-64                  > PPC 64-bit"
	@echo "armv7                   > ARMv7 32-bit"
	@echo ""
	@echo "Supported compilers:"
	@echo "--------------------"
	@echo "gcc                     > GNU compiler (default)"
	@echo "mingw                   > GNU compiler with MinGW under Windows"
	@echo "clang                   > LLVM Clang compiler"
	@echo "icc                     > Intel compiler"
	@echo ""
	@echo "Non-standard targets:"
	@echo "---------------------"
	@echo "make hpux               > Compile for HP-UX. Compiler = aCC"
	@echo ""
	@echo "Examples. If you don't know what to do, you likely want to run: "
	@echo "----------------------------------------------------------------"
	@echo "make build ARCH=x86-64    (This is for 64-bit systems)"
	@echo "make build ARCH=x86-32    (This is for 32-bit systems)"
	@echo ""

.PHONY: build profile-build
build:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) config-sanity
	@echo ""
	@echo "Step 0/4. Preparing for profile build."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_prepare)
	@echo ""
	@echo "Step 1/4. Building executable for benchmark ..."
	@touch *.cpp *.h
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_make)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	@$(PGOBENCH) > /dev/null
	@echo ""
	@echo "Step 3/4. Building final executable ..."
	@touch *.cpp
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_use)
	@echo ""
	@echo "Step 4/4. Deleting profile data ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_clean)

strip:
	strip $(EXE).exe

install:
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

clean:
	$(RM) $(EXE) $(EXE).exe *.o .depend *~ core bench.txt *.gcda

default:
	help

### ==========================================================================
### Section 5. Private targets
### ==========================================================================

all: $(EXE) .depend
	@echo "=========================================="
	@echo "*            COMPILE SUCCESS             *"
	@echo "=========================================="

config-sanity:
	@echo ""
	@echo "Config:"
	@echo "---------"
	@echo "debug   : '$(debug)'"
	@echo "optimize: '$(optimize)'"
	@echo "arch    : '$(arch)'"
	@echo "os      : '$(os)'"
	@echo "bits    : '$(bits)'"
	@echo "pfch    : '$(pfch)'"
	@echo "sse     : '$(sse)'"
	@echo "bsfq    : '$(bsfq)'"
	@echo "abm     : '$(abm)'"
	@echo "bm2     : '$(bm2)'"
	@echo "lpg     : '$(lpg)'"
	@echo ""
	@echo "Flags:"
	@echo "---------"
	@echo "CXX     : $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS : $(LDFLAGS)"
	@echo ""
	@echo "Testing config sanity. If this fails, try 'make help' ..."
	@echo ""
	@test "$(debug)" = "yes" || test "$(debug)" = "no"
	@test "$(optimize)" = "yes" || test "$(optimize)" = "no"
	@test "$(arch)" = "any" || test "$(arch)" = "x86_64" || test "$(arch)" = "i386" || \
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc" || test "$(arch)" = "armv7"
	@test "$(os)" = "any" || test "$(os)" = "osx"
	@test "$(bits)" = "32" || test "$(bits)" = "64"
	@test "$(pfch)" = "yes" || test "$(pfch)" = "no"
	@test "$(sse)" = "yes" || test "$(sse)" = "no"
	@test "$(bsfq)" = "yes" || test "$(bsfq)" = "no"
	@test "$(abm)" = "yes" || test "$(abm)" = "no"
	@test "$(bm2)" = "yes" || test "$(bm2)" = "no"
	@test "$(lpg)" = "yes" || test "$(lpg)" = "no"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || test "$(comp)" = "mingw" || test "$(comp)" = "clang"

# generating obj file
$(EXE): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

gcc-profile-prepare:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) gcc-profile-clean

gcc-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP)  \
	EXTRACXXFLAGS='-fprofile-generate' \
	EXTRALDFLAGS='-lgcov'              \
	all

gcc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP)  \
	EXTRACXXFLAGS='-fprofile-use'      \
	EXTRALDFLAGS='-lgcov'              \
	all

gcc-profile-clean:
	@rm -rf *.gcda *.gcno bench.txt

icc-profile-prepare:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) icc-profile-clean
	@mkdir profdir

icc-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP)  \
	EXTRACXXFLAGS='-prof-gen=srcpos -prof_dir ./profdir' \
	all

icc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP)  \
	EXTRACXXFLAGS='-prof_use -prof_dir ./profdir' \
	all

icc-profile-clean:
	@rm -rf profdir bench.txt

.depend:
	-@$(CXX) $(DEPENDFLAGS) -MM $(OBJS:.o=.cpp) > $@ 2> /dev/null

-include .depend


### ==========================================================================
### Section 6. Non-standard targets
### ==========================================================================

hpux:
	$(MAKE)      \
	CXX = '/opt/aCC/bin/aCC -AA +hpxstd98 -mt +O3 -DNDEBUG' \
	CXXFLAGS= "" \
	LDFLAGS = "" \
	all
