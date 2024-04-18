/*
  DON, a UCI chess playing engine derived from Glaurung 2.1

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
#include <map>
#include <sstream>

#include "ucioption.h"

namespace DON {

const Option* LastOption = nullptr;

bool        Tune::UpdateOnLast;
OptionsMap* Tune::Options;

namespace {

std::map<std::string, int> TuneResults;

void on_tune(const Option& o) noexcept {
    if (!Tune::UpdateOnLast || LastOption == &o)
        Tune::read_options();
}

void make_option(OptionsMap* options, const std::string& n, int v, const SetRange& r) noexcept {
    // Do not generate option when there is nothing to tune (ie. min = max)
    if (r(v).first == r(v).second)
        return;

    if (TuneResults.count(n))
        v = TuneResults[n];

    (*options)[n] << Option(v, r(v).first, r(v).second, on_tune);
    LastOption = &((*options)[n]);

    // Print formatted parameters, ready to be copy-pasted in Fishtest
    std::cout << n << "," << v << "," << r(v).first << "," << r(v).second << ","
              << (r(v).second - r(v).first) / 20.0 << ","
              << "0.0020" << '\n';
}

}  // namespace

std::string Tune::next(std::string& names, bool pop) noexcept {
    std::string name;

    do
    {
        std::string token = names.substr(0, names.find(','));

        if (pop)
            names.erase(0, token.size() + 1);

        std::istringstream iss(token);
        name += (iss >> token, token);  // Remove trailing whitespace

    } while (std::count(name.begin(), name.end(), '(') - std::count(name.begin(), name.end(), ')'));

    return name;
}

template<>
void Tune::Entry<int>::init_option() noexcept {
    make_option(Options, name, value, range);
}

template<>
void Tune::Entry<int>::read_option() noexcept {
    if (Options->count(name))
        value = int((*Options)[name]);
}

// Instead of a variable here we have a PostUpdate function: just call it
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

void Tune::read_results() noexcept { /* ...insert your values here... */
}

}  // namespace DON
