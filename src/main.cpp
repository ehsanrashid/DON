/*
  DON, a UCI chess playing engine derived from Glaurung 2.1

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

#include "bitboard.h"
#include "misc.h"
#include "position.h"
#include "types.h"
#include "uci.h"
#include "tune.h"

using namespace DON;

int main(int argc, const char** argv) noexcept {

    std::cout << engine_info() << '\n';

    Bitboards::init();
    Position::init();

    UCI uci(argc, argv);

    Tune::init(uci.engine_options());

    uci.handle_commands();

    return 0;
}
