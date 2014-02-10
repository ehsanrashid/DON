### Overview

<<<<<<< HEAD
DON is a free UCI chess engine derived from Glaurung 2.1.
It is not a complete chess program and requires some UCI-compatible GUI
(e.g. XBoard with PolyGlot, eboard, Arena, Sigma Chess, Shredder, Chess
Partner or Fritz) in order to be used comfortably.
Read the documentation for your GUI of choice for information
about how to use DON with it.
=======
DON is a free UCI chess engine. It is not a complete chess program and requires some UCI-compatible GUI (e.g. XBoard with PolyGlot, eboard, Arena, Sigma Chess, Shredder, Chess
Partner or Fritz) in order to be used comfortably.
Read the documentation for your GUI of choice for information about how to use DON with it.
>>>>>>> origin/minGW

This version of DON supports up to 64 CPUs. The engine defaults to one search thread,
so it is therefore recommended to inspect the value of the *Threads* UCI parameter,
and to make sure it equals the number of CPU cores on your computer.


<<<<<<< HEAD
### Files

This distribution of DON consists of the following files:

  * Readme.md, the file you are currently reading.

  * src, a subdirectory containing the full source code, including a Makefile
    that can be used to compile DON on Unix-like systems. For further
    information about how to compile DON yourself read section below.


### Opening books

This version of DON has support for PolyGlot opening books. For
information about how to create such books, consult the PolyGlot
documentation. The book file can be selected by setting the *Book File*
UCI parameter.


### Compiling it yourself

On Unix-like systems, it should be possible to compile DON
directly from the source code with the included Makefile.

DON has support for 32 or 64-bit CPUs, the hardware POPCNT instruction,
big-endian machines such as Power PC, and other platforms.

In general it is recommended to run `make help` to see a list of make
targets with corresponding descriptions. When not using the Makefile to
compile (for instance with Microsoft MSVC) you need to manually
set/unset some switches in the compiler command line; see file *types.h*
for a quick reference.


### Terms of use

DON is free, and distributed under the **GNU General Public License**
(GPL). Essentially, this means that you are free to do almost exactly
what you want with the program, including distributing it among your
friends, making it available for download from your web site, selling
it (either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute DON in
some way, you must always include the full source code, or a pointer
to where the source code can be found. If you make any changes to the
source code, these changes must also be made available under the GPL.

For full details, read the copy of the GPL found in the file named
*Copying.txt*
=======
>>>>>>> origin/minGW
