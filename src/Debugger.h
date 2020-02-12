#pragma once

#include "Types.h"

// Debug functions used mainly to collect run-time statistics
namespace Debugger
{

    extern void initialize();

    extern void hitOn(bool);
    extern void hitOn(bool, bool);

    extern void meanOf(i64);

    extern void print();

}
