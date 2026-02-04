/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

#include "bitboard.h"
#include "memory.h"
#include "misc.h"
#include "position.h"
#include "syzygy/tablebase.h"
#include "tune.h"
#include "uci.h"

using namespace DON;

int main(int argc, const char* argv[]) noexcept {

    std::cout << engine_info() << std::endl;

    BitBoard::init();
    Position::init();
    Tablebase::init();

    UCI uci(argc, argv);

    Tune::init(uci.options());

    if (uci.arguments().size() <= 1)
    {
        uci.process_input(std::cin);
    }
    else
    {
        std::string command;
        command.reserve(256);

        for (std::size_t i = 1; i < uci.arguments().size(); ++i)
        {
            if (!command.empty())
                command += ' ';

            command += uci.arguments()[i];
        }

        uci.execute(command);
    }

    return 0;
}
