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

#include "../utility/logger.h"

#include <iostream>

#include "../utility/functions.h"
#include "../utility/streambuf.h"
#include "../utility/types.h"

namespace DON {

bool Logger::start(const fs::path& logPath) noexcept {
    std::lock_guard lockGuard{instance().mutex};

    return instance().open(logPath);
}

void Logger::stop() noexcept {
    std::lock_guard lockGuard{instance().mutex};

    instance().close();
}

Logger::Logger(std::istream& isRef, std::ostream& osRef) noexcept :
    is(isRef),
    os(osRef),
    isBuf(is.rdbuf()),
    osBuf(os.rdbuf()),
    itBuf(is.rdbuf(), ofs.rdbuf()),
    otBuf(os.rdbuf(), ofs.rdbuf()) {}

Logger::~Logger() noexcept { close(); }

Logger& Logger::instance() noexcept {
    static Logger logger(std::cin, std::cout);

    return logger;
}

bool Logger::open(const fs::path& logPath) noexcept {
    if (filename == logPath.string() && is_open())
        return true;  // Already open

    close();

    if (logPath.empty())
        return true;

    filename = logPath.string();

    ofs.open(filename, std::ios::out | std::ios::app);

    if (!is_open())
    {
        DEBUG_LOG("Unable to open Log file: " << filename);
        return false;
    }

    write_timestamp("->");

    is.rdbuf(&itBuf);
    os.rdbuf(&otBuf);

    return true;
}

void Logger::close() noexcept {
    if (!is_open())
        return;

    is.rdbuf(isBuf);
    os.rdbuf(osBuf);

    write_timestamp("<-");

    ofs.close();

    filename.clear();
}

bool Logger::is_open() const noexcept { return ofs.is_open(); }

void Logger::write_timestamp(std::string_view suffix) noexcept {
    if (!ofs)
        return;

    ofs << '[' << format_time(SystemClock::now()) << "] " << suffix << std::endl;
}

}  // namespace DON
