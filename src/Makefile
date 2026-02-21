# DON, UCI chess playing engine Copyright (C) 2003-2026
#
# DON is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# DON is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.


### ==========================================================================
### Section 1. General Configuration
### ==========================================================================

### Ensure Make uses Bash for shell commands
SHELL := /bin/bash

### Establish the operating system name
UNAME_S := $(shell uname -s 2>/dev/null)
UNAME_O := $(shell uname -o 2>/dev/null)

# Snapshot the environment OS *before* overwrite it
ENV_OS := $(OS)

KERNEL := $(UNAME_S)

ifeq ($(KERNEL),Linux)
    OS := $(if $(UNAME_O),$(UNAME_O),Linux)
else
    OS := $(KERNEL)
endif

### Command prefix to run the built executable (e.g. wine, sde, qemu)
### Backward compatible alias: WINE_PATH (deprecated)
ifneq ($(strip $(WINE_PATH)),)
# Only assign RUN_PREFIX if empty
ifeq ($(strip $(RUN_PREFIX)),)
    RUN_PREFIX := $(WINE_PATH)
endif
# Top-level warnings
ifeq ($(MAKELEVEL),0)
ifneq ($(strip $(RUN_PREFIX)),$(strip $(WINE_PATH)))
    $(warning *** Both RUN_PREFIX and WINE_PATH are set; ignoring WINE_PATH. ***)
else
    $(warning *** WINE_PATH is deprecated; use RUN_PREFIX instead. ***)
endif
endif
endif

OS_WINDOWS := no
# Windows OS
ifeq ($(ENV_OS),Windows_NT)
ifneq ($(COMP),ndk)
    OS_WINDOWS := yes
endif
else ifeq ($(COMP),mingw)
    OS_WINDOWS := yes
# Set RUN_PREFIX to wine if not already set
ifeq ($(RUN_PREFIX),)
    RUN_PREFIX := $(shell command -v wine 2>/dev/null)
endif
endif

# Cross-platform command to detect executable
ifeq ($(OS_WINDOWS),yes)
    FIND_EXE = where "$1" 2>nul
else
    FIND_EXE = command -v "$1" 2>/dev/null
endif

# -------------------------------------------
# Detect clang-format (prefer newest version)
# -------------------------------------------
CLANG_FORMAT := $(shell \
    $(call FIND_EXE,clang-format-20) || \
    $(call FIND_EXE,clang-format-19) || \
    $(call FIND_EXE,clang-format-18) || \
    echo clang-format)

# Optional: log which one was chosen
#$(info Using CLANG_FORMAT = $(CLANG_FORMAT))

### Executable name
NAME := DON
ifeq ($(OS_WINDOWS),yes)
    EXE := $(NAME).exe
else
    EXE := $(NAME)
endif

### Installation dir definitions
PREFIX := /usr/local
BINDIR := $(PREFIX)/bin

### Built-in benchmark for pgo-builds
PGOBENCH := $(RUN_PREFIX) ./$(EXE) bench

### Source and object files
SRCS := benchmark.cpp bitboard.cpp engine.cpp evaluate.cpp main.cpp memory.cpp \
    misc.cpp movegen.cpp movepick.cpp option.cpp perft.cpp polybook.cpp \
    position.cpp search.cpp thread.cpp timeman.cpp tt.cpp tune.cpp uci.cpp \
    nnue/features/full_threats.cpp nnue/features/half_ka_v2_hm.cpp \
    nnue/accumulator.cpp nnue/network.cpp nnue/nmisc.cpp syzygy/tablebase.cpp

HEADERS := benchmark.h bitboard.h engine.h evaluate.h history.h memory.h misc.h movegen.h \
    movepick.h numa.h option.h perft.h polybook.h position.h prng.h search.h shm.h \
    thread.h timeman.h tt.h tune.h types.h uci.h incbin/incbin.h \
    nnue/features/full_threats.h nnue/features/half_ka_v2_hm.h \
    nnue/layers/affine_transform.h nnue/layers/affine_transform_sparse_input.h \
    nnue/layers/clipped_relu.h nnue/layers/sqr_clipped_relu.h \
    nnue/accumulator.h nnue/architecture.h nnue/common.h nnue/feature_transformer.h \
    nnue/network.h nnue/nmisc.h nnue/simd.h syzygy/tablebase.h

OBJS := $(notdir $(SRCS:.cpp=.o))

VPATH := nnue:nnue/features:syzygy

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
# To add a new ARCH:
#    1. Update Section 2.1 (general defaults)
#    2. Update Section 2.2 (architecture-specific settings)
#    3. Run 'make help' to verify your new architecture appears correctly
#
# Note: The order of flags matters; SIMD flags should be enabled after setting bits/arch
#
# Flag                --- Comp switch        --- Description
# ----------------------------------------------------------------------------
#
# debug = yes/no      --- -DNDEBUG           --- Enable/Disable debug mode
# sanitize = none/<sanitizer> ... (-fsanitize )
#                     --- ( undefined )      --- enable undefined behavior checks
#                     --- ( thread    )      --- enable threading error checks
#                     --- ( address   )      --- enable memory access checks
#                     --- ...etc...          --- see compiler documentation for supported sanitizers
# optimize = yes/no   --- (-O3/-fast etc.)   --- Enable/Disable optimizations
# arch = (name)       --- (-arch)            --- Target architecture
# bits = 64/32        --- -DIS_64BIT         --- 64-/32-bit operating system
# prefetch = yes/no   --- -DUSE_PREFETCH     --- Use prefetch asm-instruction
# popcnt = yes/no     --- -DUSE_POPCNT       --- Use popcnt asm-instruction
# sse = yes/no        --- -msse              --- Use Intel Streaming SIMD Extensions
# mmx = yes/no        --- -mmmx              --- Use Intel MMX instructions
# sse2 = yes/no       --- -msse2             --- Use Intel Streaming SIMD Extensions 2
# ssse3 = yes/no      --- -mssse3            --- Use Intel Supplemental Streaming SIMD Extensions 3
# sse41 = yes/no      --- -msse4.1           --- Use Intel Streaming SIMD Extensions 4.1
# avx2 = yes/no       --- -mavx2             --- Use Intel Advanced Vector Extensions 2
# bmi2 = yes/no       --- -DUSE_BMI2         --- Use pext & pdep x86_64 asm-instruction
# comp = yes/no       --- -DUSE_COMP         --- Internal flag for compressed-optimized variants
# avxvnni = yes/no    --- -mavxvnni          --- Use Intel Vector Neural Network Instructions AVX
# avx512 = yes/no     --- -mavx512bw         --- Use Intel Advanced Vector Extensions 512
# vnni256 = yes/no    --- -mavx256vnni       --- Use Intel Vector Neural Network Instructions 512 with 256bit operands
# vnni512 = yes/no    --- -mavx512vnni       --- Use Intel Vector Neural Network Instructions 512
# avx512icl = yes/no  --- ... multiple ...   --- Use All AVX-512 features available on both Intel Ice Lake and AMD Zen 4
# altivec = yes/no    --- -maltivec          --- Use PowerPC Altivec SIMD extension
# vsx = yes/no        --- -mvsx              --- Use POWER VSX SIMD extension
# neon = yes/no       --- -DUSE_NEON         --- Use ARM SIMD architecture
# dotprod = yes/no    --- -DUSE_NEON_DOTPROD --- Use ARM advanced SIMD Int8 dot product instructions
# lsx = yes/no        --- -mlsx              --- Use Loongson SIMD eXtension
# lasx = yes/no       --- -mlasx             --- use Loongson Advanced SIMD eXtension
#
# native              --- Auto-detect best architecture for host CPU using ../scripts/get_native_properties.sh
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.
#
# Example of use for these flags:
# make build ARCH=x86-64-avx512 debug=yes sanitize="address undefined"


### 2.1. General and architecture defaults

ifeq ($(ARCH),)
   ARCH = native
endif

ifeq ($(ARCH),native)
   override ARCH := $(shell $(SHELL) ../scripts/get_native_properties.sh | cut -d " " -f 1)
endif

# Explicitly check for the list of supported architectures (as listed with make help),
# User can override with 'make ARCH=x86-64-avx512icl ARCH_SUPPORT=yes'
# GNU Make filter:
#   - returns matches, not true/false
#   - non-empty => ARCH found in supported list
ifneq ($(filter x86-64-avx512icl-comp x86-64-avx512icl x86-64-vnni512-comp x86-64-vnni512 x86-64-vnni256-comp x86-64-vnni256 \
                x86-64-avx512-comp x86-64-avx512 x86-64-avxvnni-comp x86-64-avxvnni x86-64-bmi2-comp x86-64-bmi2 \
                x86-64-avx2 x86-64-sse41-popcnt x86-64-modern x86-64-sse41 x86-64-ssse3-popcnt x86-64-ssse3 x86-64 general-64 \
                x86-32-sse41-popcnt x86-32-sse41 x86-32-sse2 x86-32 general-32 \
                ppc-64-altivec ppc-64-vsx ppc-64 ppc-32 e2k armv7-neon armv7 armv8-dotprod armv8 apple-silicon riscv64 \
                loongarch64-lasx loongarch64-lsx loongarch64,$(ARCH)),)
   ARCH_SUPPORT = yes
else
   ARCH_SUPPORT = no
endif

optimize = yes
debug = no
sanitize = none
bits = 64
sse_fp = no
prefetch = no
popcnt = no
sse = no
mmx = no
sse2 = no
ssse3 = no
sse41 = no
avx2 = no
bmi2 = no
comp = no
avxvnni = no
avx512 = no
vnni256 = no
vnni512 = no
avx512icl = no
arm_version = 0
neon = no
dotprod = no
altivec = no
vsx = no
lsx = no
lasx = no
STRIP = strip

### 2.2 Architecture specific

ifeq ($(findstring x86,$(ARCH)),x86)

# x86-32/64
# To enable sse use: make -j build ARCH=x86-32 sse=yes
ifeq ($(findstring x86-32,$(ARCH)),x86-32)
    arch = i386
    bits = 32
    sse = no
    mmx = yes
else
    arch = x86_64
    sse = yes
    sse2 = yes
endif

ifeq ($(findstring -sse,$(ARCH)),-sse)
    sse = yes
endif

ifeq ($(findstring -popcnt,$(ARCH)),-popcnt)
    popcnt = yes
endif

ifeq ($(findstring -mmx,$(ARCH)),-mmx)
    mmx = yes
endif

ifeq ($(findstring -sse2,$(ARCH)),-sse2)
    sse = yes
    sse2 = yes
endif

ifeq ($(findstring -ssse3,$(ARCH)),-ssse3)
    sse = yes
    sse2 = yes
    ssse3 = yes
endif

ifeq ($(findstring -sse41,$(ARCH)),-sse41)
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
endif

ifeq ($(findstring -modern,$(ARCH)),-modern)
    $(warning *** ARCH=$(ARCH) is deprecated, defaulting to ARCH=x86-64-sse41-popcnt. Execute 'make help' for a list of available architectures. ***)
    $(shell sleep 5)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
endif

ifeq ($(findstring -avx2,$(ARCH)),-avx2)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
endif

ifeq ($(findstring -bmi2,$(ARCH)),-bmi2)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
endif

ifeq ($(findstring -comp,$(ARCH)),-comp)
    comp = yes
endif

ifeq ($(findstring -avxvnni,$(ARCH)),-avxvnni)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
    avxvnni = yes
endif

ifeq ($(findstring -avx512,$(ARCH)),-avx512)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
    avx512 = yes
endif

ifeq ($(findstring -vnni256,$(ARCH)),-vnni256)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
    vnni256 = yes
endif

ifeq ($(findstring -vnni512,$(ARCH)),-vnni512)
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

ifeq ($(findstring -avx512icl,$(ARCH)),-avx512icl)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
    avx512 = yes
    vnni512 = yes
    avx512icl = yes
endif

ifeq ($(sse),yes)
    prefetch = yes
endif

# 64-bit bmi2 is not available on x86-32
ifeq ($(findstring x86-32,$(ARCH)),x86-32)
    sse_fp = yes
    bmi2 = no
endif

else

# all other architectures

ifeq ($(ARCH),general-32)
    arch = any
    bits = 32
    sse_fp = yes
endif

ifeq ($(ARCH),general-64)
    arch = any
endif

ifeq ($(ARCH),armv7)
    arch = armv7
    bits = 32
    prefetch = yes
    arm_version = 7
endif

ifeq ($(ARCH),armv7-neon)
    arch = armv7
    bits = 32
    prefetch = yes
    popcnt = yes
    arm_version = 7
    neon = yes
endif

ifeq ($(ARCH),armv8)
    arch = armv8
    prefetch = yes
    popcnt = yes
    arm_version = 8
    neon = yes
endif

ifeq ($(ARCH),armv8-dotprod)
    arch = armv8
    prefetch = yes
    popcnt = yes
    arm_version = 8
    neon = yes
    dotprod = yes
endif

ifeq ($(ARCH),apple-silicon)
    arch = arm64
    prefetch = yes
    popcnt = yes
    arm_version = 8
    neon = yes
    dotprod = yes
endif

ifeq ($(ARCH),ppc-32)
    arch = ppc
    bits = 32
endif

ifeq ($(ARCH),ppc-64)
    arch = ppc64
    popcnt = yes
    prefetch = yes
endif

ifeq ($(ARCH),ppc-64-altivec)
    arch = ppc64
    popcnt = yes
    prefetch = yes
    altivec = yes
endif

ifeq ($(ARCH),ppc-64-vsx)
    arch = ppc64
    popcnt = yes
    prefetch = yes
    vsx = yes
endif

ifeq ($(findstring e2k,$(ARCH)),e2k)
    arch = e2k
    bits = 64
    sse = yes
    mmx = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    popcnt = yes
endif

ifeq ($(ARCH),riscv64)
    arch = riscv64
endif

ifeq ($(findstring loongarch64,$(ARCH)),loongarch64)
    arch = loongarch64
    prefetch = yes

    ifeq ($(findstring -lsx,$(ARCH)),-lsx)
        lsx = yes
    endif

    ifeq ($(findstring -lasx,$(ARCH)),-lasx)
        lsx = yes
        lasx = yes
    endif
endif

endif


### ==========================================================================
### Section 3. Low-level Configuration
### ==========================================================================

### 3.1 Selecting compiler (default = gcc)
ifeq ($(MAKELEVEL),0)
    export ENV_CXXFLAGS    := $(CXXFLAGS)
    export ENV_DEPENDFLAGS := $(DEPENDFLAGS)
    export ENV_LDFLAGS     := $(LDFLAGS)
endif

CXXFLAGS    = $(ENV_CXXFLAGS) -Wall -Wcast-qual -fno-exceptions -std=c++17 $(EXTRACXXFLAGS)
DEPENDFLAGS = $(ENV_DEPENDFLAGS) -Wall -Wcast-qual -std=c++17
LDFLAGS     = $(ENV_LDFLAGS) $(EXTRALDFLAGS) $(EXTRACXXFLAGS)

ifeq ($(COMP),)
    COMP=gcc
endif

ifeq ($(OS_WINDOWS),yes)
    LDFLAGS += -static
endif

# ----------------------------
# Compiler configuration
# ----------------------------

# Detect if GCC is actually Clang
GCC_VERSION :=
GCC_CLANG   := no
GCC_APPLE   := no
GCC_GNU_LD  := no

ifeq ($(COMP),gcc)
    CXX := g++
    CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-declarations
    GCC_VERSION := $(shell $(CXX) --version 2>/dev/null)
    GCC_CLANG   := $(if $(findstring clang,$(GCC_VERSION)),yes,no)
    GCC_APPLE   := $(if $(findstring Apple,$(GCC_VERSION)),yes,no)
    GCC_GNU_LD  := $(if $(findstring GNU ld,$(shell $(CXX) -Wl,--version 2>/dev/null)),yes,no)
    # Darwin GCC
    ifeq ($(KERNEL),Darwin)
        EXTRAPROFILEFLAGS = -fvisibility=hidden
    # Non-Darwin GCC
    else
        CXXFLAGS += -Wno-stringop-overflow
        ifeq ($(GCC_GNU_LD),yes)
            LDFLAGS += -Wl,--no-as-needed
        endif
        # Profile build adjustments for Clang masquerading as GCC
        ifeq ($(GCC_CLANG),yes)
        ifeq ($(GCC_APPLE),no)
        # Link std::filesystem for Linux Clang masquerading as GCC
        ifeq ($(KERNEL),Linux)
            LDFLAGS += -lstdc++fs
        endif
        endif
        endif
    endif
    # Set profile targets outside Darwin/non-Darwin split
    ifeq ($(GCC_CLANG),yes)
        profile_make = clang-profile-make
        profile_use  = clang-profile-use
    else
        CXXFLAGS += -Wstack-usage=128000
        profile_make = gcc-profile-make
        profile_use  = gcc-profile-use
    endif
    # Architectures with special handling
    # Atomic library for specific architectures (not armv8)
    ifneq ($(filter armv8 armv7 riscv64,$(arch)),)
    ifeq ($(OS),Android)
    ifneq ($(filter 32 64,$(bits)),)
        CXXFLAGS += -m$(bits)
        LDFLAGS  += -m$(bits)
    endif
    endif
    ifneq ($(filter armv7 riscv64,$(arch)),)
        LDFLAGS += -latomic
    endif
    else ifeq ($(arch),loongarch64)
        LDFLAGS += -latomic
    # Generic architectures
    else ifneq ($(filter 32 64,$(bits)),)
        CXXFLAGS += -m$(bits)
        LDFLAGS  += -m$(bits)
    endif

else ifeq ($(COMP),clang)
    CXX := clang++
    ifeq ($(OS_WINDOWS),yes)
    ifeq ($(bits),64)
        CXX := x86_64-w64-mingw32-clang++
    else
        CXX := i686-w64-mingw32-clang++
    endif
    endif
    CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-prototypes -Wconditional-uninitialized -flax-vector-conversions=none
    # Atomic library for non-Windows and non-[Darwin/FreeBSD/OpenBSD] (unless compiler-rt)
    ifneq ($(OS_WINDOWS),yes)
    ifeq ($(filter $(KERNEL),Darwin OpenBSD FreeBSD),)
    ifneq ($(RTLIB),compiler-rt)
        LDFLAGS += -latomic
    endif
    endif
    endif
    # Architectures with special handling
    # Atomic library for specific architectures (not armv8)
    ifneq ($(filter armv8 armv7 riscv64,$(arch)),)
    ifeq ($(OS),Android)
    ifneq ($(filter 32 64,$(bits)),)
        CXXFLAGS += -m$(bits)
        LDFLAGS  += -m$(bits)
    endif
    endif
    ifneq ($(filter armv7 riscv64,$(arch)),)
        LDFLAGS += -latomic
    endif
    else ifeq ($(arch),loongarch64)
        LDFLAGS += -latomic
    # Generic architectures
    else ifneq ($(filter 32 64,$(bits)),)
        CXXFLAGS += -m$(bits)
        LDFLAGS  += -m$(bits)
    endif

    # Link the separate filesystem library for Linux/Clang.
    # Older versions of Clang (pre-9) require -lstdc++fs to use std::filesystem,
    # because filesystem was not yet fully integrated into libstdc++.
    # Modern Clang versions (9+) include filesystem in the standard library,
    # so this flag is usually harmless but may be unnecessary.
    ifeq ($(KERNEL),Linux)
        LDFLAGS += -lstdc++fs
    endif
    # Profile build for Clang
    profile_make = clang-profile-make
    profile_use  = clang-profile-use

else ifeq ($(COMP),mingw)
    ifeq ($(bits),64)
        MINGW_CXX       := x86_64-w64-mingw32-c++
        MINGW_CXX_POSIX := x86_64-w64-mingw32-c++-posix
    else
        MINGW_CXX       := i686-w64-mingw32-c++
        MINGW_CXX_POSIX := i686-w64-mingw32-c++-posix
    endif

    # Prefer POSIX variant if available
    CXX := $(if $(shell command -v $(MINGW_CXX_POSIX) 2>/dev/null), \
            $(MINGW_CXX_POSIX), \
            $(MINGW_CXX))
    CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-declarations
    # Profile build for mingw (use GCC defaults)
    profile_make = gcc-profile-make
    profile_use  = gcc-profile-use

else ifeq ($(COMP),icx)
    CXX := icpx
    CXXFLAGS += --intel -pedantic -Wextra -Wshadow -Wmissing-prototypes -Wconditional-uninitialized -Wabi -Wdeprecated
    # Profile build for icx
    profile_make = icx-profile-make
    profile_use  = icx-profile-use
# To cross-compile for Android, use NDK version r27c or later
else ifeq ($(COMP),ndk)
    # Detect host tag (simplified common cases)
    ifeq ($(OS_WINDOWS),yes)
        HOST_TAG := windows-x86_64
    else ifeq ($(KERNEL),Darwin)
        HOST_TAG := darwin-x86_64
    else
        HOST_TAG := linux-x86_64
    endif

    NDK ?= $(ANDROID_NDK_ROOT)
    TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/$(HOST_TAG)/bin
    ANDROID_API ?= 29

    ifeq ($(arch),armv8)
        TARGET := aarch64-linux-android
        CXX := $(TOOLCHAIN)/$(TARGET)$(ANDROID_API)-clang++
    else ifeq ($(arch),x86_64)
        TARGET := x86_64-linux-android
        CXX := $(TOOLCHAIN)/$(TARGET)$(ANDROID_API)-clang++
    else ifeq ($(arch),armv7)
        TARGET := armv7a-linux-androideabi
        CXX := $(TOOLCHAIN)/$(TARGET)$(ANDROID_API)-clang++
        CXXFLAGS += -mthumb -march=armv7-a -mfloat-abi=softfp -mfpu=neon
    else ifeq ($(arch),x86)
        TARGET := i686-linux-android
        CXX := $(TOOLCHAIN)/$(TARGET)$(ANDROID_API)-clang++
    else
        $(error Unsupported NDK arch: $(arch))
    endif

    # Correct STL usage
    CXXFLAGS += -stdlib=libc++
    LDFLAGS  += -static-libstdc++ -lc++abi

    STRIP := $(TOOLCHAIN)/llvm-strip
    # Profile build for NDK/Clang
    profile_make = clang-profile-make
    profile_use  = clang-profile-use
endif

ifeq ($(KERNEL),Darwin)
    XCRUN = xcrun
    CXXFLAGS += -mmacosx-version-min=10.15
    LDFLAGS  += -mmacosx-version-min=10.15
    ifneq ($(arch),any)
        CXXFLAGS += -arch $(arch)
        LDFLAGS  += -arch $(arch)
    endif
endif

### Allow overwriting CXX from command line
ifdef COMPCXX
    CXX = $(COMPCXX)
endif

# Force SSE floating-point math to avoid x87 extended-precision differences
# Ensures consistent floating-point results across x86 CPUs
ifeq ($(sse_fp),yes)
    CXXFLAGS += -mfpmath=sse -msse2
endif

# llvm-profdata must be version compatible with the specified CXX (be it clang, or the gcc alias)
# make -j profile-build CXX=clang++-20 COMP=clang
# Locate the version in the same directory as the compiler used,
# with fallback to a generic one if it can't be located
# Used for PGO (profile-guided optimization) builds
LLVM_PROFDATA := $(dir $(realpath $(shell command -v $(CXX) 2>/dev/null)))llvm-profdata
# for icx
ifeq ($(wildcard $(LLVM_PROFDATA)),)
    LLVM_PROFDATA := $(dir $(realpath $(shell command -v $(CXX) 2>/dev/null)))/compiler/llvm-profdata
endif
ifeq ($(wildcard $(LLVM_PROFDATA)),)
    LLVM_PROFDATA := llvm-profdata
endif

### On mingw use Windows threads, otherwise POSIX
ifneq ($(COMP),mingw)
    CXXFLAGS += -DUSE_PTHREADS

    # Link pthreads and optionally -lrt on supported platforms

    # On Android Bionic's C library comes with its own pthread implementation bundled in
    ifneq ($(OS),Android)
    # Haiku has pthreads in its libroot, so only link it in on other platforms
    ifneq ($(KERNEL),Haiku)
    ifneq ($(COMP),ndk)
        LDFLAGS += -lpthread
        # Determine if LRT should be linked (realtime library)
        LINK_LRT := yes
        # Disable LRT on Windows
        ifeq ($(OS_WINDOWS),yes)
            LINK_LRT := no
        endif
        # Disable LRT on macOS
        ifeq ($(KERNEL),Darwin)
            LINK_LRT := no
        endif
        # Add LRT only if allowed
        ifeq ($(LINK_LRT),yes)
            LDFLAGS += -lrt
        endif
    endif
    endif
    endif
endif

### 3.2.1 Debugging
ifeq ($(debug),no)
    CXXFLAGS += -DNDEBUG
else
    CXXFLAGS += -g
    # Enable libstdc++ debug checks (harmless on libc++)
    CXXFLAGS += -D_GLIBCXX_ASSERTIONS -D_GLIBCXX_DEBUG
endif

### 3.2.2 Debugging with undefined behavior sanitizers
ifneq ($(sanitize),none)
    SAN_FLAGS := $(addprefix -fsanitize=,$(sanitize))

    CXXFLAGS += $(SAN_FLAGS) -g3 -fno-omit-frame-pointer
    LDFLAGS  += $(SAN_FLAGS)
endif

### 3.3 Optimization
ifeq ($(optimize),yes)
    CXXFLAGS += -O3 -funroll-loops

    ifeq ($(COMP),gcc)
    ifeq ($(OS),Android)
        CXXFLAGS += -fno-gcse -mthumb -march=armv7-a -mfloat-abi=softfp
    endif
    else ifneq ($(filter clang ndk,$(COMP)),)
        CLANG_MAJOR_VERSION := $(shell $(CXX) -dumpversion 2>/dev/null | cut -f1 -d.)
        ifeq ($(shell expr $(CLANG_MAJOR_VERSION) \< 16),1)
            CXXFLAGS += -fexperimental-new-pass-manager
        endif
    endif

    ifeq ($(KERNEL),Darwin)
    ifneq ($(filter clang icx ndk,$(COMP)),)
        CXXFLAGS += -mdynamic-no-pic
    else ifeq ($(COMP),gcc)
    ifneq ($(arch),arm64)
        CXXFLAGS += -mdynamic-no-pic
    endif
    endif
    endif
endif

### 3.4 Bits
ifeq ($(bits),64)
    CXXFLAGS += -DIS_64BIT
endif

### 3.5 prefetch and popcount
ifeq ($(prefetch),yes)
    CXXFLAGS += -DUSE_PREFETCH
    ifeq ($(sse),yes)
        CXXFLAGS += -msse
    endif
endif

ifeq ($(popcnt),yes)
    ifneq ($(filter ppc64 ppc64-altivec ppc64-vsx armv7 armv8 arm64,$(arch)),)
        CXXFLAGS += -DUSE_POPCNT
    else
        CXXFLAGS += -DUSE_POPCNT -msse3 -mpopcnt
    endif
endif

### 3.6 SIMD architectures
ifeq ($(avx512icl),yes)
    CXXFLAGS += -DUSE_AVX512ICL
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mavx512vnni -mavx512vbmi2 -mavx512vbmi -mavx512vpopcntdq -mavx512bitalg -mavx512ifma -mavx512vl -mavx512dq -mavx512bw -mavx512cd -mavx512f -mvpclmulqdq -mgfni -mvaes
endif
endif

ifeq ($(vnni512),yes)
    CXXFLAGS += -DUSE_VNNI
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mavx512vnni -mavx512f -mavx512bw -mavx512dq -mavx512vl -mprefer-vector-width=512
endif
endif

ifeq ($(vnni256),yes)
    CXXFLAGS += -DUSE_VNNI
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mavx512vnni -mavx512f -mavx512bw -mavx512dq -mavx512vl -mprefer-vector-width=256
endif
endif

ifeq ($(avx512),yes)
    CXXFLAGS += -DUSE_AVX512
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mavx512f -mavx512bw -mavx512dq -mavx512vl
endif
endif

ifeq ($(avxvnni),yes)
    CXXFLAGS += -DUSE_AVXVNNI -DUSE_VNNI
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mavxvnni
endif
endif

ifeq ($(avx2),yes)
    CXXFLAGS += -DUSE_AVX2
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mavx2 -mbmi
endif
endif

ifeq ($(sse41),yes)
    CXXFLAGS += -DUSE_SSE41
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -msse4.1
endif
endif

ifeq ($(ssse3),yes)
    CXXFLAGS += -DUSE_SSSE3
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mssse3
endif
endif

ifeq ($(sse2),yes)
    CXXFLAGS += -DUSE_SSE2
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -msse2
endif
endif

ifeq ($(mmx),yes)
    #CXXFLAGS += -DUSE_MMX
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mmmx
endif
endif

ifeq ($(altivec),yes)
    CXXFLAGS += -maltivec
ifeq ($(COMP),gcc)
    CXXFLAGS += -mabi=altivec
endif
endif

ifeq ($(vsx),yes)
    CXXFLAGS += -mvsx
ifeq ($(COMP),gcc)
    CXXFLAGS += -DNO_WARN_X86_INTRINSICS -DUSE_SSE2
endif
endif

ifeq ($(neon),yes)
    CXXFLAGS += -DUSE_NEON=$(arm_version)
ifeq ($(KERNEL),Linux)
ifneq ($(COMP),ndk)
ifneq ($(arch),armv8)
    CXXFLAGS += -mfpu=neon
endif
endif
endif
endif

ifeq ($(dotprod),yes)
    CXXFLAGS += -DUSE_NEON_DOTPROD -march=armv8.2-a+dotprod
endif

ifeq ($(lasx),yes)
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mlasx
endif
endif

ifeq ($(lsx),yes)
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mlsx
endif
endif

### 3.7 bmi2
ifeq ($(bmi2),yes)
    CXXFLAGS += -DUSE_BMI2
ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
    CXXFLAGS += -mbmi2
endif
ifeq ($(comp),yes)
    CXXFLAGS += -DUSE_COMP
endif
endif

### 3.8.1 Try to include git commit date for versioning
GIT_DATE := $(shell git show -s --date=format:'%Y%m%d' --format=%cd HEAD 2>/dev/null)
ifneq ($(GIT_DATE),)
    CXXFLAGS += -DGIT_DATE=$(GIT_DATE)
endif

### 3.8.2 Try to include git commit sha for versioning
GIT_SHA := $(shell git rev-parse HEAD 2>/dev/null | cut -c 1-8)
ifneq ($(GIT_SHA),)
    CXXFLAGS += -DGIT_SHA=$(GIT_SHA)
endif

### 3.8.3 Try to include architecture
ifneq ($(ARCH),)
    CXXFLAGS += -DARCH=$(ARCH)
endif

### 3.9 Link Time Optimization
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize),yes)
ifeq ($(debug),no)
    ifneq ($(KERNEL),Darwin)
        # Android NDK clang -> always use LLD
        ifeq ($(COMP),ndk)
            LDFLAGS += -fuse-ld=lld
        # Non-Android clang -> detect LLD first
        else
            HAS_LLD := $(if $(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null),yes,no)
            # Only add -fuse-ld=lld if found LLD
            ifeq ($(HAS_LLD),yes)
                # clang always supports -fuse-ld=lld
                ifeq ($(COMP),clang)
                    LDFLAGS += -fuse-ld=lld
                # gcc builds that are clang-compatible
                else ifeq ($(GCC_CLANG),yes)
                    LDFLAGS += -fuse-ld=lld
                endif
            endif
        endif
    endif
    # Determine LTO flags
    LTO_FLAGS :=
    LDFLTO_EXTRA :=
    # Clang / ICX / NDK -> always full LTO
    ifneq ($(filter clang icx ndk,$(COMP)),)
        LTO_FLAGS := -flto=full
        # ICX-specific flags
        ifeq ($(COMP),icx)
            LTO_FLAGS += -fwhole-program-vtables
        endif
    # GCC and CLANG use different methods for parallelizing LTO
    # and CLANG pretends to be GCC on some systems.
    else ifeq ($(COMP),gcc)
        ifneq ($(KERNEL),Darwin)
            # GCC (not pretending to be clang) uses parallel LTO
            ifeq ($(GCC_CLANG),no)
                LTO_FLAGS    := -flto -flto-partition=one
                LDFLTO_EXTRA := -flto=jobserver
            # GCC pretending to be clang
            else
                LTO_FLAGS := -flto=full
            endif
        endif
    # To use LTO and static linking on Windows,
    # the tool chain requires gcc version 10.1 or later.
    else ifeq ($(COMP),mingw)
        LTO_FLAGS    := -flto -flto-partition=one
        LDFLTO_EXTRA := -flto=jobserver -save-temps
    endif
    # Apply LTO flags
    CXXFLAGS += $(LTO_FLAGS)
    LDFLAGS  += $(LTO_FLAGS) $(LDFLTO_EXTRA)
endif
# Inline settings
ifneq ($(filter clang ndk,$(COMP)),)
    CXXFLAGS += -Xclang -mllvm -Xclang -inline-threshold=500
endif
endif

### 3.10 Use PIE executables on Android 5+, incompatible with Android 4 and earlier
ifeq ($(OS),Android)
    CXXFLAGS += -fPIE
    LDFLAGS  += -fPIE -pie
endif

# -------------------------------
# Compiler-specific flags
# -------------------------------

# Windows + GCC (not Clang, MinGW targets)
# Only add -fno-keep-inline-dllexport for MinGW GCC DLL builds
ifeq ($(COMP),gcc)
ifeq ($(GCC_CLANG),no)
ifneq ($(filter x86_64-w64-mingw32% i686-w64-mingw32%, $(CXX)),)
    CXXFLAGS += -fno-keep-inline-dllexport
endif
endif
endif

# Non-Windows, non-macOS: enable -pipe for faster compilation
# Disabled for Android NDK due to toolchain issues
ifneq ($(OS_WINDOWS),yes)
ifneq ($(KERNEL),Darwin)
ifneq ($(COMP),ndk)
    CXXFLAGS += -pipe
endif
endif
endif

MKDIR := mkdir -p -m 755
CP    := cp

### ==========================================================================
### Section 4. Public Targets
### ==========================================================================

help:
	@echo "" && \
	echo "To compile $(NAME), type: " && \
	echo "" && \
	echo "make -j target [ARCH=arch] [COMP=compiler] [COMPCXX=cxx]" && \
	echo "" && \
	echo "Supported targets:" && \
	echo "" && \
	echo "help                    > Display architecture details" && \
	echo "profile-build           > standard build with profile-guided optimization" && \
	echo "build                   > skip profile-guided optimization" && \
	echo "net                     > Download the default nnue nets" && \
	echo "strip                   > Strip executable" && \
	echo "install                 > Install executable" && \
	echo "clean                   > Clean up" && \
	echo "" && \
	echo "Supported archs:" && \
	echo "" && \
	echo "native                  > select the best architecture for the host processor (default)" && \
	echo "x86-64-avx512icl        > x86 64-bit with minimum avx512 support of Intel Ice Lake or AMD Zen 4" && \
	echo "x86-64-vnni512          > x86 64-bit with vnni support, operands 512-bit wide" && \
	echo "x86-64-vnni256          > x86 64-bit with vnni support, operands limited to 256-bit wide" && \
	echo "x86-64-avx512           > x86 64-bit with avx512 support" && \
	echo "x86-64-avxvnni          > x86 64-bit with vnni 256bit support" && \
	echo "x86-64-bmi2             > x86 64-bit with bmi2 support" && \
	echo "x86-64-avx2             > x86 64-bit with avx2 support" && \
	echo "x86-64-sse41-popcnt     > x86 64-bit with sse41 and popcnt support" && \
	echo "x86-64-modern           > deprecated, currently x86-64-sse41-popcnt" && \
	echo "x86-64-sse41            > x86 64-bit with sse41 support" && \
	echo "x86-64-ssse3-popcnt     > x86 64-bit with ssse3 and popcnt support" && \
	echo "x86-64-ssse3            > x86 64-bit with ssse3 support" && \
	echo "x86-64                  > x86 64-bit generic with sse2 support" && \
	echo "general-64              > unspecified 64-bit" && \
	echo "x86-32-sse41-popcnt     > x86 32-bit with sse41 and popcnt support" && \
	echo "x86-32-sse41            > x86 32-bit with sse41 support" && \
	echo "x86-32-sse2             > x86 32-bit with sse2 support" && \
	echo "x86-32                  > x86 32-bit generic with mmx support" && \
	echo "general-32              > unspecified 32-bit" && \
	echo "ppc-64-altivec          > PPC 64-bit with altivec support" && \
	echo "ppc-64-vsx              > PPC 64-bit with vsx support" && \
	echo "ppc-64                  > PPC 64-bit" && \
	echo "ppc-32                  > PPC 32-bit" && \
	echo "e2k                     > Elbrus 2000" && \
	echo "armv7                   > ARMv7 32-bit" && \
	echo "armv7-neon              > ARMv7 32-bit with popcnt and neon" && \
	echo "armv8                   > ARMv8 64-bit with popcnt and neon" && \
	echo "armv8-dotprod           > ARMv8 64-bit with popcnt, neon and dot product support" && \
	echo "apple-silicon           > Apple silicon ARM64" && \
	echo "riscv64                 > RISC-V 64-bit" && \
	echo "loongarch64             > LoongArch 64-bit" && \
	echo "loongarch64-lsx         > LoongArch 64-bit with SIMD eXtension" && \
	echo "loongarch64-lasx        > LoongArch 64-bit with Advanced SIMD eXtension" && \
	echo "" && \
	echo "Supported compilers:" && \
	echo "" && \
	echo "gcc                     > GNU compiler (default)" && \
	echo "mingw                   > GNU compiler with MinGW under Windows" && \
	echo "clang                   > LLVM Clang compiler" && \
	echo "icx                     > Intel oneAPI DPC++/C++ Compiler" && \
	echo "ndk                     > Google NDK to cross-compile for Android" && \
	echo "" && \
	echo "Simple examples. If you don't know what to do, you likely want to run one of:" && \
	echo "" && \
	echo "make -j profile-build ARCH=x86-64-avx2          # Typically fast compile for 64-bit systems" && \
	echo "make -j profile-build ARCH=x86-64-sse41-popcnt  # More portable compile for 64-bit systems" && \
	echo "make -j profile-build ARCH=x86-64               # Portable compile for 64-bit systems" && \
	echo "" && \
	echo "Advanced examples, for experienced users: " && \
	echo "" && \
	echo "make -j profile-build ARCH=x86-64-avxvnni" && \
	echo "make -j profile-build ARCH=x86-64-avxvnni COMP=gcc COMPCXX=g++-12.0" && \
	echo "make -j build ARCH=x86-64-ssse3 COMP=clang" && \
	echo ""
	@if [ "$(ARCH_SUPPORT)" = "no" ]; then \
		echo "Specify a supported architecture with the ARCH option for more details"; \
		echo ""; \
	fi

.PHONY: help analyze build profile-build strip install clean \
	objclean profileclean net config-sanity FORCE all \
	clang-profile-make clang-profile-use \
	gcc-profile-make gcc-profile-use \
	icx-profile-make icx-profile-use \
	format install-hooks

analyze: net config-sanity objclean
	$(MAKE) -k ARCH=$(ARCH) COMP=$(COMP) $(OBJS)

build: net config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build: net config-sanity objclean profileclean
	@echo ""
	@echo "Step 1/4. Building instrumented executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_make)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) > PGOBENCH.out 2>&1
	tail -n 4 PGOBENCH.out
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
	-$(MKDIR) "$(BINDIR)"
	-$(CP) "$(EXE)" "$(BINDIR)/"
	-chmod 755 "$(BINDIR)/$(EXE)"
	-$(STRIP) "$(BINDIR)/$(EXE)"

# Clean all
clean: objclean profileclean
	@rm -f .depend *~ core

# Clean binaries and objects
objclean:
	@rm -f $(EXE) $(OBJS) ./syzygy/*.o ./nnue/*.o ./nnue/features/*.o

# Clean auxiliary profiling files
profileclean:
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno ./syzygy/*.gcda ./nnue/*.gcda ./nnue/features/*.gcda *.s PGOBENCH.out
	@rm -f $(NAME).profdata *.profraw
	@rm -f $(NAME).*args*
	@rm -f $(NAME).*lt*
	@rm -f $(NAME).res
	@rm -f ./-lstdc++.res

# Evaluation network (nnue)
net:
	@$(SHELL) ../scripts/net.sh

format:
	@echo CLANG_FORMAT=$(CLANG_FORMAT)
	@if [ -n "$(SRCS)$(HEADERS)" ]; then \
	    $(CLANG_FORMAT) -i -style=file $(SRCS) $(HEADERS); \
	fi

install-hooks:
	@root="$$(git rev-parse --show-toplevel 2>/dev/null)" || { echo "Not a git repo"; exit 1; }; \
	src="$$root/.git-hooks/pre-commit"; \
	dst="$$root/.git/hooks/pre-commit"; \
	if [ ! -f "$$src" ]; then echo "Hook source not found: $$src"; exit 1; fi; \
	if [ -f "$$dst" ]; then \
	  bak="$$dst.bak.$$(date +%Y%m%d%H%M%S)"; \
	  mv "$$dst" "$$bak" && echo "Existing hook moved to $$bak"; \
	fi; \
	cp "$$src" "$$dst" && chmod +x "$$dst" && echo "Installed pre-commit hook: $$dst"

### ==========================================================================
### Section 5. Private Targets
### ==========================================================================

all: $(EXE) .depend

config-sanity: net
	@echo ""
	@echo "Config:" && \
	echo "debug: '$(debug)'" && \
	echo "sanitize: '$(sanitize)'" && \
	echo "optimize: '$(optimize)'" && \
	echo "arch: '$(arch)'" && \
	echo "bits: '$(bits)'" && \
	echo "kernel: '$(KERNEL)'" && \
	echo "os: '$(OS)'" && \
	echo "prefetch: '$(prefetch)'" && \
	echo "popcnt: '$(popcnt)'" && \
	echo "sse: '$(sse)'" && \
	echo "mmx: '$(mmx)'" && \
	echo "sse2: '$(sse2)'" && \
	echo "ssse3: '$(ssse3)'" && \
	echo "sse41: '$(sse41)'" && \
	echo "avx2: '$(avx2)'" && \
	echo "bmi2: '$(bmi2)'" && \
	echo "comp: '$(comp)'" && \
	echo "avxvnni: '$(avxvnni)'" && \
	echo "avx512: '$(avx512)'" && \
	echo "vnni256: '$(vnni256)'" && \
	echo "vnni512: '$(vnni512)'" && \
	echo "avx512icl: '$(avx512icl)'" && \
	echo "altivec: '$(altivec)'" && \
	echo "vsx: '$(vsx)'" && \
	echo "neon: '$(neon)'" && \
	echo "dotprod: '$(dotprod)'" && \
	echo "arm_version: '$(arm_version)'" && \
	echo "lsx: '$(lsx)'" && \
	echo "lasx: '$(lasx)'" && \
	echo "OS_WINDOWS: '$(OS_WINDOWS)'" && \
	echo "COMP: '$(COMP)'" && \
	echo "" && \
	echo "Flags:" && \
	echo "CXX: $(CXX)" && \
	echo "CXXFLAGS: $(CXXFLAGS)" && \
	echo "DEPENDFLAGS: $(DEPENDFLAGS)" && \
	echo "LDFLAGS: $(LDFLAGS)" && \
	echo "" && \
	echo "Testing config sanity. If this fails, try 'make help' ..." && \
	echo "" && \
	(test "$(debug)"    = "no" || test "$(debug)"    = "yes") && \
	(test "$(optimize)" = "no" || test "$(optimize)" = "yes") && \
	(test "$(ARCH_SUPPORT)" = "yes") && \
	(test "$(arch)" = "any" || test "$(arch)" = "i386" || test "$(arch)" = "x86_64" || \
	 test "$(arch)" = "ppc" || test "$(arch)" = "ppc64" || test "$(arch)" = "e2k" || \
	 test "$(arch)" = "armv7" || test "$(arch)" = "armv8" || test "$(arch)" = "arm64" || \
	 test "$(arch)" = "riscv64" || test "$(arch)" = "loongarch64") && \
	(test "$(bits)" = "32" || test "$(bits)" = "64") && \
	(test "$(prefetch)" = "no" || test "$(prefetch)" = "yes") && \
	(test "$(popcnt)"   = "no" || test "$(popcnt)"   = "yes") && \
	(test "$(sse)"      = "no" || test "$(sse)"      = "yes") && \
	(test "$(mmx)"      = "no" || test "$(mmx)"      = "yes") && \
	(test "$(sse2)"     = "no" || test "$(sse2)"     = "yes") && \
	(test "$(ssse3)"    = "no" || test "$(ssse3)"    = "yes") && \
	(test "$(sse41)"    = "no" || test "$(sse41)"    = "yes") && \
	(test "$(avx2)"     = "no" || test "$(avx2)"     = "yes") && \
	(test "$(bmi2)"     = "no" || test "$(bmi2)"     = "yes") && \
	(test "$(comp)"     = "no" || test "$(comp)"     = "yes") && \
	(test "$(avxvnni)"  = "no" || test "$(avxvnni)"  = "yes") && \
	(test "$(avx512)"   = "no" || test "$(avx512)"   = "yes") && \
	(test "$(vnni256)"  = "no" || test "$(vnni256)"  = "yes") && \
	(test "$(vnni512)"  = "no" || test "$(vnni512)"  = "yes") && \
	(test "$(avx512icl)" = "no" || test "$(avx512icl)" = "yes") && \
	(test "$(altivec)"  = "no" || test "$(altivec)"  = "yes") && \
	(test "$(vsx)"      = "no" || test "$(vsx)"      = "yes") && \
	(test "$(dotprod)"  = "no" || test "$(dotprod)"  = "yes") && \
	(test "$(neon)"     = "no" || test "$(neon)"     = "yes") && \
	(test "$(lsx)"      = "no" || test "$(lsx)"      = "yes") && \
	(test "$(lasx)"     = "no" || test "$(lasx)"     = "yes") && \
	(test "$(OS_WINDOWS)" = "no" || test "$(OS_WINDOWS)" = "yes") && \
	(test "$(COMP)" = "gcc" || test "$(COMP)" = "clang" || \
	 test "$(COMP)" = "mingw" || test "$(COMP)" = "icx" || \
	 test "$(COMP)" = "ndk" || \
	 test "$(COMP)" = "armv7a-linux-androideabi16-clang" || \
	 test "$(COMP)" = "aarch64-linux-android21-clang")

$(EXE): $(OBJS)
	+$(CXX) -o $@ $(OBJS) $(LDFLAGS)

# Force recompilation to ensure version info is up-to-date
misc.o: FORCE
FORCE:

clang-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-generate' \
	EXTRALDFLAGS='-fprofile-generate' \
	all

clang-profile-use:
	$(XCRUN) $(LLVM_PROFDATA) merge -output=$(NAME).profdata *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-use=$(NAME).profdata' \
	EXTRALDFLAGS='-fprofile-use' \
	all

gcc-profile-make:
	@mkdir -p profdir
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-generate=profdir $(EXTRAPROFILEFLAGS)' \
	EXTRALDFLAGS='-lgcov' \
	all

gcc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-use=profdir -fno-peel-loops -fno-tracer $(EXTRAPROFILEFLAGS)' \
	EXTRALDFLAGS='-lgcov' \
	all

icx-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-generate' \
	EXTRALDFLAGS='-fprofile-instr-generate' \
	all

icx-profile-use:
	$(XCRUN) $(LLVM_PROFDATA) merge -output=$(NAME).profdata *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-use=$(NAME).profdata' \
	EXTRALDFLAGS='-fprofile-use' \
	all

.depend: $(SRCS)
	@echo Generating dependencies...
	@set -o pipefail; \
	$(CXX) $(DEPENDFLAGS) -MM -MP $(SRCS) 2>&1 | \
		sed -E 's@(.*/)?DON/src/nnue/@nnue/@g; s@(.*/)?DON/src/syzygy/@syzygy/@g; s@(.*/)?DON/src/@../@g' \
		> $@.tmp
	@mv $@.tmp $@

# Include .depend only if no special goal is requested
ifeq ($(filter $(MAKECMDGOALS),help strip install clean objclean profileclean net format config-sanity install-hooks),)
    -include .depend
endif
