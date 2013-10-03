//#pragma once
#ifndef UCI_OPTION_H_
#define UCI_OPTION_H_

#include <map>
#include <memory>
#include <string>
#include "Type.h"
#include "functor.h"

namespace UCI {

    namespace OptionType {

        // Option class implements an option as defined by UCI protocol
        typedef class Option
        {

        public:
            typedef void (*OnChange) (const Option &);

            size_t index;

        protected:
            OnChange _on_change;

        public:

            Option (const OnChange on_change = NULL);
            virtual ~Option ();

            virtual ::std::string operator() ()  const   = NULL;

            virtual operator bool ()        const { return bool (); }
            virtual operator int32_t ()     const { return int32_t (); }
            virtual operator ::std::string () const { return ::std::string (); }

            virtual Option& operator= (char        v[]) = NULL;
            virtual Option& operator= (std::string &v) = NULL;

        } Option;

        typedef class ButtonOption : public Option
        {
        public:
            ButtonOption (const OnChange on_change = NULL);

            ::std::string operator() ()  const;

            Option& operator= (char        v[]);
            Option& operator= (std::string &v);

        } ButtonOption;

        typedef class CheckOption : public Option
        {
        public:
            bool default;
            bool value;

            CheckOption (const bool b, const OnChange on_change = NULL);

            ::std::string operator() ()  const;
            virtual operator bool () const;

            Option& operator= (char        v[]);
            Option& operator= (std::string &v);

        } CheckOption;

        typedef class StringOption : public Option
        {
        public:
            ::std::string default;
            ::std::string value;

            StringOption (const char s[], const OnChange on_change = NULL);

            ::std::string operator() ()  const;
            operator ::std::string () const;

            Option& operator= (char        v[]);
            Option& operator= (std::string &v);

        } StringOption;

        typedef class SpinOption : public Option
        {
        public:
            uint32_t default;
            uint32_t value;
            uint32_t min, max;

            SpinOption (uint32_t val, uint32_t min_val, uint32_t max_val, const OnChange on_change = NULL);

            ::std::string operator() ()  const;
            operator int32_t () const;

            Option& operator= (char        v[]);
            Option& operator= (std::string &v);

        } SpinOption;

        typedef class ComboOption : public Option
        {
        public:
            // value;

            ComboOption (const OnChange on_change = NULL);

            ::std::string operator() ()  const;

            Option& operator= (char       v[]);
            Option& operator= (std::string &v);

        } ComboOption;

    }

    //typedef ::std::shared_ptr<OptionType::Option> OptionPtr;
    typedef ::std::unique_ptr<OptionType::Option> OptionPtr;

    typedef ::std::map<::std::string, OptionPtr, ::std::string_less_comparer> OptionMap;

    extern OptionMap Options;

    extern void  init_options ();
    extern void clear_options ();

    ::std::string to_string (const UCI::OptionMap &options);

}

template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const UCI::OptionMap &options)
{
    os << UCI::to_string (options);
    return os;
}

#endif
