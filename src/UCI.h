//#pragma once
#ifndef UCI_H_
#define UCI_H_

#include <map>
#include <memory>
#include <string>
#include "Type.h"
#include "functor.h"

namespace UCI {

    using ::std::string;

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

            virtual string operator() ()  const   = NULL;

            virtual operator bool ()        const { return bool (); }
            virtual operator int32_t ()     const { return int32_t (); }
            virtual operator string () const { return string (); }

            virtual Option& operator= (char   *v) = NULL;
            virtual Option& operator= (string &v) = NULL;

        } Option;

        typedef class ButtonOption : public Option
        {
        public:
            ButtonOption (const OnChange on_change = NULL);

            string operator() ()  const;

            Option& operator= (char   *v);
            Option& operator= (string &v);

        } ButtonOption;

        typedef class CheckOption : public Option
        {
        public:
            bool default;
            bool value;

            CheckOption (const bool val, const OnChange on_change = NULL);

            string operator() ()  const;
            virtual operator bool () const;

            Option& operator= (char   *v);
            Option& operator= (string &v);

        } CheckOption;

        typedef class StringOption : public Option
        {
        public:
            string default;
            string value;

            StringOption (const char val[], const OnChange on_change = NULL);

            string operator() ()  const;
            operator string () const;

            Option& operator= (char   *v);
            Option& operator= (string &v);

        } StringOption;

        typedef class SpinOption : public Option
        {
        public:
            int32_t default;
            int32_t value;
            int32_t min, max;

            SpinOption (int32_t val, int32_t min_val, int32_t max_val, const OnChange on_change = NULL);

            string operator() ()  const;
            operator int32_t () const;

            Option& operator= (char   *v);
            Option& operator= (string &v);

        } SpinOption;

        typedef class ComboOption : public Option
        {
        public:
            // value;

            ComboOption (const OnChange on_change = NULL);

            string operator() ()  const;

            Option& operator= (char   *v);
            Option& operator= (string &v);

        } ComboOption;


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

    //typedef ::std::shared_ptr<OptionType::Option> OptionPtr;
    typedef ::std::unique_ptr<OptionType::Option> OptionPtr;

    typedef ::std::map<string, OptionPtr, ::std::string_less_nocase_comparer> OptionMap;

    extern void  init_options ();
    extern void clear_options ();

    template<class charT, class Traits>
    inline ::std::basic_ostream<charT, Traits>&
        operator<< (::std::basic_ostream<charT, Traits> &os, const OptionMap &options)
    {

        for (size_t idx = 0; idx < options.size (); ++idx)
        {
            for (OptionMap::const_iterator itr = options.cbegin (); itr != options.cend (); ++itr)
            {
                if (idx == itr->second->index)
                {
                    const OptionType::Option *opt = itr->second.get ();
                    os << "option name " << itr->first << " " << opt << ::std::endl;
                    break;
                }
            }
        }
        return os;
    }

    // ---------------------------------------------

    extern void start (const string &args = "");
    extern void stop ();

}

// Global string mapping of options
extern UCI::OptionMap Options;

#endif // UCI_H_
