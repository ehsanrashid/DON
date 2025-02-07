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
#include <sstream>
#include <utility>
#include <vector>

#include "misc.h"

namespace DON {

namespace {
// clang-format off
const inline std::unordered_map<OptionType, std::string_view> OptionTypeMap{
  {OPT_BUTTON, "button"},
  {OPT_CHECK,  "check"},
  {OPT_STRING, "string"},
  {OPT_SPIN,   "spin"},
  {OPT_COMBO,  "combo"}};
// clang-format on
}  // namespace

std::uint64_t CaseInsensitiveHash::operator()(const std::string& key) const noexcept {
    return std::hash<std::string>()(lower_case(key));
}

bool CaseInsensitiveEqual::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return s1.size() == s2.size()
        && std::equal(
             s1.begin(), s1.end(), s2.begin(), s2.end(),  //
             [](char c1, char c2) noexcept { return std::tolower(c1) == std::tolower(c2); });
}

bool CaseInsensitiveLess::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return std::lexicographical_compare(
      s1.begin(), s1.end(), s2.begin(), s2.end(),
      [](char c1, char c2) noexcept { return std::tolower(c1) < std::tolower(c2); });
}

std::string_view to_string(OptionType ot) noexcept {
    auto itr = OptionTypeMap.find(ot);
    return itr != OptionTypeMap.end() ? itr->second : "none";
}

Option::Option() noexcept :
    type(OPT_NONE),
    minValue(0),
    maxValue(0),
    onChange(nullptr) {}

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
    defaultValue = currentValue = is_empty(v) ? "" : v;
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

bool operator==(const Option& o, std::string_view value) noexcept {
    assert(o.type == OPT_COMBO);
    return CaseInsensitiveEqual()(o.currentValue, value);
}
bool operator!=(const Option& o, std::string_view value) noexcept { return !(o == value); }

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

// Updates currentValue and triggers on_change() action.
// It's up to the GUI to check for option's limit,
// but could receive the new value from the user by console window,
// so let's check the bounds anyway.
Option& Option::operator=(std::string value) noexcept {
    assert(is_ok(type));

    if (type != OPT_BUTTON && type != OPT_STRING && value.empty())
        return *this;

    if (type == OPT_CHECK)
    {
        if (!is_bool(value))
            return *this;
    }
    else if (type == OPT_STRING)
    {
        if (is_empty(value))
            value.clear();
    }
    else if (type == OPT_SPIN)
    {
        value = std::to_string(std::clamp(std::stoi(value), minValue, maxValue));
    }
    else if (type == OPT_COMBO)
    {
        Options combos;  // To have case-insensitive compare

        std::istringstream iss(defaultValue);
        iss >> std::skipws;

        std::string token;
        while (iss >> token)
        {
            token = lower_case(token);
            if (token == "var")
                continue;
            combos.add(token, Option());
        }
        if (lower_case(value) == "var" || !combos.contains(value))
            return *this;
    }

    if (type != OPT_BUTTON)
        currentValue = value;

    if (onChange)
    {
        auto infoOpt = onChange(*this);
        if (infoOpt && optionsPtr != nullptr && optionsPtr->infoListener)
            optionsPtr->infoListener(infoOpt);
    }

    return *this;
}

std::ostream& operator<<(std::ostream& os, const Option& option) noexcept {
    os << " type " << to_string(option.type);

    if (option.type != OPT_BUTTON)
        os << " default "
           << (option.type == OPT_STRING && is_whitespace(option.defaultValue) ? EMPTY_STRING
               : option.type == OPT_COMBO                                      ? option.currentValue
                                          : option.defaultValue);
    if (option.type == OPT_COMBO)
        os << " " << option.defaultValue;
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

    auto& o = options[name] = option;

    o.idx        = insertOrder++;
    o.optionsPtr = this;
}

void Options::set(const std::string& name, const std::string& value) noexcept {
    if (contains(name))
        options[name] = value;
    else
        std::cout << "No such option: '" << name << "'" << std::endl;
}

const Option& Options::operator[](const std::string& name) const noexcept {
    assert(contains(name));
    return options.at(name);
}

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept {
    std::vector<Options::Pair> optPairs(options.begin(), options.end());
    std::sort(optPairs.begin(), optPairs.end(),
              // Sort in ascending order
              [](const auto& op1, const auto& op2) noexcept { return op1.second < op2.second; });
    for (const auto& [fst, snd] : optPairs)
        os << "\noption name " << fst << snd;

    return os;
}

}  // namespace DON
