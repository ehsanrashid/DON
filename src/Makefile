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

### Source and Object files
SRCS =  main.cpp \
        bitbase.cpp \
        bitboard.cpp \
        cuckoo.cpp \
        endgame.cpp \
        evaluator.cpp \
        material.cpp \
        movegenerator.cpp \
        movepicker.cpp \
        notation.cpp \
        king.cpp \
        pawns.cpp \
        polyglot.cpp \
        position.cpp \
        psqtable.cpp \
        rootmove.cpp \
        searcher.cpp \
        skillmanager.cpp \
        syzygytb.cpp \
        thread.cpp \
        threadmarker.cpp \
        timemanager.cpp \
        transposition.cpp \
        tune.cpp \
        uci.cpp \
        zobrist.cpp \
        nnue/evaluate_nnue.cpp \
        nnue/features/half_kp.cpp \
        helper/commandline.cpp \
        helper/logger.cpp \
        helper/memoryhandler.cpp \
        helper/reporter.cpp \

OBJS = $(notdir $(SRCS:.cpp=.o))

VPATH = nnue:nnue/features:helper

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
# sanitize =undefined/thread/address/no (-fsanitize )
#                      --- (undefined)      --- Enable undefined behavior checks
#                      --- (thread)         --- Enable threading error checks
# optimize = yes/no    --- (-O3/-fast etc.) --- Enable/Disable optimizations
# arch     = (name)    --- (-arch)          --- Target architecture
# bits     = 64/32     --- -DIS_64BIT       --- 64-/32-bit operating system
# prefetch = yes/no    --- -DUSE_PREFETCH   --- Use Intel prefetch asm-instructions
# popcnt   = yes/no    --- -DUSE_POPCNT     --- Use Intel popcnt asm-instructions
# sse      = yes/no    --- -DUSE_PREFETCH   --- Use Intel Streaming SIMD Extensions
# mmx      = yes/no    --- -DUSE_MMX        --- Use Intel MMX instructions
# sse2     = yes/no    --- -DUSE_SSE2       --- Use Intel Streaming SIMD Extensions 2
# ssse3    = yes/no    --- -DUSE_SSSE3      --- Use Intel Supplemental Streaming SIMD Extensions 3
# sse41    = yes/no    --- -DUSE_SSE41      --- Use Intel Streaming SIMD Extensions 4.1
# avx2     = yes/no    --- -DUSE_AVX2       --- Use Intel Advanced Vector Extensions 2
# bmi2     = yes/no    --- -DUSE_BMI2       --- Use Intel bmi2 x86_64 asm-instructions
# avx512   = yes/no    --- -DUSE_AVX512     --- Use Intel Advanced Vector Extensions 512
# vnni256  = yes/no    --- -DUSE_VNNI       --- Use Intel Vector Neural Network Instructions 256
# vnni512  = yes/no    --- -DUSE_VNNI       --- Use Intel Vector Neural Network Instructions 512
# neon     = yes/no    --- -DUSE_NEON       --- Use ARM SIMD architecture
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.

### 2.1 General and architecture defaults
ifeq ($(ARCH), )
	ARCH = x86-64-modern
	help_skip_sanity = yes
endif
# explicitly check for the list of supported architectures (as listed with make help),
# the user can override with `make ARCH=x86-32-vnni256 SUPPORTED_ARCH=true`
ifeq ($(ARCH), $(filter $(ARCH), \
                        x86-64-vnni512 \
                        x86-64-vnni256 \
                        x86-64-avx512 \
                        x86-64-bmi2 \
                        x86-64-avx2 \
                        x86-64-sse41-popcnt \
                        x86-64-modern \
                        x86-64-ssse3 \
                        x86-64-sse3-popcnt \
                        x86-64 \
                        x86-32-sse41-popcnt \
                        x86-32-sse2 \
                        x86-32 \
                        ppc-64 \
                        ppc-32 \
                        armv7 \
                        armv7-neon \
                        armv8 \
                        apple-silicon \
                        general-64 \
                        general-32))
	SUPPORTED_ARCH = true
else
	SUPPORTED_ARCH = false
endif

optimize = yes
debug = no
sanitize = no
bits = 64
prefetch = no
popcnt = no
sse = no
mmx = no
sse2 = no
ssse3 = no
sse41 = no
avx2 = no
bmi2 = no
avx512 = no
vnni256 = no
vnni512 = no
neon = no

STRIP = strip

### 2.2 Architecture specific
ifeq ($(findstring x86, $(ARCH)), x86)

	# x86-32/64

	ifeq ($(findstring x86-32, $(ARCH)), x86-32)
		arch = i386
		bits = 32
		sse = yes
		mmx = yes
	else
		arch = x86_64
		sse = yes
		sse2 = yes
	endif

	ifeq ($(findstring -sse, $(ARCH)), -sse)
		sse = yes
	endif

	ifeq ($(findstring -popcnt, $(ARCH)), -popcnt)
		popcnt = yes
	endif

	ifeq ($(findstring -mmx, $(ARCH)), -mmx)
		mmx = yes
	endif

	ifeq ($(findstring -sse2, $(ARCH)), -sse2)
		sse = yes
		sse2 = yes
	endif

	ifeq ($(findstring -ssse3, $(ARCH)), -ssse3)
		sse = yes
		sse2 = yes
		ssse3 = yes
	endif

	ifeq ($(findstring -sse41, $(ARCH)), -sse41)
		sse = yes
		sse2 = yes
		ssse3 = yes
		sse41 = yes
	endif

	ifeq ($(findstring -modern, $(ARCH)), -modern)
		popcnt = yes
		sse = yes
		sse2 = yes
		ssse3 = yes
		sse41 = yes
	endif

	ifeq ($(findstring -avx2, $(ARCH)), -avx2)
		popcnt = yes
		sse = yes
		sse2 = yes
		ssse3 = yes
		sse41 = yes
		avx2 = yes
	endif

	ifeq ($(findstring -bmi2, $(ARCH)), -bmi2)
		popcnt = yes
		sse = yes
		sse2 = yes
		ssse3 = yes
		sse41 = yes
		avx2 = yes
		bmi2 = yes
	endif

	ifeq ($(findstring -avx512, $(ARCH)), -avx512)
		popcnt = yes
		sse = yes
		sse2 = yes
		ssse3 = yes
		sse41 = yes
		avx2 = yes
		bmi2 = yes
		avx512 = yes
	endif

	ifeq ($(findstring -vnni256,$(ARCH)), -vnni256)
		popcnt = yes
		sse = yes
		sse2 = yes
		ssse3 = yes
		sse41 = yes
		avx2 = yes
		bmi2 = yes
		vnni256 = yes
	endif

	ifeq ($(findstring -vnni512,$(ARCH)), -vnni512)
		popcnt = yes
		sse = yes
		sse2 = yes
		ssse3 = yes
		sse41 = yes
		avx2 = yes
		bmi2 = yes
		avx512 = yes
		vnni512 = yes
	endif

	# if sse then enable prefetch
	ifeq ($(sse), yes)
		prefetch = yes
	endif

	# 64-bit bmi2 is not available on x86-32
	ifeq ($(bits), 32)
		bmi2 = no
	endif

else

	# all other architectures

	ifeq ($(ARCH), general-32)
		arch = any
		bits = 32
	endif

	ifeq ($(ARCH), general-64)
		arch = any
	endif

	ifeq ($(ARCH), armv7)
		arch = armv7
		prefetch = yes
		bits = 32
	endif

	ifeq ($(ARCH), armv7-neon)
		arch = armv7
		prefetch = yes
		popcnt = yes
		neon = yes
		bits = 32
	endif

	ifeq ($(ARCH), armv8)
		arch = armv8
		prefetch = yes
		popcnt = yes
		neon = yes
	endif

	ifeq ($(ARCH), apple-silicon)
		arch = arm64
		prefetch = yes
		popcnt = yes
		neon = yes
	endif

	ifeq ($(ARCH), ppc-32)
		arch = ppc
		bits = 32
	endif

	ifeq ($(ARCH), ppc-64)
		arch = ppc64
		popcnt = yes
		prefetch = yes
	endif

endif

### ==========================================================================
### Section 3. Low-level Configuration
### ==========================================================================

### 3.1 Compiler Selection (default = gcc)

CXXFLAGS += -Wall -Wcast-qual -fno-exceptions -std=c++17 $(EXTRACXXFLAGS)
DEPENDFLAGS += -std=c++17
LDFLAGS += $(EXTRALDFLAGS)

ifeq ($(COMP), )
	COMP = gcc
endif

ifeq ($(COMP), gcc)
	comp = gcc
	CXX = g++
	CXXFLAGS += -Wextra -Wshadow -pedantic

	ifeq ($(arch), $(filter $(arch), armv7 armv8))
		ifeq ($(OS), Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif

	ifeq ($(arch), $(filter $(arch), armv7))
		LDFLAGS += -latomic
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
	CXXFLAGS += -Wcheck -Wabi -Wdeprecated -strict-ansi -diag-disable 1476,10120
endif

ifeq ($(COMP), clang)
	comp = clang
	CXX = clang++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifneq (, $(filter $(KERNEL), Darwin OpenBSD FreeBSD))
		LDFLAGS += -latomic
	endif

	ifeq ($(arch), $(filter $(arch), armv7 armv8))
		ifeq ($(OS), Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif
endif

ifeq ($(KERNEL), Darwin)
	CXXFLAGS += -arch $(arch) -mmacosx-version-min=10.14
	LDFLAGS += -arch $(arch) -mmacosx-version-min=10.14
	XCRUN = xcrun
endif

# To cross-compile for Android, NDK version r21 or later is recommended.
# In earlier NDK versions, you'll need to pass -fno-addrsig if using GNU binutils.
# Currently we don't know how to make PGO builds with the NDK yet.
ifeq ($(COMP), ndk)
	comp = clang
	CXXFLAGS += -stdlib=libc++ -fPIE

	ifeq ($(arch), armv7)
		CXX = armv7a-linux-androideabi16-clang++
		CXXFLAGS += -mthumb -march=armv7-a -mfloat-abi=softfp -mfpu=neon
		STRIP = arm-linux-androideabi-strip
	endif
	ifeq ($(arch), armv8)
		CXX = aarch64-linux-android21-clang++
		STRIP = aarch64-linux-android-strip
	endif
	LDFLAGS += -static-libstdc++ -pie -lm -latomic
endif

ifeq ($(comp), icc)
	profile_make = icc-profile-make
	profile_use = icc-profile-use
else ifeq ($(comp), clang)
	profile_make = clang-profile-make
	profile_use = clang-profile-use
else
#ifeq ($(comp), mingw)
	profile_make = gcc-profile-make
	profile_use = gcc-profile-use
endif

### Travis CI script uses COMPILER to overwrite CXX
ifdef COMPILER
	COMPCXX = $(COMPILER)
endif

### Allow overwriting CXX from command line
ifdef COMPCXX
	CXX = $(COMPCXX)
endif

### Sometimes gcc is really clang
ifeq ($(COMP), gcc)
	gccversion = $(shell $(CXX) --version)
	gccisclang = $(findstring clang, $(gccversion))
	ifneq ($(gccisclang), )
		profile_make = clang-profile-make
		profile_use = clang-profile-use
	endif
endif

### On minGW use Windows threads, otherwise POSIX
ifneq ($(comp), mingw)
	CXXFLAGS += -DUSE_PTHREADS
	# On Android Bionic's C library comes with its own pthread implementation bundled in
	ifneq ($(OS), Android)
		# Haiku has pthreads in its libroot, so only link it in on other platforms
		ifneq ($(KERNEL), Haiku)
			ifneq ($(COMP), ndk)
				LDFLAGS += -lpthread
			endif
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
	CXXFLAGS += -g3 -fsanitize=$(sanitize)
	LDFLAGS += -fsanitize=$(sanitize)
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
	
	ifeq ($(comp), clang)
		CXXFLAGS += -fexperimental-new-pass-manager
	endif
endif

### 3.4 Bits
ifeq ($(bits), 64)
	CXXFLAGS += -DIS_64BIT
endif

### 3.5 prefetch
ifeq ($(prefetch), yes)
	CXXFLAGS += -DUSE_PREFETCH
	ifeq ($(sse), yes)
		CXXFLAGS += -msse
	endif
endif

### 3.6 popcnt
ifeq ($(popcnt), yes)
	CXXFLAGS += -DUSE_POPCNT
	ifeq ($(arch), $(filter $(arch), ppc64 armv7 armv8 arm64))
		#CXXFLAGS +=
	else ifeq ($(comp), icc)
		CXXFLAGS += -msse3
	else
		CXXFLAGS += -msse3 -mpopcnt
	endif
endif

### 3.7 bmi2
ifeq ($(bmi2), yes)
	CXXFLAGS += -DUSE_BMI2
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -mbmi2
	endif
endif

ifeq ($(avx2), yes)
	CXXFLAGS += -DUSE_AVX2
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -mavx2
	endif
endif

ifeq ($(avx512), yes)
	CXXFLAGS += -DUSE_AVX512
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -mavx512f -mavx512bw
	endif
endif

ifeq ($(vnni256), yes)
	CXXFLAGS += -DUSE_VNNI
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -mavx512f -mavx512bw -mavx512vnni -mavx512dq -mavx512vl -mprefer-vector-width=256
	endif
endif

ifeq ($(vnni512), yes)
	CXXFLAGS += -DUSE_VNNI
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -mavx512vnni -mavx512dq -mavx512vl
	endif
endif

ifeq ($(sse41), yes)
	CXXFLAGS += -DUSE_SSE41
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -msse4.1
	endif
endif

ifeq ($(ssse3), yes)
	CXXFLAGS += -DUSE_SSSE3
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -mssse3
	endif
endif

ifeq ($(sse2), yes)
	CXXFLAGS += -DUSE_SSE2
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -msse2
	endif
endif

ifeq ($(mmx), yes)
	CXXFLAGS += -DUSE_MMX
	ifeq ($(comp), $(filter $(comp), gcc clang mingw))
		CXXFLAGS += -mmmx
	endif
endif

ifeq ($(neon), yes)
	CXXFLAGS += -DUSE_NEON
	ifeq ($(KERNEL), Linux)
		ifneq ($(COMP), ndk)
			ifneq ($(arch), armv8)
				CXXFLAGS += -mfpu=neon
			endif
		endif
	endif
endif

### 3.8 Link Time Optimization
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize), yes)
	ifeq ($(debug), no)
		ifeq ($(comp), clang)
			CXXFLAGS += -flto
			ifneq ($(findstring MINGW, $(KERNEL)), )
				CXXFLAGS += -fuse-ld=lld
			else ifneq ($(findstring MSYS, $(KERNEL)), )
				CXXFLAGS += -fuse-ld=lld
			endif
			LDFLAGS += $(CXXFLAGS)

		# GCC and CLANG use different methods for parallelizing LTO and CLANG pretends to be
		# GCC on some systems.
		else ifeq ($(comp), gcc)
			ifeq ($(gccisclang), )
				CXXFLAGS += -flto
				LDFLAGS += $(CXXFLAGS) -flto=jobserver
				ifneq ($(findstring MINGW, $(KERNEL)), )
					LDFLAGS += -save-temps
				else ifneq ($(findstring MSYS, $(KERNEL)), )
					LDFLAGS += -save-temps
				endif
			else
				CXXFLAGS += -flto
				LDFLAGS += $(CXXFLAGS)
			endif

		# To use LTO and static linking on windows, the tool chain requires a recent gcc:
		# gcc version 10.1 in msys2 or TDM-GCC version 9.2 are known to work, older might not.
		# So, only enable it for a cross from Linux by default.
		else ifeq ($(comp), mingw)
			ifeq ($(KERNEL), Linux)
				ifneq ($(arch), i386)
					CXXFLAGS += -flto
					LDFLAGS += $(CXXFLAGS) -flto=jobserver
				endif
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
ifneq ($(VERSION), )
	CXXFLAGS += -DUSE_VERSION=$(VERSION)
endif

### ==========================================================================
### Section 4. Public Targets
### ==========================================================================

help:
	@echo ""
	@echo "To compile DON, type: "
	@echo "----------------------"
	@echo "make target ARCH=arch [COMP=compiler] [COMPCXX=cxx]"
	@echo ""
	@echo "Supported targets:"
	@echo ""
	@echo "help                    > Display architecture details"
	@echo "build                   > Standard build"
	@echo "net                     > Download the default nnue net"
	@echo "profile-build           > Faster build (with profile-guided optimization)"
	@echo "strip                   > Strip executable"
	@echo "install                 > Install executable"
	@echo "clean                   > Clean up"
	@echo ""
	@echo "Supported archs:"
	@echo "------------------"
	@echo "x86-64-vnni512          > x86 64-bit with vnni support 512bit wide"
	@echo "x86-64-vnni256          > x86 64-bit with vnni support 256bit wide"
	@echo "x86-64-avx512           > x86 64-bit with avx512 support"
	@echo "x86-64-bmi2             > x86 64-bit with bmi2 support"
	@echo "x86-64-avx2             > x86 64-bit with avx2 support"
	@echo "x86-64-sse41-popcnt     > x86 64-bit with sse41 and popcnt support"
	@echo "x86-64-modern           > common modern CPU, currently x86-64-sse41-popcnt"
	@echo "x86-64-ssse3            > x86 64-bit with ssse3 support"
	@echo "x86-64-sse3-popcnt      > x86 64-bit with sse3 and popcnt support"
	@echo "x86-64                  > x86 64-bit generic (with sse2 support)"
	@echo "x86-32-sse41-popcnt     > x86 32-bit with sse41 and popcnt support"
	@echo "x86-32-sse2             > x86 32-bit with sse2 support"
	@echo "x86-32                  > x86 32-bit generic (with mmx and sse support)"
	@echo "ppc-64                  > PPC 64-bit"
	@echo "ppc-32                  > PPC 32-bit"
	@echo "armv7                   > ARMv7 32-bit"
	@echo "armv7-neon              > ARMv7 32-bit with popcnt and neon"
	@echo "armv8                   > ARMv8 64-bit with popcnt and neon"
	@echo "apple-silicon           > Apple silicon ARM64"
	@echo "general-64              > unspecified 64-bit"
	@echo "general-32              > unspecified 32-bit"
	@echo ""
	@echo "Supported compilers:"
	@echo "--------------------"
	@echo "gcc                     > GNU compiler (default)"
	@echo "mingw                   > GNU compiler with MinGW under Windows"
	@echo "clang                   > LLVM Clang compiler"
	@echo "icc                     > Intel compiler"
	@echo "ndk                     > Google NDK to cross-compile for Android"
	@echo ""
	@echo "Simple examples, for inexperienced users: "
	@echo "----------------------------------------------------------------"
	@echo ""
	@echo "make -j build ARCH=x86-64  (A portable, slow compile for 64-bit systems)"
	@echo "make -j build ARCH=x86-32  (A portable, slow compile for 32-bit systems)"
	@echo ""
	@echo "Advanced examples, for experienced users: "
	@echo ""
	@echo "make    help  ARCH=x86-64-bmi2"
	@echo "make -j profile-build ARCH=x86-64-bmi2 COMP=gcc COMPCXX=g++-9.0"
	@echo "make -j build ARCH=x86-64-ssse3 COMP=clang"
	@echo ""
ifeq ($(SUPPORTED_ARCH)$(help_skip_sanity), true)
	@echo "The selected architecture $(ARCH) will enable the following configuration: "
	@$(MAKE) ARCH=$(ARCH) COMP=$(COMP) config-sanity
else
	@echo "Specify a supported architecture with the ARCH option for more details"
	@echo ""
endif

.PHONY: help build profile-build strip install clean net objclean profileclean config-sanity \
        icc-profile-use icc-profile-make \
        gcc-profile-use gcc-profile-make \
        clang-profile-use clang-profile-make

build: net config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build: net config-sanity objclean profileclean
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
	$(STRIP) $(EXE)

install:
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

# Clean all
clean: objclean profileclean
	@rm -f .depend *~ core

# Evaluation network (nnue)
net:
	$(eval nnuenet := $(shell grep DefaultEvalFile evaluator.h | grep define | sed 's/.*\(nn-[a-z0-9]\{12\}.nnue\).*/\1/'))
	@echo "Default net: $(nnuenet)"
	$(eval nnuedownloadurl := https://tests.stockfishchess.org/api/nn/$(nnuenet))
	$(eval curl_or_wget := $(shell if hash curl 2>/dev/null; then echo "curl -skL"; elif hash wget 2>/dev/null; then echo "wget -qO-"; fi))
	@if test -f "$(nnuenet)"; then \
		echo "Already available."; \
	else \
		if [ "x$(curl_or_wget)" = "x" ]; then \
			echo "Automatic download failed: neither curl nor wget is installed. Install one of these tools or download the net manually"; \
			exit 1; \
		else \
			echo "Downloading $(nnuedownloadurl)"; \
			$(curl_or_wget) $(nnuedownloadurl) > $(nnuenet);\
		fi \
	fi
	$(eval shasum_command := $(shell if hash shasum 2>/dev/null; then echo "shasum -a 256 "; elif hash sha256sum 2>/dev/null; then echo "sha256sum "; fi))
	@if [ "x$(shasum_command)" != "x" ]; then \
		if [ "$(nnuenet)" != "nn-"`$(shasum_command) $(nnuenet) | cut -c1-12`".nnue" ]; then \
			echo "Failed download or $(nnuenet) corrupted, please delete!"; \
			exit 1; \
		fi \
	else \
		echo "shasum / sha256sum not found, skipping net validation"; \
	fi

# Clean binaries and objects
objclean:
	@rm -f $(EXE) *.o ./nnue/*.o ./nnue/features/*.o

# Clean auxiliary profiling files
profileclean:
	@rm -rf profdir
	@rm -f bench.txt *.s *.gcno *.gcda ./nnue/*.gcda ./nnue/features/*.gcda
	@rm -f DON.profdata *.profraw

default:
	help

### ==========================================================================
### Section 5. Private Targets
### ==========================================================================

all: $(EXE) .depend
	@echo "================================="
	@echo "*            SUCCESS            *"
	@echo "================================="

config-sanity: net
	@echo ""
	@echo "Config:"
	@echo "---------"
	@echo "debug   : '$(debug)'"
	@echo "sanitize: '$(sanitize)'"
	@echo "optimize: '$(optimize)'"
	@echo "arch    : '$(arch)'"
	@echo "comp    : '$(comp)'"
	@echo "bits    : '$(bits)'"
	@echo "kernel  : '$(KERNEL)'"
	@echo "os      : '$(OS)'"
	@echo "prefetch: '$(prefetch)'"
	@echo "popcnt  : '$(popcnt)'"
	@echo "sse     : '$(sse)'"
	@echo "mmx     : '$(mmx)'"
	@echo "sse2    : '$(sse2)'"
	@echo "ssse3   : '$(ssse3)'"
	@echo "sse41   : '$(sse41)'"
	@echo "avx2    : '$(avx2)'"
	@echo "bmi2    : '$(bmi2)'"
	@echo "avx512  : '$(avx512)'"
	@echo "vnni256 : '$(vnni256)'"
	@echo "vnni512 : '$(vnni512)'"
	@echo "neon    : '$(neon)'"
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
	@test "$(SUPPORTED_ARCH)" = "true"
	@test "$(arch)" = "any" || test "$(arch)" = "x86_64" || test "$(arch)" = "i386" || \
	 test "$(arch)" = "armv7" || test "$(arch)" = "armv8" || test "$(arch)" = "arm64" || \
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc"
	@test "$(bits)" = "32" || test "$(bits)" = "64"
	@test "$(prefetch)" = "yes" || test "$(prefetch)" = "no"
	@test "$(popcnt)" = "yes" || test "$(popcnt)" = "no"
	@test "$(sse)" = "yes" || test "$(sse)" = "no"
	@test "$(mmx)" = "yes" || test "$(mmx)" = "no"
	@test "$(sse2)" = "yes" || test "$(sse2)" = "no"
	@test "$(ssse3)" = "yes" || test "$(ssse3)" = "no"
	@test "$(sse41)" = "yes" || test "$(sse41)" = "no"
	@test "$(avx2)" = "yes" || test "$(avx2)" = "no"
	@test "$(bmi2)" = "yes" || test "$(bmi2)" = "no"
	@test "$(avx512)" = "yes" || test "$(avx512)" = "no"
	@test "$(vnni256)" = "yes" || test "$(vnni256)" = "no"
	@test "$(vnni512)" = "yes" || test "$(vnni512)" = "no"
	@test "$(neon)" = "yes" || test "$(neon)" = "no"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || \
	 test "$(comp)" = "mingw" || test "$(comp)" = "clang" || \
	 test "$(comp)" = "armv7a-linux-androideabi16-clang" || \
	 test "$(comp)" = "aarch64-linux-android21-clang"

# Generating obj files
$(EXE): $(OBJS)
	+$(CXX) -o $@ $(OBJS) $(LDFLAGS)

clang-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-generate' \
	EXTRALDFLAGS='-fprofile-instr-generate' \
	all

clang-profile-use:
	$(XCRUN) llvm-profdata merge -output=DON.profdata *.profraw
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
	-@$(CXX) $(DEPENDFLAGS) -MM $(SRCS) > $@ 2> /dev/null

-include .depend
