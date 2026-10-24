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

#ifndef UTILITY_LOGGER_H_INCLUDED
#define UTILITY_LOGGER_H_INCLUDED

#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <string_view>

#include "../utility/streambuf.h"
#include "../utility/types.h"

namespace DON {

// Logger
//
// Manages runtime logging by redirecting std::cin and std::cout through
// TieBuf objects to mirror I/O to a log file.
class Logger final {
   public:
    // Starts logging to the specified file.
    // Returns true on success and false if the log file cannot be opened.
    [[nodiscard]] static bool start(const fs::path& logPath) noexcept;
    // Stops logging, restores the original streams, and closes the log file.
    static void stop() noexcept;

   private:
    Logger() noexcept = delete;
    // Initializes the logger with the streams to be redirected and mirrored.
    Logger(std::istream& isRef, std::ostream& osRef) noexcept;
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;
    // Stops logging and restores the original streams.
    ~Logger() noexcept;

    // Returns the single shared Logger instance.
    [[nodiscard]] static Logger& instance() noexcept;
    // Opens the specified log file and redirects the streams through TieBuf.
    // Caller must hold 'mutex'.
    [[nodiscard]] bool open(const fs::path& logPath) noexcept;
    // Restores the original streams and closes the log file.
    // Caller must hold 'mutex'.
    void close() noexcept;
    // Returns true if the log file is open.
    [[nodiscard]] bool is_open() const noexcept;

    // Writes a timestamped marker to the log file.
    void write_timestamp(std::string_view suffix) noexcept;

    std::mutex      mutex;
    std::ofstream   ofs;
    std::istream&   is;
    std::ostream&   os;
    std::streambuf *isBuf = nullptr, *osBuf = nullptr;
    TieBuf          itBuf, otBuf;
    std::string     filename;
};

}  // namespace DON

#endif  // UTILITY_LOGGER_H_INCLUDED
