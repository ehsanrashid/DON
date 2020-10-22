#include <algorithm>
#include <iostream>
#include <sstream>

#include "type.h"
#include "thread.h"
#include "uci.h"
#include "helper/prng.h"

using std::string;

bool Tune::update_on_last;
const UCI::Option *LastOption = nullptr;
BoolConditions Conditions;
static std::map<std::string, int> TuneResults;

string Tune::next(string &names, bool pop) {

    string name;

    do {
        string token = names.substr(0, names.find(','));

        if (pop) {
            names.erase(0, token.size() + 1);
        }
        std::stringstream ws(token);
        name += (ws >> token, token); // Remove trailing whitespace

    } while (std::count(name.begin(), name.end(), '(')
        - std::count(name.begin(), name.end(), ')'));

    return name;
}

static void on_tune(UCI::Option const &o) {

    if (!Tune::update_on_last || LastOption == &o) {
        Tune::read_options();
    }
}

static void make_option(const string &n, int v, const SetRange &r) {

    // Do not generate option when there is nothing to tune (ie. min = max)
    if (r(v).first == r(v).second) {
        return;
    }

    if (TuneResults.count(n)) {
        v = TuneResults[n];
    }

    Options[n] << UCI::Option(v, r(v).first, r(v).second, on_tune);
    LastOption = &Options[n];

    // Print formatted parameters, ready to be copy-pasted in Fishtest
    std::cout
        << n << ","
        << v << ","
        << r(v).first << "," << r(v).second << ","
        << (r(v).second - r(v).first) / 20.0 << ","
        << "0.0020"
        << std::endl;
}

template<> void Tune::Entry<int>::init_option() noexcept { make_option(name, value, range); }

template<> void Tune::Entry<int>::read_option() noexcept {
    if (Options.count(name)) {
        value = int32_t(Options[name]);
    }
}

template<> void Tune::Entry<Value>::init_option() noexcept { make_option(name, value, range); }

template<> void Tune::Entry<Value>::read_option() noexcept {
    if (Options.count(name)) {
        value = Value(int32_t(Options[name]));
    }
}

template<> void Tune::Entry<Score>::init_option() noexcept {
    make_option("m" + name, mgValue(value), range);
    make_option("e" + name, egValue(value), range);
}

template<> void Tune::Entry<Score>::read_option() noexcept {
    if (Options.count("m" + name)) {
        value = makeScore(int32_t(Options["m" + name]), egValue(value));
    }
    if (Options.count("e" + name)) {
        value = makeScore(mgValue(value), int32_t(Options["e" + name]));
    }
}

// Instead of a variable here we have a PostUpdate function: just call it
template<> void Tune::Entry<Tune::PostUpdate>::init_option() noexcept {}
template<> void Tune::Entry<Tune::PostUpdate>::read_option() noexcept { value(); }


// Set binary conditions according to a probability that depends
// on the corresponding parameter value.

void BoolConditions::set() noexcept {

    static PRNG rng(now());
    static bool startup = true; // To workaround fishtest bench

    for (size_t i = 0; i < binary.size(); ++i) {
        binary[i] = !startup && (values[i] + int(rng.rand<unsigned>() % variance) > threshold);
    }
    startup = false;

    for (size_t i = 0; i < binary.size(); ++i) {
        sync_cout << binary[i] << sync_endl;
    }
}


// Init options with tuning session results instead of default values. Useful to
// get correct bench signature after a tuning session or to test tuned values.
// Just copy fishtest tuning results in a result.txt file and extract the
// values with:
//
// cat results.txt | sed 's/^param: \([^,]*\), best: \([^,]*\).*/  TuneResults["\1"] = int(round(\2));/'
//
// Then paste the output below, as the function body

#include <cmath>

void Tune::read_results() noexcept {

    /* ...insert your values here... */
}
