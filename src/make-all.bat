ECHO OFF
SET PATH=D:/MinGW/64/bin/;D:/MinGW/msys/bin/;

REM make -f MakeFile build ARCH=x86-32 COMP=mingw config-sanity
ECHO ON
make -f MakeFile build ARCH=x86-32 COMP=mingw
PAUSE