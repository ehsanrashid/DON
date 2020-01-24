REM SET PATH=C:/MinGW/32/bin/;C:/MinGW/msys/bin/;

REM make -f Makefile build ARCH=x86-32-abm         COMP=mingw debug=no VER=ddmmyy
REM make -f Makefile build ARCH=x86-32-abm         COMP=mingw debug=no VER=ddmmyy

REM -----------------------------------------------------

SET PATH=C:/MinGW/64/bin/;C:/MinGW/msys/bin/;

REM make -f Makefile build         ARCH=x86-64-abm COMP=mingw debug=no
REM make -f Makefile profile-build ARCH=x86-64-abm COMP=mingw debug=no

make -f Makefile build         ARCH=x86-64-bm2 COMP=mingw debug=no
REM make -f Makefile profile-build ARCH=x86-64-bm2 COMP=mingw debug=no

PAUSE
