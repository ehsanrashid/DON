@SET PATH=D:/MinGW/64/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=x86-32-popcnt  COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-32-popcnt  COMP=mingw

@REM make -f MakeFile build ARCH=x86-64-popcnt  COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-64-popcnt  COMP=mingw

@REM make -f MakeFile build ARCH=x86-64-bmi     COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-64-bmi     COMP=mingw

@REM make -f MakeFile build ARCH=x86-64-popcnt   COMP=mingw
@REM make -f MakeFile build ARCH=x86-64-bmi      COMP=mingw

make -f MakeFile profile-build ARCH=x86-64-popcnt  COMP=mingw
@REM make -f MakeFile profile-build ARCH=x86-64-bmi     COMP=mingw

@PAUSE