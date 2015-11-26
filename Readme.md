### Overview

[![Build Status](https://www.donchess.net)](https://www.donchess.net)

DON is a free UCI chess engine. It is not a complete chess program
and requires some UCI-compatible GUI (e.g. XBoard with Polyglot,
eboard, Arena, Sigma Chess, Shredder, Chess Partner or Fritz)
in order to be used comfortably. Read the documentation for GUI
of your choice for information about how to use engine with it.

### Features

DON uses bitboard representations, and is an alfa-beta searcher.

DON supports up to 128 cores. The engine defaults to one search thread,
so it is therefore recommended to inspect the value of the 'Threads'
UCI parameter, to make sure it equals the # of CPU cores on your computer.

DON supports up to 1 TB (1024 GB) (1048576 MB) of hash memory.

DON also support Large Memory Pages when user has permission of using it
other-wise use Default Memory.

DON has support for 32 or 64-bit CPUs, the hardware ABM/BMI instruction,
big-endian machines such as Power PC, and other platforms.

DON also support for Polyglot opening books.
For information about how to create such books, consult the Polyglot documentation.
The book file can be selected by setting the *Book File* UCI parameter.

### Files

This distribution of DON consists of the following files:

  * Readme.md, the file you are currently reading.

  * Copying.txt, a text file containing the GNU General Public License.

  * src, a subdirectory containing the full source code, including a Makefile
    that can be used to compile DON on Unix-like systems.

### Compiling it yourself

On Unix-like systems, it should be possible to compile DON
directly from the source code with the included Makefile.

In general it is recommended to run `make help` to see a list of make
targets with corresponding descriptions. When not using the Makefile to
compile (for instance with Microsoft MSVC) you need to manually
set/unset some switches in the compiler command line;
see file *Platform.h* for a quick reference.


### Terms of use

DON is free, and distributed under the **GNU General Public License** (GPL).
Essentially, this means that you are free to do almost exactly what
you want with the program, including distributing it among your friends,
making it available for download from your web site, selling it
(either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute DON in some way,
you must always include the full source code, or a pointer to where the
source code can be found. If you make any changes to the source code,
these changes must also be made available under the GPL.

For full details, read the copy of the GPL found in the file named *Copying.txt*
