#include <algorithm>
#include <iostream>
#include <sstream>

#include "type.h"
#include "thread.h"
#include "uci.h"
#include "helper/prng.h"

using std::string;

bool Tune::updateOnLast;

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

    if (!Tune::updateOnLast || LastOption == &o) {
        Tune::readOptions();
    }
}

static void makeOption(const string &n, int v, const SetRange &r) {

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

template<> void Tune::Entry<int>::initOption() noexcept { makeOption(name, value, range); }

template<> void Tune::Entry<int>::readOption() noexcept {
    if (Options.count(name)) {
        value = int32_t(Options[name]);
    }
}

template<> void Tune::Entry<Value>::initOption() noexcept { makeOption(name, value, range); }

template<> void Tune::Entry<Value>::readOption() noexcept {
    if (Options.count(name)) {
        value = Value(int32_t(Options[name]));
    }
}

template<> void Tune::Entry<Score>::initOption() noexcept {
    makeOption("m" + name, mgValue(value), range);
    makeOption("e" + name, egValue(value), range);
}

template<> void Tune::Entry<Score>::readOption() noexcept {
    if (Options.count("m" + name)) {
        value = makeScore(int32_t(Options["m" + name]), egValue(value));
    }
    if (Options.count("e" + name)) {
        value = makeScore(mgValue(value), int32_t(Options["e" + name]));
    }
}

// Instead of a variable here we have a PostUpdate function: just call it
template<> void Tune::Entry<Tune::PostUpdate>::initOption() noexcept {}
template<> void Tune::Entry<Tune::PostUpdate>::readOption() noexcept { value(); }


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

void Tune::readResults() noexcept {

    /* ...insert your values here... */
}
