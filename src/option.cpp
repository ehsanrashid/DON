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

#include "option.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <numeric>

namespace DON {

namespace {

constexpr std::string_view EMPTY_STRING{"<empty>"};

}  // namespace

std::size_t CaseInsensitiveHash::operator()(std::string_view str) const noexcept {
    auto lowerStr = lower_case(std::string(str));
    return std::hash<std::string_view>{}(std::string_view{lowerStr});
}

bool CaseInsensitiveEqual::operator()(std::string_view s1, std::string_view s2) const noexcept {
    return s1.size() == s2.size()
        && std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
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

Option::Option(OnChange&& f) noexcept :
    type(Type::BUTTON),
    onChange(std::move(f)) {}

Option::Option(bool v, OnChange&& f) noexcept :
    type(Type::CHECK),
    onChange(std::move(f)) {
    defaultValue = currentValue = bool_to_string(v);
}

Option::Option(std::string_view v, OnChange&& f) noexcept :
    type(Type::STRING),
    onChange(std::move(f)) {
    defaultValue = currentValue =
      is_whitespace(v) || lower_case(std::string(v)) == EMPTY_STRING ? "" : v;
}

Option::Option(int v, int minV, int maxV, OnChange&& f) noexcept :
    type(Type::SPIN),
    minValue(minV),
    maxValue(maxV),
    onChange(std::move(f)) {
    defaultValue = currentValue = std::to_string(v);
}

Option::Option(std::string_view v, std::string_view var, OnChange&& f) noexcept :
    type(Type::COMBO),
    onChange(std::move(f)) {
    defaultValue = currentValue = v;
    comboValues                 = split(var, "var", true);
}

Option::operator int() const noexcept {
    assert(type == Type::CHECK || type == Type::SPIN);

    return type == Type::CHECK ? string_to_bool(currentValue) : std::stoi(currentValue);
}

Option::operator std::string() const noexcept {
    assert(type == Type::STRING || type == Type::COMBO);

    return currentValue;
}

Option::operator std::string_view() const noexcept {
    assert(type == Type::STRING || type == Type::COMBO);

    return currentValue;
}

// Updates currentValue and triggers onChange() action.
// It's up to the GUI to check for option's limit,
// but could receive the new value from the user, so let's check the bounds anyway.
void Option::operator=(std::string value) noexcept {
    assert(is_ok(type));

    if (type != Type::BUTTON && type != Type::STRING && value.empty())
        return;

    switch (type)
    {
    case Type::CHECK :
        value = lower_case(value);
        if (value != "true" && value != "false")
            return;
        break;
    case Type::STRING :
        if (is_whitespace(value) || lower_case(value) == EMPTY_STRING)
            value.clear();
        break;
    case Type::SPIN :
        value = std::to_string(std::clamp(std::stoi(value), minValue, maxValue));
        break;
    case Type::COMBO :
        value = lower_case(value);
        if (std::find(comboValues.begin(), comboValues.end(), value) == comboValues.end())
            return;
        break;
    default :;
    }

    if (type != Type::BUTTON)
        currentValue = value;

    if (onChange)
    {
        auto optStr = onChange(*this);

        if (optStr && optionsPtr != nullptr && optionsPtr->infoCallback)
            optionsPtr->infoCallback(optStr);
    }
}

std::ostream& operator<<(std::ostream& os, const Option& option) noexcept {
    os << "type " << Option::to_string(option.type);

    if (option.type == OT::BUTTON)
        return os;

    os << " default ";
    if (option.type == OT::STRING && is_whitespace(option.defaultValue))
        os << EMPTY_STRING;
    else
        os << option.defaultValue;

    if (option.type == OT::SPIN)
        os << " min " << option.minValue << " max " << option.maxValue;
    else if (option.type == OT::COMBO)
        os << std::accumulate(option.comboValues.begin(), option.comboValues.end(), std::string{},
                              [](std::string acc, std::string_view s) noexcept -> std::string {
                                  return acc.append(" var ").append(s);
                              });

    return os;
}

void Options::set_info_callback(InfoCallback&& iCallback) noexcept {
    infoCallback = std::move(iCallback);
}

// Add option and assigns idx in the correct insertion order
void Options::add(std::string_view name, const Option& option) noexcept {
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

void Options::set(std::string_view name, std::string_view value) noexcept {
    if (contains(name))
        options.at(name) = std::string(value);
    else
        std::cout << "No such option: '" << name << "'" << std::endl;
}

const Option& Options::operator[](const std::string_view name) const noexcept {
    assert(contains(name));

    return options.at(name);
}

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept {

    std::vector<Options::Pair> opts(options.begin(), options.end());

    std::sort(opts.begin(), opts.end(),
              [](const auto& op1, const auto& op2) noexcept { return op1.second < op2.second; });

    for (const auto& [name, option] : opts)
        os << "\noption name " << name << ' ' << option;

    return os;
}

}  // namespace DON
