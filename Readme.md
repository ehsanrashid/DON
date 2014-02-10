### Overview

DON is a free UCI chess engine. It is not a complete chess program and requires some UCI-compatible GUI (e.g. XBoard with PolyGlot, eboard, Arena, Sigma Chess, Shredder, Chess
Partner or Fritz) in order to be used comfortably.
Read the documentation for your GUI of choice for information about how to use DON with it.

This version of DON supports up to 64 CPUs. The engine defaults to one search thread,
so it is therefore recommended to inspect the value of the *Threads* UCI parameter,
and to make sure it equals the number of CPU cores on your computer.
