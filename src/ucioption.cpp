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

#include "ucioption.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "misc.h"

namespace DON {

namespace {

constexpr inline std::string_view EMPTY_STRING{"<empty>"};

// clang-format off
const inline std::unordered_map<OptionType, std::string_view> OptionTypeMap{
  {OPT_BUTTON, "button"},
  {OPT_CHECK,  "check"},
  {OPT_STRING, "string"},
  {OPT_SPIN,   "spin"},
  {OPT_COMBO,  "combo"}};
// clang-format on
}  // namespace

std::size_t CaseInsensitiveHash::operator()(const std::string& str) const noexcept {
    return std::hash<std::string>()(lower_case(str));
}

bool CaseInsensitiveEqual::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
                      [](unsigned char c1, unsigned char c2) noexcept {
                          return std::tolower(c1) == std::tolower(c2);
                      });
}

bool CaseInsensitiveLess::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
                                        [](unsigned char c1, unsigned char c2) noexcept {
                                            return std::tolower(c1) < std::tolower(c2);
                                        });
}

std::string_view to_string(OptionType ot) noexcept {
    auto itr = OptionTypeMap.find(ot);
    return itr != OptionTypeMap.end() ? itr->second : "none";
}

Option::Option(OnChange&& f) noexcept :
    type(OPT_BUTTON),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {}

Option::Option(bool v, OnChange&& f) noexcept :
    type(OPT_CHECK),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {
    defaultValue = currentValue = bool_to_string(v);
}

Option::Option(const char* v, OnChange&& f) noexcept :
    type(OPT_STRING),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {
    defaultValue = currentValue = is_whitespace(v) || lower_case(v) == EMPTY_STRING ? "" : v;
}

Option::Option(int v, int minv, int maxv, OnChange&& f) noexcept :
    type(OPT_SPIN),
    minValue(minv),
    maxValue(maxv),
    onChange(std::move(f)) {
    defaultValue = currentValue = std::to_string(v);
}

Option::Option(const char* cur, const char* var, OnChange&& f) noexcept :
    type(OPT_COMBO),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {
    defaultValue = var;
    currentValue = cur;
}

Option::operator int() const noexcept {
    assert(type == OPT_CHECK || type == OPT_SPIN);
    return type == OPT_CHECK ? string_to_bool(currentValue) : std::stoi(currentValue);
}

Option::operator std::string() const noexcept {
    assert(type == OPT_STRING || type == OPT_COMBO);
    return currentValue;
}

bool operator==(const Option& o1, const Option& o2) noexcept {  //
    return o1.idx == o2.idx && o1.type == o2.type;
}
bool operator!=(const Option& o1, const Option& o2) noexcept {  //
    return !(o1 == o2);
}

bool operator<(const Option& o1, const Option& o2) noexcept {  //
    return o1.idx < o2.idx;
}
bool operator>(const Option& o1, const Option& o2) noexcept {  //
    return (o2 < o1);
}

// Updates currentValue and triggers onChange() action.
// It's up to the GUI to check for option's limit,
// but could receive the new value from the user, so let's check the bounds anyway.
void Option::operator=(std::string value) noexcept {
    assert(is_ok(type));

    if (type != OPT_BUTTON && type != OPT_STRING && value.empty())
        return;

    switch (type)
    {
    case OPT_CHECK :
        value = lower_case(value);
        if (!(value == "true" || value == "false"))
            return;
        break;
    case OPT_STRING :
        if (is_whitespace(value) || lower_case(value) == EMPTY_STRING)
            value.clear();
        break;
    case OPT_SPIN :
        value = std::to_string(std::clamp(std::stoi(value), minValue, maxValue));
        break;
    case OPT_COMBO : {
        value = lower_case(value);

        auto combos = split(defaultValue, "var", true);
        if (std::find(combos.begin(), combos.end(), value) == combos.end())
            return;
    }
    break;
    default :;
    }

    if (type != OPT_BUTTON)
        currentValue = value;

    if (onChange)
    {
        auto optInfo = onChange(*this);
        if (optInfo && optionsPtr != nullptr && optionsPtr->infoListener)
            optionsPtr->infoListener(optInfo);
    }
}

std::ostream& operator<<(std::ostream& os, const Option& option) noexcept {
    os << " type " << to_string(option.type);

    if (option.type == OPT_BUTTON)
        return os;

    os << " default ";
    if (option.type == OPT_COMBO)
        os << option.currentValue << " " << option.defaultValue;
    else if (option.type == OPT_STRING && is_whitespace(option.defaultValue))
        os << EMPTY_STRING;
    else
        os << option.defaultValue;

    if (option.type == OPT_SPIN)
        os << " min " << option.minValue << " max " << option.maxValue;

    return os;
}

void Options::set_info_listener(InfoListener&& listener) noexcept {
    infoListener = std::move(listener);
}

// Add option and assigns idx in the correct insertion order
void Options::add(const std::string& name, const Option& option) noexcept {
    static std::uint16_t insertOrder = 0;

    if (contains(name))
    {
        std::cerr << "Option: '" << name << "' was already added!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    auto& o = options.emplace(name, option).first->second;

    o.idx        = insertOrder++;
    o.optionsPtr = this;
}

void Options::set(const std::string& name, const std::string& value) noexcept {
    if (contains(name))
        options.at(name) = value;
    else
        std::cout << "No such option: '" << name << "'" << std::endl;
}

const Option& Options::operator[](const std::string& name) const noexcept {
    assert(contains(name));
    return options.at(name);
}

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept {

    std::vector<Options::Pair> optionPairs(options.begin(), options.end());
    std::sort(optionPairs.begin(), optionPairs.end(),
              [](const auto& op1, const auto& op2) noexcept { return op1.second < op2.second; });
    for (const auto& [name, option] : optionPairs)
        os << "\noption name " << name << option;

    return os;
}

}  // namespace DON
