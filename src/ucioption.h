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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace DON {

// Because the UCI options should be case-insensitive

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

inline bool is_ok(OptionType ot) noexcept { return OPT_BUTTON <= ot && ot <= OPT_COMBO; }

std::string_view to_string(OptionType ot) noexcept;

class Options;

// The Option class implements each option as specified by the UCI protocol
class Option final {
   public:
    using OnChange = std::function<std::optional<std::string>(const Option&)>;

    explicit Option(OnChange&& f) noexcept;
    explicit Option(bool v, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, OnChange&& f = nullptr) noexcept;
    explicit Option(int v, int minv, int maxv, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, const char* var, OnChange&& f = nullptr) noexcept;

    operator int() const noexcept;
    operator std::string() const noexcept;

    void operator<<(const Option&) noexcept = delete;

    friend bool operator==(const Option& o1, const Option& o2) noexcept;
    friend bool operator!=(const Option& o1, const Option& o2) noexcept;

    friend bool operator<(const Option& o1, const Option& o2) noexcept;
    friend bool operator>(const Option& o1, const Option& o2) noexcept;

    void operator=(std::string value) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Option& option) noexcept;

   private:
    OptionType                    type;
    std::string                   defaultValue;
    std::string                   currentValue;
    int                           minValue = 0, maxValue = 0;
    std::vector<std::string_view> comboValues;
    OnChange                      onChange;

    std::uint16_t  idx;
    const Options* optionsPtr = nullptr;

    friend class Options;
};

class Options final {
   public:
    // The options container is defined as a std::unordered_map<>
    using UnorderedMap =
      std::unordered_map<std::string, Option, CaseInsensitiveHash, CaseInsensitiveEqual>;
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

    auto contains(const std::string& name) const noexcept {  //
        return options.find(name) != options.end();
    }
    auto count(const std::string& name) const noexcept {  //
        return options.count(name);
    }

    void set_info_listener(InfoListener&& listener) noexcept;

    void add(const std::string& name, const Option& option) noexcept;

    void set(const std::string& name, const std::string& value) noexcept;

    const Option& operator[](const std::string& name) const noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Options& options) noexcept;

   private:
    UnorderedMap options;
    InfoListener infoListener;

    friend class Option;
};

}  // namespace DON

#endif  // #ifndef UCIOPTION_H_INCLUDED
