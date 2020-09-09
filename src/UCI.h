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
        Option(std::string_view, OnChange = nullptr);
        Option(double, double, double, OnChange = nullptr);
        Option(std::string_view, std::string_view, OnChange = nullptr);
        Option(Option const&) = delete;
        //Option(Option&&) = delete;

        operator std::string() const;
        operator std::string_view() const;
        operator bool() const;
        operator i16() const;
        operator u16() const;
        operator i32() const;
        operator u32() const;
        operator i64() const;
        operator u64() const;
        operator double() const;

        bool operator==(std::string_view) const;

        Option& operator=(std::string_view);

        void operator<<(Option const&);

        std::string defaultValue() const;
        std::string toString() const;
    };

    extern std::ostream& operator<<(std::ostream&, Option const&);

    /// Options container is std::map of string & Option
    using OptionMap = std::map<std::string, Option, CaseInsensitiveLessComparer>;

    extern std::ostream& operator<<(std::ostream&, OptionMap const&);


    extern void initialize() noexcept;

    extern void handleCommands(int, char const *const*);

    extern void clear() noexcept;
}

// Global nocase mapping of Options
extern UCI::OptionMap Options;

extern u16 optionThreads();
