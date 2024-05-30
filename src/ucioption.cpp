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
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "misc.h"

namespace DON {

namespace {
// clang-format off
std::unordered_map<OptionType, std::string_view> OptionTypeString{
  {OPT_NONE, "none"},
  {BUTTON,   "button"},
  {CHECK,    "check"},
  {STRING,   "string"},
  {SPIN,     "spin"},
  {COMBO,    "combo"}};
// clang-format on
}  // namespace

std::string_view to_string(OptionType ot) noexcept { return OptionTypeString[ot]; }

Option::Option() noexcept :
    type(OPT_NONE),
    minValue(0),
    maxValue(0),
    onChange(nullptr) {}

Option::Option(OnChange&& f) noexcept :
    type(BUTTON),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {}

Option::Option(bool v, OnChange&& f) noexcept :
    type(CHECK),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {
    std::ostringstream oss;
    oss << std::boolalpha << v;
    defaultValue = currentValue = oss.str();
}

Option::Option(const char* v, OnChange&& f) noexcept :
    type(STRING),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {
    defaultValue = currentValue = v;
}

Option::Option(int v, int minv, int maxv, OnChange&& f) noexcept :
    type(SPIN),
    minValue(minv),
    maxValue(maxv),
    onChange(std::move(f)) {
    defaultValue = currentValue = std::to_string(v);
}

Option::Option(const char* v, const char* cur, OnChange&& f) noexcept :
    type(COMBO),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {
    defaultValue = v;
    currentValue = cur;
}

Option::operator int() const noexcept {
    assert(type == CHECK || type == SPIN);
    return type == SPIN ? std::stoi(currentValue) : currentValue == "true";
}

Option::operator std::string() const noexcept {
    assert(type == STRING);
    return currentValue;
}

bool Option::operator==(std::string_view v) const noexcept {
    assert(type == COMBO);
    return !CaseInsensitiveLess()(currentValue, v) && !CaseInsensitiveLess()(v, currentValue);
}
bool Option::operator!=(std::string_view v) const noexcept { return !(*this == v); }

// Init options and assigns idx in the correct printing order
void Option::operator<<(const Option& o) noexcept {
    static std::uint16_t insertOrder = 0;

    *this = o;
    idx   = insertOrder++;
}

// Updates currentValue and triggers on_change() action.
// It's up to the GUI to check for option's limits,
// but could receive the new value from the user by console window,
// so let's check the bounds anyway.
Option& Option::operator=(std::string value) noexcept {
    assert(is_ok(type));

    if (type != BUTTON && type != STRING && value.empty())
        return *this;

    if (type == CHECK)
    {
        value = to_lower(value);
        if (value != "true" && value != "false")
            return *this;
    }
    else if (type == STRING)
    {
        if (!value.empty() && std::all_of(value.begin(), value.end(), isspace))
            value.clear();
    }
    else if (type == SPIN)
    {
        if (int d = std::stoi(value); minValue > d || d > maxValue)
            value = std::to_string(std::clamp(d, minValue, maxValue));
    }
    else if (type == COMBO)
    {
        OptionsMap comboMap;  // To have case-insensitive compare

        std::istringstream iss(defaultValue);

        std::string token;
        while (iss >> token)
            comboMap[token] << Option();
        if (value == "var" || !comboMap.count(value))
            return *this;
    }

    if (type != BUTTON)
        currentValue = value;

    if (onChange)
        onChange(*this);

    return *this;
}

std::ostream& operator<<(std::ostream& os, const Option& o) noexcept {
    os << " type " << to_string(o.type);

    if (o.type != BUTTON)
        os << " default " << o.defaultValue;

    if (o.type == SPIN)
        os << " min " << o.minValue << " max " << o.maxValue;

    return os;
}

bool CaseInsensitiveLess::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return std::lexicographical_compare(
      s1.begin(), s1.end(), s2.begin(), s2.end(),
      [](char c1, char c2) noexcept -> bool { return std::tolower(c1) < std::tolower(c2); });
}

void OptionsMap::setoption(const std::string& name, const std::string& value) noexcept {
    if (optionsMap.find(name) != optionsMap.end())
        optionsMap[name] = value;
    else
        sync_cout << "No such option: " << name << sync_endl;
}

Option OptionsMap::operator[](const std::string& name) const noexcept {
    auto itr = optionsMap.find(name);
    return itr != optionsMap.end() ? itr->second : Option();
}

Option& OptionsMap::operator[](const std::string& name) noexcept { return optionsMap[name]; }

std::size_t OptionsMap::count(const std::string& name) const noexcept {
    return optionsMap.count(name);
}

std::ostream& operator<<(std::ostream& os, const OptionsMap& om) noexcept {
    for (std::uint16_t idx = 0; idx < om.optionsMap.size(); ++idx)
        for (const auto& [fst, snd] : om.optionsMap)
            if (snd.idx == idx)
            {
                os << "\noption name " << fst << snd;
                break;
            }

    return os;
}

}  // namespace DON
