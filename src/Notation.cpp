#include "Notation.h"

#include <cmath>
#include <sstream>

#include "MoveGenerator.h"
#include "Searcher.h"
#include "TimeManager.h"
#include "Transposition.h"
#include "Thread.h"
#include "UCI.h"

using std::string;
using std::ostream;

string const PieceChar{ " PNBRQK  pnbrqk" };
string const ColorChar{ "wb" };

Color toColor(char c) {
    auto pos{ ColorChar.find(c) };
    return pos != string::npos ? Color(pos) : COLOR_NONE;
}
char toChar(Color c) {
    return isOk(c) ? ColorChar[c] : '-';
}

File toFile(char f) {
    return File(f - 'a');
}
char toChar(File f, bool lower) {
    return char(f + 'A' + 0x20 * lower);
}

Rank toRank(char r) {
    return Rank(r - '1');
}
char toChar(Rank r) {
    return char(r + '1');
}

string toString(Square s) {
    return{ toChar(sFile(s)), toChar(sRank(s)) };
}

Piece toPiece(char p) {
    auto pos{ PieceChar.find(p) };
    return pos != string::npos ? Piece(pos) : NO_PIECE;
}
char toChar(Piece p) {
    return isOk(p) ? PieceChar[p] : '-';
}

/// Converts a value to a string suitable for use with the UCI protocol specifications:
///
/// cp   <x>   The score x from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies.
///            If the engine is getting mated use negative values for y.
string toString(Value v) {
    assert(-VALUE_MATE <= v && v <= +VALUE_MATE);

    std::ostringstream oss;
    if (abs(v) < +VALUE_MATE_1_MAX_PLY) {
        oss << "cp " << toCP(v);
    }
    else {
        oss << "mate " << i16(v > 0 ?
                            +VALUE_MATE - v + 1 :
                            -VALUE_MATE - v + 0) / 2;
    }
    return oss.str();
}
string toString(Score s) {
    std::ostringstream oss;
    oss << std::showpos << std::showpoint
        //<< std::setw(5) << mgValue(s) << " "
        //<< std::setw(5) << egValue(s)
        << std::fixed << std::setprecision(2)
        << std::setw(5) << toCP(mgValue(s)) / 100.0 << " "
        << std::setw(5) << toCP(egValue(s)) / 100.0;
    return oss.str();
}

/// Converts a move to a string in coordinate algebraic notation.
/// The only special case is castling moves,
///  - e1g1 notation in normal chess mode,
///  - e1h1 notation in chess960 mode.
/// Internally castle moves are always coded as "king captures rook".
string moveToCAN(Move m) {
    if (MOVE_NONE == m) return { "(none)" };
    if (MOVE_NULL == m) return { "(null)" };

    std::ostringstream oss;
    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    if (CASTLE == mType(m)
     && !Options["UCI_Chess960"]) {
        assert(sRank(org) == sRank(dst));
        dst = makeSquare(dst > org ? FILE_G : FILE_C, sRank(org));
    }

    oss << org << dst;
    if (PROMOTE == mType(m)) {
        oss << (BLACK|promoteType(m));
    }
    return oss.str();
}
/// Converts a string representing a move in coordinate algebraic notation
/// to the corresponding legal move, if any.
Move moveOfCAN(string const &can, Position const &pos) {
    //// If promotion piece in uppercase, convert to lowercase
    //if (5 == can.size()
    // && isupper(can[4])) {
    //    can[4] = char(tolower(can[4]));
    //}
    assert(5 > can.size()
        || islower(can[4]));
    for (auto const &vm : MoveList<GenType::LEGAL>(pos)) {
        if (can == moveToCAN(vm)) {
            return vm;
        }
    }
    return MOVE_NONE;
}


ostream& operator<<(ostream &os, Color c) {
    os << toChar(c);
    return os;
}
ostream& operator<<(ostream &os, File f) {
    os << toChar(f);
    return os;
}
ostream& operator<<(ostream &os, Rank r) {
    os << toChar(r);
    return os;
}
ostream& operator<<(ostream &os, Square s) {
    os << toString(s);
    return os;
}
ostream& operator<<(ostream &os, Piece p) {
    os << toChar(p);
    return os;
}
ostream& operator<<(ostream &os, Value v) {
    os << toString(v);
    return os;
}
ostream& operator<<(ostream &os, Score s) {
    os << toString(s);
    return os;
}
ostream& operator<<(ostream &os, Move m) {
    os << moveToCAN(m);
    return os;
}


/// multipvInfo() formats PV information according to UCI protocol.
/// UCI requires that all (if any) un-searched PV lines are sent using a previous search score.
string multipvInfo(Thread const *const &th, Depth depth, Value alfa, Value beta) {
    auto elapsed{ TimeMgr.elapsed() + 1 };
    auto nodes{ Threadpool.sum(&Thread::nodes) };
    auto tbHits{ Threadpool.sum(&Thread::tbHits)
               + th->rootMoves.size() * TBHasRoot };

    std::ostringstream oss;
    for (u32 i = 0; i < PVCount; ++i)
    {
        bool updated{ -VALUE_INFINITE != th->rootMoves[i].newValue };
        if (1 == depth
         && !updated) {
            continue;
        }

        auto d{ updated ?
                    depth :
                    Depth(depth - 1) };
        auto v{ updated ?
                    th->rootMoves[i].newValue :
                    th->rootMoves[i].oldValue };

        bool tb{ TBHasRoot
              && abs(v) < +VALUE_MATE_1_MAX_PLY };
        v = tb ? th->rootMoves[i].tbValue : v;

        //if (oss.rdbuf()->in_avail()) // Not at first line
        //    oss << "\n";
        oss << "info"
            << " depth "    << d
            << " seldepth " << th->rootMoves[i].selDepth
            << " multipv "  << i + 1
            << " score "    << v;
        if (!tb && i == th->pvCur) {
        oss << (beta <= v ? " lowerbound" :
                v <= alfa ? " upperbound" : "");
        }
        oss << " nodes "    << nodes
            << " time "     << elapsed
            << " nps "      << nodes * 1000 / elapsed
            << " tbhits "   << tbHits;
        // Hashfull after 1 sec
        if (1000 < elapsed) {
        oss << " hashfull " << TT.hashFull();
        }
        oss << " pv"        << th->rootMoves[i];
        if (i < PVCount - 1) {
            oss << "\n";
        }
    }
    return oss.str();
}


namespace {

    /// Ambiguity
    enum Ambiguity : u08 {
        AMB_NONE,
        AMB_RANK,
        AMB_FILE,
        AMB_SQUARE,
    };

    /// Ambiguity if more then one piece of same type can reach 'dst' with a legal move.
    /// NOTE: for pawns it is not needed because 'org' file is explicit.
    Ambiguity ambiguity(Move m, Position const &pos) {
        assert(pos.pseudoLegal(m)
            && pos.legal(m));

        auto org{ orgSq(m) };
        auto dst{ dstSq(m) };
        auto pt{ pType(pos[org]) };
        // Disambiguation if have more then one piece with destination
        // note that for pawns is not needed because starting file is explicit.
        Bitboard piece{ pos.attacksFrom(pt, dst)
                      & pos.pieces(pos.active, pt) };

        Bitboard amb{ piece ^ org };
        if (0 == amb) {
            return AMB_NONE;
        }

        Bitboard pcs{ amb };
                    // If pinned piece is considered as ambiguous
                    //& ~pos.kingBlockers(pos.active) };
        while (0 != pcs) {
            auto sq{ popLSq(pcs) };
            auto move{ makeMove<NORMAL>(sq, dst) };
            if (!(pos.pseudoLegal(move)
               && pos.legal(move))) {
                amb ^= sq;
            }
        }
        if (0 == (amb & fileBB(org))) return AMB_RANK;
        if (0 == (amb & rankBB(org))) return AMB_FILE;
        return AMB_SQUARE;
    }

    /*
    string pretty_value(Value v) {
        assert(-VALUE_MATE <= v && v <= +VALUE_MATE);
        std::ostringstream oss;
        if (abs(v) < +VALUE_MATE_1_MAX_PLY) {
            oss << std::showpos << std::fixed << std::setprecision(2)
                << toCP(v) / 100.0;
        }
        else {
            oss << std::showpos
                << "#" << i16(v > VALUE_ZERO ?
                            +VALUE_MATE - v + 1 :
                            -VALUE_MATE - v + 0) / 2;
        }
        return oss.str();
    }

    constexpr u32 SecondMilliSec = 1000;
    constexpr u32 MinuteMilliSec = 60*SecondMilliSec;
    constexpr u32 HourMilliSec   = 60*MinuteMilliSec;
    string pretty_time(u64 time) {
        u32 hours  = u32(time / HourMilliSec);
        time      %= HourMilliSec;
        u32 minutes= u32(time / MinuteMilliSec);
        time      %= MinuteMilliSec;
        u32 seconds= u32(time / SecondMilliSec);
        time      %= SecondMilliSec;
        time      /= 10;

        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(2) << hours   << ":"
            << std::setw(2) << minutes << ":"
            << std::setw(2) << seconds << "."
            << std::setw(2) << time
            << std::setfill(' ');
        return oss.str();
    }
    */
}

/// Converts a move to a string in short algebraic notation.
string moveToSAN(Move m, Position &pos) {
    if (MOVE_NONE == m) return { "(none)" };
    if (MOVE_NULL == m) return { "(null)" };
    assert(MoveList<GenType::LEGAL>(pos).contains(m));

    std::ostringstream oss;
    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };

    if (CASTLE != mType(m)) {
        auto pt = pType(pos[org]);
        if (PAWN != pt) {
            oss << (WHITE|pt);
            if (KING != pt) {
                // Disambiguation if have more then one piece of type 'pt' that can reach 'dst' with a legal move.
                switch (ambiguity(m, pos)) {
                case AMB_RANK:   oss << sFile(org); break;
                case AMB_FILE:   oss << sRank(org); break;
                case AMB_SQUARE: oss << org;        break;
                case AMB_NONE: default:             break;
                }
            }
        }

        if (pos.capture(m)) {
            if (PAWN == pt) {
                oss << sFile(org);
            }
            oss << "x";
        }

        oss << dst;

        if (PAWN == pt
         && PROMOTE == mType(m)) {
            oss << "=" << (WHITE|promoteType(m));
        }
    }
    else {
        oss << (dst > org ? "O-O" : "O-O-O");
    }

    // Move marker for check & checkmate
    if (pos.giveCheck(m)) {
        StateInfo si;
        pos.doMove(m, si, true);
        oss << (0 != MoveList<GenType::LEGAL>(pos).size() ? "+" : "#");
        pos.undoMove(m);
    }

    return oss.str();
}
/// Converts a string representing a move in short algebraic notation
/// to the corresponding legal move, if any.
Move moveOfSAN(string const &san, Position &pos) {
    for (auto const &vm : MoveList<GenType::LEGAL>(pos)) {
        if (san == moveToSAN(vm, pos)) {
            return vm;
        }
    }
    return MOVE_NONE;
}

/*
/// Returns formated human-readable search information.
string prettyInfo(Thread *const &th) {
    u64 nodes{ Threadpool.sum(&Thread::nodes) };

    std::ostringstream oss;
    oss << std::setw( 4) << th->finishedDepth
        << std::setw( 8) << pretty_value(th->rootMoves.front().newValue)
        << std::setw(12) << pretty_time(TimeMgr.elapsed());

         if (nodes < 10ULL*1000) {
        oss << std::setw(8) << u16(nodes);
    }
    else if (nodes < 10ULL*1000*1000) {
        oss << std::setw(7) << u16(std::round(nodes / 1000.0)) << "K";
    }
    else if (nodes < 10ULL*1000*1000*1000) {
        oss << std::setw(7) << u16(std::round(nodes / 1000.0*1000.0)) << "M";
    }
    else {
        oss << std::setw(7) << u16(std::round(nodes / 1000.0*1000.0*1000.0)) << "G";
    }
    oss << " ";

    StateListPtr states{ new deque<StateInfo>(0) };
    std::for_each(th->rootMoves.front().begin(),
                  th->rootMoves.front().end(),
                  [&](Move m) {
                      assert(MOVE_NONE != m);
                      oss << moveToSAN(m, th->rootPos) << " ";
                      states->emplace_back();
                      th->rootPos.doMove(m, states->back());
                  });
    std::for_each(th->rootMoves.front().rbegin(),
                  th->rootMoves.front().rend(),
                  [&](Move m) {
                      assert(MOVE_NONE != m);
                      th->rootPos.undoMove(m);
                      states->pop_back();
                  });

    return oss.str();
}
*/
