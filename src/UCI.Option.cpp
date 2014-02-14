#include "UCI.h"

#include <iomanip>
#include <sstream>
#include <fstream>
#include <iostream>
#include "xstring.h"

#include "Transposition.h"
#include "Evaluator.h"
#include "Searcher.h"
#include "Thread.h"
#include "DebugLogger.h"

// Global string mapping of options
UCI::OptionMap Options;

namespace UCI {

    using namespace ::std;

    namespace OptionType {

        Option::Option (const OnChange on_change)
            : _on_change (on_change)
            , index (Options.size ())
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
        //Option& ButtonOption::operator= (char *value)
        //{
        //    value;
        //    if (_on_change) _on_change (*this);
        //    return *this;
        //}
        Option& ButtonOption::operator= (string &value)
        {
        	(void) value;
            if (_on_change) _on_change (*this);
            return *this;
        }

        CheckOption::CheckOption (const bool value, const OnChange on_change)
            : Option (on_change)
        {
            _default = _value = value;
        }
        string CheckOption::operator() () const
        {
            return string ("type check default ") + string (_default ? "true" : "false");
        }
        CheckOption::operator bool () const
        {
            return _value;
        }
        //Option& CheckOption::operator= (char *value)
        //{
        //    if (whitespace (value)) return *this;
        //    bool value = iequals (value, "true");
        //    if (_value != value)
        //    {
        //        _value = value;
        //        if (_on_change) _on_change (*this);
        //    }
        //    return *this;
        //}
        Option& CheckOption::operator= (string &value)
        {
            if (whitespace (value)) return *this;
            bool bol = iequals (value, "true");
            if (_value != bol)
            {
                _value = bol;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }

        StringOption::StringOption (const char value[], const OnChange on_change)
            : Option (on_change)
        {
            _default = _value = value;
        }
        string StringOption::operator() () const
        {
            return string ("type string default ") + string (whitespace (_default) ? "<empty>" : _value);
        }
        StringOption::operator string () const
        {
            return whitespace (_value) ? "<empty>" : _value;
        }
        //Option& StringOption::operator= (char *value)
        //{
        //    if (_value != value)
        //    {
        //        _value = value;
        //        if (_on_change) _on_change (*this);
        //    }
        //    return *this;
        //}
        Option& StringOption::operator= (string &value)
        {
            if (_value != value)
            {
                _value = value;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }

        SpinOption::SpinOption (int32_t value, int32_t min_val, int32_t max_val, const OnChange on_change)
            : Option (on_change)
        {
            _default = _value = value;
            _min_value = min_val;
            _max_value = max_val;
        }
        string SpinOption::operator() () const
        {
            return string ("type spin default ") + std::to_string (int64_t (_default)) +
                string (" min ") + std::to_string (int64_t (_min_value)) +
                string (" max ") + std::to_string (int64_t (_max_value));
        }
        SpinOption::operator int32_t () const
        {
            return _value;
        }
        //Option& SpinOption::operator= (char *value)
        //{
        //    if (whitespace (value)) return *this;
        //    int32_t value = to_int (value);
        //    value = min (max (value, _min_value), _max_value);
        //    //if (_min_value < value && value < _max_value)
        //    {
        //        if (_value != value)
        //        {
        //            _value = value;
        //            if (_on_change) _on_change (*this);
        //        }
        //    }
        //    return *this;
        //}
        Option& SpinOption::operator= (string &value)
        {
            if (whitespace (value)) return *this;
            int32_t val = stoi (value);
            val = min (max (val, _min_value), _max_value);
            //if (_min_value < val && val < _max_value)
            {
                if (_value != val)
                {
                    _value = val;
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
        //Option& ComboOption::operator= (char *value)
        //{
        //    value;
        //    if (_on_change) _on_change (*this);
        //    return *this;
        //}
        Option& ComboOption::operator= (string &value)
        {
        	(void) value;
            if (_on_change) _on_change (*this);
            return *this;
        }
    }

    // option-events
    namespace {

        using namespace OptionType;

        void on_clear_hash      (const Option &opt)
        {
            (void) opt;
            ClearHash = true;
            sync_cout << "info string hash cleared." << sync_endl;
        }

        void on_resize_hash     (const Option &opt)
        {
            TT.resize (int32_t (opt));
            sync_cout << "info string " << TT.size () << " MB Hash." << sync_endl;
        }

        void on_save_hash       (const Option &opt)
        {
        	(void) opt;
            ofstream ofstm (*(Options["Hash File"]), ios_base::out | ios_base::binary);
            ofstm << TT;
            ofstm.close ();
        }

        void on_load_hash       (const Option &opt)
        {
        	(void) opt;
            ifstream ifstm (*(Options["Hash File"]), ios_base::in | ios_base::binary);
            ifstm >> TT;
            ifstm.close ();
        }

        void on_change_book     (const Option &opt)
        {
        	(void) opt;
            if (Searcher::Book.is_open ()) Searcher::Book.close ();
        }

        void on_change_threads  (const Option &opt)
        {
        	(void) opt;
            Threads.read_uci_options ();
        }

        void on_change_eval     (const Option& opt)
        {
        	(void) opt;
            Evaluator::initialize ();
        }

        void on_change_50_move_distance (const Option &opt)
        {
            Position::fifty_move_distance = 2 * int32_t (opt);
        }

        void on_log_debug       (const Option &opt)
        {
            log_debug (bool (opt));
        }

        void on_query           (const Option &opt)
        {
        	(void) opt;
        }

    }

    void   initialize ()
    {

        // Hash Memory Options
        // -------------------

        // Amount of hash table memory used by engine, in MB.
        // Default 128, min 4, max 1024 (32-bit) or 4096 (64-bit Standard) or 262144 (64-bit Pro).
        //
        // The value is rounded down to a power of 2 (4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144 MB).
        //
        // For infinite analysis or long time control matches you should use the largest hash that fits in the physical memory of your system.
        // For example, on a system with 4 GB of memory you can use up to 2048 MB hash size.
        // For shorter games, for example 3 to 5 minute games, it'val better to use 256 MB or 512 MB as this will provide the best performance.
        // For 16 min games 1024 or 2048 MB hash size should be fine.
        //
        // In the FAQ about Hash Size you'll find a formula to compute the optimal hash size for your hardware and time control.
        Options["Hash"]                         = OptionPtr (new SpinOption (
            TranspositionTable::DEF_SIZE_TT,
            TranspositionTable::SIZE_MIN_TT,
            TranspositionTable::SIZE_MAX_TT,
            on_resize_hash));

        // Button to clear the Hash Memory.
        // If the Never Clear Hash option is enabled, this button doesn't do anything.
        Options["Clear Hash"]                   = OptionPtr (new ButtonOption (on_clear_hash));

        // This option prevents the Hash Memory from being cleared between successive games or positions belonging to different games.
        // Check this option also if you want to Load the Hash from disk file,
        // otherwise your loaded Hash could be cleared by a subsequent ucinewgame or Clear Hash command.
        Options["Never Clear Hash"]             = OptionPtr (new CheckOption (false));

        // Persistent Hash Options
        // -----------------------

        // Persistent Hash means that the in-memory hash table can be saved to a disk file and reloaded later for continuing the analysis.
        // The goal is to be able to interrupt a long position analysis anytime and save the hash table to a disk file.
        // At a later date you can reload the hash file in memory and continue the analysis at the point where it was interrupted.
        // 
        // Correspondence chess players could profit from this feature by keeping one hash disk file per ongoing chess game.
        // For that purpose the following procedure can be used.
        // 
        // To save a Hash file to disk:
        // .	End the analysis
        // .	Go to the options window, enter the name of the Hash File (e.g. C:\Chess\Game.dat)
        // .	Press the Save Hash to File button, and OK in the options window.
        // 
        // To load a Hash file from disk:
        // .	Load the correspondence game
        // .	Go to the options window, enter the name of the Hash File (e.g. C:\Chess\Game.dat)
        // .	Press the Load Hash from File button, and OK in the options window.
        // -----------------------------------------------------------------------------------------

        // File name for saving or loading the hash file with the Save Hash to File or Load Hash from File buttons.
        // A full file name is required, for example C:\Chess\Hash000.dat.
        // By default Houdini will use the hash.dat file in the "My Documents" folder of the current user.
        Options["Hash File"]                    = OptionPtr (new StringOption ("hash.dat"));

        // Save the current hash table to a disk file specified by the Hash File option.
        // Use the Save Hash File button after ending the analysis of the position.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the file will be identical to the size of the hash memory, so this operation could take a while.
        // This feature can be used to interrupt and restart a deep analysis at any time.
        Options["Save Hash"]                    = OptionPtr (new ButtonOption (on_save_hash));

        // Load a previously saved hash file from disk.
        // Use the Load Hash File button after loading the game or position, but before starting the analysis.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the hash memory will automatically be set to the size of the saved file.
        // Please make sure to check the Never Clear Hash option,
        // as otherwise your loaded Hash could be cleared by a subsequent ucinewgame or Clear Hash command.
        Options["Load Hash"]                    = OptionPtr (new ButtonOption (on_load_hash));

        // Position Learning Options
        // -------------------------

        // Openings Book Options
        // ---------------------
        Options["Own Book"]                     = OptionPtr (new CheckOption (false));
        Options["Book File"]                    = OptionPtr (new StringOption ("book.bin", on_change_book));
        Options["Best Book Move"]               = OptionPtr (new CheckOption (false));

        // End Game Table Bases Options
        // ----------------------------
        // 


        // Cores and Threads Options
        // -------------------------

        // Maximum number of threads (cores) used by the analysis.
        // Default is hardware-dependent, min 1, max 6 (Standard) or 32 (Pro).
        //
        // Houdini will automatically limit the number of threads to the number of logical processors of your hardware. If your computer supports hyper-threading it is recommended not using more threads than physical cores, as the extra hyper-threads would usually degrade the performance of the engine. 
        Options["Threads"]                      = OptionPtr (new SpinOption ( 1, 1, MAX_THREADS, on_change_threads));

        // When using multiple threads, the Split Depth parameter defines the minimum depth at which work will be split between cores.
        // Default 0, min 8, max 99.
        //
        // Default 0 means auto setting which depends on the threads
        // This parameter can impact the speed of the engine (nodes per second) and can be fine-tuned to get the best performance out of your hardware. The default value 10 is tuned for Intel quad-core i5/i7 systems, but on other systems it may be advantageous to increase this to 12 or 14.
        Options["Split Depth"]                  = OptionPtr (new SpinOption ( 0, 8, MAX_SPLIT_DEPTH, on_change_threads));

        Options["Threads per Split Point"]      = OptionPtr (new SpinOption ( 5, 4, MAX_THREADS_SPLIT_POINT, on_change_threads));

        Options["Idle Threads Sleep"]           = OptionPtr (new CheckOption (true));

        // Game Play Options
        // -----------------

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

        // TODO::
        //// Limit the multi-PV analysis to moves within a range of the best move.
        //// Default 0, min 0, max 999.
        ////
        //// Values are in centipawn. Because of contempt and evaluation corrections in different stages of the game, this value is only approximate.
        //// A value of 0 means that this parameter will not be taken into account.
        //Options["MultiPV_cp"]                   = OptionPtr (new SpinOption (0, 0, 999));

        // TODO::
        //// Level of contempt to avoid draws in game play.
        //// Default 1, min 0 (none), max 2 (aggressive).
        ////
        //// The notion of "contempt" implies that engine will try to avoid draws by evaluating its own position slightly too optimistically.
        //// The Contempt level can be chosen between 0 (none) and 2 (aggressive), the default value of 1 should be a good compromise in most situations.
        ////
        //// .	0 = No Contempt
        //// The evaluations are accurate and identical for both sides. This is recommended for position analysis in which you analyze alternatively for White and Black.
        //// The starting position evaluates as approx. +0.15.
        ////
        //// .	1 = Default Contempt
        //// Contempt 1 is primarily based on piece value imbalance, engine will value its own pieces higher than the opponent pieces, so will only exchange them if there's a clear positional advantage in doing so.
        //// This also means that the score is evaluated optimistically for the side to move (at most 0.15 pawn). For example, the starting position evaluates as approx. +0.30 when analyzing for White and +0.00 when viewed from Black.
        //// This is only recommended for position analysis if you always analyze for the same side.
        ////
        //// .	2 = Aggressive
        //// Contempt 2 adds some king safety imbalance, leading to a more attacking style.
        //// It would draw less, It will also lose more, especially if your opponent is strong.
        ////
        //// The contempt settings are fairly mild and have little impact on the objective strength of the engine.
        //// It's hard to say which will give the best results against a given opponent,
        //// it may depend on the style and strength of the opponent.
        //// One could envisage more pronounced contempt but this would start to degrade the engine's objective strength.
        //// By default the contempt is only activated during game play, not during infinite analysis.
        //// If you enable the Analysis Contempt checkbox, engine will also take into account the contempt for infinite analysis.
        //Options["Contempt"]                     = OptionPtr (new SpinOption (1,   0,  2));

        // Factor for adjusted contempt. Changes playing style.
        Options["Contempt Factor"]              = OptionPtr (new SpinOption (0, -50, 50));

        // Analyse Mode
        Options["UCI_AnalyseMode"]              = OptionPtr (new CheckOption (false, on_change_eval));

        // TODO::
        //// Activate Contempt for position analysis.
        //// Default false.
        ////
        //// It is usually not recommended to activate the contempt for analyzing positions.
        //// When contempt is active, the score of the analysis will be optimistic (over-evaluated) for the side that is to move.
        //// That means that if you use Analysis Contempt the evaluations will change depending on whether White or Black has the move.
        //// For example, from the start position, when you do an analysis with Analysis Contempt (and Contempt value 1)
        //// you could find a best move e2-e4 scoring about +0.3 for White.
        //// If you then play e2-e4 and analyze for Black you could find a score close to +0.0.
        //// If you do the same without Analysis Contempt, you should find a consistent +0.15 score whether it's White or Black to move.
        //Options["Analysis Contempt"]            = OptionPtr (new CheckOption (false));

        Options["Mobility (Midgame)"]           = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));
        Options["Mobility (Endgame)"]           = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));

        Options["Pawn Structure (Midgame)"]     = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));
        Options["Pawn Structure (Endgame)"]     = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));

        Options["Passed Pawns (Midgame)"]       = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));
        Options["Passed Pawns (Endgame)"]       = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));

        Options["Aggressive"]                   = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));
        Options["Cowardice"]                    = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));
        Options["Space"]                        = OptionPtr (new SpinOption (100, 0, 200, on_change_eval));

        // TODO::
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
        Options["Fifty Move Distance"]          = OptionPtr (new SpinOption (50,  5, 50, on_change_50_move_distance));

        // TODO::
        // Maximum search depth for mate search.
        // Default 0, min 0, max 99.
        //
        // If set, this option will usually speed-up a mate search.
        // If you know that a position is "mate in X", you can use X or a value slightly larger than X in the Mate Search option.
        // This will prevent Houdini from going too deep in variations that don't lead to mate in the required number of moves.
        Options["Mate Search"]                  = OptionPtr (new SpinOption ( 0,  0, 99));

        Options["Skill Level"]                  = OptionPtr (new SpinOption (20,  0, 20));

        Options["Emergency Move Horizon"]       = OptionPtr (new SpinOption (40,  0, 50));
        Options["Emergency Base Time"]          = OptionPtr (new SpinOption (60,  0, 30000));
        Options["Emergency Move Time"]          = OptionPtr (new SpinOption (30,  0, 5000));
        Options["Minimum Thinking Time"]        = OptionPtr (new SpinOption (20,  0, 5000));
        Options["Slow Mover"]                   = OptionPtr (new SpinOption (80, 10, 1000));

        // Activate Fischer Random Chess a.k.a. Chess960 games.
        // Default false.
        //
        // The Chess960 feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["UCI_Chess960"]                 = OptionPtr (new CheckOption (false));


        // TODO::
        //// Activate the strength limit specified in the UCI_Elo parameter.
        //// Default false.
        ////
        //// This feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        //Options["UCI_LimitStrength"]            = OptionPtr (new CheckOption (false));

        //// UCI-protocol compliant version of Strength parameter.
        //// Default 3000, min 1200, max 3000.
        ////
        //// Internally the UCI_ELO value will be converted to a Strength value according to the table given above.
        //// The UCI_ELO feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        //Options["UCI_ELO"]                      = OptionPtr (new SpinOption (3000, 1200, 3000));


        ///Debug Options
        Options["Write Debug Log"]              = OptionPtr (new CheckOption (false, on_log_debug));
        Options["Write Search Log"]             = OptionPtr (new CheckOption (false));
        Options["Search Log File"]              = OptionPtr (new StringOption ("search_log.txt"));

        // TODO::
        Options["UCI_Query"]                    = OptionPtr (new ButtonOption (on_query));

    }

    void deinitialize ()
    {
        Options.clear ();
    }

}
