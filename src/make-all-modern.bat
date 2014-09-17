@REM @SET PATH=D:/MinGW/32/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=x86-32-abm         COMP=mingw VER=ddmmyy


@REM -----------------------------------------------------

@SET PATH=D:/MinGW/64/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=x86-64-abm         COMP=mingw VER=ddmmyy
@REM make -f MakeFile build ARCH=x86-64-bm2         COMP=mingw VER=ddmmyy

@REM make -f MakeFile profile-build ARCH=x86-64-abm COMP=mingw VER=ddmmyy
@REM make -f MakeFile profile-build ARCH=x86-64-bm2 COMP=mingw VER=ddmmyy

make -f MakeFile build ARCH=x86-64-abm         COMP=mingw
@REM make -f MakeFile profile-build ARCH=x86-64-abm COMP=mingw

@PAUSE