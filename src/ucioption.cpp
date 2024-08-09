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
    return std::hash<std::string>()(to_lower(key));
}

bool CaseInsensitiveEqual::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return s1.size() == s2.size()
        && std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
                      [](char c1, char c2) noexcept -> bool {
                          return std::tolower(c1) == std::tolower(c2);
                      });
}

bool CaseInsensitiveLess::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return std::lexicographical_compare(
      s1.begin(), s1.end(), s2.begin(), s2.end(),
      [](char c1, char c2) noexcept -> bool { return std::tolower(c1) < std::tolower(c2); });
}

std::string_view to_string(OptionType ot) noexcept {
    auto itr = OptionTypeMap.find(ot);
    return itr != OptionTypeMap.end() ? itr->second : "none";
}

Option::Option(const Options* pOptions) noexcept :
    type(OPT_NONE),
    options(pOptions) {}

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

Option::Option(const char* v, const char* cur, OnChange&& f) noexcept :
    type(OPT_COMBO),
    minValue(0),
    maxValue(0),
    onChange(std::move(f)) {
    defaultValue = v;
    currentValue = cur;
}

Option::operator int() const noexcept {
    assert(type == OPT_CHECK || type == OPT_SPIN);
    return type == OPT_CHECK ? string_to_bool(currentValue) : std::stoi(currentValue);
}

Option::operator std::string() const noexcept {
    assert(type == OPT_STRING);
    return currentValue;
}

bool Option::operator==(const std::string& value) const noexcept {
    assert(type == OPT_COMBO);
    return CaseInsensitiveEqual()(currentValue, value);
}
bool Option::operator!=(const std::string& value) const noexcept { return !(*this == value); }

// Assign option and set idx in the correct insertion order
void Option::operator<<(const Option& option) noexcept {
    static std::uint16_t insertOrder = 0;

    auto pOptions = options;
    *this         = option;
    options       = pOptions;

    idx = insertOrder++;
}

bool Option::operator==(const Option& option) const noexcept {
    return idx == option.idx && type == option.type;
}
bool Option::operator!=(const Option& option) const noexcept { return !(*this == option); }

bool Option::operator<(const Option& option) const noexcept { return idx < option.idx; }
bool Option::operator>(const Option& option) const noexcept { return idx > option.idx; }

// Updates currentValue and triggers on_change() action.
// It's up to the GUI to check for option's limits,
// but could receive the new value from the user by console window,
// so let's check the bounds anyway.
Option& Option::operator=(std::string value) noexcept {
    assert(is_ok(type));

    if (type != OPT_BUTTON && type != OPT_STRING && value.empty())
        return *this;

    if (type == OPT_CHECK)
    {
        if (!is_valid_bool(value))
            return *this;
    }
    else if (type == OPT_STRING)
    {
        if (is_empty(value))
            value.clear();
    }
    else if (type == OPT_SPIN)
    {
        int d = std::stoi(value);
        if (d < minValue || maxValue < d)
            value = std::to_string(std::clamp(d, minValue, maxValue));
    }
    else if (type == OPT_COMBO)
    {
        Options combos;  // To have case-insensitive compare

        std::istringstream iss(defaultValue);
        iss >> std::skipws;

        std::string token;
        while (iss >> token)
        {
            token = to_lower(token);
            if (token == "var")
                continue;
            combos[token] << Option();
        }
        if (to_lower(value) == "var" || !combos.count(value))
            return *this;
    }

    if (type != OPT_BUTTON)
        currentValue = value;

    if (onChange != nullptr)
    {
        const auto ret = onChange(*this);
        if (ret && options != nullptr && options->infoListener != nullptr)
            options->infoListener(ret);
    }

    return *this;
}

std::ostream& operator<<(std::ostream& os, const Option& option) noexcept {
    os << " type " << to_string(option.type);

    if (option.type != OPT_BUTTON)
        os << " default "
           << (option.type == OPT_STRING && is_whitespace(option.defaultValue)
                 ? EMPTY_STRING
                 : option.defaultValue);

    if (option.type == OPT_SPIN)
        os << " min " << option.minValue << " max " << option.maxValue;

    return os;
}

void Options::add_info_listener(InfoListener&& listener) { infoListener = std::move(listener); }

void Options::setoption(const std::string& name, const std::string& value) noexcept {
    if (options.find(name) != options.end())
        options[name] = value;
    else
        sync_cout << "No such option: " << name << sync_endl;
}

std::size_t Options::count(const std::string& name) const noexcept { return options.count(name); }

Option Options::operator[](const std::string& name) const noexcept {
    auto itr = options.find(name);
    return itr != options.end() ? itr->second : Option(this);
}

Option& Options::operator[](const std::string& name) noexcept {
    if (options.find(name) == options.end())
        options[name] = Option(this);
    return options[name];
}

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept {
    /*
    for (std::uint16_t idx = 0; idx < options.size(); ++idx)
        for (const auto& [fst, snd] : options)
            if (idx == snd.idx)
            {
                os << "\noption name " << fst << snd;
                break;
            }
    */

    std::vector<Options::OptionPair> vecPairs(options.begin(), options.end());
    std::sort(vecPairs.begin(), vecPairs.end(), Options::compare_pair);
    for (const auto& [fst, snd] : vecPairs)
        os << "\noption name " << fst << snd;

    return os;
}

}  // namespace DON
