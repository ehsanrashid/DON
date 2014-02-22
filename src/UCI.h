//#pragma once
#ifndef UCI_H_
#define UCI_H_

#include <map>
#include <memory>
#include <string>

#include "Type.h"
#include "functor.h"

namespace UCI {

    namespace OptionType {

        // Option class implements an option as defined by UCI protocol
        typedef class       Option
        {

        public:
            typedef void (*OnChange) (const Option &);

            uint8_t index;

        protected:
            OnChange _on_change;

        public:

            Option (const OnChange on_change = NULL);
            virtual ~Option ();

            virtual std::string operator() ()  const   = 0;

            virtual operator bool ()        const { return bool (); }
            virtual operator int32_t ()     const { return int32_t (); }
            virtual operator std::string () const { return std::string (); }

            //virtual Option& operator= (char        *value) = 0;
            virtual Option& operator= (std::string &value) = 0;

        }       Option;

        typedef class ButtonOption : public Option
        {
        public:
            ButtonOption (const OnChange on_change = NULL);

            std::string operator() ()  const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        } ButtonOption;

        typedef class  CheckOption : public Option
        {
        public:
            bool  _default
                , _value;

            CheckOption (const bool val, const OnChange on_change = NULL);

            std::string operator() ()  const;
            virtual operator bool () const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        }  CheckOption;

        typedef class StringOption : public Option
        {
        public:
            std::string _default
                ,       _value;

            StringOption (const char val[], const OnChange on_change = NULL);

            std::string operator() ()  const;
            operator std::string () const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        } StringOption;

        typedef class   SpinOption : public Option
        {
        public:
            int32_t _default
                ,   _minimum
                ,   _maximum
                ,   _value;

            SpinOption (int32_t val, int32_t minimum, int32_t maximum, const OnChange on_change = NULL);

            std::string operator() ()  const;
            operator int32_t () const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        }   SpinOption;

        //typedef class  ComboOption : public Option
        //{
        //public:
        //    ComboOption (const OnChange on_change = NULL);
        //
        //    std::string operator() ()  const;
        //
        //    //Option& operator= (char        *value);
        //    Option& operator= (std::string &value);
        //
        //}  ComboOption;


        template<class charT, class Traits>
        inline ::std::basic_ostream<charT, Traits>&
            operator<< (::std::basic_ostream<charT, Traits> &os, const Option &opt)
        {
            os << opt.operator() ();
            return os;
        }

        template<class charT, class Traits>
        inline ::std::basic_ostream<charT, Traits>&
            operator<< (::std::basic_ostream<charT, Traits> &os, const Option *opt)
        {
            os << opt->operator() ();
            return os;
        }

    }

    typedef ::std::unique_ptr<OptionType::Option> OptionPtr;

    typedef ::std::map<std::string, OptionPtr, ::std::string_less_nocase_comparer> OptionMap;

    extern void   initialize ();
    extern void deinitialize ();

    template<class charT, class Traits>
    inline ::std::basic_ostream<charT, Traits>&
        operator<< (::std::basic_ostream<charT, Traits> &os, const OptionMap &options)
    {
        for (uint8_t idx = 0; idx < options.size (); ++idx)
        {
            for (OptionMap::const_iterator
                itr = options.begin ();
                itr != options.end (); ++itr)
            {
                const OptionPtr &option_ptr = itr->second;
                if (idx == (option_ptr)->index)
                {
                    const OptionType::Option *option = (option_ptr).get ();
                    os << "option name " << itr->first << " " << option << std::endl;
                    break;
                }
            }
        }
        return os;
    }

    // ---------------------------------------------

    extern void start (const ::std::string &args = "");

    extern void stop ();
}

// Global string mapping of options
extern UCI::OptionMap Options;

#endif // UCI_H_
