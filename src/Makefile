# DON, UCI chess playing engine
# Ehsan Rashid Copyright (C) 2008+
#
# DON is housed in https://github.com/ehsanrashid/DON

### ==========================================================================
### Section 1. General Configuration
### ==========================================================================

### Executable name
ifeq ($(COMP), mingw)
	EXE = DON.exe
else
	EXE = DON
endif

### Installation directory definitions
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

### Built-in benchmark for pgo-builds
PGOBENCH = ./$(EXE) bench

### Object files
OBJS = Bitbase.o Bitboard.o Cuckoo.o Debugger.o Endgame.o \
       Engine.o Evaluator.o Helper.o Logger.o Main.o Material.o \
       MoveGenerator.o MovePicker.o Notation.o Option.o Pawns.o \
       Polyglot.o Position.o PSQTable.o RootMove.o Searcher.o \
       SkillManager.o SyzygyTB.o Thread.o TimeManager.o \
       Transposition.o Zobrist.o


### Establish the operating system name
KERNEL = $(shell uname -s)
ifeq ($(KERNEL), Linux)
	OS = $(shell uname -o)
endif

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
#
# Flag                --- Comp switch      --- Description
# ----------------------------------------------------------------------------
#
# debug    = yes/no    --- -DNDEBUG         --- Enable/Disable debug mode
# sanitize =undefined/thread/no (-fsanitize )
#                      --- (undefined)      --- Enable undefined behavior checks
#                      --- (thread)         --- Enable threading error checks
# optimize = yes/no    --- (-O3/-fast etc.) --- Enable/Disable optimizations
# arch     = (name)    --- (-arch)          --- Target architecture
# bits     = 64/32     --- -DBIT64          --- 64/32-bit operating system
# prefetch = yes/no    --- -DPREFETCH       --- Use Prefetch x86 asm-instruction
# sse      = yes/no    --- -msse            --- Use Intel Streaming SIMD Extensions
# abm      = yes/no    --- -DABM            --- Use ABM (Adv Bit Manipulation) x86_64 asm-instructions (POPCNT, LZCNT)
# bm2      = yes/no    --- -DBM2            --- Use BM2 (Bit Manipulation 2) x86_64 asm-instructions (PEXT, PDEP)
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.

### 2.1 General and architecture defaults
debug = no
sanitize = no
optimize = yes

arch = any
bits = 32

prefetch = no
sse = no
abm = no
bm2 = no

### 2.2 Architecture specific

ifeq ($(ARCH), x86-64-bm2)
	arch = x86_64
	bits = 64
	prefetch = yes
	sse  = yes
	abm  = yes
	bm2  = yes
endif

ifeq ($(ARCH), x86-64-abm)
	arch = x86_64
	bits = 64
	prefetch = yes
	sse  = yes
	abm  = yes
endif

ifeq ($(ARCH), x86-64)
	arch = x86_64
	bits = 64
	prefetch = yes
	sse  = yes
endif

ifeq ($(ARCH), x86-32)
	arch = i386
	bits = 32
	prefetch = yes
	sse  = yes
endif

ifeq ($(ARCH), x86-32-old)
	arch = i386
	bits = 32
endif

ifeq ($(ARCH), ppc-64)
	arch = ppc64
	bits = 64
	prefetch = yes
	abm  = yes
endif

ifeq ($(ARCH), ppc-32)
	arch = ppc32
	bits = 32
endif

ifeq ($(ARCH), armv7)
	arch = armv7
	bits = 32
	prefetch = yes
endif

ifeq ($(ARCH), general-64)
	arch = any
	bits = 64
endif

ifeq ($(ARCH), general-32)
	arch = any
	bits = 32
endif

### ==========================================================================
### Section 3. Low-level Configuration
### ==========================================================================

### 3.1 Compiler Selection (Default = gcc)

CXXFLAGS += -Wall -Wcast-qual -Wno-type-limits -fno-exceptions $(EXTRACXXFLAGS)
DEPENDFLAGS += -std=c++11
LDFLAGS += $(EXTRALDFLAGS)

# Default (gcc)
ifeq ($(COMP), )
	COMP = gcc
endif

ifeq ($(COMP), gcc)
	comp = gcc
	CXX = g++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifeq ($(ARCH), armv7)
		ifeq ($(OS), Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif
	ifneq ($(KERNEL), Darwin)
		LDFLAGS += -Wl,--no-as-needed
	endif
endif

ifeq ($(COMP), mingw)
	comp = mingw

	ifeq ($(KERNEL), Linux)
		ifeq ($(bits), 64)
			ifeq ($(shell which x86_64-w64-mingw32-c++-posix), )
				CXX = x86_64-w64-mingw32-c++
			else
				CXX = x86_64-w64-mingw32-c++-posix
			endif
		else
			ifeq ($(shell which i686-w64-mingw32-c++-posix), )
				CXX = i686-w64-mingw32-c++
			else
				CXX = i686-w64-mingw32-c++-posix
			endif
		endif
	else
		CXX = g++
	endif

	CXXFLAGS += -Wextra -Wshadow
	LDFLAGS += -static
endif

ifeq ($(COMP), icc)
	comp = icc
	CXX = icpc
	CXXFLAGS += -diag-disable 1476,10120 -Wcheck -Wabi -Wdeprecated -strict-ansi
endif

ifeq ($(COMP), clang)
	comp = clang
	CXX = clang++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifneq ($(KERNEL), Darwin)
		ifneq ($(KERNEL), OpenBSD)
			LDFLAGS += -latomic
		endif
	endif

	ifeq ($(ARCH), armv7)
		ifeq ($(OS), Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif
endif

ifeq ($(comp), icc)
	profile_make = icc-profile-make
	profile_use = icc-profile-use
else
ifeq ($(comp), clang)
	profile_make = clang-profile-make
	profile_use = clang-profile-use
else
	profile_make = gcc-profile-make
	profile_use = gcc-profile-use
endif
endif

ifeq ($(KERNEL), Darwin)
	CXXFLAGS += -arch $(arch) -mmacosx-version-min=10.9
	LDFLAGS += -arch $(arch) -mmacosx-version-min=10.9
endif

### Travis CI script uses COMPILER to overwrite CXX
ifdef COMPILER
	COMPCXX = $(COMPILER)
endif

### Allow overwriting CXX from command line
ifdef COMPCXX
	CXX = $(COMPCXX)
endif

### On MinGW use Windows threads, otherwise POSIX
ifneq ($(comp), mingw)
	# On Android Bionic's C library comes with its own pthread implementation bundled in
	ifneq ($(OS), Android)
		# Haiku has pthreads in its libroot, so only link it in on other platforms
		ifneq ($(KERNEL), Haiku)
			LDFLAGS += -lpthread
		endif
	endif
endif

### 3.2.1 Debugging
ifeq ($(debug), no)
	CXXFLAGS += -DNDEBUG
else
	CXXFLAGS += -g
endif

### 3.2.2 Debugging with undefined behavior sanitizers
ifneq ($(sanitize), no)
	CXXFLAGS += -g3 -fsanitize=$(sanitize) -fuse-ld=gold
	LDFLAGS += -fsanitize=$(sanitize) -fuse-ld=gold
endif

### 3.3 Optimization
ifeq ($(optimize), yes)
	CXXFLAGS += -O3

	ifeq ($(comp), gcc)
		ifeq ($(OS), Android)
			CXXFLAGS += -fno-gcse -mthumb -march=armv7-a -mfloat-abi=softfp
		endif
	endif

	ifeq ($(comp), $(filter $(comp), gcc clang icc))
		ifeq ($(KERNEL), Darwin)
			CXXFLAGS += -mdynamic-no-pic
		endif
	endif
endif

### 3.4 Bits
ifeq ($(bits), 64)
	CXXFLAGS += -DBIT64
endif

### 3.5 Prefetch
ifeq ($(prefetch), yes)
	CXXFLAGS += -DPREFETCH
	ifeq ($(sse), yes)
		CXXFLAGS += -msse
		DEPENDFLAGS += -msse
	endif
endif

### 3.6 ABM (Advance Bit Manipulation)
ifeq ($(abm), yes)
	ifeq ($(arch), ppc64)
		CXXFLAGS += -DABM
	else ifeq ($(comp), icc)
		CXXFLAGS += -DABM -msse3
	else
		CXXFLAGS += -DABM -msse3 -mpopcnt
	endif
endif

### 3.7 BM2 (Bit Manipulation Instruction Set 2)
ifeq ($(bm2), yes)
	CXXFLAGS += -DBM2
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -msse4 -mbmi -mbmi2
	endif
endif

### 3.8 Link Time Optimization, it works since gcc 4.5 but not on mingw under windows.
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize), yes)
	ifeq ($(debug), no)
		ifeq ($(comp), $(filter $(comp), gcc clang))
			CXXFLAGS += -flto
			LDFLAGS += $(CXXFLAGS)
		endif

		ifeq ($(comp), mingw)
			ifeq ($(KERNEL), Linux)
				CXXFLAGS += -flto
				LDFLAGS += $(CXXFLAGS)
			endif
		endif
	endif
endif

### 3.9 Android 5 can only run position independent executables.
### Note that this breaks Android 4.0 and earlier.
ifeq ($(OS), Android)
	CXXFLAGS += -fPIE
	LDFLAGS += -fPIE -pie
endif

### 3.10 Custom Version
ifneq ($(VER), )
	CXXFLAGS += -DVER=$(VER)
endif

### ==========================================================================
### Section 4. Public targets
### ==========================================================================

help:
	@echo ""
	@echo "To compile DON, type: "
	@echo "----------------------"
	@echo "make target ARCH=arch [COMP=compiler] [COMPCXX=cxx]"
	@echo ""
	@echo "Supported targets:"
	@echo "------------------"
	@echo "build         > Standard build"
	@echo "profile-build > PGO build"
	@echo "strip         > Strip executable"
	@echo "install       > Install executable"
	@echo "clean         > Clean up"
	@echo ""
	@echo "Supported architectures:"
	@echo "------------------------"
	@echo "x86-64-bm2   > x86 64-bit with BM2 support (also SSE4 support)"
	@echo "x86-64-abm   > x86 64-bit with ABM support (also SSE3 support)"
	@echo "x86-64       > x86 64-bit generic"
	@echo "x86-32       > x86 32-bit (also SSE support)"
	@echo "x86-32-old   > x86 32-bit fall back for old hardware"
	@echo "ppc-64       > PPC 64-bit"
	@echo "ppc-32       > PPC 32-bit"
	@echo "armv7        > ARMv7 32-bit"
	@echo "general-64   > unspecified 64-bit"
	@echo "general-32   > unspecified 32-bit"
	@echo ""
	@echo "Supported compilers:"
	@echo "--------------------"
	@echo "gcc          > GNU compiler (default)"
	@echo "mingw        > GNU compiler with MinGW under Windows"
	@echo "clang        > LLVM Clang compiler"
	@echo "icc          > Intel compiler"
	@echo ""
	@echo "Simple examples, for inexperienced users: "
	@echo "----------------------------------------------------------------"
	@echo "make build ARCH=x86-64    (This is for 64-bit systems)"
	@echo "make build ARCH=x86-32    (This is for 32-bit systems)"
	@echo ""
	@echo "Advanced examples, for experienced users: "
	@echo ""
	@echo "make build ARCH=x86-64 COMP=clang"
	@echo "make profile-build ARCH=x86-64-bm2 COMP=gcc COMPCXX=g++-4.8"
	@echo ""

.PHONY: help build profile-build strip install clean objclean profileclean config-sanity \
        icc-profile-use icc-profile-make \
        clang-profile-use clang-profile-make \
        gcc-profile-use gcc-profile-make

build: config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build: config-sanity objclean profileclean
	@echo ""
	@echo "Step 1/4. Building instrumented executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_make)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) > /dev/null
	@echo ""
	@echo "Step 3/4. Building optimized executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) objclean
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_use)
	@echo ""
	@echo "Step 4/4. Deleting profile data ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) profileclean

strip:
	strip $(EXE)

install:
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

# Clean all
clean: objclean profileclean
	@rm -f .depend *~ core

# Clean binaries and objects
objclean:
	@rm -f $(EXE) *.o

# Clean auxiliary profiling files
profileclean:
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno
	@rm -f DON.profdata *.profraw

default:
	help

### ==========================================================================
### Section 5. Private targets
### ==========================================================================

all: $(EXE) .depend
	@echo "================================="
	@echo "*            SUCCESS            *"
	@echo "================================="

config-sanity:
	@echo ""
	@echo "Config:"
	@echo "---------"
	@echo "debug   : '$(debug)'"
	@echo "sanitize: '$(sanitize)'"
	@echo "optimize: '$(optimize)'"
	@echo "arch    : '$(arch)'"
	@echo "comp    : '$(comp)'"
	@echo "bits    : '$(bits)'"
	@echo "KERNEL  : '$(KERNEL)'"
	@echo "OS      : '$(OS)'"
	@echo "prefetch: '$(prefetch)'"
	@echo "sse     : '$(sse)'"
	@echo "abm     : '$(abm)'"
	@echo "bm2     : '$(bm2)'"
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
	@test "$(sanitize)" = "undefined" || test "$(sanitize)" = "thread" || test "$(sanitize)" = "address" || test "$(sanitize)" = "no"
	@test "$(optimize)" = "yes" || test "$(optimize)" = "no"
	@test "$(arch)" = "x86_64" || test "$(arch)" = "i386" || test "$(arch)" = "any" || \
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc32" || test "$(arch)" = "armv7"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || test "$(comp)" = "mingw" || test "$(comp)" = "clang"
	@test "$(bits)" = "64" || test "$(bits)" = "32"
	@test "$(prefetch)" = "yes" || test "$(prefetch)" = "no"
	@test "$(sse)" = "yes" || test "$(sse)" = "no"
	@test "$(abm)" = "yes" || test "$(abm)" = "no"
	@test "$(bm2)" = "yes" || test "$(bm2)" = "no"

# Generating obj files
$(EXE): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

clang-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-generate' \
	EXTRALDFLAGS='-fprofile-instr-generate' \
	all

clang-profile-use:
	llvm-profdata merge -output=DON.profdata *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-use=DON.profdata' \
	EXTRALDFLAGS='-fprofile-use' \
	all

gcc-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-generate' \
	EXTRALDFLAGS='-lgcov' \
	all

gcc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-use -fno-peel-loops -fno-tracer' \
	EXTRALDFLAGS='-lgcov' \
	all

icc-profile-make:
	@mkdir -p profdir
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-prof-gen=srcpos -prof_dir ./profdir' \
	all

icc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-prof_use -prof_dir ./profdir' \
	all

.depend:
	-@$(CXX) $(DEPENDFLAGS) -MM $(OBJS:.o=.cpp) > $@ 2> /dev/null

-include .depend

