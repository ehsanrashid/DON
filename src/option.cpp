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
#include <iostream>

namespace DON {

Option::Option(std::string_view str, OnChange&& onCng) noexcept :
    defaultValue(str),
    currentValue(str),
    onChange(std::move(onCng)) {}

std::string_view Option::current_value() const noexcept { return currentValue; }

std::string_view Option::default_value() const noexcept { return defaultValue; }

Option::operator int() const noexcept {
    assert(false);
    return 0;
}

Option::operator std::string_view() const noexcept {
    assert(false);
    return {};
}

void Option::on_change() noexcept {
    if (!onChange)
        return;

    auto changeInfo = onChange(*this);

    if (!changeInfo)
        return;

    if (optionsPtr == nullptr)
        return;

    optionsPtr->on_info(std::move(changeInfo));
}

std::ostream& operator<<(std::ostream& os, const Option& option) noexcept {
    os << "type " << option.type();

    option.print(os);

    return os;
}

namespace {

class ButtonOption final: public Option {
   public:
    explicit ButtonOption(OnChange&& onCng = {}) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    void operator=(std::string_view value) noexcept override;
};

ButtonOption::ButtonOption(OnChange&& onCng) noexcept :
    Option{"", std::move(onCng)} {}

std::string_view ButtonOption::type() const noexcept { return "button"; }

void ButtonOption::print(std::ostream&) const noexcept {}

void ButtonOption::operator=(const std::string_view) noexcept { on_change(); }

class CheckOption final: public Option {
   public:
    explicit CheckOption(bool value, OnChange&& onCng = {}) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator int() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    static std::string normalize(std::string str) noexcept;
};

CheckOption::CheckOption(const bool b, OnChange&& onCng) noexcept :
    Option{bool_to_str(b), std::move(onCng)} {}

std::string_view CheckOption::type() const noexcept { return "check"; }

void CheckOption::print(std::ostream& os) const noexcept {  //
    os << " default " << default_value();
}

CheckOption::operator int() const noexcept { return str_to_bool(current_value()); }

void CheckOption::operator=(const std::string_view value) noexcept {
    if (!str_is_bool(value))
        return;

    currentValue = normalize(std::string{value});

    on_change();
}

std::string CheckOption::normalize(std::string str) noexcept { return lower_case(std::move(str)); }

class StringOption final: public Option {
   public:
    explicit StringOption(std::string_view str, OnChange&& onCng = {}) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator std::string_view() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    static std::string normalize(std::string str) noexcept;
};

StringOption::StringOption(const std::string_view str, OnChange&& onCng) noexcept :
    Option{normalize(std::string{str}), std::move(onCng)} {}

std::string_view StringOption::type() const noexcept { return "string"; }

void StringOption::print(std::ostream& os) const noexcept {
    os << " default " << (is_whitespace(default_value()) ? EMPTY_STRING : default_value());
}

StringOption::operator std::string_view() const noexcept { return current_value(); }

void StringOption::operator=(const std::string_view value) noexcept {
    currentValue = normalize(std::string{value});

    on_change();
}

std::string StringOption::normalize(std::string str) noexcept {
    if (is_whitespace(str) || lower_case(str) == EMPTY_STRING)
        str.clear();

    return str;
}

class SpinOption final: public Option {
   public:
    SpinOption(int v, int minV, int maxV, OnChange&& onCng = {}) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator int() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    const int minValue;
    const int maxValue;
};

SpinOption::SpinOption(const int v, const int minV, const int maxV, OnChange&& onCng) noexcept :
    Option{std::to_string(v), std::move(onCng)},
    minValue(minV),
    maxValue(maxV) {}

std::string_view SpinOption::type() const noexcept { return "spin"; }

void SpinOption::print(std::ostream& os) const noexcept {
    os << " default " << default_value() << " min " << minValue << " max " << maxValue;
}

SpinOption::operator int() const noexcept { return str_to_int(current_value()); }

void SpinOption::operator=(const std::string_view value) noexcept {
    if (!str_in_range(value, minValue, maxValue))
        return;

    currentValue = value;

    on_change();
}

class ComboOption final: public Option {
   public:
    ComboOption(std::string_view str, StringViews&& vrs, OnChange&& onCng = {}) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator std::string_view() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    bool contains(std::string_view value) const noexcept;

    const StringViews vars;
};

ComboOption::ComboOption(const std::string_view str, StringViews&& vrs, OnChange&& onCng) noexcept :
    Option{str, std::move(onCng)},
    vars(std::move(vrs)) {
    assert(contains(default_value()));
}

std::string_view ComboOption::type() const noexcept { return "combo"; }

void ComboOption::print(std::ostream& os) const noexcept {
    os << " default " << default_value();

    for (const auto& var : vars)
        os << " var " << var;
}

ComboOption::operator std::string_view() const noexcept { return current_value(); }

void ComboOption::operator=(const std::string_view value) noexcept {
    if (is_whitespace(value) || !contains(value))
        return;

    currentValue = value;

    on_change();
}

bool ComboOption::contains(const std::string_view value) const noexcept {
    return std::find_if(vars.begin(), vars.end(),
                        [&](const auto& var) { return CaseInsensitiveEqual{}(var, value); })
        != vars.end();
}

}  // namespace

Option::Ptr Option::button(OnChange onCng) noexcept {
    return std::make_unique<ButtonOption>(std::move(onCng));
}

Option::Ptr Option::check(const bool b, OnChange onCng) noexcept {
    return std::make_unique<CheckOption>(b, std::move(onCng));
}

Option::Ptr Option::string(const std::string_view str, OnChange onCng) noexcept {
    return std::make_unique<StringOption>(str, std::move(onCng));
}

Option::Ptr Option::spin(const int v, const int minV, const int maxV, OnChange onCng) noexcept {
    return std::make_unique<SpinOption>(v, minV, maxV, std::move(onCng));
}

Option::Ptr Option::combo(const std::string_view str, StringViews vars, OnChange onCng) noexcept {
    return std::make_unique<ComboOption>(str, std::move(vars), std::move(onCng));
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

usize Options::count(const std::string_view name) const noexcept { return indexMap.count(name); }

auto Options::find(const std::string_view name) noexcept { return indexMap.find(name); }

auto Options::find(const std::string_view name) const noexcept { return indexMap.find(name); }

bool Options::add(const std::string_view name, Option::Ptr option) noexcept {
    // Already a member.
    if (contains(name))
    {
        std::cerr << "Option: '" << name << "' was already added!" << std::endl;
        return false;
    }

    assert(option != nullptr);
    if (option == nullptr)
        return false;

    // Append the option in insertion order and obtain its iterator.
    const auto listItr = list.emplace(list.end(), name, std::move(option));
    assert(listItr != list.end());

    // Associate the option with its owning Options object.
    listItr->second->optionsPtr = this;

    const std::string_view nameView = listItr->first;

    // Associate the name with its corresponding list node.
    [[maybe_unused]] const auto [indexMapItr, inserted] = indexMap.emplace(nameView, listItr);
    // The initial membership check guarantees that the name is not already indexed.
    assert(inserted);
    assert(indexMapItr->second == listItr);

    // Establish name membership after the ordered list and name index are updated.
    [[maybe_unused]] const auto [setItr, registered] = set.emplace(nameView);
    // The initial membership check guarantees that this insertion succeeds.
    assert(registered);
    assert(setItr != set.end());

    return true;
}

bool Options::remove(const std::string_view name) noexcept {
    const auto setItr = set.find(name);

    // Not a member.
    if (!contains(setItr))
        return false;

    const auto indexMapItr = find(name);
    // Set membership guarantees that the name is indexed.
    assert(indexMapItr != indexMap.end());

    // Retrieve the corresponding list node.
    const auto listItr = indexMapItr->second;
    // Verify the list node and its name.
    assert(listItr != list.end());
    assert(CaseInsensitiveEqual{}(listItr->first, name));

    // Remove the membership entry.
    set.erase(setItr);

    // Remove the index entry.
    indexMap.erase(indexMapItr);

    // Remove the option from the ordered list.
    list.erase(listItr);

    return true;
}

void Options::setoption(const std::string_view name, const std::string_view value) noexcept {
    const auto indexMapItr = find(name);

    if (indexMapItr != indexMap.end())
        *indexMapItr->second->second = value;
    else
        std::cerr << "No such option: '" << name << "'" << std::endl;
}

const Option& Options::operator[](const std::string_view name) const noexcept {
    const auto indexMapItr = find(name);
    assert(indexMapItr != indexMap.end());

    return *indexMapItr->second->second;
}

void Options::set_on_info(OnInfo&& onInf) noexcept { onInfo = std::move(onInf); }

void Options::on_info(ChangeInfo changeInfo) const noexcept {
    if (!onInfo)
        return;

    onInfo(changeInfo);
}

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept {
    for (const auto& [name, option] : options)
        os << "\noption name " << name << ' ' << *option;

    return os;
}

}  // namespace DON
