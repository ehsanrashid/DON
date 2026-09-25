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

#include "tune.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <unordered_map>

#include "option.h"

namespace DON {

namespace {

const Option* LastOption = nullptr;

std::unordered_map<std::string_view, int> TuneResults;

std::optional<std::string> on_tune(const Option& option) noexcept {
    if (!Tune::IsLastUpdate || LastOption == &option)
        Tune::read_options();

    return std::nullopt;
}

}  // namespace

RangeSetter::RangeSetter(const RangeFun f) noexcept :
    rangeFun(f) {}

RangeSetter::RangeSetter(const int min, const int max) noexcept :
    rangeFun(nullptr),
    range(min, max) {}

Range RangeSetter::operator()(const int v) const noexcept {
    return rangeFun != nullptr ? rangeFun(v) : range;
}

Tune& Tune::instance() noexcept {
    static Tune tune;
    return tune;
}

std::string Tune::next(std::string& names, const bool pop) noexcept {
    std::string name;

    do
    {
        auto token = names.substr(0, names.find(','));

        if (pop)
            names.erase(0, token.size() + 1);

        name += rtrim(token);  // Remove trailing whitespace

    } while (std::count(name.begin(), name.end(), '(') - std::count(name.begin(), name.end(), ')')
             != 0);

    return name;
}

void Tune::make_option(Options* const         optionsPtr,
                       const std::string_view name,
                       int                    value,
                       const RangeSetter&     range) noexcept {
    // Do not generate option when there is nothing to tune (i.e. min = max)
    if (range(value).first == range(value).second)
        return;

    if (const auto itr = TuneResults.find(name); itr != TuneResults.end())
        value = itr->second;

    optionsPtr->add(name,
                    OptionFactory::spin(value, range(value).first, range(value).second, on_tune));

    LastOption = &(*optionsPtr)[name];

    // Print formatted parameters, ready to be copy-pasted in Fishtest
    std::cout << name << ','                                               //
              << value << ','                                              //
              << range(value).first << ','                                 //
              << range(value).second << ','                                //
              << (range(value).second - range(value).first) / 20.0 << ','  //
              << "0.0020" << std::endl;
}

void Tune::init(Options& options) noexcept {
    OptionsPtr = &options;

    for (auto& entry : instance().entries)
        entry->init();

    read_options();
}

void Tune::read_options() noexcept {
    for (auto& entry : instance().entries)
        entry->read_option();
}

template<>
void Tune::Entry<int>::init() noexcept {
    make_option(OptionsPtr, name, value, range);
}

template<>
void Tune::Entry<int>::read_option() noexcept {
    value = 0;  // default
    if (OptionsPtr->contains(name))
        value = int((*OptionsPtr)[name]);
}

// Instead of a variable here have a PostUpdate function: just call it
template<>
void Tune::Entry<Tune::PostUpdate>::init() noexcept {}
template<>
void Tune::Entry<Tune::PostUpdate>::read_option() noexcept {
    value();
}

}  // namespace DON


// Init options with tuning session results instead of default values. Useful to
// get correct bench signature after a tuning session or to test tuned values.
// Just copy fishtest tuning results in a result.txt file and extract the
// values with:
//
// cat results.txt | sed 's/^param: \([^,]*\), best: \([^,]*\).*/  TuneResults["\1"] = int(round(\2));/'
//
// Then paste the output below, as the function body


namespace DON {

void Tune::read_results() noexcept { /* ...insert your values here... */ }

}  // namespace DON
