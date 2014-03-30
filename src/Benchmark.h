#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BENCHMARK_H_INC_
#define _BENCHMARK_H_INC_

#include <iosfwd>

class Position;

extern void benchmark (std::istream &is, const Position &pos);
extern void benchtest (std::istream &is, const Position &pos);

#endif // _BENCHMARK_H_INC_
