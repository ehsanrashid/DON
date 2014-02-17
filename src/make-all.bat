@SET PATH=D:/MinGW/64/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=x86-32 COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-32 COMP=mingw
@REM make -f MakeFile build ARCH=x86-64 COMP=mingw

make -f MakeFile build ARCH=x86-32 COMP=mingw
@PAUSE