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

        using OnChange = void(*)(); // std::add_pointer<void()>;

        Option(OnChange = nullptr);
        Option(bool, OnChange = nullptr);
        Option(std::string_view, OnChange = nullptr);
        Option(double, double, double, OnChange = nullptr);
        Option(std::string_view, std::string_view, OnChange = nullptr);
        //Option(Option const&) = delete;
        //Option(Option&&) = delete;

        //operator std::string() const;
        operator std::string_view() const noexcept;
        operator bool() const noexcept;
        operator i16() const noexcept;
        operator u16() const noexcept;
        operator i32() const noexcept;
        operator u32() const noexcept;
        operator i64() const noexcept;
        operator u64() const noexcept;
        operator double() const noexcept;

        bool operator==(std::string_view) const;

        Option& operator=(std::string_view);

        void operator<<(Option const&);

        std::string defaultValue() const noexcept;
        std::string toString() const;

        u32     index;

    private:

        std::string type,
                    defaultVal,
                    currentVal;
        double  minVal,
                maxVal;

        OnChange onChange;
    };

    extern std::ostream& operator<<(std::ostream&, Option const&);

    /// Options container is std::map of string & Option
    using OptionMap = std::map<std::string, Option, CaseInsensitiveLessComparer>;

    extern std::ostream& operator<<(std::ostream&, OptionMap const&);


    extern void initialize() noexcept;

    extern void handleCommands(int, char const *const[]);

    extern void clear() noexcept;
}

// Global nocase mapping of Options
extern UCI::OptionMap Options;

extern u16 optionThreads();