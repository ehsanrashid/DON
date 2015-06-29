#include "UCI.h"

#include <sstream>
#include <fstream>
#include <iomanip>

#include "Transposition.h"
#include "Thread.h"
#include "Searcher.h"
#include "Evaluator.h"

UCI::OptionMap  Options; // Global string mapping of Options

namespace UCI {

    using namespace std;
    using namespace Transposition;
    using namespace Searcher;

    Option::Option (OnChange on_change)
        : _type ("button")
        , _value ("")
        , _minimum (0)
        , _maximum (0)
        , _on_change (on_change)
    {}
    Option::Option (const bool  val, OnChange on_change)
        : _type ("check")
        , _minimum (0)
        , _maximum (0)
        , _on_change (on_change)
    {
        _value = (val ? "true" : "false");
    }
    Option::Option (const char *val, OnChange on_change)
        : Option (string(val), on_change)
    {}
    Option::Option (const string &val, OnChange on_change)
        : _type ("string")
        , _minimum (0)
        , _maximum (0)
        , _on_change (on_change)
    {
        _value = val;
    }
    Option::Option (const i32 val, i32 minimum, i32 maximum, OnChange on_change)
        : _type ("spin")
        , _minimum (minimum)
        , _maximum (maximum)
        , _on_change (on_change)
    {
        ostringstream oss; oss << val; _value = oss.str ();
    }

    Option::operator bool () const
    {
        assert (_type == "check");
        return (_value == "true");
    }
    Option::operator i32 () const
    {
        assert (_type == "spin");
        return stoi (_value);
    }
    Option::operator string () const
    {
        assert (_type == "string");
        return _value;
    }

    // operator=() updates value and triggers on_change() action.
    // It's up to the GUI to check for option's limits,
    // but could receive the new value from the user by console window,
    // so let's check the bounds anyway.
    Option& Option::operator= (const char   *value)
    {
        return *this = string(value);
    }
    Option& Option::operator= (const string &value)
    {
        assert (!_type.empty ());

        if (!( (_type != "button" && value.empty ())
            || (_type == "check"  && value != "true" && value != "false")
            || (_type == "spin"   && (_minimum > stoi (value) || stoi (value) > _maximum))
             )
           )
        {
            if (_type != "button")
            {
                if (_value != value)
                {
                    _value = value;
                    if (_on_change != nullptr) _on_change (*this);
                }
            }
            else
            {
                if (_on_change != nullptr) _on_change (*this);
            }
        }
        return *this;
    }

    // operator<<() inits options and assigns idx in the correct printing order
    void Option::operator<< (const Option &opt)
    {
        static u08 insert_order = 0;
        *this = opt;
        _index = insert_order++;
    }

    // operator()() is to string method of option
    string Option::operator() ()  const
    {
        ostringstream oss;
        oss << " type " << _type;
        if (_type != "button")
        {
            oss << " default " << _value;
            if (_type == "spin")
            {
                oss << " min " << _minimum
                    << " max " << _maximum;
            }
        }
        return oss.str ();
    }

    // Option Actions
    namespace {

#   ifdef LPAGES
        void large_pages (const Option &)
        {
            TT.resize ();
        }
#   endif

        void clear_hash  (const Option &)
        {
            reset ();
        }

        void never_clear_hash (const Option &opt)
        {
            TranspositionTable::ClearHash = !bool(opt);
        }

        void change_hash (const Option &opt)
        {
            TT.auto_size (i32(opt), false);
        }

        void save_hash   (const Option &)
        {
            string hash_fn = string(Options["Hash File"]);
            TT.save (hash_fn);
        }

        void load_hash   (const Option &)
        {
            string hash_fn = string(Options["Hash File"]);
            TT.load (hash_fn);
        }

        void configure_threadpool (const Option &)
        {
            Threadpool.configure ();
        }

        void fifty_move_dist (const Option &opt)
        {
            Position::FiftyMoveDist = u08(2 * i32(opt));
        }

        void configure_hash (const Option &)
        {
            HashFile         = string(Options["Hash File"]);
            AutoSaveHashTime = u16(i32(Options["Auto Save Hash (min)"]));
        }

        void configure_contempt (const Option &)
        {
            FixedContempt = i16(i32(Options["Fixed Contempt"]));
            ContemptTime  = i16(i32(Options["Timed Contempt (sec)"]));
            ContemptValue = i16(i32(Options["Valued Contempt (cp)"]));
        }

        void configure_multipv (const Option &)
        {
            MultiPV        = u08(i32(Options["MultiPV"]));
            //MultiPV_cp= i32(Options["MultiPV_cp"]);
        }

        void change_search_log (const Option &opt)
        {
            SearchLog = string(opt);
            if (!white_spaces (SearchLog))
            {
                trim (SearchLog);
                if (!white_spaces (SearchLog))
                {
                    convert_path (SearchLog);
                    remove_extension (SearchLog);
                    if (!white_spaces (SearchLog)) SearchLog += ".txt";
                }
                if (white_spaces (SearchLog)) SearchLog = "SearchLog.txt";
            }
        }

        void configure_book (const Option &)
        {
            BookFile     = string(Options["Book File"]);
            BookMoveBest = bool(Options["Book Move Best"]);
        }

        void change_skill_level (const Option &opt)
        {
            SkillMgr.change_level (u08(i32(opt)));
        }

        void configure_time (const Option &)
        {
            //MaximumMoveHorizon   = i32(Options["Maximum Move Horizon"]);
            //EmergencyMoveHorizon = i32(Options["Emergency Move Horizon"]);
            //EmergencyClockTime   = i32(Options["Emergency Clock Time"]);
            //EmergencyMoveTime    = i32(Options["Emergency Move Time"]);
            //MinimumMoveTime      = i32(Options["Minimum Move Time"]);
            MoveSlowness         = i32(Options["Move Slowness"]);
            NodesTime            = i32(Options["Nodes Time"]);
            Ponder               = bool(Options["Ponder"]);
        }

        void uci_chess960 (const Option &opt)
        {
            Chess960 = bool(opt);
        }
    }

    void initialize ()
    {

        // Hash Memory Options
        // -------------------

        // The amount of memory to use for hash table during search by engine, in MB (megabytes).
        // This number should be smaller than the amount of physical memory for your system.
        // Default 16, Min 4, Max 1048576 MB = 1024 GB.
        //
        // The value is rounded down to a power of 2 (4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144) MB.
        //
        // For infinite analysis or long time control matches you should use the largest hash that fits in the physical memory of your system.
        // For example, on a system with 4 GB of memory you can use up to 2048 MB hash size.
        // For shorter games, for example 3 to 5 minute games, it'val better to use 256 MB or 512 MB as this will provide the best performance.
        // For 16 Min games 1024 or 2048 MB hash size should be fine.
        //
        // In the FAQ about Hash Size you'll find a formula to compute the optimal hash size for your hardware and time control.
        Options["Hash"]                         << Option (TranspositionTable::DefSize,
                                                           0,//TranspositionTable::MinSize,
                                                           TranspositionTable::MaxSize, change_hash);
#ifdef LPAGES
        Options["Large Pages"]                  << Option (true, large_pages);
#endif

        // Button to clear the Hash Memory.
        // If the Never Clear Hash option is enabled, this button doesn't do anything.
        Options["Clear Hash"]                   << Option (clear_hash);

        // This option prevents the Hash Memory from being cleared between successive games or positions belonging to different games.
        // Default false
        //
        // Check this option also if you want to Load the Hash from disk file,
        // otherwise your loaded Hash could be cleared by a subsequent ucinewgame or Clear Hash command.
        Options["Never Clear Hash"]             << Option (false, never_clear_hash);

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
        // .    Go to the options window, enter the name of the Hash File(e.g. C:\Chess\Game.dat)
        // .    Press the Save Hash to File button, and OK in the options window.
        // 
        // To load a Hash file from disk:
        // .    Load the correspondence game
        // .    Go to the options window, enter the name of the Hash File(e.g. C:\Chess\Game.dat)
        // .    Press the Load Hash from File button, and OK in the options window.
        // -----------------------------------------------------------------------------------------

        // File name for saving or loading the Hash file with the Save Hash to File or Load Hash from File buttons.
        // A full file name is required, for example C:\Chess\Hash000.dat.
        // By default DON will use the hash.dat file in the current folder of the engine.
        Options["Hash File"]                    << Option (HashFile, configure_hash);
        // Auto Save Hash Time (min)
        Options["Auto Save Hash (min)"]         << Option (AutoSaveHashTime,    0,  60, configure_hash);

        // Save the current Hash table to a disk file specified by the Hash File option.
        // Use the Save Hash File button after ending the analysis of the position.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the file will be identical to the size of the hash memory, so this operation could take a while.
        // This feature can be used to interrupt and restart a deep analysis at any time.
        Options["Save Hash"]                    << Option (save_hash);

        // Load a previously saved Hash file from disk.
        // Use the Load Hash File button after loading the game or position, but before starting the analysis.
        // Some GUIs (e.g. Shredder, Fritz) wait for sending the button command to the engine until you click OK in the engine options window.
        // The size of the Hash memory will automatically be set to the size of the saved file.
        // Please make sure to check the Never Clear Hash option,
        // as otherwise your loaded Hash could be cleared by a subsequent ucinewgame or Clear Hash command.
        Options["Load Hash"]                    << Option (load_hash);


        // Position Learning Options
        // -------------------------

        // Openings Book Options
        // ---------------------
        // The filename of the Opening Book.
        Options["Book File"]                    << Option (BookFile, configure_book);
        // Whether or not to always play the best move from the Opening Book.
        // False will lead to more variety in opening play.
        Options["Book Move Best"]               << Option (BookMoveBest, configure_book);


        // End-Game Table Bases Options
        // ----------------------------

        // Cores and Threads Options
        // -------------------------

        // The maximum number of Threads (cores) to use during the search.
        // This number should be set to the number of cores in your CPU.
        // Default is hardware-dependent, Min 1, Max 32 (Standard) or 64 (Pro).
        //
        // DON will automatically limit the number of Threads to the number of logical processors of your hardware.
        // If your computer supports hyper-threading it is recommended not using more threads than physical cores,
        // as the extra hyper-threads would usually degrade the performance of the engine. 
        Options["Threads"]                      << Option ( 1, 1, MAX_THREADS, configure_threadpool);

        // The depth at which work will be split between cores, when using multiple threads.
        // Default 5, Min 0, Max 12.
        //
        // Default 0 means auto setting which depends on the threads.
        // This parameter can impact the speed of the engine (nodes per second) and can be fine-tuned to get the best performance out of your hardware.
        // The default value 10 is tuned for Intel quad-core i5/i7 systems, but on other systems it may be advantageous to increase this to 12 or 14.
        Options["Split Depth"]                  << Option ( 5, 0, 12, configure_threadpool);

        // Game Play Options
        // -----------------

        // How well you want engine to play.
        // Default MAX_SKILL_LEVEL, Min 0, Max MAX_SKILL_LEVEL.
        //
        // At level 0, engine will make dumb moves. MAX_SKILL_LEVEL is best/strongest play.
        Options["Skill Level"]                  << Option (MAX_SKILL_LEVEL,  0, MAX_SKILL_LEVEL, change_skill_level);

        // The number of principal variations (alternate lines of analysis) to display.
        // Specify 1 to just get the best line. Asking for more lines slows down the search.
        // Default 1, Min 1, Max 50.
        //
        // The MultiPV feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["MultiPV"]                      << Option (MultiPV  ,   1,  50, configure_multipv);

        // Limit the multi-PV analysis to moves within a range of the best move.
        // Default 0, Min 0, Max 999.
        //
        // Values are in centipawn. Because of contempt and evaluation corrections in different stages of the game, this value is only approximate.
        // A value of 0 means that this parameter will not be taken into account.
        // The MultiPV_cp feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        //Options["MultiPV_cp"]                   << Option (MultiPV_cp, 0, VALUE_NONE+1, Searcher::configure_multipv);

        // Changes playing style.
        // ----------------------
        // Fixed Contempt roughly equivalent to "Optimism".
        // Positive values of contempt favor more "risky" play,
        // while negative values will favor draws. Zero is neutral.
        // Default 0, Min -100, Max +100.
        Options["Fixed Contempt"]               << Option (FixedContempt,-100,+100, configure_contempt);
        // Time (sec) for Timed Contempt
        // Default +6, Min 0, Max +900.
        Options["Timed Contempt (sec)"]         << Option (ContemptTime ,   0,+900, configure_contempt);
        // Centipawn (cp) for Valued Contempt
        // Default +50, Min 0, Max +1000.
        Options["Valued Contempt (cp)"]         << Option (ContemptValue,   0,+1000, configure_contempt);

        // The number of moves after which the 50-move rule will kick in.
        // Default 50, Min 5, Max 50.
        //
        // This setting defines the number of moves after which the 50-move rule will kick in - the default value is 50,
        // i.e. the official 50-moves rule.
        // Setting this option in the range of 10 to 15 moves can be useful to analyse more correctly blockade or fortress positions:
        // - Closed positions in which no progress can be made without some sort of sacrifice (blockade);
        // - End games with a material advantage that is insufficient for winning (fortress).
        //
        // By setting Fifty Move Distance to 15, you're telling the engine that if it cannot make any progress in the next 15 moves, the game is a draw.
        // It's a reasonably generic way to decide whether a material advantage can be converted or not.
        Options["Fifty Move Distance"]          << Option (Position::FiftyMoveDist/2,+  5,+ 50, fifty_move_dist);

        //// Plan time management at most this many moves ahead, in num of moves.
        //Options["Maximum Move Horizon"]         << Option (MaximumMoveHorizon  , 0, 100, configure_time);
        //// Be prepared to always play at least this many moves, in num of moves.
        //Options["Emergency Move Horizon"]       << Option (EmergencyMoveHorizon, 0, 100, configure_time);
        //// Always attempt to keep at least this much time at clock, in milliseconds.
        //Options["Emergency Clock Time"]         << Option (EmergencyClockTime  , 0, 30000, configure_time);
        //// Attempt to keep at least this much time for each remaining move, in milliseconds.
        //Options["Emergency Move Time"]          << Option (EmergencyMoveTime   , 0, 5000, configure_time);
        //// The minimum amount of time to analyze, in milliseconds.
        //Options["Minimum Move Time"]            << Option (MinimumMoveTime     , 0, 5000, configure_time);
        // How slow you want engine to play, 100 is neutral, in %age.
        Options["Move Slowness"]                << Option (MoveSlowness        ,+ 10,+ 1000, configure_time);
        Options["Nodes Time"]                   << Option (NodesTime           ,   0,+10000, configure_time);
        // Whether or not the engine should analyze when it is the opponent's turn.
        // Default true.
        //
        // The Ponder feature (sometimes called "Permanent Brain") is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        Options["Ponder"]                       << Option (Ponder, configure_time);

        // ---------------------------------------------------------------------------------------
        // Other Options
        // -------------

        // The filename of the search log.
        Options["Search Log"]                   << Option (SearchLog, change_search_log);

        // Whether or not engine should play using Chess960 (Fischer Random Chess) mode.
        // Chess960 is a chess variant where the back ranks are scrambled.
        // This feature is controlled by the chess GUI, and usually doesn't appear in the configuration window.
        // Default false.
        Options["UCI_Chess960"]                 << Option (Chess960, uci_chess960);
        
        // Weaken engine.
        //Options["UCI_LimitStrength"]            << Option (false);

        // Ignored if UCI_LimitStrength = false.
        //Options["UCI_ELO"]                      << Option ( 0, 0, 3000);

    }

    void exit ()
    {
        Options.clear ();
    }

}
