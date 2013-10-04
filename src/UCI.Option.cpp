#include "UCI.h"

#include <iomanip>
#include <sstream>
#include <iostream>
#include "xcstring.h"
#include "xstring.h"
#include "atomicstream.h"

#include "Transposition.h"

//#include "Thread.h"

//#undef min
//#undef max

namespace UCI {

    // Global string mapping of options
    OptionMap Options;

    namespace OptionType {

        Option::Option (const OnChange on_change)
            : _on_change (on_change), index (Options.size ())
        {}
        Option::~Option ()
        {
            if (_on_change) _on_change = NULL;
        }

        ButtonOption::ButtonOption (const OnChange on_change)
            : Option (on_change)
        {}
        std::string ButtonOption::operator() () const
        {
            return std::string ("type button");
        }
        Option& ButtonOption::operator= (char       v[])
        {
            if (_on_change) _on_change (*this);
            return *this;
        }
        Option& ButtonOption::operator= (std::string &v)
        {
            if (_on_change) _on_change (*this);
            return *this;
        }

        CheckOption::CheckOption (const bool b, const OnChange on_change)
            : Option (on_change)
        {
            default = value = b;
        }
        std::string CheckOption::operator() () const
        {
            return std::string ("type check default ") + std::string ((default) ? "true" : "false");
        }
        CheckOption::operator bool () const
        {
            return value;
        }
        Option& CheckOption::operator= (char       v[])
        {
            if (iswhitespace (v)) return *this;
            bool val = iequals (v, "true");
            if (value != val)
            {
                value = val;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }
        Option& CheckOption::operator= (std::string &v)
        {
            if (iswhitespace (v)) return *this;
            bool val = iequals (v, "true");
            if (value != val)
            {
                value = val;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }

        StringOption::StringOption (const char s[], const OnChange on_change)
            : Option (on_change)
        {
            default = value = s;
        }
        std::string StringOption::operator() () const
        {
            return std::string ("type string default ") + std::string (iswhitespace (default) ? "<empty>" : value);
        }
        StringOption::operator std::string () const
        {
            return iswhitespace (value) ? "<empty>" : value;
        }
        Option& StringOption::operator= (char       v[])
        {
            if (value != v)
            {
                value = v;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }
        Option& StringOption::operator= (std::string &v)
        {
            if (value != v)
            {
                value = v;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }

        SpinOption::SpinOption (uint32_t val, uint32_t min_val, uint32_t max_val, const OnChange on_change)
            : Option (on_change)
        {
            default = value = val;
            min = min_val;
            max = max_val;
        }
        std::string SpinOption::operator() () const
        {
            return std::string ("type spin default ") + std::to_string (default) +
                std::string (" min ") + std::to_string (min) +
                std::string (" max ") + std::to_string (max);
        }
        SpinOption::operator int32_t () const
        {
            return value;
        }
        Option& SpinOption::operator= (char       v[])
        {
            if (iswhitespace (v)) return *this;
            uint32_t val = to_int (v);
            val = std::min (std::max (val, min), max);
            //if (min < val && val < max)
            {
                if (value != val)
                {
                    value = val;
                    if (_on_change) _on_change (*this);
                }
            }
            return *this;
        }
        Option& SpinOption::operator= (std::string &v)
        {
            if (iswhitespace (v)) return *this;
            uint32_t val = std::stoi (v);
            val = std::min (std::max (val, min), max);
            //if (min < val && val < max)
            {
                if (value != val)
                {
                    value = val;
                    if (_on_change) _on_change (*this);
                }
            }
            return *this;
        }

        ComboOption::ComboOption (const OnChange on_change)
            : Option (on_change)
        {}
        std::string ComboOption::operator() () const
        {
            return std::string ("type combo");
        }
        Option& ComboOption::operator= (char       v[])
        {
            if (_on_change) _on_change (*this);
            return *this;
        }
        Option& ComboOption::operator= (std::string &v)
        {
            if (_on_change) _on_change (*this);
            return *this;
        }
    }

    using namespace OptionType;

    // option-events
    namespace {

        void on_clear_hash (const Option &opt)
        {
            std::atom () << "hash cleared" << std::endl;
            TT.clear ();
        }

        void on_resize_hash (const Option &opt)
        {
            std::atom () << "info string hash resized " << opt << " MB Hash" << std::endl;
            TT.resize (int32_t (opt));
        }

        void on_change_threads (const Option &opt)
        {
            std::atom () << "thread changed" << std::endl;
        }

        void on_evaluation (const Option& opt)
        {

        }

    }

    void  init_options ()
    {

#pragma region old
        //int16_t cpu   = std::min (cpu_count (), MAX_THREADS);
        // max split depth
        //int16_t max_spl_depth = cpu < 8 ? 4 : 7;

        //Options["Use Debug Log"]               = Option(false, on_logger);
        //Options["Use Search Log"]              = Option(false);
        //Options["Search Log Filename"]         = Option("SearchLog.txt");
        //Options["Book File"]                   = Option("book.bin");
        //Options["Best Book Move"]              = Option(false);
        //Options["Contempt Factor"]             = Option(0, -50,  50);
        //Options["Mobility (Middle Game)"]      = Option(100, 0, 200, on_eval);
        //Options["Mobility (Endgame)"]          = Option(100, 0, 200, on_eval);
        //Options["Passed Pawns (Middle Game)"]  = Option(100, 0, 200, on_eval);
        //Options["Passed Pawns (Endgame)"]      = Option(100, 0, 200, on_eval);
        //Options["Space"]                       = Option(100, 0, 200, on_eval);
        //Options["Min Split Depth"]             = Option(max_spl_depth, 4, 7, on_threads);
        //Options["Max Threads per Split Point"] = Option(5, 4, 8, on_threads);
        //Options["Threads"]                     = Option(cpus, 1, MAX_THREADS, on_threads);
        //Options["Use Sleeping Threads"]        = Option(true);

#pragma endregion

        Options["Clear Hash"]                    = OptionPtr (new ButtonOption (on_clear_hash));
        Options["Hash"]                          = OptionPtr (new SpinOption (TranspositionTable::DEF_SIZE_TT, TranspositionTable::MIN_SIZE_TT, TranspositionTable::MAX_SIZE_TT, on_resize_hash));
        Options["Ponder"]                        = OptionPtr (new CheckOption (true));

        //Options["Min Split Depth"]               = OptionPtr (new SpinOption (max_spl_depth, 4, 7, on_change_threads));
        Options["Min Split Depth"]               = OptionPtr (new SpinOption (0, 0, MAX_SPLIT_DEPTH, on_change_threads));
        Options["Threads"]                       = OptionPtr (new SpinOption (1, 1, MAX_THREADS, on_change_threads));
        Options["Max Threads per Split Point"]   = OptionPtr (new SpinOption (5, 4, MAX_SPLITPOINTS_PER_THREAD, on_change_threads));
        Options["Use Sleeping Threads"]          = OptionPtr (new CheckOption (true));

        Options["Book"]                          = OptionPtr (new CheckOption (false));
        Options["Book File"]                     = OptionPtr (new StringOption ("book.bin"));
        Options["Best Book Move"]                = OptionPtr (new CheckOption (false));

        Options["MultiPV"]                       = OptionPtr (new SpinOption (1, 1, 500));

        Options["UCI_Chess960"]                  = OptionPtr (new CheckOption (false));
        Options["UCI_AnalyseMode"]               = OptionPtr (new CheckOption (false, on_evaluation));


        //Options["Skill Level"]                  = &SpinOption(20, 0, 20);
        //Options["Emergency Move Horizon"]       = &SpinOption(40, 0, 50);

        //Options["Emergency Base Time"]        = &SpinOption(200, 0, 30000);
        //Options["Emergency Move Time"]        = &SpinOption(70, 0, 5000);
        //Options["Minimum Thinking Time"]      = &SpinOption(20, 0, 5000);
        //Options["Slow Mover"]                 = &SpinOption(100, 10, 1000);
        //Options["UCI_Chess960"]               = &CheckOption(false);
        //Options["UCI_AnalyseMode"]            = &CheckOption(false/*, on_eval*/);



        //std::cout << int32_t (*(Options["Hash"]));

        //std::cout << (*Options["hash"])();
        //*Options["Clear Hash"]  = std::string("");
        //*Options["Hash"]        = std::string("128");

        //::std::cout << Options;
    }

    void clear_options ()
    {
        Options.clear ();
    }

}
