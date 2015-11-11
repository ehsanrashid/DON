# DON, a UCI chess playing engine
# Ehsan Rashid Copyright (C) 2008+
#
# DON is housed in https://github.com/erashid/DON 

### ==========================================================================
### Section 1. General Configuration
### ==========================================================================

### Establish the operating system name
UNAME = $(shell uname)

### Executable name
EXE = DON

### Installation dir definitions
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

### Built-in benchmark for pgo-builds
PGOBENCH  = ./$(EXE) bench 16 1 1000 movetime default

### Object files
OBJS = Benchmark.o BitBases.o BitBoard.o Endgame.o Engine.o Evaluator.o Main.o Material.o \
	MoveGenerator.o MovePicker.o Notation.o Pawns.o PolyglotBook.o Position.o Searcher.o  \
	Thread.o Transposition.o UCI.o UCI.Option.o Zobrist.o MemoryHandler.o Debugger.o  \

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
# bits    = 64/32     --- -DBIT64          --- 64/32-bit operating system
# pfch    = yes/no    --- -DPREFETCH       --- Use Prefetch x86 asm-instruction
# bsfq    = yes/no    --- -DBSFQ           --- Use Bitscans x86_64 asm-instruction (only with GCC and ICC 64-bit)
# sse     = yes/no    --- -msse            --- Use Intel Streaming SIMD Extensions instructions
# pop     = yes/no    --- -DPOP            --- Use Internal POP Count Table
# abm     = yes/no    --- -DABM            --- Use ABM (Adv Bit Manipulation) x86_64 asm-instructions
# bm2     = yes/no    --- -DBM2            --- Use BM2 (Bit Manipulation 2) x86_64 asm-instructions
# lpg     = yes/no    --- -DLPAGES         --- Use Large Pages
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.

### Redirect output to file
# DON bench <param>  > bench.txt 2>&1 // works fine
# DON bench <param> 2> bench.txt 1>&2 // merge std::cerr into std::cout

### 2.1. General and architecture defaults
debug   = no
optimize= yes

bits = 32
pfch = no
sse  = no
pop  = no
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
endif

ifeq ($(ARCH),general-32-pop)
	arch = any
	pop  = yes
endif

ifeq ($(ARCH),x86-32-old-pop)
	arch = i386
	pop  = yes
endif

ifeq ($(ARCH),x86-32-pop)
	arch = i386
	pfch = yes
	sse  = yes
	pop  = yes
endif

ifeq ($(ARCH),x86-32-abm)
	arch = i386
	pfch = yes
	sse  = yes
	abm  = yes
endif

ifeq ($(ARCH),general-64)
	arch = any
	bits = 64
endif

ifeq ($(ARCH),x86-64)
	arch = x86_64
	bits = 64
	pfch = yes
	sse  = yes
	bsfq = yes
endif

ifeq ($(ARCH),general-64-pop)
	arch = any
	bits = 64
	lpg  = yes
	pop  = yes
endif

ifeq ($(ARCH),x86-64-pop)
	arch = x86_64
	bits = 64
	pfch = yes
	sse  = yes
	pop  = yes
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
endif

ifeq ($(ARCH),ppc-64)
	arch = ppc64
	bits = 64
	lpg  = yes
endif

ifeq ($(ARCH),ppc-32-pop)
	arch = ppc
	pop  = yes
endif

ifeq ($(ARCH),ppc-64-pop)
	arch = ppc64
	bits = 64
	pop  = yes
	lpg  = yes
endif

ifeq ($(ARCH),armv7)
	arch = armv7
	pfch = yes
	bsfq = yes
endif

ifeq ($(ARCH),armv7-pop)
	arch = armv7
	pfch = yes
	pop  = yes
	bsfq = yes
endif

### ==========================================================================
### Section 3. Low-level configuration
### ==========================================================================

### 3.1 Selecting compiler (default = gcc)

CXXFLAGS	+= -std=c++11 -Wall -Wcast-qual -fno-exceptions -fno-rtti -Wno-type-limits $(EXTRACXXFLAGS)
DEPENDFLAGS += -std=c++11
LDFLAGS		+= $(EXTRALDFLAGS)

# gcc (default)
ifeq ($(COMP),)
	COMP= gcc
endif

ifeq ($(COMP),gcc)
	comp= gcc
	CXX = g++
	CXXFLAGS += -pedantic -Wextra -Wshadow
	ifneq ($(UNAME),Darwin)
		LDFLAGS  += -Wl,--no-as-needed
	endif
endif

ifeq ($(COMP),mingw)
	comp= mingw
	CXX = g++
	CXXFLAGS += -Wextra -Wshadow
	LDFLAGS  += -static
endif

ifeq ($(COMP),icc)
	comp= icc
	CXX = icpc
	CXXFLAGS += -diag-disable 1476,10120 -Wcheck -Wabi -Wdeprecated -strict-ansi
endif

ifeq ($(COMP),clang)
	comp= clang
	CXX = clang++
	CXXFLAGS += -pedantic -Wextra -Wshadow
	ifeq ($(UNAME),Darwin)
		CXXFLAGS    += -std=c++0x -stdlib=libc++
		DEPENDFLAGS += -std=c++0x -stdlib=libc++
	endif
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
	CXXFLAGS += -arch $(arch) -mmacosx-version-min=10.9
	LDFLAGS  += -arch $(arch) -mmacosx-version-min=10.9
endif

### Travis CI script uses COMPILER to overwrite CXX
ifdef COMPILER
	CXX=$(COMPILER)
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
	CXXFLAGS += -DBIT64
endif

### 3.7 Prefetch
ifeq ($(pfch),yes)
	CXXFLAGS += -DPREFETCH
	ifeq ($(sse),yes)
		CXXFLAGS    += -msse
		DEPENDFLAGS += -msse
	endif
endif

### 3.8 Bitscans
ifeq ($(bsfq),yes)
	CXXFLAGS += -DBSFQ
endif

### 3.9 ABM (Adv Bit Manipulation)
ifeq ($(abm),yes)
	CXXFLAGS += -DABM
	ifeq ($(comp),icc)
		CXXFLAGS += -msse3
	else
		CXXFLAGS += -msse3 -mpopcnt
	endif
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

### 3.12 Pop Tables
ifeq ($(pop),yes)
	CXXFLAGS += -DPOP
endif

### 3.13 Link Time Optimization, it works since gcc 4.5 but not on mingw.
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(comp),gcc)
	ifeq ($(optimize),yes)
	ifeq ($(debug),no)
		CXXFLAGS += -flto
		LDFLAGS += $(CXXFLAGS)
	endif
	endif
endif

### 3.14 Android 5 can only run position independent executables.
### Note that this breaks Android 4.0 and earlier.
ifeq ($(arch),armv7)
	CXXFLAGS += -fPIE
	LDFLAGS  += -fPIE -pie
endif

ifneq ($(VER),)
	CXXFLAGS += -DVER=$(VER)
endif

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
	@echo "uninstall               > Remove executable"
	@echo "clean                   > Clean up"
	@echo ""
	@echo "Supported architectures:"
	@echo "------------------------"
	@echo "general-32              > unspecified 32-bit"
	@echo "x86-32-old              > x86 32-bit fall back for old hardware"
	@echo "x86-32                  > x86 32-bit with SSE support"
	@echo "general-32-pop          > unspecified 32-bit with POP"
	@echo "x86-32-old-pop          > x86 32-bit fall back for old hardware with POP"
	@echo "x86-32-pop              > x86 32-bit with SSE support with POP"
	@echo "x86-32-abm              > x86 32-bit with ABM support"
	@echo "general-64              > unspecified 64-bit"
	@echo "x86-64                  > x86 64-bit"
	@echo "general-64-pop          > unspecified 64-bit with POP"
	@echo "x86-64-pop              > x86 64-bit with POP"
	@echo "x86-64-abm              > x86 64-bit with ABM support"
	@echo "x86-64-bm2              > x86 64-bit with BM2 support"
	@echo "ppc-32                  > PPC 32-bit"
	@echo "ppc-64                  > PPC 64-bit"
	@echo "ppc-32-pop              > PPC 32-bit with POP"
	@echo "ppc-64-pop              > PPC 64-bit with POP"
	@echo "armv7                   > ARMv7 32-bit"
	@echo "armv7-pop               > ARMv7 32-bit with POP"
	@echo ""
	@echo "Supported compilers:"
	@echo "--------------------"
	@echo "gcc                     > GNU compiler (default)"
	@echo "mingw                   > GNU compiler with MinGW under Windows"
	@echo "clang                   > LLVM Clang compiler"
	@echo "icc                     > Intel compiler"
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
	@touch *.cpp *.h
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_use)
	@echo ""
	@echo "Step 4/4. Deleting profile data ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_clean)

strip:
	strip $(EXE)

install:
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

uninstall:
	-$(RM) $(BINDIR)/$(EXE)

clean:
	$(RM) $(EXE) $(EXE).exe *.o *.gcda .depend *~ core bench.txt

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
	@echo "bits    : '$(bits)'"
	@echo "pfch    : '$(pfch)'"
	@echo "sse     : '$(sse)'"
	@echo "pop     : '$(pop)'"
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
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc32" || test "$(arch)" = "armv7"
	@test "$(bits)" = "32" || test "$(bits)" = "64"
	@test "$(pfch)" = "yes" || test "$(pfch)" = "no"
	@test "$(sse)" = "yes" || test "$(sse)" = "no"
	@test "$(pop)" = "yes" || test "$(pop)" = "no"
	@test "$(bsfq)" = "yes" || test "$(bsfq)" = "no"
	@test "$(abm)" = "yes" || test "$(abm)" = "no"
	@test "$(bm2)" = "yes" || test "$(bm2)" = "no"
	@test "$(lpg)" = "yes" || test "$(lpg)" = "no"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || test "$(comp)" = "mingw" || test "$(comp)" = "clang"

# Generating obj file
$(EXE): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

gcc-profile-prepare:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) gcc-profile-clean

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

gcc-profile-clean:
	@rm -rf *.gcda *.gcno bench.txt

icc-profile-prepare:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) icc-profile-clean
	@mkdir profdir

icc-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-prof-gen=srcpos -prof_dir ./profdir' \
	all

icc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-prof_use -prof_dir ./profdir' \
	all

icc-profile-clean:
	@rm -rf profdir bench.txt

.depend:
	-@$(CXX) $(DEPENDFLAGS) -MM $(OBJS:.o=.cpp) > $@ 2> /dev/null

-include .depend

