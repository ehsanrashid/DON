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

#ifndef OPTION_H_INCLUDED
#define OPTION_H_INCLUDED

#include <functional>
#include <iosfwd>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "misc.h"

namespace DON {

class Options;

// Option class implements each option as specified by the UCI protocol
class Option final {
   public:
    enum class Type : u8 {
        BUTTON,
        CHECK,
        STRING,
        SPIN,
        COMBO
    };

    static constexpr bool is_ok(const Type t) noexcept {
        return Type::BUTTON <= t && t <= Type::COMBO;
    }

    static constexpr std::string_view to_string(const Type t) noexcept {
        switch (t)
        {
        case Type::BUTTON :
            return "button";
        case Type::CHECK :
            return "check";
        case Type::STRING :
            return "string";
        case Type::SPIN :
            return "spin";
        case Type::COMBO :
            return "combo";
        }
        return "none";
    }

    using OnChange = std::function<std::optional<std::string>(const Option&)>;

    explicit Option(OnChange&& f = nullptr) noexcept;
    explicit Option(bool v, OnChange&& f = nullptr) noexcept;
    explicit Option(char ch, OnChange&& f = nullptr) noexcept = delete;
    explicit Option(std::string_view v, OnChange&& f = nullptr) noexcept;
    explicit Option(const char* v, OnChange&& f = nullptr) noexcept :
        Option(std::string_view(v), std::forward<OnChange>(f)) {}
    Option(int v, int minV, int maxV, OnChange&& f = nullptr) noexcept;
    Option(std::string_view v, StringViews&& var, OnChange&& f = nullptr) noexcept;

    operator int() const noexcept;
    operator std::string_view() const noexcept;

    void operator=(std::string value) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Option& option) noexcept;

   private:
    Type        type;
    std::string defaultValue;
    std::string currentValue;
    int         minValue = 0, maxValue = 0;
    StringViews varSvs;
    OnChange    onChange;

    const Options* optionsPtr = nullptr;

    friend class Options;
};

using OT = Option::Type;

class Options final {
   public:
    // clang-format off
    // Stores the option name view and the option, preserving the original name case.
    using Entry    = std::pair<std::string_view, Option>;
    // Preserves insertion order and the original name case.
    using List     = std::list<Entry>;
    // Provides fast case-insensitive name lookup and removal.
    using IndexMap = std::unordered_map<std::string_view, List::iterator, CaseInsensitiveHash, CaseInsensitiveEqual>;
    // Provides fast case-insensitive uniqueness and membership checks.
    using Set      = std::unordered_set<std::string_view, CaseInsensitiveHash, CaseInsensitiveEqual>;
    // clang-format on

    using OnInfo = std::function<void(std::optional<std::string_view>)>;

    Options() noexcept                          = default;
    Options(const Options&) noexcept            = delete;
    Options& operator=(const Options&) noexcept = delete;
    Options(Options&&) noexcept                 = delete;
    Options& operator=(Options&&) noexcept      = delete;

    auto begin() noexcept;
    auto end() noexcept;
    auto begin() const noexcept;
    auto end() const noexcept;

    usize size() const noexcept;
    bool  empty() const noexcept;

    bool contains(Set::const_iterator setItr) const noexcept;
    bool contains(std::string_view name) const noexcept;

    usize count(std::string_view name) const noexcept;

    auto find(std::string_view name) noexcept;
    auto find(std::string_view name) const noexcept;

    bool add(std::string_view name, const Option& option) noexcept;
    bool remove(std::string_view name) noexcept;

    void setoption(std::string_view name, std::string_view value) noexcept;

    const Option& operator[](std::string_view name) const noexcept;

    void set_on_info(OnInfo&& f) noexcept;

   private:
    List     list;
    IndexMap indexMap;
    Set      set;

    OnInfo onInfo;

    friend class Option;
};

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept;

}  // namespace DON

#endif  // OPTION_H_INCLUDED
