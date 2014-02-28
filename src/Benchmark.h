#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BENCHMARK_H_
#define _BENCHMARK_H_

#include <iosfwd>

class Position;

extern void benchmark (std::istream &is, const Position &pos);

#endif // _BENCHMARK_H_