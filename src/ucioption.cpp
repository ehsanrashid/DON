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
const std::unordered_map<OptionType, std::string_view> OptionTypeString{
  {BUTTON,   "button"},
  {CHECK,    "check"},
  {STRING,   "string"},
  {SPIN,     "spin"},
  {COMBO,    "combo"}};
// clang-format on
}  // namespace


bool CaseInsensitiveLess::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return std::lexicographical_compare(
      s1.begin(), s1.end(), s2.begin(), s2.end(),
      [](char c1, char c2) noexcept -> bool { return std::tolower(c1) < std::tolower(c2); });
}

std::string_view to_string(OptionType optType) noexcept {
    auto itr = OptionTypeString.find(optType);
    return itr != OptionTypeString.end() ? itr->second : "none";
}

Option::Option(const Options* ptrOptions) :
    type(OPT_NONE),
    options(ptrOptions) {}

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
void Option::operator<<(const Option& option) noexcept {
    static std::uint16_t insertOrder = 0;

    auto ptrOptions = this->options;
    *this           = option;
    this->options   = ptrOptions;

    idx = insertOrder++;
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
        if (!value.empty() && is_whitespace(value))
            value.clear();
    }
    else if (type == SPIN)
    {
        int d = std::stoi(value);
        if (d < minValue || maxValue < d)
            value = std::to_string(std::clamp(d, minValue, maxValue));
    }
    else if (type == COMBO)
    {
        Options combos;  // To have case-insensitive compare

        std::istringstream iss(defaultValue);

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

    if (type != BUTTON)
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

    if (option.type != BUTTON)
        os << " default " << option.defaultValue;

    if (option.type == SPIN)
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

Option Options::operator[](const std::string& name) const noexcept {
    auto itr = options.find(name);
    return itr != options.end() ? itr->second : Option(this);
}

Option& Options::operator[](const std::string& name) noexcept {
    if (options.find(name) == options.end())
        options[name] = Option(this);
    return options[name];
}

std::size_t Options::count(const std::string& name) const noexcept { return options.count(name); }

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept {
    for (std::uint16_t idx = 0; idx < options.size(); ++idx)
        for (const auto& [fst, snd] : options)
            if (snd.idx == idx)
            {
                os << "\noption name " << fst << snd;
                break;
            }

    return os;
}

}  // namespace DON
