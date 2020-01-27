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
        assert(pos.pseudo_legal(m)
            && pos.legal(m));

        auto org = org_sq(m);
        auto dst = dst_sq(m);
        auto pt = ptype(pos[org]);
        // Disambiguation if have more then one piece with destination
        // note that for pawns is not needed because starting file is explicit.
        Bitboard piece = pos.attacks_from(pt, dst) & pos.pieces(pos.active, pt);

        Bitboard amb = piece ^ org;
        if (0 == amb)
        {
            return Ambiguity::AMB_NONE;
        }

        Bitboard pcs = amb;
                    // If pinned piece is considered as ambiguous
                    // & ~(pos.si->king_blockers[pos.active] & pos.pieces(pos.active));
        while (0 != pcs)
        {
            auto sq = pop_lsq(pcs);
            if (!pos.legal(make_move<NORMAL>(sq, dst)))
            {
                amb ^= sq;
            }
        }
        if (0 == (amb & file_bb(org))) return Ambiguity::AMB_RANK;
        if (0 == (amb & rank_bb(org))) return Ambiguity::AMB_FILE;
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
                << value_to_cp(v) / 100.0
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
string move_to_can(Move m)
{
    if (MOVE_NONE == m) return {"(none)"};
    if (MOVE_NULL == m) return {"(null)"};
    ostringstream oss;
    oss << to_string(org_sq(m))
        << to_string(fix_dst_sq(m, bool(Options["UCI_Chess960"])));
    if (PROMOTE == mtype(m))
    {
        oss << (BLACK|promote(m));
    }
    return oss.str();
}
/// Converts a string representing a move in coordinate algebraic notation
/// to the corresponding legal move, if any.
Move move_from_can(const string &can, const Position &pos)
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
        if (can == move_to_can(vm))
        {
            return vm;
        }
    }
    return MOVE_NONE;
}

/// Converts a move to a string in short algebraic notation.
string move_to_san(Move m, Position &pos)
{
    if (MOVE_NONE == m) return {"(none)"};
    if (MOVE_NULL == m) return {"(null)"};
    assert(MoveList<GenType::LEGAL>(pos).contains(m));

    ostringstream oss;
    auto org = org_sq(m);
    auto dst = dst_sq(m);

    if (CASTLE != mtype(m))
    {
        auto pt = ptype(pos[org]);
        if (PAWN != pt)
        {
            oss << (WHITE|pt);
            if (KING != pt)
            {
                // Disambiguation if have more then one piece of type 'pt' that can reach 'dst' with a legal move.
                switch (ambiguity(m, pos))
                {
                case Ambiguity::AMB_RANK: oss << to_char(_file(org)); break;
                case Ambiguity::AMB_FILE: oss << to_char(_rank(org)); break;
                case Ambiguity::AMB_SQUARE: oss << to_string(org);    break;
                case Ambiguity::AMB_NONE:
                default: break;
                }
            }
        }

        if (pos.capture(m))
        {
            if (PAWN == pt)
            {
                oss << to_char(_file(org));
            }
            oss << "x";
        }

        oss << to_string(dst);

        if (   PAWN == pt
            && PROMOTE == mtype(m))
        {
            oss << "=" << (WHITE|promote(m));
        }
    }
    else
    {
        oss << (dst > org ? "O-O" : "O-O-O");
    }

    // Move marker for check & checkmate
    if (pos.gives_check(m))
    {
        StateInfo si;
        pos.do_move(m, si, true);
        oss << (0 != MoveList<GenType::LEGAL>(pos).size() ? '+' : '#');
        pos.undo_move(m);
    }

    return oss.str();
}
/// Converts a string representing a move in short algebraic notation
/// to the corresponding legal move, if any.
Move move_from_san(const string &san, Position &pos)
{
    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
    {
        if (san == move_to_san(vm, pos))
        {
            return vm;
        }
    }
    return MOVE_NONE;
}

///// Converts a move to a string in long algebraic notation.
//string move_to_lan(Move m, Position &pos)
//{
//    if (MOVE_NONE == m) return "(none)";
//    if (MOVE_NULL == m) return "(null)";
//    assert(MoveList<GenType::LEGAL>(pos).contains(m));
//    string lan;
//    return lan;
//}
///// Converts a string representing a move in long algebraic notation
///// to the corresponding legal move, if any.
//Move move_from_lan(const string &lan, Position &pos)
//{
//    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
//    {
//        if (lan == move_to_lan(vm, pos))
//        {
//            return vm;
//        }
//    }
//    return MOVE_NONE;
//}

/// multipv_info() formats PV information according to UCI protocol.
/// UCI requires that all (if any) un-searched PV lines are sent using a previous search score.
string multipv_info(const Thread *const &th, Depth depth, Value alfa, Value beta)
{
    auto elapsed_time = std::max(Threadpool.main_thread()->time_mgr.elapsed_time(), TimePoint(1));
    auto &rms = th->root_moves;
    auto pv_cur = th->pv_cur;

    auto total_nodes = Threadpool.nodes();
    auto tb_hits = Threadpool.tb_hits();
    if (TBHasRoot)
    {
        tb_hits += rms.size();
    }

    ostringstream oss;
    for (u32 i = 0; i < Threadpool.pv_limit; ++i)
    {
        bool updated = -VALUE_INFINITE != rms[i].new_value;

        if (   !updated
            && 1 == depth)
        {
            continue;
        }

        Depth d = updated ?
                    depth :
                    depth - 1;
        auto v = updated ?
                    rms[i].new_value :
                    rms[i].old_value;
        bool tb = TBHasRoot
                && abs(v) < +VALUE_MATE - i32(DEP_MAX);
        if (tb)
        {
            v = rms[i].tb_value;
        }

        oss << "info"
            << " multipv " << i + 1
            << " depth " << d
            << " seldepth " << rms[i].sel_depth
            << " score " << to_string(v);
        if (   !tb
            && i == pv_cur)
        {
            oss << (beta <= v ? " lowerbound" : v <= alfa ? " upperbound" : "");
        }
        oss << " nodes " << total_nodes
            << " time " << elapsed_time
            << " nps " << total_nodes * 1000 / elapsed_time
            << " tbhits " << tb_hits;
        if (elapsed_time > 1000)
        {
            oss << " hashfull " << TT.hash_full();
        }
        oss << " pv" << rms[i];
        if (i < Threadpool.pv_limit - 1)
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
    u64 nodes = Threadpool.nodes();

    ostringstream oss;
    oss << setw( 4) << th->finished_depth
        << setw( 8) << pretty_value(th->root_moves.front().new_value)
        << setw(12) << pretty_time(Threadpool.main_thread()->time_mgr.elapsed_time());

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
    std::for_each(th->root_moves.front().begin(),
                  th->root_moves.front().end(),
                  [&](Move m)
                  {
                      assert(MOVE_NONE != m);
                      oss << move_to_san(m, th->root_pos) << " ";
                      states->emplace_back();
                      th->root_pos.do_move(m, states->back());
                  });
    std::for_each(th->root_moves.front().rbegin(),
                  th->root_moves.front().rend(),
                  [&](Move m)
                  {
                      assert(MOVE_NONE != m);
                      th->root_pos.undo_move(m);
                      states->pop_back();
                  });

    return oss.str();
}
*/
