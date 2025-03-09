/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdlib>
#include <iostream>

#include "bitboard.h"
#include "misc.h"
#include "position.h"
//#include "tune.h"
#include "types.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

using namespace DON;

int main(int argc, const char** argv) noexcept {

    std::cout << engine_info() << std::endl;

#if !defined(NDEBUG)
    Debug::init();
#endif

    BitBoard::init();
    Tablebases::init();
    Position::init();

    UCI uci(argc, argv);

    //Tune::init(uci.engine_options());

    uci.execute();

    return EXIT_SUCCESS;
}
