#include "Notation.h"

#include <cmath>
#include <sstream>

#include "MoveGenerator.h"
#include "Thread.h"
#include "UCI.h"

std::string const PieceChar{ " PNBRQK  pnbrqk" };
std::string const ColorChar{ "wb" };

Color toColor(char c) noexcept {
    auto const pos{ ColorChar.find(c) };
    return pos != std::string::npos ? Color(pos) : COLORS;
}

std::string toString(Square s) noexcept {
    return{ toChar(sFile(s)), toChar(sRank(s)) };
}

char toChar(PieceType pt) noexcept {
    return PieceChar[pt];
}

Piece toPiece(char p) noexcept {
    auto const pos{ PieceChar.find(p) };
    return pos != std::string::npos ? Piece(pos) : NO_PIECE;
}
char toChar(Piece p) noexcept {
    return isOk(p) ? PieceChar[p] : '-';
}

/// Converts a value to a string suitable for use with the UCI protocol specifications:
///
/// cp   <x>   The score x from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies.
///            If the engine is getting mated use negative values for y.
std::string toString(Value v) {
    assert(-VALUE_MATE <= v && v <= +VALUE_MATE);

    std::ostringstream oss{};
    if (std::abs(v) < +VALUE_MATE_1_MAX_PLY) {
        oss << "cp " << i32(toCP(v));
    }
    else {
        oss << "mate " << i16(v > 0 ? +VALUE_MATE - v + 1 : -VALUE_MATE - v + 0) / 2;
    }
    return oss.str();
}
std::string toString(Score s) {
    std::ostringstream oss{};
    oss << std::showpos << std::showpoint
        //<< std::setw(6) << mgValue(s) << " "
        //<< std::setw(6) << egValue(s)
        << std::fixed << std::setprecision(2)
        << std::setw(6) << toCP(mgValue(s)) / 100 << " "
        << std::setw(6) << toCP(egValue(s)) / 100;
    return oss.str();
}

/// Converts a move to a string in coordinate algebraic notation.
/// The only special case is castling moves,
///  - e1g1 notation in normal chess mode,
///  - e1h1 notation in chess960 mode.
/// Internally castle moves are always coded as "king captures rook".
std::string moveToCAN(Move m) {
    if (m == MOVE_NONE) return { "(none)" };
    if (m == MOVE_NULL) return { "(null)" };

    std::ostringstream oss{};
    oss << orgSq(m)
        << ((mType(m) != CASTLE
          || Options["UCI_Chess960"]) ?
            dstSq(m) : kingCastleSq(orgSq(m), dstSq(m)));
    if (mType(m) == PROMOTE) {
        oss << (BLACK|promoteType(m));
    }
    return oss.str();
}
/// Converts a string representing a move in coordinate algebraic notation
/// to the corresponding legal move, if any.
Move moveOfCAN(std::string_view can, Position const &pos) {
    //// If promotion piece in uppercase, convert to lowercase
    //if (can.size() == 5 && isupper(can[4])) {
    //    can[4] = char(tolower(can[4]));
    //}
    assert(can.size() < 5 || islower(can[4]));
    for (auto const &vm : MoveList<LEGAL>(pos)) {
        if (moveToCAN(vm) == can) {
            return vm;
        }
    }
    return MOVE_NONE;
}


std::ostream& operator<<(std::ostream &ostream, Color c) {
    ostream << toChar(c);
    return ostream;
}
std::ostream& operator<<(std::ostream &ostream, File f) {
    ostream << toChar(f);
    return ostream;
}
std::ostream& operator<<(std::ostream &ostream, Rank r) {
    ostream << toChar(r);
    return ostream;
}
std::ostream& operator<<(std::ostream &ostream, Square s) {
    ostream << toString(s);
    return ostream;
}
std::ostream& operator<<(std::ostream &ostream, Piece p) {
    ostream << toChar(p);
    return ostream;
}
std::ostream& operator<<(std::ostream &ostream, Value v) {
    ostream << toString(v);
    return ostream;
}
std::ostream& operator<<(std::ostream &ostream, Score s) {
    ostream << toString(s);
    return ostream;
}
std::ostream& operator<<(std::ostream &ostream, Move m) {
    ostream << moveToCAN(m);
    return ostream;
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

        auto const org{ orgSq(m) };
        auto const dst{ dstSq(m) };
        auto const pt{ pType(pos[org]) };
        // Disambiguation if have more then one piece with destination
        // note that for pawns is not needed because starting file is explicit.
        Bitboard const piece{
            attacksBB(pt, dst, pos.pieces())
          & pos.pieces(pos.activeSide(), pt) };

        Bitboard amb{ piece ^ org };
        if (amb == 0) {
            return AMB_NONE;
        }

        Bitboard pcs{ amb };
                    // If pinned piece is considered as ambiguous
                    //& ~pos.kingBlockers(pos.activeSide()) };
        while (pcs != 0) {
            auto const sq{ popLSq(pcs) };
            auto const move{ makeMove<SIMPLE>(sq, dst) };
            if (!(pos.pseudoLegal(move)
               && pos.legal(move))) {
                amb ^= sq;
            }
        }
        if ((amb & fileBB(org)) == 0) return AMB_RANK;
        if ((amb & rankBB(org)) == 0) return AMB_FILE;
        return AMB_SQUARE;
    }

    /*
    string prettyValue(Value v) {
        assert(-VALUE_MATE <= v && v <= +VALUE_MATE);
        std::ostringstream oss{};
        if (std::abs(v) < +VALUE_MATE_1_MAX_PLY) {
            oss << std::showpos << std::fixed << std::setprecision(2)
                << toCP(v) / 100;
        }
        else {
            oss << std::showpos
                << "#" << i16(v > VALUE_ZERO ?
                            +VALUE_MATE - v + 1 :
                            -VALUE_MATE - v + 0) / 2;
        }
        return oss.str();
    }

    constexpr u32 SecondMilliSec{ 1000 };
    constexpr u32 MinuteMilliSec{ 60*SecondMilliSec };
    constexpr u32 HourMilliSec  { 60*MinuteMilliSec };
    string prettyTime(u64 time) {
        u32 hours  = u32(time / HourMilliSec);
        time      %= HourMilliSec;
        u32 minutes= u32(time / MinuteMilliSec);
        time      %= MinuteMilliSec;
        u32 seconds= u32(time / SecondMilliSec);
        time      %= SecondMilliSec;
        time      /= 10;

        std::ostringstream oss{};
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
std::string moveToSAN(Move m, Position &pos) {
    if (m == MOVE_NONE) return { "(none)" };
    if (m == MOVE_NULL) return { "(null)" };
    assert(MoveList<LEGAL>(pos).contains(m));

    std::ostringstream oss{};
    auto const org{ orgSq(m) };
    auto const dst{ dstSq(m) };

    if (mType(m) != CASTLE) {
        auto const pt = pType(pos[org]);
        if (pt != PAWN) {
            oss << (WHITE|pt);
            if (pt != KING) {
                // Disambiguation if have more then one piece of type 'pt' that can reach 'dst' with a legal move.
                auto const amb{ ambiguity(m, pos) };
                amb == AMB_RANK   ? oss << sFile(org) :
                amb == AMB_FILE   ? oss << sRank(org) :
                amb == AMB_SQUARE ? oss << org : oss << "";
            }
        }

        if (pos.capture(m)) {
            if (pt == PAWN) {
                oss << sFile(org);
            }
            oss << "x";
        }

        oss << dst;

        if (pt == PAWN
         && mType(m) == PROMOTE) {
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
        oss << (MoveList<LEGAL>(pos).size() != 0 ? "+" : "#");
        pos.undoMove(m);
    }

    return oss.str();
}
/// Converts a string representing a move in short algebraic notation
/// to the corresponding legal move, if any.
Move moveOfSAN(std::string_view san, Position &pos) {
    for (auto const &vm : MoveList<LEGAL>(pos)) {
        if (moveToSAN(vm, pos) == san) {
            return vm;
        }
    }
    return MOVE_NONE;
}

/*
/// Returns formated human-readable search information.
string prettyInfo(Thread *th) {
    u64 nodes{ Threadpool.accumulate(&Thread::nodes) };

    std::ostringstream oss{};
    oss << std::setw( 4) << th->finishedDepth
        << std::setw( 8) << prettyValue(th->rootMoves[0].newValue)
        << std::setw(12) << prettyTime(TimeMgr.elapsed());

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

    StateListPtr states{ new StateList{ 0 } };
    std::for_each(th->rootMoves[0].begin(),
                  th->rootMoves[0].end(),
                  [&](Move m) {
                      assert(m != MOVE_NONE);
                      oss << moveToSAN(m, th->rootPos) << " ";
                      states->emplace_back();
                      th->rootPos.doMove(m, states->back());
                  });
    std::for_each(th->rootMoves[0].rbegin(),
                  th->rootMoves[0].rend(),
                  [&](Move m) {
                      assert(m != MOVE_NONE);
                      th->rootPos.undoMove(m);
                      states->pop_back();
                  });

    return oss.str();
}
*/
