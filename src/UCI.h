#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _UCI_H_INC_
#define _UCI_H_INC_

#include <map>
#include <memory>
#include <string>

#include "Type.h"
#include "functor.h"

namespace UCI {

    namespace OptionType {

        // Option class implements an option as defined by UCI protocol
        class       Option
        {

        public:
            
            typedef void (*OnChange) (const Option &);

            u08 index;

        protected:
            
            OnChange _on_change;

        public:

            Option (const OnChange on_change = NULL);
            virtual ~Option ();

            virtual std::string operator() ()  const   = 0;

            virtual operator bool ()        const { return bool (); }
            virtual operator i32 ()         const { return i32 (); }
            virtual operator std::string () const { return std::string (); }
            virtual Option& operator= (std::string &value) = 0;

        };

        class ButtonOption : public Option
        {
        public:
            ButtonOption (const OnChange on_change = NULL);

            std::string operator() ()  const;

            Option& operator= (std::string &value);

        };

        class  CheckOption : public Option
        {
        public:
            bool  _default
                , _value;

            CheckOption (const bool val, const OnChange on_change = NULL);

            std::string operator() ()  const;
            virtual operator bool () const;

            Option& operator= (std::string &value);

        };

        class StringOption : public Option
        {
        public:
            std::string _default
                ,       _value;

            StringOption (const char val[], const OnChange on_change = NULL);

            std::string operator() ()  const;
            operator std::string () const;

            Option& operator= (std::string &value);

        };

        class   SpinOption : public Option
        {
        public:
            i32 _default
                ,   _minimum
                ,   _maximum
                ,   _value;

            SpinOption (i32 val, i32 minimum, i32 maximum, const OnChange on_change = NULL);

            std::string operator() ()  const;
            operator i32 () const;

            Option& operator= (std::string &value);

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
            operator<< (std::basic_ostream<charT, Traits> &os, const Option *opt)
        {
            os << opt->operator() ();
            return os;
        }

    }

    typedef std::unique_ptr<OptionType::Option> OptionPtr;

    typedef std::map<std::string, OptionPtr, std::string_less_nocase_comparer> OptionMap;

    extern void   initialize ();
    extern void deinitialize ();

    template<class charT, class Traits>
    inline std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const OptionMap &options)
    {
        for (u08 idx = 0; idx <= options.size (); ++idx)
        {
            for (OptionMap::const_iterator
                itr  = options.begin ();
                itr != options.end (); ++itr)
            {
                const OptionType::Option *option = itr->second.get ();
                if (idx == option->index)
                {
                    os << "option name " << itr->first << " " << option << std::endl;
                    break;
                }
            }
        }
        return os;
    }

    // ---------------------------------------------

    extern void start (const std::string &args = "");

    extern void stop ();
}

extern UCI::OptionMap Options;  // Global string mapping of Options

#endif // _UCI_H_INC_
