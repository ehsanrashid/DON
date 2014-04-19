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
    typedef std::map<std::string, Option, std::no_case_less_comparer> OptionMap;

    // Option class implements an option as defined by UCI protocol
    class Option
    {
    private:
        typedef void (*OnChange) (const Option&);

        template<class charT, class Traits>
        friend std::basic_ostream<charT, Traits>&
            operator<< (std::basic_ostream<charT, Traits> &os, const Option &opt);

        template<class charT, class Traits>
        friend std::basic_ostream<charT, Traits>&
            operator<< (std::basic_ostream<charT, Traits> &os, const OptionMap &optmap);

        u08 _idx;
        std::string _type;

        std::string
              _default
            , _value;

        i32   _minimum
            , _maximum;

        OnChange _on_change;

    public:
        Option (OnChange on_change = NULL);
        Option (const bool  val, OnChange on_change = NULL);
        Option (const char *val, OnChange on_change = NULL);
        Option (const i32   val, i32 minimum, i32 maximum, OnChange on_change = NULL);

        operator bool () const;
        operator i32  () const;
        operator std::string() const;

        Option& operator=  (const std::string &value);

        void    operator<< (const Option &opt);

        std::string operator() ()  const;
    };

    template<class charT, class Traits>
    inline std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const Option &opt)
    {
        os << opt.operator() ();
        return os;
    }

    // operator<<() is used to print all the options default values in chronological
    // insertion order (the idx field) and in the format defined by the UCI protocol.
    template<class charT, class Traits>
    inline std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const OptionMap &optmap)
    {
        for (u08 idx = 0; idx < optmap.size (); ++idx)
        {
            for (OptionMap::const_iterator
                pair  = optmap.begin ();
                pair != optmap.end (); ++pair)
            {
                const Option &option = pair->second;
                if (idx == option._idx)
                {
                    os << "option name " << pair->first << option << std::endl;
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
