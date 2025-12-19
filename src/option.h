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

#ifndef OPTION_H_INCLUDED
#define OPTION_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "misc.h"

namespace DON {

// Because the options should be case-insensitive

// Define a custom case-insensitive hash
struct CaseInsensitiveHash final {
    std::size_t operator()(std::string_view str) const noexcept;
};
// Define a custom case-insensitive equality
struct CaseInsensitiveEqual final {
    bool operator()(std::string_view s1, std::string_view s2) const noexcept;
};
// Define a custom case-insensitive less
struct CaseInsensitiveLess final {
    bool operator()(std::string_view s1, std::string_view s2) const noexcept;
};

enum OptionType : std::uint8_t {
    OPT_BUTTON,
    OPT_CHECK,
    OPT_STRING,
    OPT_SPIN,
    OPT_COMBO
};

constexpr bool is_ok(OptionType ot) noexcept { return OPT_BUTTON <= ot && ot <= OPT_COMBO; }

std::string_view to_string(OptionType ot) noexcept;

class Options;

// Option class implements each option as specified by the UCI protocol
class Option final {
   public:
    using OnChange = std::function<std::optional<std::string>(const Option&)>;

    explicit Option(OnChange&& f = nullptr) noexcept;
    explicit Option(bool v, OnChange&& f = nullptr) noexcept;
    explicit Option(std::string_view v, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, OnChange&& f = nullptr) noexcept :
        Option(std::string_view(v), std::forward<OnChange>(f)) {}
    Option(int v, int minv, int maxv, OnChange&& f = nullptr) noexcept;
    Option(std::string_view v, std::string_view var, OnChange&& f = nullptr) noexcept;

    operator int() const noexcept;
    operator std::string() const noexcept;
    operator std::string_view() const noexcept;

    friend constexpr bool operator==(const Option& o1, const Option& o2) noexcept {
        return o1.idx == o2.idx && o1.type == o2.type;
    }
    friend constexpr bool operator!=(const Option& o1, const Option& o2) noexcept {
        return !(o1 == o2);
    }

    friend constexpr bool operator<(const Option& o1, const Option& o2) noexcept {
        return o1.idx < o2.idx;
    }
    friend constexpr bool operator>(const Option& o1, const Option& o2) noexcept {
        return (o2 < o1);
    }

    void operator=(std::string value) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Option& option) noexcept;

   private:
    OptionType  type;
    std::string defaultValue;
    std::string currentValue;
    int         minValue = 0, maxValue = 0;
    StringViews comboValues;
    OnChange    onChange;

    std::uint16_t  idx;
    const Options* optionsPtr = nullptr;

    friend class Options;
};

class Options final {
   public:
    // The options container is defined as a std::unordered_map<>
    using UnorderedMap =
      std::unordered_map<std::string_view, Option, CaseInsensitiveHash, CaseInsensitiveEqual>;
    using Pair = std::pair<UnorderedMap::key_type, UnorderedMap::mapped_type>;

    using InfoListener = std::function<void(const std::optional<std::string>&)>;

    Options() noexcept                          = default;
    Options(const Options&) noexcept            = delete;
    Options(Options&&) noexcept                 = delete;
    Options& operator=(const Options&) noexcept = delete;
    Options& operator=(Options&&) noexcept      = delete;

    auto begin() const noexcept { return options.begin(); }
    auto end() const noexcept { return options.end(); }
    auto begin() noexcept { return options.begin(); }
    auto end() noexcept { return options.end(); }

    auto size() const noexcept { return options.size(); }
    auto empty() const noexcept { return options.empty(); }

    auto contains(std::string_view name) const noexcept {
        return options.find(name) != options.end();
    }
    auto count(std::string_view name) const noexcept { return options.count(name); }

    void set_info_listener(InfoListener&& infoHandler) noexcept;

    void add(std::string_view name, const Option& option) noexcept;

    void set(std::string_view name, std::string_view value) noexcept;

    const Option& operator[](std::string_view name) const noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Options& options) noexcept;

   private:
    UnorderedMap options;
    InfoListener infoListener;

    friend class Option;
};

}  // namespace DON

#endif  // #ifndef OPTION_H_INCLUDED
