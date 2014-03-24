#include "UCI.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

#include "Transposition.h"
#include "Evaluator.h"
#include "Searcher.h"
#include "Thread.h"
#include "Debugger.h"
#include "TB_Syzygy.h"

UCI::OptionMap  Options; // Global string mapping of Options

namespace UCI {

    using namespace std;
    using namespace Threads;
    using namespace Searcher;

    namespace OptionType {

        Option:: Option (const OnChange on_change)
            : index (Options.size ())
            , _on_change (on_change)
        {}

        Option::~Option ()
        {
            if (_on_change)
            {
                _on_change = NULL;
            }
        }

        ButtonOption::ButtonOption (const OnChange on_change)
            : Option (on_change)
        {}
        string ButtonOption::operator() () const
        {
            ostringstream oss;
            oss << "type button";
            return oss.str ();
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
            ostringstream oss;
            oss << "type check"
                << " default " << boolalpha << _default;
            return oss.str ();
        }
        CheckOption::operator bool () const { return _value; }

        //Option& CheckOption::operator= (char *value)
        //{
        //    if (empty (value)) return *this;
        //    bool value = equals (value, "true");
        //    if (_value != value)
        //    {
        //        _value = value;
        //        if (_on_change) _on_change (*this);
        //    }
        //    return *this;
        //}
        Option& CheckOption::operator= (string &value)
        {
            if (value.empty ()) return *this;
            bool bol = (value == "true");
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
            ostringstream oss;
            oss << "type string"
                << " default " << _default; //(_default.empty () ? "<empty>" : _default);
            return oss.str ();
        }
        StringOption::operator string () const
        {
            return _value; //(_value.empty () ? "<empty>" : _value);
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

        SpinOption::SpinOption (i32 value, i32 minimum, i32 maximum, const OnChange on_change)
            : Option (on_change)
        {
            _default = _value = value;
            _minimum = minimum;
            _maximum = maximum;
        }
        string SpinOption::operator() () const
        {
            ostringstream oss;
            oss << "type spin"
                << " default " << _default
                << " min "     << _minimum
                << " max "     << _maximum;
            return oss.str ();
        }
        SpinOption::operator i32 () const { return _value; }
        
        //Option& SpinOption::operator= (char *value)
        //{
        //    if (empty (value)) return *this;
        //    i32 val = atoi (value);
        //    //val = min (max (val, _minimum), _maximum);
        //    if (val < _minimum) val = _minimum;
        //    if (val > _maximum) val = _maximum;
        //    if (_value != val)
        //    {
        //        _value = val;
        //        if (_on_change) _on_change (*this);
        //    }
        //    return *this;
        //}
        Option& SpinOption::operator= (string &value)
        {
            if (value.empty ()) return *this;
            i32 val = atoi (value.c_str ()); //stoi (value);
            //val = min (max (val, _minimum), _maximum);
            if (val < _minimum) val = _minimum;
            if (val > _maximum) val = _maximum;
            if (_value != val)
            {
                _value = val;
                if (_on_change) _on_change (*this);
            }
            return *this;
        }

        //ComboOption::ComboOption (const OnChange on_change)
        //    : Option (on_change)
        //{}
        //string ComboOption::operator() () const
        //{
        //    ostringstream oss;
        //    oss << "type combo";
        //    return oss.str ();
        //}
        ////Option& ComboOption::operator= (char *value)
        ////{
        ////    value;
        ////    if (_on_change) _on_change (*this);
        ////    return *this;
        ////}
        //Option& ComboOption::operator= (string &value)
        //{
        //    (void) value;
        //    if (_on_change) _on_change (*this);
        //    return *this;
        //}

    }

    // Option Events
    namespace {

        using namespace OptionType;

#   ifdef LPAGES
        void on_large_pages     (const Option &)
        {
            TT.resize ();
        }
#   endif

        void on_clear_hash  (const Option &)
        {
            TT.master_clear ();
        }

        void on_resize_hash (const Option &opt)
        {
            TT.resize (i32 (opt), false);
        }

        void on_save_hash   (const Option &)
        {
            string hash_fn = string (*(Options["Hash File"]));
            ofstream ofhash (hash_fn, ios_base::out|ios_base::binary);
            ofhash << TT;
            ofhash.close ();
            sync_cout << "info string Hash saved to file \'" << hash_fn << "\'." << sync_endl;
        }

        void on_load_hash   (const Option &)
        {
            string hash_fn = string (*(Options["Hash File"]));
            ifstream ifhash (hash_fn, ios_base::in|ios_base::binary);
            ifhash >> TT;
            ifhash.close ();
            sync_cout << "info string Hash loaded from file \'" << hash_fn << "\'." << sync_endl;
        }

        void on_change_book (const Option &)
        {
            Searcher::Book.close ();
        }

        void on_change_tb_syzygy (const Option &opt)
        {
            string syzygy_path = string (opt);
            TBSyzygy::initialize (syzygy_path);
        }
        
        void on_config_threadpool(const Option &)
        {
            Threadpool.configure ();
        }

        void on_change_evaluation(const Option &)
        {
            Evaluator::initialize ();
        }

        void on_force_null_move  (const Option &opt)
        {
            Searcher::ForceNullMove = bool (opt);
        }

        void on_50_move_dist (const Option &opt)
        {
            Position::_50_move_dist = 2 * i32 (opt);
        }

        void on_io_log (const Option &opt)
        {
            log_io (bool (opt));
        }

        //// TODO::
        //void on_query (const Option &opt)
        //{
        //    
        //    (void) opt;
        //}

    }

    void   initialize ()
    {

        // Hash Memory Options
        // -------------------

        // The amount of memory to use for hash table during search by engine, in MB (megabytes).
        // This number should be smaller than the amount of physical memory for your system.
        // Default 128, Min 4, Max 1024 (32-bit) or 4096 (64-bit Standard) or 262144 (64-bit Pro).
        //
        // The value is rounded down to a power of 2 (4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144) MB.
        //
        // For infinite analysis or long time control matches you should use the largest hash that fits in the physical memory of your system.
        // For example, on a system with 4 GB of memory you can use up to 2048 MB hash size.
        // For shorter games, for example 3 to 5 minute games, it'val better to use 256 MB or 512 MB as this will provide the best performance.
        // For 16 Min games 1024 or 2048 MB hash size should be fine.
        //
        // In the FAQ about Hash Size you'll find a formula to compute the optimal hash size for your hardware and time control.
        Options["Hash"]                         = OptionPtr (new SpinOption (128, TranspositionTable::MIN_TT_SIZE, TranspositionTable::MAX_TT_SIZE, on_resize_hash));
#ifdef LPAGES
        Options["Large Pages"]                  = OptionPtr (new CheckOption (true, on_large_pages));
#endif

        // Button to clear the Hash Memory.
        // If the Never Clear Hash option is enabled, this button doesn't do anything.
        Options["Clear Hash"]                   = OptionPtr (new ButtonOption (on_clear_hash));

        // This option prevents the Hash Memory from being cleared between successive games or positions belonging to different games.
        // Default false
        //
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
        // .    End the analysis
        // .    Go to the options window, enter the name of the Hash File (e.g. C:\Chess\Game.dat)
        // .    Press the Save Hash to File button, and OK in the options window.
        // 
        // To load a Hash file from disk:
        // .    Load the correspondence game
        // .    Go to the options window, enter the name of the Hash File (e.g. C:\Chess\Game.dat)
        // .    Press the Load Hash from File button, and OK in the options window.
        // -----------------------------------------------------------------------------------------

        // File name for saving or loading the Hash file with the Save Hash to File or Load Hash from File buttons.
        // A full file name is required, for example C:\Chess\Hash000.dat.
        // By default DON will use the hash.dat file in the current folder of the engine.
        Options["Hash File"]                    = OptionPtr (new StringOption ("Hash.dat"));

        // Save the current Hash table to a disk file specified by the Hash File option.
        // Use the Save Hash File button after ending the analysis of the position.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the file will be identical to the size of the hash memory, so this operation could take a while.
        // This feature can be used to interrupt and restart a deep analysis at any time.
        Options["Save Hash"]                    = OptionPtr (new ButtonOption (on_save_hash));

        // Load a previously saved Hash file from disk.
        // Use the Load Hash File button after loading the game or position, but before starting the analysis.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the Hash memory will automatically be set to the size of the saved file.
        // Please make sure to check the Never Clear Hash option,
        // as otherwise your loaded Hash could be cleared by a subsequent ucinewgame or Clear Hash command.
        Options["Load Hash"]                    = OptionPtr (new ButtonOption (on_load_hash));

        // Position Learning Options
        // -------------------------

        // Openings Book Options
        // ---------------------
        // Whether or not the engine should use the Opening Book.
        Options["Own Book"]                     = OptionPtr (new CheckOption (false));
        // The filename of the Opening Book.
        Options["Book File"]                    = OptionPtr (new StringOption ("Book.bin", on_change_book));
        // Whether or not to always play the best move from the Opening Book.
        // False will lead to more variety in opening play.
        Options["Best Book Move"]               = OptionPtr (new CheckOption (true));

        // End Game Table Bases Options
        // ----------------------------
        // 
        Options["Syzygy Path"]                  = OptionPtr (new StringOption ("", on_change_tb_syzygy));
        Options["Syzygy Probe Depth"]           = OptionPtr (new SpinOption ( 1, 1, 100));
        Options["Syzygy 50 Move Rule"]          = OptionPtr (new CheckOption (true));
        Options["Syzygy Probe Limit"]           = OptionPtr (new SpinOption ( 6, 0, 6));

        // Cores and Threads Options
        // -------------------------

        // The maximum number of Threads (cores) to use during the search.
        // This number should be set to the number of cores in your CPU.
        // Default is hardware-dependent, Min 1, Max 32 (Standard) or 64 (Pro).
        //
        // DON will automatically limit the number of Threads to the number of logical processors of your hardware.
        // If your computer supports hyper-threading it is recommended not using more threads than physical cores,
        // as the extra hyper-threads would usually degrade the performance of the engine. 
        Options["Threads"]                      = OptionPtr (new SpinOption ( 1, 1, MAX_THREADS, on_config_threadpool));

        // Minimum depth at which work will be split between cores, when using multiple threads.
        // Default 0, Min 0, Max 15.
        //
        // Default 0 means auto setting which depends on the threads.
        // This parameter can impact the speed of the engine (nodes per second) and can be fine-tuned to get the best performance out of your hardware.
        // The default value 10 is tuned for Intel quad-core i5/i7 systems, but on other systems it may be advantageous to increase this to 12 or 14.
        Options["Split Depth"]                  = OptionPtr (new SpinOption ( 0, 0, MAX_SPLIT_DEPTH, on_config_threadpool));

        // If this is set to true, threads are suspended when there is no work to do.
        // This saves CPU power consumption, but waking a thread takes a small bit of time.
        // For maximum performance, set this option to false,
        // but if you need to reduce power consumption (i.e. on mobile devices) set this option to true.
        // Default true
        Options["Idle Threads Sleep"]           = OptionPtr (new CheckOption (true));

        // Game Play Options
        // -----------------

        // Whether or not the engine should analyze when it is the opponent's turn.
        // Default true.
        //
        // The Ponder feature (sometimes called "Permanent Brain") is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["Ponder"]                       = OptionPtr (new CheckOption (true));

        // The number of principal variations (alternate lines of analysis) to display.
        // Specify 1 to just get the best line. Asking for more lines slows down the search.
        // Default 1, Min 1, Max 50.
        //
        // The MultiPV feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["MultiPV"]                      = OptionPtr (new SpinOption ( 1, 1, 50));

        // TODO::
        //// Limit the multi-PV analysis to moves within a range of the best move.
        //// Default 0, Min 0, Max 999.
        ////
        //// Values are in centipawn. Because of contempt and evaluation corrections in different stages of the game, this value is only approximate.
        //// A value of 0 means that this parameter will not be taken into account.
        //Options["MultiPV_cp"]                   = OptionPtr (new SpinOption (0, 0, 999));

        // TODO::
        //// Level of contempt to avoid draws in game play.
        //// Default 1, Min 0 (none), Max 2 (aggressive).
        ////
        //// The notion of "contempt" implies that engine will try to avoid draws by evaluating its own position slightly too optimistically.
        //// The Contempt level can be chosen between 0 (none) and 2 (aggressive), the default value of 1 should be a good compromise in most situations.
        ////
        //// .    0 = No Contempt
        //// The evaluations are accurate and identical for both sides. This is recommended for position analysis in which you analyze alternatively for White and Black.
        //// The starting position evaluates as approx. +0.15.
        ////
        //// .    1 = Default Contempt
        //// Contempt 1 is primarily based on piece value imbalance, engine will value its own pieces higher than the opponent pieces, so will only exchange them if there's a clear positional advantage in doing so.
        //// This also means that the score is evaluated optimistically for the side to move (at most 0.15 pawn). For example, the starting position evaluates as approx. +0.30 when analyzing for White and +0.00 when viewed from Black.
        //// This is only recommended for position analysis if you always analyze for the same side.
        ////
        //// .    2 = Aggressive
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

        // Roughly equivalent to "Optimism."
        // Factor for adjusted contempt. Changes playing style.
        // Positive values of contempt favor more "risky" play,
        // while negative values will favor draws. Zero is neutral.
        // Default 0, Min -50, Max +50.
        Options["Contempt Factor"]              = OptionPtr (new SpinOption (0, -50, +50));
        
        Options["Force Null Move"]              = OptionPtr (new CheckOption (false, on_force_null_move));

        // The number of moves after which the 50-move rule will kick in.
        // Default 50, Min 5, Max 50.
        //
        // This setting defines the number of moves after which the 50-move rule will kick in - the default value is 50,
        // i.e. the official 50-moves rule.
        // Setting this option in the range of 10 to 15 moves can be useful to analyse more correctly blockade or fortress positions:
        // - Closed positions in which no progress can be made without some sort of sacrifice (blockade);
        // - End games with a material advantage that is insufficient for winning (fortress).
        //
        // By setting 50 Move Distance to 15, you're telling the engine that if it cannot make any progress in the next 15 moves, the game is a draw.
        // It's a reasonably generic way to decide whether a material advantage can be converted or not.
        Options["50 Move Distance"]             = OptionPtr (new SpinOption (50, 5, 50, on_50_move_dist));


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

        Options["Mobility (Midgame)"]           = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));
        Options["Mobility (Endgame)"]           = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));

        Options["Pawn Structure (Midgame)"]     = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));
        Options["Pawn Structure (Endgame)"]     = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));

        Options["Passed Pawns (Midgame)"]       = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));
        Options["Passed Pawns (Endgame)"]       = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));
        
        // Degree of agressiveness.
        Options["Aggressive"]                   = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));
        // Degree of cowardice.
        Options["Cowardice"]                    = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));
        Options["Space"]                        = OptionPtr (new SpinOption (100, 0, 200, on_change_evaluation));


        // TODO::
        // Maximum search depth for mate search.
        // Default 0, Min 0, Max 99.
        //
        // If set, this option will usually speed-up a mate search.
        // If you know that a position is "mate in <x>", you can use <x> or a value slightly larger than <x> in the Mate Search option.
        // This will prevent DON from going too deep in variations that don't lead to mate in the required number of moves.
        Options["Mate Search"]                  = OptionPtr (new SpinOption (  0, 0, 99));
        // How well you want engine to play.
        // At level 0, engine will make dumb moves. MAX_SKILL_LEVEL is best/strongest play.
        Options["Skill Level"]                  = OptionPtr (new SpinOption (MAX_SKILL_LEVEL,  0, MAX_SKILL_LEVEL));

        Options["Emergency Move Horizon"]       = OptionPtr (new SpinOption ( 40, 0, 50));
        Options["Emergency Base Time"]          = OptionPtr (new SpinOption ( 60, 0, 30000));
        Options["Emergency Move Time"]          = OptionPtr (new SpinOption ( 30, 0, 5000));
        // The minimum amount of time to analyze, in milliseconds.
        Options["Minimum Thinking Time"]        = OptionPtr (new SpinOption ( 20, 0, 5000));
        // Move fast if small value, 100 is neutral
        Options["Slow Mover"]                   = OptionPtr (new SpinOption ( 80, 10, 1000));

        // Debug Options
        // -------------
        // Whether or not to write a debug log.
        Options["Write IO Log"]                 = OptionPtr (new CheckOption (false, on_io_log));
        // Whether or not to write a search log.
        Options["Write Search Log"]             = OptionPtr (new CheckOption (false));
        // The filename of the search log.
        Options["Search Log File"]              = OptionPtr (new StringOption ("SearchLog.txt"));

        /// ---------------------------------------------------------------------------------------

        // Whether or not engine should play using Chess960 (Fischer Random Chess) mode.
        // Chess960 is a chess variant where the back ranks are scrambled.
        // This feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        // Default false.
        Options["UCI_Chess960"]                 = OptionPtr (new CheckOption (false));

        // TODO::
        //// Activate the strength limit specified in the UCI_Elo parameter.
        //// This feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        //// Default false.
        ////
        //Options["UCI_LimitStrength"]            = OptionPtr (new CheckOption (false));

        //// UCI-protocol compliant version of Strength parameter.
        //// Internally the UCI_ELO value will be converted to a Strength value according to the table given above.
        //// This feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        //// Default 3000, Min 1200, Max 3000.
        //Options["UCI_ELO"]                      = OptionPtr (new SpinOption (3000, 1200, 3000));

        // TODO::
        //// This feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        //Options["UCI_Query"]                    = OptionPtr (new ButtonOption (on_query));

    }

    void deinitialize ()
    {
        Options.clear ();
    }

}
