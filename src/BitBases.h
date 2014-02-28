#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BITBASES_H_INC_
#define _BITBASES_H_INC_

#include "Type.h"

namespace BitBases {

    void initialize ();

    bool probe_kpk (Color c, Square wk_sq, Square wp_sq, Square bk_sq);

}

#endif // _BITBASES_H_INC_
