#ifndef _BENCHMARK_H_INC_
#define _BENCHMARK_H_INC_

#include <iosfwd>

class Position;

extern void benchmark (std::istream &is, const Position &cur_pos);

extern void auto_tune (std::istream &is);

#endif // _BENCHMARK_H_INC_
