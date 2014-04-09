#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _UCI_H_INC_
#define _UCI_H_INC_

#include <map>
#include <string>

#include "Type.h"
#include "functor.h"

namespace UCI {

    class Option;

    // Our options container is actually a std::map
    typedef std::map<std::string, Option, std::string_less_nocase_comparer> OptionMap;

    // Option class implements an option as defined by UCI protocol
    class Option
    {
    private:
        typedef void (*OnChange) (const Option&);

        template<class charT, class Traits>
        friend std::basic_ostream<charT, Traits>&
            operator<< (std::basic_ostream<charT, Traits> &os, const OptionMap &options);

        size_t index;
        std::string _type;

        std::string
              _default
            , _value;

        i32   _minimum
            , _maximum;

        OnChange _on_change;

    public:
        Option (OnChange = NULL);
        Option (bool val, OnChange = NULL);
        Option (const char *val, OnChange = NULL);
        Option (i32 val, i32 minimum, i32 maximum, OnChange = NULL);

        operator bool () const;
        operator i32  () const;
        operator std::string() const;

        Option& operator=  (const std::string &value);
        std::string operator() ()  const;

        void    operator<< (const Option &opt);

    };

    template<class charT, class Traits>
    inline std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const Option &opt)
    {
        os << opt.operator() ();
        return os;
    }

    template<class charT, class Traits>
    inline std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const OptionMap &options)
    {
        for (size_t idx = 0; idx < options.size (); ++idx)
        {
            for (OptionMap::const_iterator
                itr  = options.begin ();
                itr != options.end (); ++itr)
            {
                const Option &option = itr->second;
                if (idx == option.index)
                {
                    os << "option name " << itr->first << option << std::endl;
                    break;
                }
            }
        }
        return os;
    }

    extern void   initialize ();
    extern void deinitialize ();

    // ---------------------------------------------

    extern void start (const std::string &args = "");

    extern void stop ();
}

extern UCI::OptionMap Options;  // Global string mapping of Options

#endif // _UCI_H_INC_
