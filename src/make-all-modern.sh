#!/bin/bash

# make -f Makefile build         ARCH=x86-64-popcnt COMP=gcc debug=no
# make -f Makefile profile-build ARCH=x86-64-popcnt COMP=gcc debug=no

make -f Makefile build         ARCH=x86-64-bmi2 COMP=gcc debug=yes
# make -f Makefile profile-build ARCH=x86-64-bmi2 COMP=gcc debug=no

# sleep 10
