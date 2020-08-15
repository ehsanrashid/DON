#pragma once

#include <map>
#include <string>
#include <type_traits>

#include "Comparer.h"
#include "Type.h"

extern std::string const Name;
extern std::string const Version;
extern std::string const Author;

extern std::string const engineInfo();

extern std::string const compilerInfo();

namespace UCI {

    /// Option class implements an option as defined by UCI protocol
    class Option {

    private:
        using OnChange = void(*)(); // std::add_pointer<void()>;

        std::string type,
                    defaultVal,
                    currentVal;

        double  minVal,
                maxVal;

        OnChange onChange{ nullptr };

    public:

        u32 index;

        Option(OnChange = nullptr);
        Option(bool, OnChange = nullptr);
        Option(char const*, OnChange = nullptr);
        Option(std::string const&, OnChange = nullptr);
        Option(double, double, double, OnChange = nullptr);
        Option(char const*, char const*, OnChange = nullptr);
        Option(std::string const&, std::string const&, OnChange = nullptr);
        Option(Option const&) = delete;
        //Option(Option&&) = delete;

        operator std::string() const;
        operator bool() const;
        operator i16() const;
        operator u16() const;
        operator i32() const;
        operator u32() const;
        operator i64() const;
        operator u64() const;
        operator double() const;

        bool operator==(char const*) const;
        bool operator==(std::string const&) const;

        Option& operator=(char const*);
        Option& operator=(std::string&);

        void operator<<(Option const&);

        std::string defaultValue() const;
        std::string toString() const;
    };

    extern std::ostream& operator<<(std::ostream&, Option const&);

    /// Options container is std::map of string & Option
    using OptionMap = std::map<std::string, Option, CaseInsensitiveLessComparer>;

    extern std::ostream& operator<<(std::ostream&, OptionMap const&);


    extern void initialize();

    extern void handleCommands(std::string const&);

    extern void clear();
}

// Global nocase mapping of Options
extern UCI::OptionMap Options;

extern u16 optionThreads();
