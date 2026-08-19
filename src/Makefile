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

### Establish the operating system name
KERNEL := $(shell uname -s 2>/dev/null)
ifeq ($(KERNEL),Linux)
    OS := $(or $(shell uname -o 2>/dev/null),Linux)
endif

### Target operating system (may differ from KERNEL when cross-compiling)
TARGET_KERNEL := $(KERNEL)
ifeq ($(COMP),ndk)
    TARGET_KERNEL := Linux
    OS := Android
endif

### Command prefix to run the built executable (e.g. wine, sde, qemu)
### Backward compatible alias: WINE_PATH (deprecated)
ifneq ($(strip $(WINE_PATH)),)
    ifeq ($(strip $(RUN_PREFIX)),)
        RUN_PREFIX := $(WINE_PATH)
    endif
    ifeq ($(MAKELEVEL),0)
        ifneq ($(strip $(RUN_PREFIX)),$(strip $(WINE_PATH)))
            $(warning *** Both RUN_PREFIX and WINE_PATH are set; ignoring WINE_PATH. ***)
        else
            $(warning *** WINE_PATH is deprecated; use RUN_PREFIX instead. ***)
        endif
    endif
endif

### Target Windows OS
IS_WINDOWS := no
ifeq ($(OS),Windows_NT)
    # Android NDK is never treated as Windows
    ifneq ($(COMP),ndk)
        IS_WINDOWS := yes
    endif
else ifeq ($(COMP),mingw)
    IS_WINDOWS := yes
    ifeq ($(RUN_PREFIX),)
        RUN_PREFIX := $(shell command -v wine 2>/dev/null)
    endif
endif

# Cross-platform command to detect an executable
ifeq ($(IS_WINDOWS),yes)
    FIND_EXE = where "$(1)" 2>/dev/null
else
    FIND_EXE = command -v "$(1)" 2>/dev/null
endif

# -------------------------------------------
# Detect clang-format (prefer newest version)
# -------------------------------------------
CLANG_FORMAT := $(shell \
    $(call FIND_EXE,clang-format-24) || \
    $(call FIND_EXE,clang-format-23) || \
    $(call FIND_EXE,clang-format-22) || \
    $(call FIND_EXE,clang-format-21) || \
    $(call FIND_EXE,clang-format-20) || \
    $(call FIND_EXE,clang-format-19) || \
    $(call FIND_EXE,clang-format-18) || \
    echo clang-format)

### Executable name
NAME := DON
ifeq ($(IS_WINDOWS),yes)
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
SRCS := attacks.cpp benchmark.cpp bitboard.cpp engine.cpp evaluate.cpp main.cpp memory.cpp \
    misc.cpp movegen.cpp movepick.cpp notation.cpp option.cpp perft.cpp \
    position.cpp search.cpp thread.cpp timeman.cpp tt.cpp tune.cpp uci.cpp \
    nnue/features/full_threats.cpp nnue/features/half_ka_hm.cpp \
    nnue/accumulator.cpp nnue/network.cpp nnue/nmisc.cpp \
    book/polyglot.cpp tablebase/syzygy.cpp

EXTRA_SRCS = universal/entry_x86.cpp universal/entry_arm64.cpp universal/entry_riscv64.cpp universal/nnue_embed.cpp

HEADERS := attacks.h benchmark.h bitboard.h engine.h evaluate.h history.h memory.h \
    misc.h movegen.h movepick.h notation.h numa.h option.h perft.h platform_win.h \
    position.h prng.h search.h shm.h thread.h timeman.h tt.h tune.h types.h uci.h \
    nnue/features/full_threats.h nnue/features/half_ka_hm.h \
    nnue/layers/affine_transform.h nnue/layers/affine_transform_sparse_input.h \
    nnue/layers/clipped_relu.h nnue/layers/sqr_clipped_relu.h nnue/accumulator.h nnue/architecture.h \
    nnue/common.h nnue/feature_transformer.h nnue/network.h nnue/nmisc.h nnue/simd.h \
    incbin/incbin.h book/polyglot.h tablebase/syzygy.h

OBJS := $(notdir $(SRCS:.cpp=.o))

VPATH := nnue:nnue/features:book:tablebase

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
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
# cmp = yes/no        --- -DUSE_CMP          --- Internal flag for compressed-optimized variants
# avxvnni = yes/no    --- -mavxvnni          --- Use Intel Vector Neural Network Instructions AVX
# avx512 = yes/no     --- -mavx512bw         --- Use Intel Advanced Vector Extensions 512
# vnni256 = yes/no    --- -mavx512vnni[256]  --- Use Intel Vector Neural Network Instructions 512 with 256bit operands
# vnni512 = yes/no    --- -mavx512vnni       --- Use Intel Vector Neural Network Instructions 512
# avx512icl = yes/no  --- -mavx512cd ...     --- Use All AVX-512 features available on both Intel Ice Lake and AMD Zen 4
# altivec = yes/no    --- -maltivec          --- Use PowerPC Altivec SIMD extension
# vsx = yes/no        --- -mvsx              --- Use POWER VSX SIMD extension
# neon = yes/no       --- -DUSE_NEON         --- Use ARM SIMD architecture
# dotprod = yes/no    --- -DUSE_NEON_DOTPROD --- Use ARM advanced SIMD Int8 dot product instructions
# lsx = yes/no        --- -mlsx              --- Use Loongson SIMD eXtension
# lasx = yes/no       --- -mlasx             --- Use Loongson Advanced SIMD eXtension
# relaxedsimd = y/n   --- -mrelaxed-simd     --- Use WebAssembly relaxed SIMD extension
# tablebases = yes/no --- -DNO_TABLEBASES    --- Support tablebase probing
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
# User can override with `make ARCH=x86-64-avx512icl ARCH_SUPPORTED=yes`
ARCH_SUPPORTED := $(if $(filter \
    x86-64-universal x86-64-avx512icl x86-64-vnni512 x86-64-vnni256 x86-64-avx512 x86-64-avxvnni \
    x86-64-bmi2 x86-64-avx2 x86-64-bitmanip-cmp x86-64-bitmanip x86-64-sse41-popcnt \
    x86-64-modern x86-64-sse41 x86-64-ssse3-popcnt x86-64-ssse3 x86-64 general-64 \
    x86-32-sse41-popcnt x86-32-sse41 x86-32-ssse3 x86-32-sse2 x86-32 general-32 \
    ppc-64-vsx ppc-64-altivec ppc-64 ppc-32 e2k \
    arm64-universal armv8-dotprod armv8 armv7-neon armv7 apple-silicon \
    riscv64-universal riscv64-rva23 riscv64 \
    loongarch64-lasx loongarch64-lsx loongarch64 \
    wasm32 wasm32-relaxed-simd,$(ARCH)),yes,no)

UNIVERSAL_BUILD := $(if $(filter x86-64-universal arm64-universal riscv64-universal,$(ARCH)),yes,no)

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
cmp = no
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
rva23 = no
lsx = no
lasx = no
relaxedsimd = no
tablebases = yes
STRIP = strip

### 2.2 Architecture specific

ifneq ($(findstring x86,$(ARCH)),)

# x86-32/64
# To enable sse use: make -j build ARCH=x86-32 sse=yes
ifneq ($(findstring x86-32,$(ARCH)),)
    arch = i386
    bits = 32
    sse_fp = yes
    sse = no
    mmx = yes
else
    arch = x86_64
    sse = yes
    sse2 = yes
endif

ifneq ($(findstring -sse,$(ARCH)),)
    sse = yes
endif

ifneq ($(findstring -popcnt,$(ARCH)),)
    popcnt = yes
endif

ifneq ($(findstring -mmx,$(ARCH)),)
    mmx = yes
endif

ifneq ($(findstring -sse2,$(ARCH)),)
    sse = yes
    sse2 = yes
endif

ifneq ($(findstring -ssse3,$(ARCH)),)
    sse = yes
    sse2 = yes
    ssse3 = yes
endif

ifneq ($(findstring -sse41,$(ARCH)),)
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
endif

ifneq ($(findstring -modern,$(ARCH)),)
    $(warning *** ARCH=$(ARCH) is deprecated, defaulting to ARCH=x86-64-sse41-popcnt. Execute `make help` for a list of available architectures. ***)
    $(shell sleep 5)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
endif

ifneq ($(findstring -bitmanip,$(ARCH)),)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    bmi2 = yes
endif

ifneq ($(findstring -bitmanip-cmp,$(ARCH)),)
    cmp = yes
endif

ifneq ($(findstring -avx2,$(ARCH)),)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
endif

ifneq ($(findstring -bmi2,$(ARCH)),)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
endif

ifneq ($(findstring -avxvnni,$(ARCH)),)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
    avxvnni = yes
endif

ifneq ($(findstring -avx512,$(ARCH)),)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
    avx512 = yes
endif

ifneq ($(findstring -vnni256,$(ARCH)),)
    popcnt = yes
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    bmi2 = yes
    vnni256 = yes
endif

ifneq ($(findstring -vnni512,$(ARCH)),)
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

ifneq ($(findstring -avx512icl,$(ARCH)),)
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
ifeq ($(bits),32)
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

ifeq ($(ARCH),arm64-universal)
    arch = armv8
    prefetch = yes
    popcnt = yes
    arm_version = 8
    neon = yes
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

ifeq ($(ARCH),e2k)
    arch = e2k
    sse = yes
    mmx = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    popcnt = yes
endif

ifneq ($(findstring riscv64,$(ARCH)),)
    arch = riscv64
endif

ifeq ($(ARCH),riscv64-rva23)
    rva23 = yes
    popcnt = yes
    prefetch = yes
endif

ifneq ($(findstring loongarch64,$(ARCH)),)
    arch = loongarch64
    prefetch = yes
ifneq ($(findstring -lsx,$(ARCH)),)
    lsx = yes
endif
ifneq ($(findstring -lasx,$(ARCH)),)
    lsx = yes
    lasx = yes
endif
endif

# WASM is largely compiled using emcc's x86 compatibility layer, with occasional
# WASM-specific intrinsics
ifeq ($(ARCH),wasm32)
    arch = wasm32
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    tablebases = no
endif

ifeq ($(ARCH),wasm32-relaxed-simd)
    arch = wasm32
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    relaxedsimd = yes
    tablebases = no
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

CXXFLAGS    = $(ENV_CXXFLAGS) -Wall -Wcast-qual -fno-exceptions -std=c++17 $(EXTRA_CXXFLAGS)
DEPENDFLAGS = $(ENV_DEPENDFLAGS) -std=c++17
LDFLAGS     = $(ENV_LDFLAGS) $(EXTRA_LDFLAGS)

ifeq ($(COMP),)
    COMP=gcc
endif

# On Windows, use static linking by default.
#
# This avoids runtime dependency issues (missing DLLs) and produces a
# self-contained executable suitable for redistribution and CI environments.
# Disable this with `STATIC=no`.
ifeq ($(IS_WINDOWS),yes)
    ifneq ($(STATIC),no)
        LDFLAGS += -static
    endif
endif

BITS_FLAG := $(if $(filter 32 64,$(bits)),-m$(bits),)

# ----------------------------
# Compiler configuration
# ----------------------------

# Detect if the compiler is Clang/Apple Clang
CXX_VERSION        :=
CXX_IS_CLANG       := no
CXX_IS_APPLE_CLANG := no

# GCC
ifeq ($(COMP),gcc)
    CXX := $(if $(COMPCXX),$(COMPCXX),g++)
    CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-declarations
    # Detect whether the selected compiler is actually Clang
    CXX_VERSION        := $(shell $(CXX) --version 2>/dev/null | head -n 1 | tr '[:upper:]' '[:lower:]')
    CXX_IS_CLANG       := $(if $(findstring clang,$(CXX_VERSION)),yes,no)
    CXX_IS_APPLE_CLANG := $(if $(findstring apple clang,$(CXX_VERSION)),yes,no)

    # Android ARM architectures require explicit bitness flags
    ifneq ($(filter armv8 armv7,$(arch)),)
        ifeq ($(OS),Android)
            CXXFLAGS += $(BITS_FLAG)
            LDFLAGS  += $(BITS_FLAG)
        endif
    # RISC-V64 and LoongArch64 determine bitness from the target
    else ifneq ($(filter riscv64 loongarch64,$(arch)),)
    # Generic architectures use the configured bitness
    else
        CXXFLAGS += $(BITS_FLAG)
        LDFLAGS  += $(BITS_FLAG)
    endif
    ifeq ($(arch),wasm32)
    # Non-Darwin (Linux) specific compiler/linker flags
    else ifneq ($(TARGET_KERNEL),Darwin)
        CXXFLAGS += -Wno-stringop-overflow
        # Detect whether GNU ld is being used
        LINKER_VERSION := $(shell $(CXX) -Wl,--version 2>/dev/null)
        IS_GNU_LD      := $(if $(findstring GNU ld,$(LINKER_VERSION)),yes,no)
        ifeq ($(IS_GNU_LD),yes)
            LDFLAGS += -Wl,--no-as-needed
        endif
        # Determine GCC major version for Linux Clang masquerading as GCC
        GCC_MAJOR := $(shell [ "$(CXX_IS_CLANG)" = "yes" ] && [ "$(CXX_IS_APPLE_CLANG)" = "no" ] && $(CXX) -dumpversion 2>/dev/null | cut -f1 -d. || echo)
        # Link std::filesystem explicitly if using Linux Clang < 9
        ifneq ($(GCC_MAJOR),)
            ifeq ($(shell test $(GCC_MAJOR) -lt 9 && echo yes),yes)
                LDFLAGS += -lstdc++fs
            endif
        endif
    endif
    # Configure compiler-specific flags and profile targets
    ifeq ($(CXX_IS_CLANG),yes)
        ifeq ($(CXX_IS_APPLE_CLANG),yes)
            # Apple Clang
        else
            # LLVM Clang
            CXXFLAGS += -fconstexpr-steps=500000000
        endif
        PROFILE_MAKE = clang-profile-make
        PROFILE_USE  = clang-profile-use
    else
        # GCC
        CXXFLAGS += -Wstack-usage=128000 -fno-ipa-cp-clone -fconstexpr-ops-limit=500000000
        ifeq ($(TARGET_KERNEL),Darwin)
            EXTRA_PROFILE_FLAGS = -fvisibility=hidden
        endif
        PROFILE_MAKE = gcc-profile-make
        PROFILE_USE  = gcc-profile-use
    endif
# Clang
else ifeq ($(COMP),clang)
    CXX := clang++
    CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-prototypes -Wconditional-uninitialized -flax-vector-conversions=none -fconstexpr-steps=500000000
    # Detect whether the selected compiler is actually Clang
    CXX_VERSION        := $(shell $(CXX) --version 2>/dev/null | head -n 1 | tr '[:upper:]' '[:lower:]')
    CXX_IS_CLANG       := $(if $(findstring clang,$(CXX_VERSION)),yes,no)
    CXX_IS_APPLE_CLANG := $(if $(findstring apple clang,$(CXX_VERSION)),yes,no)

    ifeq ($(IS_WINDOWS),yes)
        MINGW_ARCH := $(if $(filter 64,$(bits)),x86_64,i686)
        CXX := $(MINGW_ARCH)-w64-mingw32-clang++
    endif
    # Android ARM architectures require explicit bitness flags
    ifneq ($(filter armv8 armv7,$(arch)),)
        ifeq ($(OS),Android)
            CXXFLAGS += $(BITS_FLAG)
            LDFLAGS  += $(BITS_FLAG)
        endif
    # RISC-V64 and LoongArch64 determine bitness from the target
    else ifneq ($(filter riscv64 loongarch64,$(arch)),)
    # Generic architectures use the configured bitness
    else
        CXXFLAGS += $(BITS_FLAG)
        LDFLAGS  += $(BITS_FLAG)
    endif
    # Use libatomic on non-Windows, non-BSD systems unless using compiler-rt
    ifeq ($(IS_WINDOWS),no)
        ifeq ($(filter $(TARGET_KERNEL),Darwin FreeBSD OpenBSD NetBSD),)
            ifneq ($(RTLIB),compiler-rt)
                LDFLAGS += -latomic
            endif
        endif
    endif
    # Link std::filesystem for Linux Clang (non-Apple)
    ifeq ($(TARGET_KERNEL),Linux)
        ifeq ($(CXX_IS_APPLE_CLANG),no)
            LDFLAGS += -lstdc++fs
        endif
    endif
    # Profile build for Clang
    PROFILE_MAKE = clang-profile-make
    PROFILE_USE  = clang-profile-use
# mingw
else ifeq ($(COMP),mingw)
    MINGW_ARCH := $(if $(filter 64,$(bits)),x86_64,i686)
    MINGW_CXX       := $(MINGW_ARCH)-w64-mingw32-c++
    MINGW_CXX_POSIX := $(MINGW_ARCH)-w64-mingw32-c++-posix
    # Prefer POSIX variant if available
    CXX := $(if $(shell command -v $(MINGW_CXX_POSIX) 2>/dev/null),$(MINGW_CXX_POSIX),$(MINGW_CXX))
    CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-declarations -fno-ipa-cp-clone -fconstexpr-ops-limit=500000000
    # Profile build for mingw (use GCC defaults)
    PROFILE_MAKE = gcc-profile-make
    PROFILE_USE  = gcc-profile-use
# icx
else ifeq ($(COMP),icx)
    CXX := icpx
    CXXFLAGS += --intel -pedantic -Wextra -Wshadow -Wmissing-prototypes -Wconditional-uninitialized -Wabi -Wdeprecated -fconstexpr-steps=500000000
    # Profile build for icx
    PROFILE_MAKE = icx-profile-make
    PROFILE_USE  = icx-profile-use
# NDK: To cross-compile for Android, use NDK version r27c or later
else ifeq ($(COMP),ndk)
    # Detect host tag (simplified common cases)
    ifeq ($(IS_WINDOWS),yes)
        HOST_TAG := windows-x86_64
    else ifeq ($(TARGET_KERNEL),Darwin)
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
    LDFLAGS  += -static-libstdc++
    OBJCOPY  := llvm-objcopy

    STRIP := $(TOOLCHAIN)/llvm-strip
    # Profile build for NDK/Clang
    PROFILE_MAKE = clang-profile-make
    PROFILE_USE  = clang-profile-use
endif

ifeq ($(TARGET_KERNEL),Darwin)
    MACOS_BUILD_FLAGS := -mmacosx-version-min=10.15
    ifneq ($(arch),any)
        MACOS_BUILD_FLAGS += -arch $(arch)
    endif
    CXXFLAGS += $(MACOS_BUILD_FLAGS)
    LDFLAGS  += $(MACOS_BUILD_FLAGS)
    XCRUN     = xcrun
endif

### Allow overriding CXX from the environment
ifdef COMPCXX
    CXX = $(COMPCXX)
endif

# Force SSE floating-point math to avoid x87 extended-precision differences
# Ensures consistent floating-point results across x86 CPUs
ifeq ($(sse_fp),yes)
    CXXFLAGS += -mfpmath=sse -msse2
endif

## Locate llvm-profdata compatible with CXX for PGO (profile-guided optimization) builds
# For Clang/GCC, look alongside CXX
LLVM_PROFDATA := $(dir $(realpath $(shell command -v $(CXX) 2>/dev/null)))llvm-profdata
# For Intel icx, look in the compiler's bundled LLVM directory
ifeq ($(wildcard $(LLVM_PROFDATA)),)
    LLVM_PROFDATA := $(dir $(realpath $(shell command -v $(CXX) 2>/dev/null)))/compiler/llvm-profdata
endif
# Fall back to llvm-profdata available in PATH
ifeq ($(wildcard $(LLVM_PROFDATA)),)
    LLVM_PROFDATA := llvm-profdata
endif

ifneq ($(arch),wasm32)
    ## Link pthreads and optionally LRT on supported platforms
    # Android has Bionic's C library with its own pthread implementation bundled
    ifneq ($(OS),Android)
        # Haiku has pthreads in its libroot
        ifneq ($(KERNEL),Haiku)
            ifneq ($(COMP),ndk)
                LDFLAGS += -lpthread
                # Link real-time library (librt) on non-Windows, non-[Darwin/FreeBSD/OpenBSD/NetBSD] platforms
                ifeq ($(IS_WINDOWS),no)
                    ifeq ($(filter $(TARGET_KERNEL),Darwin FreeBSD OpenBSD NetBSD),)
                        LDFLAGS += -lrt
                    endif
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
    # Convert comma-separated sanitize list to -fsanitize= flags
    SAN_FLAGS := $(addprefix -fsanitize=,$(sanitize))
    # Enable sanitizers with debug info and frame pointers
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
        # Determine Clang major version
        CLANG_MAJOR := $(shell $(CXX) -dumpversion 2>/dev/null | cut -f1 -d.)
        ifneq ($(CLANG_MAJOR),)
            ifeq ($(shell expr $(CLANG_MAJOR) \< 16),1)
                CXXFLAGS += -fexperimental-new-pass-manager
            endif
        endif
    endif

    ifeq ($(TARGET_KERNEL),Darwin)
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
    CXXFLAGS += -DUSE_POPCNT
    ifeq ($(filter ppc64 ppc64-altivec ppc64-vsx armv7 armv8 arm64 riscv64,$(arch)),)
        CXXFLAGS += -msse3 -mpopcnt
    endif
endif

### 3.6 SIMD architectures
ifeq ($(avx512icl),yes)
    CXXFLAGS += -DUSE_AVX512 -DUSE_VNNI -DUSE_AVX512ICL
    ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
        CXXFLAGS += -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vnni -mavx512cd -mavx512ifma -mavx512vbmi -mavx512vbmi2 -mavx512vpopcntdq -mavx512bitalg -mvpclmulqdq -mgfni -mvaes
    endif
endif

ifeq ($(vnni512),yes)
    CXXFLAGS += -DUSE_VNNI
    ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
        CXXFLAGS += -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vnni
    endif
endif

ifeq ($(vnni256),yes)
    CXXFLAGS += -DUSE_VNNI
    ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
        CXXFLAGS += -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vnni -mprefer-vector-width=256
    endif
endif

ifeq ($(avx512),yes)
    CXXFLAGS += -DUSE_AVX512
    ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
        CXXFLAGS += -mavx512f -mavx512bw -mavx512dq -mavx512vl
    endif
endif

ifeq ($(avxvnni),yes)
    CXXFLAGS += -DUSE_VNNI -DUSE_AVXVNNI
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
    ifeq ($(TARGET_KERNEL),Linux)
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

ifeq ($(rva23),yes)
    CXXFLAGS += -DUSE_RVV -march=rv64gcv1p0_zba_zbb_zbs_zicbop_zicond
endif

ifeq ($(lasx),yes)
    CXXFLAGS += -DUSE_LASX
    ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
        CXXFLAGS += -mlasx
    endif
endif

ifeq ($(lsx),yes)
    CXXFLAGS += -DUSE_LSX
    ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
        CXXFLAGS += -mlsx
    endif
endif

ifeq ($(arch),wasm32)
    CXXFLAGS += -DUSE_POPCNT -DNO_NNUE_EMBEDDING -DUSE_SLOPPY_ATOMICS -pthread -msimd128
    LDFLAGS  += -sSTACK_SIZE=3MB -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH -pthread
    ifeq ($(relaxedsimd),yes)
        CXXFLAGS += -mrelaxed-simd
    endif
endif

### 3.7 bmi2
ifeq ($(bmi2),yes)
    CXXFLAGS += -DUSE_BMI2
    ifneq ($(filter gcc clang mingw icx ndk,$(COMP)),)
        CXXFLAGS += -mbmi2
    endif
    ifeq ($(cmp),yes)
        CXXFLAGS += -DUSE_CMP
    endif
endif

### Syzygy tablebases
ifeq ($(tablebases),no)
    CXXFLAGS += -DNO_TABLEBASES
endif

### 3.8.1 Try to include git info for versioning and avoid recompiles if nothing changes
BUILD_DATE_FILE      := .build_date.txt
BUILD_SHA_FILE       := .build_sha.txt
BUILD_DIFFINDEX_FILE := .build_diffindex.txt
GIT_DATE             := $(shell git show -s --date=format:%Y%m%d --format=%cd HEAD 2>/dev/null || true)
GIT_SHA              := $(shell git rev-parse HEAD 2>/dev/null | cut -c 1-8 || true)
GIT_DIFFINDEX        := $(shell git update-index --refresh >/dev/null 2>&1; git -c core.filemode=false -c core.autocrlf=false diff-index --quiet HEAD -- 2>/dev/null; [ $$? -eq 1 ] && echo 1 || true)
COMPILER_DATE        := $(shell date +%Y%m%d 2>/dev/null)

BUILD_DATE           := $(if $(GIT_DATE),$(GIT_DATE),$(COMPILER_DATE))

define cache_file_contents
$(shell \
	if [ ! -f "$(1)" ] || [ "$$(cat "$(1)" 2>/dev/null)" != "$(2)" ]; then \
		printf '%s\n' "$(2)" > "$(1)"; \
	fi
)
endef

ifneq ($(filter-out help strip install clean net objclean profileclean format config-sanity,$(MAKECMDGOALS)),)
_ := $(call cache_file_contents,$(BUILD_DATE_FILE),$(BUILD_DATE))
_ := $(call cache_file_contents,$(BUILD_SHA_FILE),$(GIT_SHA))
_ := $(call cache_file_contents,$(BUILD_DIFFINDEX_FILE),$(BUILD_DIFFINDEX))
endif

### 3.8.2 Try to include architecture
ifneq ($(ARCH),)
    CXXFLAGS += -DARCH=$(ARCH)
endif

### 3.9 Link Time Optimization
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize),yes)
    ifeq ($(debug),no)
        ifeq ($(arch),wasm32)
        # Non-macOS
        else ifneq ($(TARGET_KERNEL),Darwin)
            # Android NDK Clang
            ifeq ($(COMP),ndk)
                LDFLAGS += -fuse-ld=lld
            # Non-Android Clang
            else
                # Detect LLD
                HAS_LLD := $(if $(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null),yes,no)
                ifeq ($(HAS_LLD),yes)
                    # Clang
                    ifeq ($(COMP),clang)
                        LDFLAGS += -fuse-ld=lld
                    # GCC (Clang-compatible)
                    else ifeq ($(CXX_IS_CLANG),yes)
                        LDFLAGS += -fuse-ld=lld
                    endif
                endif
            endif
        endif
        # Determine LTO flags
        LTO_FLAGS   :=
        LTO_LDFLAGS :=
        # Clang / ICX / NDK
        ifneq ($(filter clang icx ndk,$(COMP)),)
            LTO_FLAGS := -flto=full
            # ICX
            ifeq ($(COMP),icx)
                CXXFLAGS += -fwhole-program-vtables
            endif
        # GCC
        else ifeq ($(COMP),gcc)
            # GCC (native)
            ifeq ($(CXX_IS_CLANG),no)
                LTO_FLAGS   := -flto -flto-partition=one
                ifneq ($(TARGET_KERNEL),Darwin)
                    LTO_LDFLAGS := -flto=jobserver
                endif
            # GCC (Clang-compatible)
            else
                LTO_FLAGS := -flto=full
            endif
        # Windows (requires GCC 10.1+ for LTO and static linking)
        else ifeq ($(COMP),mingw)
            LTO_FLAGS   := -flto -flto-partition=one
            LTO_LDFLAGS := -flto=jobserver -save-temps
        endif
        # Apply LTO flags
        CXXFLAGS += $(LTO_FLAGS)
        LDFLAGS  += $(LTO_FLAGS) $(LTO_LDFLAGS)
    endif
    # Inline settings
    ifneq ($(filter clang ndk,$(COMP)),)
        CXXFLAGS += -Xclang -mllvm -Xclang -inline-threshold=500
    endif
endif

### 3.10 Use PIC and PIE on Android 5+, incompatible with Android 4 and earlier
ifeq ($(OS),Android)
    CXXFLAGS += -fPIE
    LDFLAGS  += -pie
endif

MKDIR := mkdir -p -m 755
CP    := cp

### ==========================================================================
### Section 4. Public Targets
### ==========================================================================

help:
	@echo ""; \
	echo "To compile $(NAME), type: "; \
	echo ""; \
	echo "make -j target [ARCH=arch] [COMP=compiler] [COMPCXX=cxx]"; \
	echo ""; \
	echo "Supported targets:"; \
	echo ""; \
	echo "help                    > Display architecture details"; \
	echo "profile-build           > standard build with profile-guided optimization"; \
	echo "build                   > skip profile-guided optimization"; \
	echo "macos-lipo              > macOS x86-64 + Apple silicon universal binary"; \
	echo "net                     > Download the default nnue nets"; \
	echo "strip                   > Strip executable"; \
	echo "install                 > Install executable"; \
	echo "clean                   > Clean up"; \
	echo ""; \
	echo "Supported archs:"; \
	echo ""; \
	echo "native                  > select the best architecture for the host processor (default)"; \
	echo "x86-64-universal        > x86 64-bit with automatic runtime selection of the best architecture"; \
	echo "x86-64-avx512icl        > x86 64-bit with minimum avx512 support of Intel Ice Lake or AMD Zen 4"; \
	echo "x86-64-vnni512          > x86 64-bit with vnni support, operands 512-bit wide"; \
	echo "x86-64-vnni256          > x86 64-bit with vnni support, operands limited to 256-bit wide"; \
	echo "x86-64-avx512           > x86 64-bit with avx512 support"; \
	echo "x86-64-avxvnni          > x86 64-bit with vnni 256bit support"; \
	echo "x86-64-bmi2             > x86 64-bit with bmi2 support"; \
	echo "x86-64-avx2             > x86 64-bit with avx2 support"; \
	echo "x86-64-bitmanip-cmp     > x86 64-bit with bit manipulation support and compressed instructions"; \
	echo "x86-64-bitmanip         > x86 64-bit with bit manipulation support"; \
	echo "x86-64-sse41-popcnt     > x86 64-bit with sse41 and popcnt support"; \
	echo "x86-64-modern           > deprecated, currently x86-64-sse41-popcnt"; \
	echo "x86-64-sse41            > x86 64-bit with sse41 support"; \
	echo "x86-64-ssse3-popcnt     > x86 64-bit with ssse3 and popcnt support"; \
	echo "x86-64-ssse3            > x86 64-bit with ssse3 support"; \
	echo "x86-64                  > x86 64-bit generic with sse2 support"; \
	echo "general-64              > unspecified 64-bit"; \
	echo "x86-32-sse41-popcnt     > x86 32-bit with sse41 and popcnt support"; \
	echo "x86-32-sse41            > x86 32-bit with sse41 support"; \
	echo "x86-32-ssse3            > x86 32-bit with ssse3 support"; \
	echo "x86-32-sse2             > x86 32-bit with sse2 support"; \
	echo "x86-32                  > x86 32-bit generic with mmx support"; \
	echo "general-32              > unspecified 32-bit"; \
	echo "ppc-64-altivec          > PPC 64-bit with altivec support"; \
	echo "ppc-64-vsx              > PPC 64-bit with vsx support"; \
	echo "ppc-64                  > PPC 64-bit"; \
	echo "ppc-32                  > PPC 32-bit"; \
	echo "e2k                     > Elbrus 2000"; \
	echo "arm64-universal         > ARMv8 64-bit with automatic runtime selection of the best architecture"; \
	echo "armv8-dotprod           > ARMv8 64-bit with popcnt, neon and dot product support"; \
	echo "armv8                   > ARMv8 64-bit with popcnt and neon"; \
	echo "armv7-neon              > ARMv7 32-bit with popcnt and neon"; \
	echo "armv7                   > ARMv7 32-bit"; \
	echo "apple-silicon           > Apple silicon ARM64"; \
	echo "riscv64-universal       > RISC-V 64-bit with automatic runtime selection of the best architecture"; \
	echo "riscv64-rva23           > RISC-V 64-bit RVA23 profile"; \
	echo "riscv64                 > RISC-V 64-bit"; \
	echo "loongarch64-lasx        > LoongArch 64-bit with Advanced SIMD eXtension"; \
	echo "loongarch64-lsx         > LoongArch 64-bit with SIMD eXtension"; \
	echo "loongarch64             > LoongArch 64-bit"; \
	echo ""; \
	echo "Supported compilers:"; \
	echo ""; \
	echo "gcc                     > GNU compiler (default)"; \
	echo "mingw                   > GNU compiler with MinGW under Windows"; \
	echo "clang                   > LLVM Clang compiler"; \
	echo "icx                     > Intel oneAPI DPC++/C++ Compiler"; \
	echo "ndk                     > Google NDK to cross-compile for Android"; \
	echo ""; \
	echo "Simple examples. If you don't know what to do, you likely want to run one of:"; \
	echo ""; \
	echo 'make -j profile-build ARCH=native               # Fast compile for local system'; \
	echo 'make -j profile-build ARCH=x86-64-avx2          # Portable compile for most x86-64 systems'; \
	echo 'make -j profile-build ARCH=x86-64-sse41-popcnt  # Portable compile for most x86-64 systems'; \
	echo 'make -j profile-build ARCH=x86-64               # Portable compile for x86-64 systems'; \
	echo ""; \
	echo "Advanced examples, for experienced users: "; \
	echo ""; \
	echo 'make -j profile-build ARCH=x86-64-avxvnni'; \
	echo 'make -j profile-build ARCH=x86-64-avxvnni COMP=gcc CXX=g++-12.0'; \
	echo 'make -j profile-build ARCH=x86-64-universal RUN_PREFIX="/path/to/sde -future --" CXX=g++-15'; \
	echo 'make -j build ARCH=x86-64-ssse3 COMP=clang'; \
	echo ""

ifeq ($(ARCH_SUPPORTED),no)
	@echo "Specify a supported architecture with the ARCH option for more details"; \
	echo ""
endif

.PHONY: help analyze build profile-build macos-lipo strip install clean net \
	objclean profileclean config-sanity \
	clang-profile-make clang-profile-use \
	gcc-profile-make gcc-profile-use \
	icx-profile-make icx-profile-use \
	format install-hooks

analyze: net config-sanity objclean
	$(MAKE) -k ARCH=$(ARCH) COMP=$(COMP) $(OBJS)

ifeq ($(UNIVERSAL_BUILD),no)
build: net config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build: net config-sanity objclean profileclean
	@echo ""
	@echo "Step 1/4. Building instrumented executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(PROFILE_MAKE)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) > PGOBENCH.out 2>&1
	tail -n 4 PGOBENCH.out
	@echo ""
	@echo "Step 3/4. Building optimized executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) objclean
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(PROFILE_USE)
	@echo ""
	@echo "Step 4/4. Deleting profile data ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) profileclean
endif

# Default network filename, read from evaluate.h's EvalFileDefaultName.
macos-lipo: NNUE_NET = $(shell grep -oE 'nn-[0-9a-f]+\.nnue' $(CURDIR)/evaluate.h | head -1)
macos-lipo:
	@echo ""
	@echo "Step 1/4. Building x86-64 slice (network shared from Apple-silicon slice) ..."
	CXXFLAGS="-arch x86_64" $(MAKE) build ARCH=x86-64-universal COMP=$(COMP) MACOS_X86_SLICE=1
	mv $(EXE) $(EXE)-x86_64
	@echo ""
	@echo "Step 2/4. Building Apple silicon slice ..."
	$(MAKE) profile-build ARCH=apple-silicon COMP=$(COMP)
	mv $(EXE) $(EXE)-arm64
	@echo ""
	@echo "Step 3/4. Combining slices and locating the embedded network ..."
	lipo -create $(EXE)-x86_64 $(EXE)-arm64 -output $(EXE)
	$(SHELL) universal/patch_x86_slice.sh $(EXE) $(EXE)-x86_64 $(NNUE_NET)
	@echo ""
	@echo "Step 4/4. Re-combining slices with the patched x86-64 slice ..."
	lipo -create $(EXE)-x86_64 $(EXE)-arm64 -output $(EXE)
	@rm -f $(EXE)-x86_64 $(EXE)-arm64
	@echo "Universal macOS binary built: $(EXE)"

# Apple uses ld64 which doesn't have --save-temps
ifeq ($(TARGET_KERNEL),Darwin)
    LTO_OBJ_SUFFIX := .lto.o
    SAVE_TEMPS := -Wl,-object_path_lto,DON.lto.o
else ifneq ($(filter clang ndk,$(COMP)),)
    LTO_OBJ_SUFFIX := .lto.o
    ifeq ($(IS_WINDOWS),yes)
        SAVE_TEMPS := -Wl,--lto-emit-asm  # ld.lld doesn't have a save temps functionality
        USE_LTO_EMIT_ASM := yes
    else
        SAVE_TEMPS := -Wl,--save-temps
    endif
else
    LTO_OBJ_SUFFIX := .ltrans0.ltrans.o
    SAVE_TEMPS := -save-temps
endif

ifeq ($(arch),armv8)
    ASM_FLAGS := -mcpu=neoverse-v2
endif

MODNAME := $(subst -,_,$(notdir $(CURDIR)))

universal-object-pgo: objclean profileclean universalclean
	@echo ""
	@echo "Step 1/4. Building instrumented executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) ENV_LDFLAGS='$(ENV_LDFLAGS) $(NNUE_EMBED_OBJ)' $(PROFILE_MAKE)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) > PGOBENCH.out 2>&1
	tail -n 4 PGOBENCH.out
	@echo ""
	@echo "Step 3/4. Building optimized object files ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) objclean
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) ENV_LDFLAGS='$(ENV_LDFLAGS) $(NNUE_EMBED_OBJ) $(SAVE_TEMPS)' $(PROFILE_USE)
	@echo ""
ifeq ($(USE_LTO_EMIT_ASM),yes)
	@echo "Step 4/4. Renaming ctors and extracting LTO-optimized relocatable object ..."
	awk -v MODNAME=$(MODNAME) -f ../../universal/rewrite_asm_sections.awk *.lto.s > renamed.s
# -mcpu=neoverse-v2 (or another CPU supporting dotprod) is required for clang
# to not complain when assembling
	$(CXX) -c renamed.s -o DON.o $(ASM_FLAGS)
else
	@echo "Step 4/4. Extracting LTO-optimized relocatable object ..."
	cp "$(basename $(EXE))$(LTO_OBJ_SUFFIX)" DON.o
endif

universal-object-nopgo: objclean universalclean
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) ENV_LDFLAGS='$(ENV_LDFLAGS) $(NNUE_EMBED_OBJ) $(SAVE_TEMPS)' all
ifeq ($(USE_LTO_EMIT_ASM),yes)
	awk -v MODNAME=$(MODNAME) -f ../../universal/rewrite_asm_sections.awk *.lto.s > renamed.s
	$(CXX) -c renamed.s -o DON.o $(ASM_FLAGS)
else
	cp "$(basename $(EXE))$(LTO_OBJ_SUFFIX)" DON.o
endif

strip:
	$(STRIP) $(EXE)

install:
	$(MKDIR) "$(BINDIR)"
	$(CP) "$(EXE)" "$(BINDIR)/"
	chmod 755 "$(BINDIR)/$(EXE)"
	$(STRIP) "$(BINDIR)/$(EXE)"

# Clean all
clean: objclean profileclean
	@rm -f .depend *~ core

# Clean binaries and objects
objclean:
	@rm -f $(EXE) $(OBJS) *.o ./book/*.o ./tablebase/*.o ./nnue/*.o ./nnue/features/*.o $(BUILD_DATE_FILE) $(BUILD_SHA_FILE) $(BUILD_DIFFINDEX_FILE)

# Clean auxiliary profiling files
profileclean:
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno ./book/*.gcda ./tablebase/*.gcda ./nnue/*.gcda ./nnue/features/*.gcda *.s PGOBENCH.out
	@rm -f "$(NAME).profdata" *.profraw
	@rm -f "$(NAME)".*args*
	@rm -f "$(NAME)".*lt*
	@rm -f "$(NAME).res"
	@rm -f ./-lstdc++.res

universalclean:
# On Windows "ln -s" to a directory makes a native directory symlink that rm
# cannot unlink; rmdir unlinks it without following it. No-op elsewhere.
	@find $(TEMP_DIR) -type l -exec rmdir {} + 2>/dev/null || true
	@rm -rf $(TEMP_DIR)
	@rm -f universal/network_dump.inc

# Evaluation network (nnue)
net:
	@$(SHELL) ../scripts/net.sh

format:
	@echo CLANG_FORMAT=$(CLANG_FORMAT)
	@if [ -n "$(CLANG_FORMAT)" ] && [ -n "$(SRCS)$(EXTRA_SRCS)$(HEADERS)" ]; then \
		$(CLANG_FORMAT) -i -style=file $(SRCS) $(EXTRA_SRCS) $(HEADERS); \
	fi

install-hooks:
	@root=$$(git rev-parse --show-toplevel 2> /dev/null); \
	if [ -z "$$root" ]; then \
		echo "Not a git repo"; exit 1; \
	fi; \
	src="$$root/.git-hooks/pre-commit"; \
	dst="$$root/.git/hooks/pre-commit"; \
	if [ ! -f "$$src" ]; then \
		echo "Hook source not found: $$src"; exit 1; \
	fi; \
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
	@echo "" && \
	echo "Config:" && \
	echo "KERNEL: '$(KERNEL)'" && \
	echo "TARGET_KERNEL: '$(TARGET_KERNEL)'" && \
	echo "OS: '$(OS)'" && \
	echo "IS_WINDOWS: '$(IS_WINDOWS)'" && \
	echo "COMP: '$(COMP)'" && \
	echo "RUN_PREFIX: '$(RUN_PREFIX)'" && \
	echo "debug: '$(debug)'" && \
	echo "sanitize: '$(sanitize)'" && \
	echo "optimize: '$(optimize)'" && \
	echo "arch: '$(arch)'" && \
	echo "bits: '$(bits)'" && \
	echo "prefetch: '$(prefetch)'" && \
	echo "popcnt: '$(popcnt)'" && \
	echo "sse: '$(sse)'" && \
	echo "mmx: '$(mmx)'" && \
	echo "sse2: '$(sse2)'" && \
	echo "ssse3: '$(ssse3)'" && \
	echo "sse41: '$(sse41)'" && \
	echo "avx2: '$(avx2)'" && \
	echo "bmi2: '$(bmi2)'" && \
	echo "cmp: '$(cmp)'" && \
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
	echo "rva23: '$(rva23)'" && \
	echo "lsx: '$(lsx)'" && \
	echo "lasx: '$(lasx)'" && \
	echo "tablebases: '$(tablebases)'" && \
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
	(test "$(ARCH_SUPPORTED)" = "yes") && \
	(test "$(arch)" = "any" || test "$(arch)" = "i386" || test "$(arch)" = "x86_64" || \
	 test "$(arch)" = "ppc" || test "$(arch)" = "ppc64" || test "$(arch)" = "e2k" || \
	 test "$(arch)" = "armv7" || test "$(arch)" = "armv8" || test "$(arch)" = "arm64" || \
	 test "$(arch)" = "riscv64" || test "$(arch)" = "loongarch64" || \
	 test "$(arch)" = "wasm32") && \
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
	(test "$(cmp)"      = "no" || test "$(cmp)"      = "yes") && \
	(test "$(avxvnni)"  = "no" || test "$(avxvnni)"  = "yes") && \
	(test "$(avx512)"   = "no" || test "$(avx512)"   = "yes") && \
	(test "$(vnni256)"  = "no" || test "$(vnni256)"  = "yes") && \
	(test "$(vnni512)"  = "no" || test "$(vnni512)"  = "yes") && \
	(test "$(avx512icl)" = "no" || test "$(avx512icl)" = "yes") && \
	(test "$(vsx)"      = "no" || test "$(vsx)"      = "yes") && \
	(test "$(altivec)"  = "no" || test "$(altivec)"  = "yes") && \
	(test "$(dotprod)"  = "no" || test "$(dotprod)"  = "yes") && \
	(test "$(neon)"     = "no" || test "$(neon)"     = "yes") && \
	(test "$(lasx)"     = "no" || test "$(lasx)"     = "yes") && \
	(test "$(lsx)"      = "no" || test "$(lsx)"      = "yes") && \
	(test "$(tablebases)" = "yes" || test "$(tablebases)" = "no") && \
	(test "$(IS_WINDOWS)" = "no" || test "$(IS_WINDOWS)" = "yes") && \
	(test "$(COMP)" = "gcc" || test "$(COMP)" = "clang" || test "$(COMP)" = "mingw" || \
	 test "$(COMP)" = "icx" || test "$(COMP)" = "ndk" || \
	 test "$(COMP)" = "armv7a-linux-androideabi16-clang" || \
	 test "$(COMP)" = "aarch64-linux-android21-clang")

$(EXE): $(OBJS)
	+$(CXX) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(strip $(CXX) $(CPPFLAGS) $(CXXFLAGS)) -c -o $@ $<

misc.o: misc.cpp $(BUILD_DATE_FILE) $(BUILD_SHA_FILE) $(BUILD_DIFFINDEX_FILE)
	$(strip $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(if $(GIT_DATE),-DGIT_DATE=$(GIT_DATE)) $(if $(GIT_SHA),-DGIT_SHA=$(GIT_SHA))) $(if $(GIT_DIFFINDEX),-DGIT_DIFFINDEX=$(GIT_DIFFINDEX)) -c -o $@ $<

# Clang PGO
clang-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRA_CXXFLAGS='-fprofile-generate' \
	EXTRA_LDFLAGS='-fprofile-generate' \
	all

clang-profile-use:
	$(XCRUN) $(LLVM_PROFDATA) merge -output="$(NAME).profdata" *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRA_CXXFLAGS='-fprofile-use=$(NAME).profdata' \
	EXTRA_LDFLAGS='-fprofile-use=$(NAME).profdata' \
	all

# GCC PGO
gcc-profile-make:
	@mkdir -p profdir
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRA_CXXFLAGS='-fprofile-generate=profdir $(EXTRA_PROFILE_FLAGS)' \
	EXTRA_LDFLAGS='-lgcov' \
	all

gcc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRA_CXXFLAGS='-fprofile-use=profdir -fprofile-correction -fno-peel-loops -fno-tracer $(EXTRA_PROFILE_FLAGS)' \
	EXTRA_LDFLAGS='-lgcov' \
	all

# ICC/ICX PGO
icx-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRA_CXXFLAGS='-fprofile-instr-generate' \
	EXTRA_LDFLAGS='-fprofile-instr-generate' \
	all

icx-profile-use:
	$(XCRUN) $(LLVM_PROFDATA) merge -output="$(NAME).profdata" *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRA_CXXFLAGS='-fprofile-instr-use=$(NAME).profdata' \
	EXTRA_LDFLAGS='-fprofile-use' \
	all

.depend: $(SRCS)
	@echo Generating dependencies...
	@$(CXX) $(DEPENDFLAGS) -MM -MP $(SRCS) > $@.tmp || { rm -f $@.tmp; exit 1; }
	@sed -E 's@(.*/)?DON/src/nnue/@nnue/@g; s@(.*/)?DON/src/book/@book/@g; s@(.*/)?DON/src/tablebase/@tablebase/@g; s@(.*/)?DON/src/@../@g' $@.tmp > $@
	@rm -f $@.tmp

ifeq ($(UNIVERSAL_BUILD),no)
# Include .depend only if no special goal is requested
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter help strip install clean objclean profileclean net format config-sanity install-hooks,$(MAKECMDGOALS)),)
    -include .depend
endif
endif
endif

### ==========================================================================
### Section 6. Universal binary driver
### ==========================================================================
#
# Usage:
#   make -j ARCH=x86-64-universal build          # non-PGO
#   make -j ARCH=x86-64-universal profile-build  # PGO (use RUN_PREFIX for intel SDE)
#   make -j ARCH=arm64-universal build

TEMP_DIR             := $(CURDIR)/temp_builds
UNIVERSAL_EXE        := $(CURDIR)/$(EXE)

ifeq ($(UNIVERSAL_BUILD),yes)

UNIVERSAL_SRC_DIR    := $(CURDIR)/universal
NNUE_EMBED_OBJ       := $(TEMP_DIR)/nnue_embed.o
NET_SENTINEL         := $(TEMP_DIR)/.net-done

ifeq ($(arch),x86_64)
    UNIVERSAL_ARCHES    := x86-64 x86-64-sse41-popcnt x86-64-avx2 x86-64-bmi2 x86-64-avxvnni #x86-64-avx512 x86-64-vnni512 x86-64-avx512icl
    UNIVERSAL_ENTRY_SRC := $(UNIVERSAL_SRC_DIR)/entry_x86.cpp
    UNIVERSAL_ENTRY_OBJ := $(TEMP_DIR)/entry_x86.o
    BASELINE_ARCH       := x86-64
else ifeq ($(arch),riscv64)
    UNIVERSAL_ARCHES    := riscv64 riscv64-rva23
    UNIVERSAL_ENTRY_SRC := $(UNIVERSAL_SRC_DIR)/entry_riscv64.cpp
    UNIVERSAL_ENTRY_OBJ := $(TEMP_DIR)/entry_riscv64.o
    BASELINE_ARCH       := riscv64
else ifeq ($(arch),armv8)
    UNIVERSAL_ARCHES    := armv8 armv8-dotprod
    UNIVERSAL_ENTRY_SRC := $(UNIVERSAL_SRC_DIR)/entry_arm64.cpp
    UNIVERSAL_ENTRY_OBJ := $(TEMP_DIR)/entry_arm64.o
    BASELINE_ARCH       := armv8
endif

# Top-level tracked items in src/ (directories and files)
TRACKED_TOP := $(sort $(foreach f,$(SRCS) $(HEADERS) Makefile incbin,$(firstword $(subst /, ,$(f)))))

# Link the baseline architecture first so it is preferred when the linker
# encounters duplicate definitions shared by multiple architecture slices.
ARCH_OBJS := $(TEMP_DIR)/$(BASELINE_ARCH)/DON.o \
             $(patsubst %,$(TEMP_DIR)/%/DON.o,$(filter-out $(BASELINE_ARCH),$(UNIVERSAL_ARCHES)))

UNIVERSAL_BUILD_FLAGS := -fno-exceptions -Os -Wno-c++26-extensions -Wno-c23-extensions # suppress #embed warnings

ifeq ($(TARGET_KERNEL),Darwin)
    UNIVERSAL_BUILD_FLAGS += $(MACOS_BUILD_FLAGS)
else ifeq ($(TARGET_KERNEL),Linux)
    UNIVERSAL_BUILD_FLAGS += -static-libstdc++ -static-libgcc
    ifeq ($(COMP),ndk)
        UNIVERSAL_BUILD_FLAGS += -static
    else
        UNIVERSAL_BUILD_FLAGS += -Wl,--allow-multiple-definition -lrt -lpthread
    endif
else  # Windows
    UNIVERSAL_BUILD_FLAGS += -Wl,--allow-multiple-definition -static
    ifeq ($(COMP),clang)
        UNIVERSAL_BUILD_FLAGS += -lpthread
    endif
endif

# $(call ARCH-NAMESPACE,x86-64-avx2) => DON_x86_64_avx2
ARCH-SUFFIX    = $(subst -,_,$(1))
ARCH-NAMESPACE = DON_$(call ARCH-SUFFIX,$(1))
# Itanium ABI mangled name for DON_<suffix>::main(int, char**)
ARCH-MANGLED   = _ZN$(shell printf %s '$(call ARCH-NAMESPACE,$(1))' | wc -c | tr -d ' ')$(call ARCH-NAMESPACE,$(1))4mainEiPPKc
# Mach-O initializer section name: strip the "x86-64" or "x86-64-" prefix,
# replace remaining '-' with '_', and prefix with "_i_".
# Mach-O section names are limited to 16 characters, hence this naming scheme.
ARCH-MACOS-SECTION = _i_$(subst -,_,$(patsubst x86-64,,$(patsubst x86-64-%,%,$(1))))

ifeq ($(TARGET_KERNEL),Darwin)
    ARCH-MAIN-LDFLAGS = -Wl,-alias,_$(call ARCH-MANGLED,$(1)),_main
# lld-link, used on Windows+clang, doesn't accept --defsym, so
# define main directly in main.cpp instead and delete it later.
else ifeq ($(IS_WINDOWS)+$(COMP),yes+clang)
    ARCH-MAIN-CXXFLAGS = -DUNIVERSAL_NEEDS_MAIN_SHIM
else
    ARCH-MAIN-LDFLAGS = -Wl,--defsym=main=$(call ARCH-MANGLED,$(1))
endif

EMBED_DIR_SUPPORTED := $(shell echo 'int main(){}' | $(CXX) -x c++ --embed-dir=. -o /dev/null -c - 2>/dev/null && echo 1 || echo 0)
ifeq ($(EMBED_DIR_SUPPORTED),1)
    EMBED_DIR = --embed-dir=$(CURDIR)
endif

# macos-lipo sets MACOS_X86_SLICE=1 when building the x86-64 slice so that the
# slice reads the network out of the Apple-silicon slice instead of embedding
# its own copy (see universal/nnue_embed.cpp and universal/patch_x86_slice.sh).
ifeq ($(MACOS_X86_SLICE),1)
    SLICE_DEF := -DUNIVERSAL_BINARY_MACOS_X86_SLICE
endif

ARCH-CXXFLAGS = $(EMBED_DIR) -DDON=$(call ARCH-NAMESPACE,$(1)) \
                -DUNIVERSAL_BINARY $(SLICE_DEF) \
                $(ARCH-MAIN-CXXFLAGS) \
                -Wno-c++26-extensions -Wno-c23-extensions
ARCH-LDFLAGS  = $(ARCH-MAIN-LDFLAGS)

UOBJ_TGT ?= universal-object-pgo
ifeq ($(COMP),clang)
    OBJCOPY ?= llvm-objcopy
else
    OBJCOPY ?= objcopy
endif

.PHONY: build profile-build universalclean

build: UOBJ_TGT := universal-object-nopgo
build: $(UNIVERSAL_EXE)

profile-build: UOBJ_TGT := universal-object-pgo
profile-build: $(UNIVERSAL_EXE)

$(TEMP_DIR):
	@mkdir -p $@

# Download nnue files once in src/ (reused by every sub-arch via --embed-dir).
$(NET_SENTINEL): | $(TEMP_DIR)
	@cd $(CURDIR) && $(SHELL) ../scripts/net.sh $(EMBED_DIR_SUPPORTED)
	@touch $@

$(NNUE_EMBED_OBJ): $(UNIVERSAL_SRC_DIR)/nnue_embed.cpp $(NET_SENTINEL) | $(TEMP_DIR)
	$(CXX) -O2 -std=c++20 -Wno-c++26-extensions $(SLICE_DEF) $(MACOS_BUILD_FLAGS) $(EXTRA_CXXFLAGS) $(EMBED_DIR) -c $< -o $@

$(UNIVERSAL_ENTRY_OBJ): $(UNIVERSAL_ENTRY_SRC) | $(TEMP_DIR)
	$(CXX) -O2 -std=c++20 $(MACOS_BUILD_FLAGS) $(EXTRA_CXXFLAGS) -c $< -o $@

# Symlink tracked top-level items from src/ into temp_builds/<arch>/.
$(TEMP_DIR)/%/.setup: | $(TEMP_DIR)
	@rm -rf $(TEMP_DIR)/$*
	@mkdir -p $(TEMP_DIR)/$*
	@$(foreach f,$(TRACKED_TOP),ln -s $(CURDIR)/$(f) $(TEMP_DIR)/$*/$(f);)
	@touch $@

# Per-arch DON.o
.SECONDARY: $(patsubst %,$(TEMP_DIR)/%/.setup,$(UNIVERSAL_ARCHES))

$(TEMP_DIR)/%/DON.o: $(TEMP_DIR)/%/.setup $(NNUE_EMBED_OBJ) $(NET_SENTINEL)
	+ENV_CXXFLAGS='$(ENV_CXXFLAGS) $(call ARCH-CXXFLAGS,$*)' \
	 ENV_LDFLAGS='$(ENV_LDFLAGS) $(call ARCH-LDFLAGS,$*)' \
	    $(MAKE) -C $(TEMP_DIR)/$* $(UOBJ_TGT) \
	        ARCH=$* CXX=$(CXX) COMP=$(COMP) \
	        NNUE_EMBED_OBJ=$(NNUE_EMBED_OBJ)
ifeq ($(TARGET_KERNEL),Darwin)
# Rename the initializer section
	ld -r -rename_section __DATA __mod_init_func __DATA $(call ARCH-MACOS-SECTION,$*) $@ -o $@.tmp
	mv $@.tmp $@
else
# Drop COMDAT groups; clang LTO leaves them around internalized LOCAL
# template instantiations which causes problems at the final link
	$(OBJCOPY) -R .group $@
ifeq ($(USE_LTO_EMIT_ASM),yes)
# Delete per-arch main shim
	$(OBJCOPY) -N main $@
else
# Rename .init_array (Linux) or .ctors (Windows) so we can manually invoke them
	$(OBJCOPY) --rename-section .init_array=$(call ARCH-SUFFIX,$*)_init $@
	$(OBJCOPY) --rename-section .ctors=$(call ARCH-SUFFIX,$*)_init $@
# Make the array inert
	$(OBJCOPY) --set-section-flags $(call ARCH-SUFFIX,$*)_init=alloc,data $@
endif
endif

$(UNIVERSAL_EXE): $(UNIVERSAL_ENTRY_OBJ) $(NNUE_EMBED_OBJ) $(ARCH_OBJS)
	$(CXX) -o $@ $(UNIVERSAL_ENTRY_OBJ) $(NNUE_EMBED_OBJ) $(ARCH_OBJS) $(UNIVERSAL_BUILD_FLAGS) $(EXTRA_LDFLAGS)
	@echo "Universal binary built: $@"

endif  # UNIVERSAL_BUILD
