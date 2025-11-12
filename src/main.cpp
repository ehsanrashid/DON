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

#include <iostream>
#include <memory>

#include "bitboard.h"
#include "misc.h"
#include "nnue/features/full_threats.h"
#include "position.h"
#include "search.h"
#include "syzygy/tbbase.h"
#include "tune.h"
#include "uci.h"

using namespace DON;

int main(int argc, const char* argv[]) {

    std::cout << engine_info() << std::endl;

    BitBoard::init();
    Position::init();
    NNUE::Features::FullThreats::init();
    Search::init();
    Tablebases::init();

    auto uci = std::make_unique<UCI>(argc, argv);

    Tune::init(uci->options());

    uci->run();

    return 0;
}
