@REM @SET PATH=D:/MinGW/32/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=general-32     COMP=mingw
@REM make -f MakeFile build ARCH=x86-32-old     COMP=mingw
@REM make -f MakeFile build ARCH=x86-32         COMP=mingw
@REM make -f MakeFile build ARCH=general-32-pop COMP=mingw
@REM make -f MakeFile build ARCH=x86-32-old-pop COMP=mingw
@REM make -f MakeFile build ARCH=x86-32-pop     COMP=mingw

@REM make -f MakeFile profile-build ARCH=x86-32 COMP=mingw

@REM make -f MakeFile build ARCH=x86-32-pop     COMP=mingw

@REM -----------------------------------------------------

@SET PATH=D:/MinGW/64/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=general-64     COMP=mingw
@REM make -f MakeFile build ARCH=x86-64         COMP=mingw
@REM make -f MakeFile build ARCH=general-64-pop COMP=mingw
@REM make -f MakeFile build ARCH=x86-64-pop     COMP=mingw

@REM make -f MakeFile profile-build ARCH=x86-64 COMP=mingw

make -f MakeFile build ARCH=x86-64-pop      COMP=mingw

@PAUSE