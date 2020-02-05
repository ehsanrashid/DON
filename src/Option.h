#pragma once

#include <map>
#include <sstream>

#include "Type.h"
#include "Util.h"

namespace UCI {

    /// Option class implements an option as defined by UCI protocol
    class Option
    {
    private:
        typedef void (*OnChange)();

        std::string type
            ,       defaultValue
            ,       currentValue;

        i32         minimum
            ,       maximum;

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

        void    operator<<(const Option&);

        std::string operator()()  const;

    };

    /// Options container is actually a std::map of Option
    typedef std::map<std::string, Option, CaseInsensitiveLessComparer> StringOptionMap;

    extern void initialize();

    template<typename Elem, typename Traits>
    inline std::basic_ostream<Elem, Traits>&
        operator<<(std::basic_ostream<Elem, Traits> &os, const Option &opt)
    {
        os << opt.operator()();
        return os;
    }

    /// operator<<() is used to print all the options default values in chronological
    /// insertion order and in the format defined by the UCI protocol.
    template<typename Elem, typename Traits>
    inline std::basic_ostream<Elem, Traits>&
        operator<<(std::basic_ostream<Elem, Traits> &os, const StringOptionMap &str_opt_map)
    {
        for (size_t idx = 0; idx < str_opt_map.size(); ++idx)
        {
            for (auto &str_opt_pair : str_opt_map)
            {
                if (str_opt_pair.second.index == idx)
                {
                    os  << "option name "
                        << str_opt_pair.first
                        << str_opt_pair.second
                        << std::endl;
                }
            }
        }
        return os;
    }
}

// Global nocase mapping of Options
extern UCI::StringOptionMap Options;

extern u32 optionThreads();
