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
//#include <cstdlib>
#include <iostream>

namespace DON {

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
      is_whitespace(v) || lower_case(std::string{v}) == EMPTY_STRING ? "" : v;
}

Option::Option(int v, int minV, int maxV, OnChange&& f) noexcept :
    type(Type::SPIN),
    minValue(minV),
    maxValue(maxV),
    onChange(std::move(f)) {
    defaultValue = currentValue = std::to_string(v);
}

Option::Option(std::string_view v, StringViews&& vSvs, OnChange&& f) noexcept :
    type(Type::COMBO),
    defaultValue(v),
    currentValue(v),
    varSvs(std::move(vSvs)),
    onChange(std::move(f)) {}

Option::operator int() const noexcept {
    assert(type == Type::CHECK || type == Type::SPIN);

    return type == Type::CHECK ? sv_to_bool(currentValue) : sv_to_int(currentValue);
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

    if ((type != Type::BUTTON && type != Type::STRING && value.empty())
        || (type == Type::CHECK && !value_is_bool_string(value))
        || (type == Type::SPIN && !value_in_range(value, minValue, maxValue)))
        return;

    if (type == Type::CHECK)
        value = lower_case(value);
    else if (type == Type::STRING && (is_whitespace(value) || lower_case(value) == EMPTY_STRING))
        value.clear();
    else if (type == Type::COMBO)
    {
        value = lower_case(value);
        if (std::find(varSvs.begin(), varSvs.end(), value) == varSvs.end())
            return;
    }

    if (type != Type::BUTTON)
        currentValue = value;

    if (onChange)
    {
        const auto info = onChange(*this);

        if (!info)
            return;

        if (optionsPtr != nullptr && optionsPtr->onInfo)
            optionsPtr->onInfo(info);
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
    {
        std::string varStr;
        varStr.reserve(16 * option.varSvs.size());

        for (const auto var : option.varSvs)
            varStr.append(" var ").append(var);

        os << varStr;
    }

    return os;
}

auto Options::begin() noexcept { return list.begin(); }

auto Options::end() noexcept { return list.end(); }

auto Options::begin() const noexcept { return list.begin(); }

auto Options::end() const noexcept { return list.end(); }

usize Options::size() const noexcept { return list.size(); }

bool Options::empty() const noexcept { return list.empty(); }

bool Options::contains(const Set::const_iterator setItr) const noexcept {
    return setItr != set.end();
}

bool Options::contains(const std::string_view name) const noexcept {
    return contains(set.find(name));
}

usize Options::count(const std::string_view name) const noexcept { return set.count(name); }

auto Options::find(const std::string_view name) noexcept { return indexMap.find(name); }

auto Options::find(const std::string_view name) const noexcept { return indexMap.find(name); }

// Adds an option to the Options.
//
// Options are stored in insertion order through 'list' and indexed by name
// through 'indexMap'. Returns false if an option with the same name already exists.
bool Options::add(const std::string_view name, const Option& option) noexcept {
    if (contains(name))
    {
        std::cerr << "Option: '" << name << "' was already added!" << std::endl;
        //std::exit(EXIT_FAILURE);
        return false;
    }

    // Append the option to the ordered list and obtain its iterator.
    const auto listItr = list.emplace(list.end(), name, option);
    assert(listItr != list.end());

    // Associate the option with its owning Options object.
    listItr->second.optionsPtr = this;

    // Associate the name with its corresponding list node.
    [[maybe_unused]] const auto [indexMapItr, inserted] = indexMap.emplace(name, listItr);
    // The initial membership check guarantees that the name is not already indexed.
    assert(inserted);
    assert(indexMapItr->second == listItr);

    // Establish name membership after the list and index map are updated.
    [[maybe_unused]] const auto [setItr, registered] = set.emplace(name);
    // The initial membership check guarantees that this insertion succeeds.
    assert(registered);
    assert(setItr != set.end());

    return true;
}

// Removes an option from the Options.
//
// Returns false if no option with the specified name exists.
bool Options::remove(const std::string_view name) noexcept {
    const auto setItr = set.find(name);

    // Not registered.
    if (!contains(setItr))
        return false;

    const auto indexMapItr = find(name);
    // Set guarantees that indexMap contains the option.
    assert(indexMapItr != indexMap.end());

    // Retrieve the corresponding list node.
    const auto listItr = indexMapItr->second;
    // Internal consistency checks.
    assert(listItr != list.end());
    assert(lower_case(std::string{listItr->first}) == lower_case(std::string{name}));

    // Remove the membership entry.
    set.erase(setItr);

    // Remove the index entry.
    indexMap.erase(indexMapItr);

    // Remove the option from the ordered list.
    list.erase(listItr);

    return true;
}

void Options::set_value(const std::string_view name, const std::string_view value) noexcept {
    const auto indexMapItr = find(name);

    if (indexMapItr != indexMap.end())
        indexMapItr->second->second = std::string{value};
    else
        std::cerr << "No such option: '" << name << "'" << std::endl;
}

const Option& Options::operator[](const std::string_view name) const noexcept {
    const auto indexMapItr = find(name);
    assert(indexMapItr != indexMap.end());

    return indexMapItr->second->second;
}

void Options::set_on_info(OnInfo&& f) noexcept { onInfo = std::move(f); }

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept {
    for (const auto& [name, option] : options)
        os << "\noption name " << name << ' ' << option;

    return os;
}

}  // namespace DON
