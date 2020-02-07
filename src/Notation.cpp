#include "Notation.h"

#include <cmath>

#include "MoveGenerator.h"
#include "Searcher.h"
#include "Transposition.h"
#include "Thread.h"

using namespace std;
using namespace BitBoard;
using namespace Searcher;

namespace {

    /// Ambiguity
    enum Ambiguity : u08
    {
        AMB_NONE,
        AMB_RANK,
        AMB_FILE,
        AMB_SQUARE,
    };

    /// Ambiguity if more then one piece of same type can reach 'dst' with a legal move.
    /// NOTE: for pawns it is not needed because 'org' file is explicit.
    Ambiguity ambiguity(Move m, const Position &pos)
    {
        assert(pos.pseudoLegal(m)
            && pos.legal(m));

        auto org = orgSq(m);
        auto dst = dstSq(m);
        auto pt = pType(pos[org]);
        // Disambiguation if have more then one piece with destination
        // note that for pawns is not needed because starting file is explicit.
        Bitboard piece = pos.attacksFrom(pt, dst) & pos.pieces(pos.active, pt);

        Bitboard amb = piece ^ org;
        if (0 == amb)
        {
            return Ambiguity::AMB_NONE;
        }

        Bitboard pcs = amb;
                    // If pinned piece is considered as ambiguous
                    // & ~(pos.si->kingBlockers[pos.active] & pos.pieces(pos.active));
        while (0 != pcs)
        {
            auto sq = popLSq(pcs);
            if (!pos.legal(makeMove<NORMAL>(sq, dst)))
            {
                amb ^= sq;
            }
        }
        if (0 == (amb & fileBB(org))) return Ambiguity::AMB_RANK;
        if (0 == (amb & rankBB(org))) return Ambiguity::AMB_FILE;
        return Ambiguity::AMB_SQUARE;
    }

    /*
    string pretty_value(Value v)
    {
        assert(-VALUE_MATE <= v && v <= +VALUE_MATE);
        ostringstream oss;
        if (abs(v) < +VALUE_MATE - i32(DEP_MAX))
        {
            oss << showpos << setprecision(2) << fixed
                << valueCP(v) / 100.0
                << noshowpos;
        }
        else
        {
            oss << showpos << "#"
                << i32(v > VALUE_ZERO ?
                        +(VALUE_MATE - v + 1) :
                        -(VALUE_MATE + v + 0)) / 2
                << noshowpos;
        }
        return oss.str();
    }
    string pretty_time(u64 time)
    {
        constexpr u32 SecondMilliSec = 1000;
        constexpr u32 MinuteMilliSec = 60*SecondMilliSec;
        constexpr u32 HourMilliSec   = 60*MinuteMilliSec;

        u32 hours  = u32(time / HourMilliSec);
        time      %= HourMilliSec;
        u32 minutes= u32(time / MinuteMilliSec);
        time      %= MinuteMilliSec;
        u32 seconds= u32(time / SecondMilliSec);
        time      %= SecondMilliSec;
        time      /= 10;

        ostringstream oss;
        oss << setfill('0')
            << setw(2) << hours   << ":"
            << setw(2) << minutes << ":"
            << setw(2) << seconds << "."
            << setw(2) << time
            << setfill(' ');
        return oss.str();
    }
    */
}

/// Converts a move to a string in coordinate algebraic notation.
/// The only special case is castling moves,
///  - e1g1 notation in normal chess mode,
///  - e1h1 notation in chess960 mode.
/// Internally castle moves are always coded as "king captures rook".
string canMove(Move m)
{
    if (MOVE_NONE == m) return {"(none)"};
    if (MOVE_NULL == m) return {"(null)"};
    ostringstream oss;
    auto org = orgSq(m);
    auto dst = dstSq(m);
    if (   CASTLE == mType(m)
        && !bool(Options["UCI_Chess960"]))
    {
        assert(sRank(org) == sRank(dst));
        dst = makeSquare(dst > org ? F_G : F_C, sRank(org));
    }

    oss << toString(org)
        << toString(dst);
    if (PROMOTE == mType(m))
    {
        oss << (BLACK|promoteType(m));
    }
    return oss.str();
}
/// Converts a string representing a move in coordinate algebraic notation
/// to the corresponding legal move, if any.
Move canMove(const string &can, const Position &pos)
{
    //// If promotion piece in uppercase, convert to lowercase
    //if (   5 == can.size()
    //    && isupper(can[4]))
    //{
    //    can[4] = char(tolower(can[4]));
    //}
    assert(5 > can.size()
        || islower(can[4]));
    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
    {
        if (can == canMove(vm))
        {
            return vm;
        }
    }
    return MOVE_NONE;
}

/// Converts a move to a string in short algebraic notation.
string sanMove(Move m, Position &pos)
{
    if (MOVE_NONE == m) return {"(none)"};
    if (MOVE_NULL == m) return {"(null)"};
    assert(MoveList<GenType::LEGAL>(pos).contains(m));

    ostringstream oss;
    auto org = orgSq(m);
    auto dst = dstSq(m);

    if (CASTLE != mType(m))
    {
        auto pt = pType(pos[org]);
        if (PAWN != pt)
        {
            oss << (WHITE|pt);
            if (KING != pt)
            {
                // Disambiguation if have more then one piece of type 'pt' that can reach 'dst' with a legal move.
                switch (ambiguity(m, pos))
                {
                case Ambiguity::AMB_RANK: oss << toChar(sFile(org)); break;
                case Ambiguity::AMB_FILE: oss << toChar(sRank(org)); break;
                case Ambiguity::AMB_SQUARE: oss << toString(org);    break;
                case Ambiguity::AMB_NONE:
                default: break;
                }
            }
        }

        if (pos.capture(m))
        {
            if (PAWN == pt)
            {
                oss << toChar(sFile(org));
            }
            oss << "x";
        }

        oss << toString(dst);

        if (   PAWN == pt
            && PROMOTE == mType(m))
        {
            oss << "=" << (WHITE|promoteType(m));
        }
    }
    else
    {
        oss << (dst > org ? "O-O" : "O-O-O");
    }

    // Move marker for check & checkmate
    if (pos.giveCheck(m))
    {
        StateInfo si;
        pos.doMove(m, si, true);
        oss << (0 != MoveList<GenType::LEGAL>(pos).size() ? '+' : '#');
        pos.undoMove(m);
    }

    return oss.str();
}
/// Converts a string representing a move in short algebraic notation
/// to the corresponding legal move, if any.
Move sanMove(const string &san, Position &pos)
{
    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
    {
        if (san == sanMove(vm, pos))
        {
            return vm;
        }
    }
    return MOVE_NONE;
}

///// Converts a move to a string in long algebraic notation.
//string lanMove(Move m, Position &pos)
//{
//    if (MOVE_NONE == m) return "(none)";
//    if (MOVE_NULL == m) return "(null)";
//    assert(MoveList<GenType::LEGAL>(pos).contains(m));
//    string lan;
//    return lan;
//}
///// Converts a string representing a move in long algebraic notation
///// to the corresponding legal move, if any.
//Move lanMove(const string &lan, Position &pos)
//{
//    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
//    {
//        if (lan == lanMove(vm, pos))
//        {
//            return vm;
//        }
//    }
//    return MOVE_NONE;
//}

/// multipvInfo() formats PV information according to UCI protocol.
/// UCI requires that all (if any) un-searched PV lines are sent using a previous search score.
string multipvInfo(const Thread *const &th, Depth depth, Value alfa, Value beta)
{
    auto elapsedTime = std::max(Threadpool.mainThread()->timeMgr.elapsedTime(), TimePoint(1));
    auto nodes = Threadpool.sum(&Thread::nodes);
    auto tbHits = Threadpool.sum(&Thread::tbHits);
    if (TBHasRoot)
    {
        tbHits += th->rootMoves.size();
    }

    ostringstream oss;
    for (u32 i = 0; i < Threadpool.pvCount; ++i)
    {
        bool updated = -VALUE_INFINITE != th->rootMoves[i].newValue;

        if (   !updated
            && 1 == depth)
        {
            continue;
        }

        Depth d = updated ?
                    depth :
                    depth - 1;
        auto v = updated ?
                    th->rootMoves[i].newValue :
                    th->rootMoves[i].oldValue;
        bool tb = TBHasRoot
               && abs(v) < +VALUE_MATE - 1*DEP_MAX;
        if (tb)
        {
            v = th->rootMoves[i].tbValue;
        }

        oss << "info"
            << " multipv "  << i + 1
            << " depth "    << d
            << " seldepth " << th->rootMoves[i].selDepth
            << " score "    << toString(v);
        if (!tb && i == th->pvCur)
        oss << (beta <= v ? " lowerbound" :
                v <= alfa ? " upperbound" : "");
        oss << " nodes "    << nodes
            << " time "     << elapsedTime
            << " nps "      << nodes * 1000 / elapsedTime
            << " tbhits "   << tbHits;
        // Hashfull after 1 sec
        if (elapsedTime > 1000)
        oss << " hashfull " << TT.hashFull();

        oss << " pv"        << th->rootMoves[i];
        if (i < Threadpool.pvCount - 1)
        {
            oss << "\n";
        }
    }
    return oss.str();
}
/*
/// Returns formated human-readable search information.
string pretty_pv_info(Thread *const &th)
{
    u64 nodes = Threadpool.sum(&Thread::nodes);

    ostringstream oss;
    oss << setw( 4) << th->finishedDepth
        << setw( 8) << pretty_value(th->rootMoves.front().newValue)
        << setw(12) << pretty_time(Threadpool.mainThread()->timeMgr.elapsedTime());

    if (nodes < 10ULL*1000)
        oss << setw(8) << u16(nodes);
    else
    if (nodes < 10ULL*1000*1000)
        oss << setw(7) << u16(std::round(nodes / 1000.0)) << "K";
    else
    if (nodes < 10ULL*1000*1000*1000)
        oss << setw(7) << u16(std::round(nodes / 1000.0*1000.0)) << "M";
    else
        oss << setw(7) << u16(std::round(nodes / 1000.0*1000.0*1000.0)) << "G";
    oss << " ";

    StateListPtr states{new deque<StateInfo>(0)};
    std::for_each(th->rootMoves.front().begin(),
                  th->rootMoves.front().end(),
                  [&](Move m)
                  {
                      assert(MOVE_NONE != m);
                      oss << sanMove(m, th->rootPos) << " ";
                      states->emplace_back();
                      th->rootPos.doMove(m, states->back());
                  });
    std::for_each(th->rootMoves.front().rbegin(),
                  th->rootMoves.front().rend(),
                  [&](Move m)
                  {
                      assert(MOVE_NONE != m);
                      th->rootPos.undoMove(m);
                      states->pop_back();
                  });

    return oss.str();
}
*/
