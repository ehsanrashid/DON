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

            size_t index;

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
            bool _default;
            bool _value;

            CheckOption (const bool val, const OnChange on_change = NULL);

            std::string operator() ()  const;
            virtual operator bool () const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        }  CheckOption;

        typedef class StringOption : public Option
        {
        public:
            std::string _default;
            std::string _value;

            StringOption (const char val[], const OnChange on_change = NULL);

            std::string operator() ()  const;
            operator std::string () const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        } StringOption;

        typedef class   SpinOption : public Option
        {
        public:
            int32_t _default;
            int32_t _value;
            int32_t _min_value
                ,   _max_value;

            SpinOption (int32_t val, int32_t min_val, int32_t max_val, const OnChange on_change = NULL);

            std::string operator() ()  const;
            operator int32_t () const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        }   SpinOption;

        typedef class  ComboOption : public Option
        {
        public:
            // _value;

            ComboOption (const OnChange on_change = NULL);

            std::string operator() ()  const;

            //Option& operator= (char        *value);
            Option& operator= (std::string &value);

        }  ComboOption;


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

    //class OptionType::Option;

    //typedef std::shared_ptr<OptionType::Option> OptionPtr;
    //typedef std::auto_ptr<OptionType::Option> OptionPtr;
    typedef std::unique_ptr<OptionType::Option> OptionPtr;
    //#define OptionPtr std::unique_ptr<OptionType::Option>

    typedef std::map<std::string, OptionPtr, std::string_less_nocase_comparer> OptionMap;

    extern void  initialize ();
    extern void deinitialize ();

    template<class charT, class Traits>
    inline std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const OptionMap &options)
    {
        for (size_t idx = 0; idx < options.size (); ++idx)
        {
            for (OptionMap::const_iterator
                itr = options.begin ();
                itr != options.end (); ++itr)
            {
                if (idx == itr->second->index)
                {
                    const OptionType::Option *opt = itr->second.get ();
                    os << "option name " << itr->first << " " << opt << std::endl;
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

// Global string mapping of options
extern UCI::OptionMap Options;

#endif // UCI_H_
