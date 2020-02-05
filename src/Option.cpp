#include "Option.h"

#include "Polyglot.h"
#include "Logger.h"
#include "Thread.h"
#include "Transposition.h"
#include "Searcher.h"
#include "TBsyzygy.h"

UCI::StringOptionMap Options;

using namespace std;

namespace UCI {

    using namespace Searcher;
    using namespace TBSyzygy;

    size_t Option::InsertOrder = 0;

    Option::Option(OnChange on_cng)
        : type("button")
        , defaultValue("")
        , currentValue("")
        , minimum(0)
        , maximum(0)
        , onChange(on_cng)
    {}
    Option::Option(const char *val, OnChange on_cng)
        : type("string")
        , minimum(0)
        , maximum(0)
        , onChange(on_cng)
    {
        defaultValue = currentValue = val;
    }
    Option::Option(const bool val, OnChange on_cng)
        : type("check")
        , minimum(0)
        , maximum(0)
        , onChange(on_cng)
    {
        defaultValue = currentValue = (val ? "true" : "false");
    }
    Option::Option(const i32 val, i32 min, i32 max, OnChange on_cng)
        : type("spin")
        , minimum(min)
        , maximum(max)
        , onChange(on_cng)
    {
        defaultValue = currentValue = std::to_string(val);
    }
    Option::Option(const char* v, const char* cur, OnChange on_cng)
        : type("combo")
        , minimum(0)
        , maximum(0)
        , onChange(on_cng)
    {
        defaultValue = v;
        currentValue = cur;
    }

    Option::operator string() const
    {
        assert(type == "string");
        return currentValue;
    }
    Option::operator bool() const
    {
        assert(type == "check");
        return currentValue == "true";
    }
    Option::operator i32() const
    {
        assert(type == "spin");
        return std::stoi(currentValue);
    }
    bool Option::operator==(const char *val) const
    {
        assert(type == "combo");
        return !CaseInsensitiveLessComparer()(currentValue, val)
            && !CaseInsensitiveLessComparer()(val, currentValue);
    }

    /// Option::operator=() updates value and triggers onChange() action.
    Option& Option::operator=(const char *value)
    {
        return *this = string(value);
    }
    /// Option::operator=() updates value and triggers onChange() action.
    Option& Option::operator=(const string &value)
    {
        assert(!type.empty());

        if (type != "button")
        {
            auto val = value;

            if (type == "check")
            {
                toLower(val);
                if (   val != "true"
                    && val != "false")
                {
                    return *this;
                }
            }
            else
            if (type == "spin")
            {
                val = std::to_string(clamp(std::stoi(val), minimum, maximum));
            }
            else
            if (type == "string")
            {
                if (white_spaces(val))
                {
                    val = "<empty>";
                }
            }
            else
            if (type == "combo")
            {
                StringOptionMap comboMap; // To have case insensitive compare
                istringstream iss{ defaultValue };
                string token;
                while (iss >> token)
                {
                    if (token == "var")
                    {
                        continue;
                    }
                    comboMap[token] << Option();
                }
                if (comboMap.find(val) == comboMap.end())
                {
                    return *this;
                }
            }

            currentValue = val;
        }

        if (nullptr != onChange)
        {
            onChange ();
        }

        return *this;
    }

    /// Option::operator<<() inits options and assigns idx in the correct printing order
    void Option::operator<<(const Option &opt)
    {
        *this = opt;
        index = InsertOrder++;
    }
    /// Option::operator()() is to string method of option
    string Option::operator()()  const
    {
        ostringstream oss;
        oss << " type " << type;

        if (type != "button")
        {
            if (   type == "string"
                || type == "check"
                || type == "combo")
            {
                oss << " default " << defaultValue;
            }
            else
            if (type == "spin")
            {
                oss << " default " << i32(std::stoi(defaultValue))
                    << " min " << minimum
                    << " max " << maximum;
            }
            //oss << " current " << currentValue;
        }
        return oss.str();
    }

    /// 'On change' actions, triggered by an option's value change

    namespace {

        void onHash()
        {
            TT.autoResize(i32(Options["Hash"]));
        }

        void onClearHash()
        {
            clear();
        }

        void onSaveHash()
        {
            TT.save(string(Options["Hash File"]));
        }
        void onLoadHash()
        {
            TT.load(string(Options["Hash File"]));
        }

        void onThreads()
        {
            auto threadCount = optionThreads();
            if (threadCount != Threadpool.size())
            {
                Threadpool.configure(threadCount);
            }
        }

        void onBookFile()
        {
            Book.initialize(string(Options["Book.bin"]));
        }

        void onDebugFile()
        {
            Log.set(string(Options["Debug File"]));
        }

        void onSyzygyPath()
        {
            TBSyzygy::initialize(string(Options["SyzygyPath"]));
        }

    }

    void initialize()
    {

        Options["Hash"]               << Option(16, 0, TTable::MaxHashSize, onHash);

        Options["Clear Hash"]         << Option(onClearHash);
        Options["Retain Hash"]        << Option(false);

        Options["Hash File"]          << Option("Hash.dat");
        Options["Save Hash"]          << Option(onSaveHash);
        Options["Load Hash"]          << Option(onLoadHash);

        Options["Use Book"]           << Option(false);
        Options["Book File"]          << Option("Book.bin", onBookFile);
        Options["Book Pick Best"]     << Option(true);
        Options["Book Move Num"]      << Option(20, 0, 100);

        Options["Threads"]            << Option(1, 0, 512, onThreads);

        Options["Skill Level"]        << Option(MaxLevel,  0, MaxLevel);

        Options["MultiPV"]            << Option( 1, 1, 500);

        Options["Fixed Contempt"]     << Option(  0, -100, 100);
        Options["Contempt Time"]      << Option( 40, 0, 1000);
        Options["Contempt Value"]     << Option(100, 0, 1000);
        Options["Analysis Contempt"]  << Option("Both var Off var White var Black var Both", "Both");

        Options["Draw MoveCount"]     << Option(50, 5, 50);

        Options["Overhead MoveTime"]  << Option(30,  0, 5000);
        Options["Minimum MoveTime"]   << Option(20,  0, 5000);
        Options["Move Slowness"]      << Option(84, 10, 1000);
        Options["Time Nodes"]         << Option( 0,  0, 10000);
        Options["Ponder"]             << Option(true);

        Options["SyzygyPath"]         << Option("<empty>", onSyzygyPath);
        Options["SyzygyProbeDepth"]   << Option(TBProbeDepth, 1, 100);
        Options["SyzygyLimitPiece"]   << Option(TBLimitPiece, 0, 6);
        Options["SyzygyUseRule50"]    << Option(TBUseRule50);

        Options["Debug File"]         << Option("<empty>", onDebugFile);
        //Options["Output File"]        << Option("<empty>");

        Options["UCI_Chess960"]       << Option(false);
        Options["UCI_AnalyseMode"]    << Option(false);
        Options["UCI_LimitStrength"]  << Option(false);
        Options["UCI_Elo"]            << Option(1350, 1350, 3100);

    }

}

u32 optionThreads()
{
    auto threadCount = i32(Options["Threads"]);
    if (0 == threadCount)
    {
        threadCount = thread::hardware_concurrency();
    }
    return threadCount;
}
