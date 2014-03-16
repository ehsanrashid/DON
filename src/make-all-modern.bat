@SET PATH=D:/MinGW/64/bin/;D:/MinGW/msys/bin/;

@REM make -f MakeFile build ARCH=x86-32-modern COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-64-modern COMP=mingw config-sanity
@REM make -f MakeFile build ARCH=x86-32-modern COMP=mingw
@REM make -f MakeFile build ARCH=x86-64-modern COMP=mingw


@REM make -f MakeFile build ARCH=x86-64-modern-debug COMP=mingw
@REM make -f MakeFile build ARCH=x86-64-modern COMP=mingw
make -f MakeFile profile-build ARCH=x86-64-modern COMP=mingw
@PAUSE