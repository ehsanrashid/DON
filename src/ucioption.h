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

#ifndef UCIOPTION_H_INCLUDED
#define UCIOPTION_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>

namespace DON {

// Define a custom comparator, because the UCI options should be case-insensitive
struct CaseInsensitiveLess final {
    bool operator()(std::string_view s1, std::string_view s2) const noexcept;
};

enum OptionType : std::uint8_t {
    OPT_NONE,
    BUTTON,
    CHECK,
    STRING,
    SPIN,
    COMBO
};

inline bool is_ok(OptionType ot) noexcept { return BUTTON <= ot && ot <= COMBO; }

std::string_view to_string(OptionType ot) noexcept;

// The Option class implements each option as specified by the UCI protocol
class Option final {
   public:
    using OnChange = std::function<void(const Option&)>;

    Option() noexcept;
    explicit Option(OnChange&& f) noexcept;
    explicit Option(bool v, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, OnChange&& f = nullptr) noexcept;
    explicit Option(int v, int minv, int maxv, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, const char* cur, OnChange&& f = nullptr) noexcept;

    operator int() const noexcept;
    operator std::string() const noexcept;

    bool operator==(std::string_view v) const noexcept;
    bool operator!=(std::string_view v) const noexcept;

    void    operator<<(const Option& option) noexcept;
    Option& operator=(std::string value) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Option& option) noexcept;

    std::uint16_t idx = -1;

   private:
    OptionType  type;
    std::string defaultValue, currentValue;
    int         minValue, maxValue;
    OnChange    onChange;
};

class Options final {
   public:
    void setoption(const std::string& name, const std::string& value) noexcept;

    Option  operator[](const std::string& name) const noexcept;
    Option& operator[](const std::string& name) noexcept;

    auto begin() const noexcept { return options.begin(); }
    auto end() const noexcept { return options.end(); }

    auto size() const noexcept { return options.size(); }

    std::size_t count(const std::string& name) const noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Options& options) noexcept;

   private:
    // The options container is defined as a std::map
    using OptionMap = std::map<std::string, Option, CaseInsensitiveLess>;

    OptionMap options;
};

}  // namespace DON

#endif  // #ifndef UCIOPTION_H_INCLUDED
