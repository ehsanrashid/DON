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

namespace DON {

// Because the UCI options should be case-insensitive

// Define a custom case-insensitive hash
struct CaseInsensitiveHash final {
    std::uint64_t operator()(const std::string& key) const noexcept;
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
    OPT_NONE,
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

    explicit Option(const Options* pOptions) noexcept;
    Option() noexcept;
    explicit Option(OnChange&& f) noexcept;
    explicit Option(bool v, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, OnChange&& f = nullptr) noexcept;
    explicit Option(int v, int minv, int maxv, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, const char* cur, OnChange&& f = nullptr) noexcept;

    operator int() const noexcept;
    operator std::string() const noexcept;

    void operator<<(const Option& option) noexcept;

    friend bool operator==(const Option& o, std::string_view value) noexcept;
    friend bool operator!=(const Option& o, std::string_view value) noexcept;

    friend bool operator==(const Option& o1, const Option& o2) noexcept;
    friend bool operator!=(const Option& o1, const Option& o2) noexcept;

    friend bool operator<(const Option& o1, const Option& o2) noexcept;
    friend bool operator>(const Option& o1, const Option& o2) noexcept;

    Option& operator=(std::string value) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Option& option) noexcept;
    friend std::ostream& operator<<(std::ostream& os, const Options& options) noexcept;

   private:
    std::uint16_t idx = -1;

    OptionType  type;
    std::string defaultValue, currentValue;
    int         minValue, maxValue;
    OnChange    onChange;

    const Options* options = nullptr;

    //friend class Options;
};

class Options final {
   public:
    // The options container is defined as a std::unordered_map<>
    using OptionMap =
      std::unordered_map<std::string, Option, CaseInsensitiveHash, CaseInsensitiveEqual>;
    using OptionPair = std::pair<OptionMap::key_type, OptionMap::mapped_type>;

    using InfoListener = std::function<void(std::optional<std::string>)>;

    // Sort in ascending order
    static bool compare_pair(const OptionPair& op1, const OptionPair& op2) noexcept {
        return op1.second < op2.second;
    }

    Options() noexcept                          = default;
    Options(const Options&) noexcept            = delete;
    Options(Options&&) noexcept                 = delete;
    Options& operator=(const Options&) noexcept = delete;
    Options& operator=(Options&&) noexcept      = delete;

    void add_info_listener(InfoListener&&);

    void setoption(const std::string& name, const std::string& value) noexcept;

    auto begin() const noexcept { return options.begin(); }
    auto end() const noexcept { return options.end(); }

    auto size() const noexcept { return options.size(); }

    std::size_t count(const std::string& name) const noexcept;

    Option  operator[](const std::string& name) const noexcept;
    Option& operator[](const std::string& name) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Options& options) noexcept;

   private:
    OptionMap    options;
    InfoListener infoListener;

    friend class Option;
};

}  // namespace DON

#endif  // #ifndef UCIOPTION_H_INCLUDED