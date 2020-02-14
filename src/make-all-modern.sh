#!/bin/bash

# make -f Makefile build         ARCH=x86-64-abm COMP=gcc debug=no
# make -f Makefile profile-build ARCH=x86-64-abm COMP=gcc debug=no

 make -f Makefile build         ARCH=x86-64-bm2 COMP=gcc debug=no
# make -f Makefile profile-build ARCH=x86-64-bm2 COMP=gcc debug=no

# sleep 10
