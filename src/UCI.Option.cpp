#include "UCI.h"

#include <iomanip>
#include <sstream>
#include <fstream>
#include <iostream>
#include "xcstring.h"
#include "xstring.h"
#include "atomicstream.h"

#include "Searcher.h"
#include "Transposition.h"
#include "Searcher.h"

#include "iologger.h"

//#include "Thread.h"

// Global string mapping of options
UCI::OptionMap Options;

namespace UCI {

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
        string ButtonOption::operator() () const
        {
            return string ("type button");
        }
        Option& ButtonOption::operator= (char   *v)
        {
            if (_on_change) _on_change (*this);
            return *this;
        }
        Option& ButtonOption::operator= (string &v)
        {
            if (_on_change) _on_change (*this);
            return *this;
        }

        CheckOption::CheckOption (const bool val, const OnChange on_change)
            : Option (on_change)
        {
            default = value = val;
        }
        string CheckOption::operator() () const
        {
            return string ("type check default ") + string ((default) ? "true" : "false");
        }
        CheckOption::operator bool () const
        {
            return value;
        }
        Option& CheckOption::operator= (char   *v)
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
        Option& CheckOption::operator= (string &v)
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

        StringOption::StringOption (const char val[], const OnChange on_change)
            : Option (on_change)
        {
            default = value = val;
        }
        string StringOption::operator() () const
        {
            return string ("type string default ") + string (iswhitespace (default) ? "<empty>" : value);
        }
        StringOption::operator string () const
        {
            return iswhitespace (value) ? "<empty>" : value;
        }
        Option& StringOption::operator= (char   *v)
        {
            if (value != v)
            {
                value = v;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }
        Option& StringOption::operator= (string &v)
        {
            if (value != v)
            {
                value = v;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }

        SpinOption::SpinOption (int32_t val, int32_t min_val, int32_t max_val, const OnChange on_change)
            : Option (on_change)
        {
            default = value = val;
            min = min_val;
            max = max_val;
        }
        string SpinOption::operator() () const
        {
            return string ("type spin default ") + std::to_string (default) +
                string (" min ") + std::to_string (min) +
                string (" max ") + std::to_string (max);
        }
        SpinOption::operator int32_t () const
        {
            return value;
        }
        Option& SpinOption::operator= (char   *v)
        {
            if (iswhitespace (v)) return *this;
            int32_t val = to_int (v);
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
        Option& SpinOption::operator= (string &v)
        {
            if (iswhitespace (v)) return *this;
            int32_t val = std::stoi (v);
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
        string ComboOption::operator() () const
        {
            return string ("type combo");
        }
        Option& ComboOption::operator= (char   *v)
        {
            if (_on_change) _on_change (*this);
            return *this;
        }
        Option& ComboOption::operator= (string &v)
        {
            if (_on_change) _on_change (*this);
            return *this;
        }
    }

    // option-events
    namespace {

        using namespace OptionType;

        void on_clear_hash (const Option &opt)
        {
            TT.clear ();
            std::atom () << "info string hash cleared." << std::endl;
        }

        void on_resize_hash (const Option &opt)
        {
            uint32_t size_mb = int32_t (opt);
            TT.resize (size_mb);
            std::atom () << "info string hash resized " << size_mb << " MB Hash..." << std::endl;
        }

        void on_save_hash (const Option &opt)
        {
            //ofstream ofstm ("hash.dat", ::std::ios_base::out | ::std::ios_base::binary);
            //ofstm << tt;
            //ofstm.close ();
        }

        void on_load_hash (const Option &opt)
        {
            //ifstream ifstm ("hash.dat", ::std::ios_base::in | ::std::ios_base::binary);
            //ifstm >> tt;
            //ifstm.close ();
        }

        void on_change_book (const Option &opt)
        {
            if (book.is_open ()) book.close ();
        }

        void on_change_threads (const Option &opt)
        {
            std::atom () << "thread changed" << std::endl;
        }

        void on_evaluation (const Option& opt)
        {

        }


        void on_log_io (const Option &opt)
        {
            log_io (bool (opt));
        }

        void on_query (const Option &opt)
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

#pragma region Hash Memory Options

        // Amount of hash table memory used by engine, in MB.
        // Default 128, min 4, max 1024 (32-bit) or 4096 (64-bit Standard) or 262144 (64-bit Pro).
        //
        // The value is rounded down to a power of 2 (4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144 MB).
        //
        // For infinite analysis or long time control matches you should use the largest hash that fits in the physical memory of your system.
        // For example, on a system with 4 GB of memory you can use up to 2048 MB hash size.
        // For shorter games, for example 3 to 5 minute games, it’val better to use 256 MB or 512 MB as this will provide the best performance.
        // For 16 min games 1024 or 2048 MB hash size should be fine.
        //
        // In the FAQ about Hash Size you'll find a formula to compute the optimal hash size for your hardware and time control.
        Options["Hash"]                         = OptionPtr (new SpinOption (
            TranspositionTable::DEF_SIZE_TT,
            TranspositionTable::MIN_SIZE_TT,
            TranspositionTable::MAX_SIZE_TT,
            on_resize_hash));

        // Button to clear the Hash Memory.
        // If the Never Clear Hash option is enabled, this button doesn't do anything.
        Options["Clear Hash"]                   = OptionPtr (new ButtonOption (on_clear_hash));

        // This option prevents the Hash Memory from being cleared between successive games or positions belonging to different games.
        // Check this option also if you want to Load the Hash from disk file,
        // otherwise your loaded Hash could be cleared by a subsequent ucinewgame or Clear Hash command.
        Options["Never Clear Hash"]             = OptionPtr (new CheckOption (false));

#pragma endregion

#pragma region Persistent Hash Options
        // Persistent Hash means that the in-memory hash table can be saved to a disk file and reloaded later for continuing the analysis.
        // The goal is to be able to interrupt a long position analysis anytime and save the hash table to a disk file.
        // At a later date you can reload the hash file in memory and continue the analysis at the point where it was interrupted.
        // 
        // Correspondence chess players could profit from this feature by keeping one hash disk file per ongoing chess game.
        // For that purpose the following procedure can be used.
        // 
        // To save a Hash file to disk:
        // •	End the analysis
        // •	Go to the options window, enter the name of the Hash File (e.g. C:\Chess\Game1.dat)
        // •	Press the Save Hash to File button, and OK in the options window.
        // 
        // To load a Hash file from disk:
        // •	Load the correspondence game
        // •	Go to the options window, enter the name of the Hash File (e.g. C:\Chess\Game1.dat)
        // •	Press the Load Hash from File button, and OK in the options window.
        // -----------------------------------------------------------------------------------------

        // File name for saving or loading the hash file with the Save Hash to File or Load Hash from File buttons.
        // A full file name is required, for example C:\Chess\Hash001.dat.
        // By default Houdini will use the hash.dat file in the "My Documents" folder of the current user.
        Options["Hash File"]                    = OptionPtr (new StringOption ("hash.dat"));

        // Save the current hash table to a disk file specified by the Hash File option.
        // Use the Save Hash File button after ending the analysis of the position.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the file will be identical to the size of the hash memory, so this operation could take a while.
        // This feature can be used to interrupt and restart a deep analysis at any time.
        Options["Save Hash File"]               = OptionPtr (new ButtonOption (on_save_hash));

        // Load a previously saved hash file from disk.
        // Use the Load Hash File button after loading the game or position, but before starting the analysis.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the hash memory will automatically be set to the size of the saved file.
        // Please make sure to check the Never Clear Hash option,
        // as otherwise your loaded Hash could be cleared by a subsequent ucinewgame or Clear Hash command.
        Options["Load Hash File"]               = OptionPtr (new ButtonOption (on_load_hash));

#pragma endregion

#pragma region Position Learning Options



#pragma endregion

#pragma region Book Options

        Options["Use Book"]                     = OptionPtr (new CheckOption (false));
        Options["Book File"]                    = OptionPtr (new StringOption ("book.bin", on_change_book));
        Options["Best Book Move"]               = OptionPtr (new CheckOption (false));

#pragma endregion

#pragma region End Game Table Bases Options


#pragma endregion

#pragma region Cores and Threads Options

        // Maximum number of threads (cores) used by the analysis.
        // Default is hardware-dependent, min 1, max 6 (Standard) or 32 (Pro).
        //
        // Houdini will automatically limit the number of threads to the number of logical processors of your hardware. If your computer supports hyper-threading it is recommended not using more threads than physical cores, as the extra hyper-threads would usually degrade the performance of the engine. 
        Options["Threads"]                      = OptionPtr (new SpinOption (1, 1, MAX_THREADS, on_change_threads));

        // When using multiple threads, the Split Depth parameter defines the minimum depth at which work will be split between cores.
        // Default 10, min 8, max 99.
        // 
        // This parameter can impact the speed of the engine (nodes per second) and can be fine-tuned to get the best performance out of your hardware. The default value 10 is tuned for Intel quad-core i5/i7 systems, but on other systems it may be advantageous to increase this to 12 or 14.
        Options["Split Depth"]                  = OptionPtr (new SpinOption (10, 8, MAX_SPLIT_DEPTH, on_change_threads));

        Options["Threads per Split Point"]      = OptionPtr (new SpinOption (5, 4, MAX_SPLITPOINTS_PER_THREAD, on_change_threads));
        Options["Use Sleeping Threads"]         = OptionPtr (new CheckOption (true));


#pragma endregion

#pragma region Game Play Options

        // Have the engine think during its opponent's time.
        // Default true.
        //
        // The Ponder feature (sometimes called "Permanent Brain") is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["Ponder"]                       = OptionPtr (new CheckOption (true));

        // Number of principal variations shown.
        // Default 1, min 1, max 32.
        //
        // The MultiPV feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["MultiPV"]                      = OptionPtr (new SpinOption (1, 1, 32));

        // Limit the multi-PV analysis to moves within a range of the best move.
        // Default 0, min 0, max 999.
        //
        // Values are in centipawn. Because of contempt and evaluation corrections in different stages of the game, this value is only approximate.
        // A value of 0 means that this parameter will not be taken into account.
        Options["MultiPV_cp"]                   = OptionPtr (new SpinOption (0, 0, 999));

        // Level of contempt to avoid draws in game play.
        // Default 1, min 0 (none), max 2 (aggressive).
        //
        // The notion of "contempt" implies that engine will try to avoid draws by evaluating its own position slightly too optimistically.
        // The Contempt level can be chosen between 0 (none) and 2 (aggressive), the default value of 1 should be a good compromise in most situations.
        //
        // •	0 = No Contempt
        // The evaluations are accurate and identical for both sides. This is recommended for position analysis in which you analyze alternatively for White and Black.
        // The starting position evaluates as approx. +0.15.
        //
        // •	1 = Default Contempt
        // Contempt 1 is primarily based on piece value imbalance, engine will value its own pieces higher than the opponent pieces, so will only exchange them if there’s a clear positional advantage in doing so.
        // This also means that the score is evaluated optimistically for the side to move (at most 0.15 pawn). For example, the starting position evaluates as approx. +0.30 when analyzing for White and +0.00 when viewed from Black.
        // This is only recommended for position analysis if you always analyze for the same side.
        //
        // •	2 = Aggressive
        // Contempt 2 adds some king safety imbalance, leading to a more attacking style.
        //
        // The contempt settings are fairly mild and have little impact on the objective strength of the engine.
        // It’s hard to say which will give the best results against a given opponent,
        // it may depend on the style and strength of the opponent.
        // One could envisage more pronounced contempt but this would start to degrade the engine’s objective strength.
        // By default the contempt is only activated during game play, not during infinite analysis.
        // If you enable the Analysis Contempt checkbox, engine will also take into account the contempt for infinite analysis.
        Options["Contempt"]                     = OptionPtr (new SpinOption (1, 0, 2));

        Options["Contempt Factor"]              = OptionPtr (new SpinOption (0, -50, 50));

        // Activate Contempt for position analysis.
        // Default false.
        //
        // It is usually not recommended to activate the contempt for analyzing positions.
        // When contempt is active, the score of the analysis will be optimistic (over-evaluated) for the side that is to move.
        // That means that if you use Analysis Contempt the evaluations will change depending on whether White or Black has the move.
        // For example, from the start position, when you do an analysis with Analysis Contempt (and Contempt value 1)
        // you could find a best move e2-e4 scoring about +0.3 for White.
        // If you then play e2-e4 and analyze for Black you could find a score close to +0.0.
        // If you do the same without Analysis Contempt, you should find a consistent +0.15 score whether it’s White or Black to move.
        Options["Analysis Contempt"]            = OptionPtr (new CheckOption (false));

        // The number of moves after which the 50-move rule will kick in.
        // Default 50, min 5, max 50.
        //
        // This setting defines the number of moves after which the 50-move rule will kick in - the default value is 50, i.e. the official 50-moves rule.
        // Setting this option in the range of 10 to 15 moves can be useful to analyse more correctly blockade or fortress positions:
        // - Closed positions in which no progress can be made without some sort of sacrifice (blockade);
        // - End games with a material advantage that is insufficient for winning (fortress).
        //
        // By setting FiftyMoveDistance to 15, you're telling the engine that if it cannot make any progress in the next 15 moves, the game is a draw.
        // It's a reasonably generic way to decide whether a material advantage can be converted or not.
        Options["Fifty Move Distance"]          = OptionPtr (new SpinOption (50, 5, 50));

        // Maximum search depth for mate search.
        // Default 0, min 0, max 99.
        //
        // If set, this option will usually speed-up a mate search.
        // If you know that a position is "mate in X", you can use X or a value slightly larger than X in the Mate Search option.
        // This will prevent Houdini from going too deep in variations that don't lead to mate in the required number of moves.
        Options["Mate Search"]                  = OptionPtr (new SpinOption (0, 0, 99));

        // Activate Fischer Random Chess a.k.a. Chess960 games.
        // Default false.
        //
        // The Chess960 feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["UCI_Chess960"]                 = OptionPtr (new CheckOption (false));

        Options["UCI_AnalyseMode"]              = OptionPtr (new CheckOption (false, on_evaluation));

        // Activate the strength limit specified in the UCI_Elo parameter.
        // Default false.
        //
        // This feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["UCI_LimitStrength"]            = OptionPtr (new CheckOption (false));

        // UCI-protocol compliant version of Strength parameter.
        // Default 3000, min 1200, max 3000.
        //
        // Internally the UCI_ELO value will be converted to a Strength value according to the table given above.
        // The UCI_ELO feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["UCI_ELO"]                      = OptionPtr (new SpinOption (3000, 1200, 3000));

        //Options["Skill Level"]                  = &SpinOption(20, 0, 20);
        //Options["Emergency Move Horizon"]       = &SpinOption(40, 0, 50);
        //Options["Emergency Base Time"]        = &SpinOption(200, 0, 30000);
        //Options["Emergency Move Time"]        = &SpinOption(70, 0, 5000);
        //Options["Minimum Thinking Time"]      = &SpinOption(20, 0, 5000);
        //Options["Slow Mover"]                 = &SpinOption(100, 10, 1000);

#pragma endregion

#pragma region Debug Options

        Options["Write Debug Log"]              = OptionPtr (new CheckOption (false, on_log_io));
        Options["Write Search Log"]             = OptionPtr (new CheckOption (false));
        Options["Search Log File"]              = OptionPtr (new StringOption ("log_search.txt"));

#pragma endregion

        Options["UCI_Query"]                    = OptionPtr (new ButtonOption (on_query));

        //std::cout << int32_t (*(Options["Hash"]));

        //std::cout << (*Options["hash"])();
        //*Options["Clear Hash"]  = string("");
        //*Options["Hash"]        = string("128");

        //::std::cout << Options;
    }

    void clear_options ()
    {
        Options.clear ();
    }

}
