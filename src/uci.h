#pragma once

#include <map>
#include <string>
#include <string_view>
#include <type_traits>

#include "type.h"
#include "helper/comparer.h"

extern std::string const Name;
extern std::string const Version;
extern std::string const Author;

extern std::string const engineInfo();

extern std::string const compilerInfo();

namespace UCI {

    /// Option class implements an option as defined by UCI protocol
    class Option {

    public:

        using OnChange = void(*)(Option const&);

        Option(OnChange = nullptr) noexcept;
        Option(bool, OnChange = nullptr) noexcept;
        Option(std::string_view, OnChange = nullptr) noexcept;
        Option(double, double, double, OnChange = nullptr) noexcept;
        Option(std::string_view, std::string_view, OnChange = nullptr) noexcept;
        //Option(Option const&) = delete;
        //Option(Option&&) = delete;

        //operator std::string() const;
        operator std::string_view() const noexcept;
        operator     bool() const noexcept;
        operator  int16_t() const noexcept;
        operator uint16_t() const noexcept;
        operator  int32_t() const noexcept;
        operator uint32_t() const noexcept;
        operator  int64_t() const noexcept;
        operator uint64_t() const noexcept;
        operator   double() const noexcept;

        bool operator==(std::string_view) const;

        Option& operator=(std::string_view);

        void operator<<(Option const&) noexcept;

        const std::string& defaultValue() const noexcept;
        std::string toString() const noexcept;

        uint32_t    index{ 0 };

    private:

        std::string type,
                    defaultVal,
                    currentVal;
        double  minVal{ 0.0 },
                maxVal{ 0.0 };

        OnChange onChange;
    };


    /// Options container is std::map of string & Option
    using OptionMap = std::map<std::string, Option, CaseInsensitiveLessComparer>;

    extern void initialize() noexcept;

    extern void handleCommands(int, char const *const[]);

    extern void clear() noexcept;
}

extern std::ostream &operator<<(std::ostream &, UCI::Option const &);
extern std::ostream &operator<<(std::ostream &, UCI::OptionMap const &);

// Global nocase mapping of Options
extern UCI::OptionMap Options;

extern uint16_t optionThreads();
