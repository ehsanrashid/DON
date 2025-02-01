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

#ifndef TUNE_H_INCLUDED
#define TUNE_H_INCLUDED

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace DON {

class Options;

using Range    = std::pair<int, int>;  // Option's min-max values
using RangeFun = Range(int);

struct RangeSetter final {
   public:
    explicit RangeSetter(RangeFun f) noexcept :
        rangeFun(f) {}
    RangeSetter(int min, int max) noexcept :
        rangeFun(nullptr),
        range(min, max) {}

    Range operator()(int v) const noexcept { return rangeFun != nullptr ? rangeFun(v) : range; }

    RangeFun* rangeFun;
    Range     range;
};

// Default Range function, to calculate Option's min-max values
inline Range default_range(int v) noexcept { return v > 0 ? Range(0, 2 * v) : Range(2 * v, 0); }

#define SetDefaultRange RangeSetter(default_range)


// Tune class implements the 'magic' code that makes the setup of a fishtest tuning
// session as easy as it can be. Mainly you have just to remove const qualifiers
// from the variables you want to tune and flag them for tuning, so if you have:
//
//   const Value myValue[][2] = { { V(100), V(20) }, { V(7), V(78) } };
//
// If you have a my_post_update() function to run after values have been updated,
// and a my_range() function to set custom Option's min-max values, then you just
// remove the 'const' qualifiers and write somewhere below in the file:
//
//   TUNE(RangeSetter(my_range), myValue, my_post_update);
//
// You can also set the range directly, and restore the default at the end
//
//   TUNE(RangeSetter(-100, 100), myValue, SetDefaultRange);
//
// In case update function is slow and you have many parameters, you can add:
//
//   ON_LAST_UPDATE();
//
// And the values update, including post update function call, will be done only
// once, after the engine receives the last UCI option, that is the one defined
// and created as the last one, so the GUI should send the options in the same
// order in which have been defined.

class Tune final {
   private:
    using PostUpdate = void();  // Post-update function

    Tune() noexcept { read_results(); }
    Tune(const Tune&) noexcept            = delete;
    Tune(Tune&&) noexcept                 = delete;
    Tune& operator=(const Tune&) noexcept = delete;
    Tune& operator=(Tune&&) noexcept      = delete;

    void read_results() noexcept;

    // Singleton
    static Tune& instance() noexcept {
        static Tune tune;
        return tune;
    }

    // Use polymorphism to accommodate Entry of different types in the same vector
    struct EntryBase {
       public:
        virtual ~EntryBase() = default;

        virtual void init_option() noexcept = 0;
        virtual void read_option() noexcept = 0;
    };

    template<typename T>
    struct Entry final: public EntryBase {
       public:
        static_assert(!std::is_const_v<T>, "Parameter cannot be const!");
        static_assert(std::is_same_v<T, int> || std::is_same_v<T, PostUpdate>,
                      "Parameter type not supported!");

        Entry(const std::string& n, T& v, const RangeSetter& r) noexcept :
            name(n),
            value(v),
            range(r) {}

        // Because 'value' is a reference
        Entry(const Entry&) noexcept            = delete;
        Entry(Entry&&) noexcept                 = delete;
        Entry& operator=(const Entry&) noexcept = delete;
        Entry& operator=(Entry&&) noexcept      = delete;

        void init_option() noexcept override;
        void read_option() noexcept override;

        std::string name;
        T&          value;
        RangeSetter range;
    };

    // Our facility to fill the container, each Entry corresponds to a parameter
    // to tune. Use variadic templates to deal with an unspecified number of
    // entries, each one of a possible different type.
    static std::string next(std::string& names, bool pop = true) noexcept;

    static void make_option(Options*           optionsPtr,
                            const std::string& name,
                            int                value,
                            const RangeSetter& range) noexcept;

    int add(const RangeSetter&, std::string&&) noexcept { return 0; }

    template<typename T, typename... Args>
    int add(const RangeSetter& range, std::string&& names, T& value, Args&&... args) noexcept {
        entries.push_back(std::make_unique<Entry<T>>(next(names), value, range));
        return add(range, std::move(names), args...);
    }

    // Template specialization for arrays: recursively handle multi-dimensional arrays
    template<typename T, std::size_t N, typename... Args>
    int add(const RangeSetter& range, std::string&& names, T (&value)[N], Args&&... args) noexcept {
        for (std::size_t i = 0; i < N; ++i)
            add(range, next(names, i == N - 1) + "[" + std::to_string(i) + "]", value[i]);
        return add(range, std::move(names), args...);
    }

    // Template specialization for RangeSetter
    template<typename... Args>
    int add(const RangeSetter&, std::string&& names, RangeSetter& value, Args&&... args) noexcept {
        return add(value, (next(names), std::move(names)), args...);
    }

    std::vector<std::unique_ptr<EntryBase>> entries;

   public:
    template<typename... Args>
    static int add(const std::string& names, Args&&... args) noexcept {
        // Remove trailing parenthesis
        return instance().add(SetDefaultRange, names.substr(1, names.size() - 2), args...);
    }

    // Deferred, due to UCI::engine_options() access
    static void init(Options& options) noexcept {
        OptionsPtr = &options;

        for (auto& entry : instance().entries)
            entry->init_option();

        read_options();
    }

    static void read_options() noexcept {
        for (auto& entry : instance().entries)
            entry->read_option();
    }

    static bool     OnLastUpdate;
    static Options* OptionsPtr;
};

// Some macro magic :-) define a dummy int variable that the compiler initializes calling Tune::add()
#if !defined(STRINGIFY)
    #define STRING_LITERAL(x) #x
    #define STRINGIFY(x) STRING_LITERAL(x)
#endif
#define UNIQUE2(x, y) x##y
#define UNIQUE(x, y) UNIQUE2(x, y)  // Two indirection levels to expand __LINE__
#define TUNE(...) int UNIQUE(p, __LINE__) = Tune::add(STRINGIFY((__VA_ARGS__)), __VA_ARGS__)

#define ON_LAST_UPDATE() bool UNIQUE(p, __LINE__) = Tune::OnLastUpdate = true

}  // namespace DON

#endif  // #ifndef TUNE_H_INCLUDED
