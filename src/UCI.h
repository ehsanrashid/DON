#ifndef _UCI_H_INC_
#define _UCI_H_INC_

#include <map>
#include <iostream>

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
        typedef void (*OnChange) ();

        u08 _index = 0;
        std::string
              _type  = ""
            , _value = "";
        i32   _minimum = 0
            , _maximum = 0;

        OnChange _on_change = nullptr;

    public:
        //Option () = delete;
        explicit Option (OnChange on_change = nullptr);
        Option (const bool  val, OnChange on_change = nullptr);
        Option (const char *val, OnChange on_change = nullptr);
        Option (const std::string &val, OnChange on_change = nullptr);
        Option (const i32 val, i32 minimum, i32 maximum, OnChange on_change = nullptr);

        u08 index () const { return _index; }

        explicit operator bool () const;
        explicit operator i32  () const;
        explicit operator std::string () const;

        Option& operator= (const char        *value);
        Option& operator= (const std::string &value);

        void    operator<< (const Option &opt);

        std::string operator() ()  const;

    };

    extern void initialize ();

    extern void loop (const std::string &arg = "");

    extern void deinitialize ();

    // ---------------------------------------------

    template<class CharT, class Traits>
    inline std::basic_ostream<CharT, Traits>&
        operator<< (std::basic_ostream<CharT, Traits> &os, const Option &opt)
    {
        os << opt.operator() ();
        return os;
    }

    // operator<<() is used to print all the options default values in chronological
    // insertion order (the idx field) and in the format defined by the UCI protocol.
    template<class CharT, class Traits>
    inline std::basic_ostream<CharT, Traits>&
        operator<< (std::basic_ostream<CharT, Traits> &os, const OptionMap &optmap)
    {
        for (u08 idx = 0; idx < optmap.size (); ++idx)
        {
            for (auto &pair : optmap)
            {
                const Option &option = pair.second;
                if (idx == option.index ())
                {
                    os << "option name " << pair.first << option << std::endl;
                    break;
                }
            }
        }
        return os;
    }

}

extern UCI::OptionMap Options;  // Global string mapping of Options

#endif // _UCI_H_INC_
