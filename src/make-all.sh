#!/bin/bash

# make -f Makefile build         ARCH=general-64 COMP=mingw debug=no
# make -f Makefile profile-build ARCH=general-64 COMP=mingw debug=no

make -f Makefile build         ARCH=x86-64     COMP=mingw debug=no
# make -f Makefile profile-build ARCH=x86-64     COMP=mingw debug=no

# sleep 10
