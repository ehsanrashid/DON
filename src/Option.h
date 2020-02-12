#pragma once

#include <map>
#include <sstream>

#include "Types.h"

namespace UCI {

    /// Option class implements an option as defined by UCI protocol
    class Option
    {
    private:
        using OnChange = void(*)();

        std::string type
            ,       defaultValue
            ,       currentValue;

        i32         minimumValue
            ,       maximumValue;

        OnChange onChange = nullptr;

    public:

        static size_t InsertOrder;

        size_t index;

        explicit Option(OnChange = nullptr);
        Option(const char*, OnChange = nullptr);
        Option(const bool, OnChange = nullptr);
        Option(const i32, i32, i32, OnChange = nullptr);
        Option(const char*, const char*, OnChange = nullptr);
        Option(const Option&) = delete;

        explicit operator std::string() const;
        explicit operator bool() const;
        explicit operator i32() const;
        bool operator==(const char*) const;

        Option& operator=(const char*);
        Option& operator=(const std::string&);

        void operator<<(const Option&);

        std::string operator()() const;

    };

    extern std::ostream& operator<<(std::ostream&, const Option&);

    /// Options container is actually a std::map of Option
    using StringOptionMap = std::map<std::string, Option, CaseInsensitiveLessComparer>;

    extern std::ostream& operator<<(std::ostream&, const StringOptionMap&);

    extern void initialize();

}

// Global nocase mapping of Options
extern UCI::StringOptionMap Options;

extern u32 optionThreads();
