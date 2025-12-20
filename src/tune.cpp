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

#include "tune.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_map>

#include "option.h"

namespace DON {

bool     Tune::IsLastUpdate = false;
Options* Tune::OptionsPtr   = nullptr;

namespace {

const Option* LastOption = nullptr;

std::unordered_map<std::string_view, int> TuneResults;

std::optional<std::string> on_tune(const Option& option) noexcept {
    if (!Tune::IsLastUpdate || LastOption == &option)
        Tune::read_options();

    return std::nullopt;
}

}  // namespace

std::string Tune::next(std::string& names, bool pop) noexcept {
    std::string name;

    do
    {
        auto token = names.substr(0, names.find(','));

        if (pop)
            names.erase(0, 1 + token.size());

        std::istringstream iss{token};
        name += (iss >> token, token);  // Remove trailing whitespace

    } while (std::count(name.begin(), name.end(), '(') - std::count(name.begin(), name.end(), ')'));

    return name;
}

void Tune::make_option(Options*           optionsPtr,
                       std::string_view   name,
                       int                value,
                       const RangeSetter& range) noexcept {
    // Do not generate option when there is nothing to tune (ie. min = max)
    if (range(value).first == range(value).second)
        return;

    if (TuneResults.count(name))
        value = TuneResults[name];

    optionsPtr->add(name, Option(value, range(value).first, range(value).second, on_tune));
    LastOption = &((*optionsPtr)[name]);

    // Print formatted parameters, ready to be copy-pasted in Fishtest
    std::cout << name << ','                                               //
              << value << ','                                              //
              << range(value).first << ','                                 //
              << range(value).second << ','                                //
              << (range(value).second - range(value).first) / 20.0 << ','  //
              << "0.0020" << std::endl;
}

template<>
void Tune::Entry<int>::init_option() noexcept {
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
void Tune::Entry<Tune::PostUpdate>::init_option() noexcept {}
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
