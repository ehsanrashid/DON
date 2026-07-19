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

#include <iostream>
#include <string>
#include <string_view>

#include "attacks.h"
#include "memory.h"
#include "misc.h"
#include "position.h"
#include "tune.h"
#include "uci.h"
#include "tablebase/syzygy.h"

using namespace DON;

int main(int argc, const char* argv[]) noexcept {

    set_console_output(ConsoleOutputMode::UTF8);

    std::cout << engine_info() << std::endl;

    std::cout << timestamp() << std::endl;

    show_logo();

    Attacks::init();

    Position::init();

    Tablebase::Syzygy::init();

    CommandLine commandLine(argc, argv);

    UCI uci(path_from_utf8(!commandLine.arguments.empty() ? commandLine.arguments[0] : "."));

    Tune::init(uci.options());

    if (commandLine.arguments.size() > 1)
    {
        std::string command;
        command.reserve(256);

        for (usize i = 1; i < commandLine.arguments.size(); ++i)
        {
            if (!command.empty())
                command.push_back(' ');

            command.append(commandLine.arguments[i]);
        }

        uci.execute(command);
    }
    else
    {
        uci.process_input(std::cin);
    }
    return 0;
}
