@SET PATH=D:/MinGW/64/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=x86-32-abm  COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-32-abm  COMP=mingw

@REM make -f MakeFile build ARCH=x86-64-abm  COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-64-abm  COMP=mingw

@REM make -f MakeFile build ARCH=x86-64-bm2  COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-64-bm2  COMP=mingw

@REM make -f MakeFile build ARCH=x86-64-abm  COMP=mingw
@REM make -f MakeFile build ARCH=x86-64-bm2  COMP=mingw

make -f MakeFile profile-build ARCH=x86-64-abm  COMP=mingw
@REM make -f MakeFile profile-build ARCH=x86-64-bm2  COMP=mingw

@PAUSE