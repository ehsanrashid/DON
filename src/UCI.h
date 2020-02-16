#pragma once

#include <map>
#include <string>

#include "Comparer.h"
#include "Type.h"

extern const std::string Name;
extern const std::string Version;
extern const std::string Author;

extern const std::string engineInfo();

extern const std::string compilerInfo();

namespace UCI
{
    /// Option class implements an option as defined by UCI protocol
    class Option
    {
    private:
        using OnChange = void(*)();

        std::string type
            , defaultVal{}
            , currentVal{};

        double minVal{}
            ,  maxVal{};

        OnChange onChange{nullptr};

    public:

        u32 index;

        Option(OnChange = nullptr);
        Option(bool, OnChange = nullptr);
        Option(const char*, OnChange = nullptr);
        Option(double, double, double, OnChange = nullptr);
        Option(const char*, const char*, OnChange = nullptr);
        Option(const Option&) = delete;

        operator std::string() const;
        operator bool() const;
        operator i16() const;
        operator u16() const;
        operator i32() const;
        operator u32() const;
        operator i64() const;
        operator u64() const;
        operator double() const;
        bool operator==(const char*) const;

        Option& operator=(std::string&);

        void operator<<(const Option&);

        std::string toString() const;
    };

    extern std::ostream& operator<<(std::ostream&, const Option&);

    /// Options container is actually a std::map of Option
    using StringOptionMap = std::map<std::string, Option, CaseInsensitiveLessComparer>;

    extern std::ostream& operator<<(std::ostream&, const StringOptionMap&);


    extern void initialize();

    extern void handleCommands(u32, const char *const*);

    extern void reset();
}

// Global nocase mapping of Options
extern UCI::StringOptionMap Options;

extern u32 optionThreads();
