/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef POSITION_H_INCLUDED
#define POSITION_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(USE_AVX512ICL)
    #include <immintrin.h>
#endif

#include "attacks.h"
#include "bitboard.h"
#include "misc.h"
#include "types.h"

namespace DON {

// Zobrist - Hash key generator
//
// This class provides access to precomputed Zobrist keys for all relevant
// aspects of a chess position. It is used to efficiently compute a unique
// hash for a given board state, which is essential for transposition tables,
// move ordering, repetition detection, and other chess engine optimizations.
//
// Key features:
//  - Provides keys for piece positions, castling rights, en passant squares,
//    turn (side to move), and the 50-move rule counter (MR50).
//  - All members and functions are static; no instances of this class can
//    be created, copied, or moved.
//  - Designed for fast access; all keys are stored in statically allocated arrays.
//
// Interface summary:
//  - init() - Initializes all Zobrist keys; must be called before use.
//  - piece_square(Color, PieceType, Square) / piece_square(Piece, Square)
//      Returns the Zobrist key for a piece on a specific square.
//  - castling(CastlingRights) - Returns the Zobrist key for a given castling right.
//  - enpassant(Square) - Returns the Zobrist key for an en passant square.
//  - turn() - Returns the Zobrist key for the side to move.
//  - mr50(int) - Returns the Zobrist key for the 50-move rule counter.
//
// Notes:
//  - The class is static-only; it cannot be instantiated. (Restriction)
//  - All data is stored in 'inline-static' arrays for fast, constant-time access.
//  - Ensure 'init()' is called before using any other function.
//  - Accessors perform debug-time assertions to validate input values.
//  - MR50 array handles the 50-move rule with offset and factor constants.
//
// Example usage:
//   Zobrist::init();
//   Key key = Zobrist::piece_square(Piece::WHITE_KNIGHT, Square::G1);
//   key ^= Zobrist::turn();
struct Zobrist final {
   public:
    static void init() noexcept;

    static Key piece_square(Color c, PieceType pt, Square s) noexcept {
        assert(is_ok(c) && is_ok(pt) && is_ok(s));

        return PieceSquare[c][pt][s];
    }
    static Key piece_square(Piece pc, Square s) noexcept {
        assert(is_ok(pc) && is_ok(s));

        return piece_square(color_of(pc), type_of(pc), s);
    }

    static Key castling(CastlingRights cr) noexcept {
        assert(+cr < Castling.size());

        return Castling[+cr];
    }

    static Key enpassant(Square enPassantSq) noexcept {
        return int(enPassantSq != SQ_NONE) * Enpassant[file_of(enPassantSq)];
    }

    static Key turn() noexcept { return Turn; }

    static Key mr50(i16 rule50Count) noexcept {
        return int(rule50Count >= R50_OFFSET)
             * MR50[std::clamp((rule50Count - R50_OFFSET) / R50_FACTOR, 0, int(MR50.size()) - 1)];
    }

    static constexpr usize PAWN_OFFSET = 8;

   private:
    Zobrist() noexcept                          = delete;
    ~Zobrist() noexcept                         = delete;
    Zobrist(const Zobrist&) noexcept            = delete;
    Zobrist& operator=(const Zobrist&) noexcept = delete;
    Zobrist(Zobrist&&) noexcept                 = delete;
    Zobrist& operator=(Zobrist&&) noexcept      = delete;

    static inline Array<Key, COLOR_NB, 1 + PIECE_TYPE_CNT, SQUARE_NB> PieceSquare;
    static inline Array<Key, CASTLING_RIGHTS_NB>                      Castling;
    static inline Array<Key, FILE_NB>                                 Enpassant;
    static inline Key                                                 Turn;

    static constexpr u8 R50_OFFSET = 14;
    static constexpr u8 R50_FACTOR = 8;

    static inline Array<Key, (PLY_MAX + 1 - R50_OFFSET) / R50_FACTOR + 2> MR50;
};

// State struct stores information needed to restore Position object
// to its previous state when retract any move. (Size = 256)
struct State final {
   public:
    State() noexcept                           = default;
    State(const State&) noexcept               = default;
    State& operator=(const State& st) noexcept = default;
    State(State&&) noexcept                    = delete;
    State& operator=(State&&) noexcept         = delete;

    void clear() noexcept;

    void dump(std::ostream& os = std::cout) const noexcept;

    // --- Copied when making a move
    Key                     key;
    Array<Key, COLOR_NB>    pawnKeys;
    Array<Key, COLOR_NB, 2> nonPawnKeys;
    Array<bool, COLOR_NB>   hasCastleds;

    u16            rule50Count;
    u16            nullPly;  // Plies from Null-Move
    Square         enPassantSq;
    Square         capturedSq;
    CastlingRights castlingRights;
    bool           hasRule50High;

    // --- Not copied when making a move (will be recomputed anyhow)
    Bitboard                       checkersBB;
    Array<Bitboard, COLOR_NB>      pinnersBB;
    Array<Bitboard, COLOR_NB>      blockersBB;
    Array<Bitboard, PIECE_TYPE_NB> checksBB;
    Array<Bitboard, PIECE_TYPE_NB> accAttacksBB;
    i16                            repetition;
    Piece                          capturedPc;
    Piece                          promotedPc;
    const State*                   preSt;

    // Copy relevant fields from the state.
    // excluding those that will recomputed from scratch anyway and
    // then switch the state pointer to point to the new state.
    template<typename T = Bitboard>
    void switch_to_prefix(const State* st, T State::* member = &State::checkersBB) noexcept {
        // Compute offset dynamically for this object
        usize size = reinterpret_cast<const char*>(&(st->*member))  //
                   - reinterpret_cast<const char*>(st);

        assert(size <= sizeof(*this) && "size exceeds object size");

        std::memcpy(this, st, size);

        preSt = st;
    }
};

static_assert(std::is_standard_layout_v<State> && std::is_trivially_copyable_v<State>,
              "State must be standard-layout and trivially copyable");
//static_assert(sizeof(State) == 256, "State size must be 256 bytes");

class Worker;

// Position class stores information regarding the board representation as
// pieces, active color, hash keys, castling info, etc. (Size = 248)
// Important methods are do_move() and undo_move(),
// used by the search to update node info when traversing the search tree.
class Position final {
   public:
    static void init() noexcept;

    Position() noexcept                           = default;
    Position& operator=(const Position&) noexcept = default;

   private:
    Position(const Position&) noexcept       = delete;
    Position(Position&&) noexcept            = delete;
    Position& operator=(Position&&) noexcept = delete;

   public:
    void clear() noexcept;

    // FEN string input/output
    void                      set(std::string_view fens, State* newSt) noexcept;
    void                      set(std::string_view code, Color c, State* newSt) noexcept;
    void                      set(const Position& pos, State* newSt) noexcept;
    [[nodiscard]] std::string fen(bool complete = true) const noexcept;

    // Position representation
    [[nodiscard]] const auto& piece_map() const noexcept;
    [[nodiscard]] const auto& type_bbs() const noexcept;
    [[nodiscard]] const auto& color_bbs() const noexcept;

    [[nodiscard]] Piece    operator[](Square s) const noexcept;
    [[nodiscard]] Bitboard operator[](PieceType pt) const noexcept;
    [[nodiscard]] Bitboard operator[](Color c) const noexcept;

    [[nodiscard]] Piece piece(Square s) const noexcept;
    [[nodiscard]] bool  empty(Square s) const noexcept;

    template<typename... PieceTypes>
    [[nodiscard]] Bitboard pieces_bb(PieceTypes... pts) const noexcept;
    [[nodiscard]] Bitboard pieces_bb(Color c) const noexcept;
    template<typename... PieceTypes>
    [[nodiscard]] Bitboard pieces_bb(Color c, PieceTypes... pts) const noexcept;
    [[nodiscard]] Bitboard pieces_bb(Piece pc) const noexcept;
    [[nodiscard]] Bitboard pieces_bb() const noexcept;

    template<typename... PieceTypes>
    [[nodiscard]] u8 count(PieceTypes... pts) const noexcept;
    [[nodiscard]] u8 count(Color c) const noexcept;
    template<typename... PieceTypes>
    [[nodiscard]] u8 count(Color c, PieceTypes... pts) const noexcept;
    [[nodiscard]] u8 count(Piece pc) const noexcept;
    [[nodiscard]] u8 count() const noexcept;

    template<PieceType PT>
    [[nodiscard]] Square square(Color c) const noexcept;

    [[nodiscard]] Square en_passant_sq() const noexcept;
    [[nodiscard]] Square captured_sq() const noexcept;

    [[nodiscard]] u16   ply() const noexcept;
    [[nodiscard]] Color active_color() const noexcept;
    [[nodiscard]] i32   move_num() const noexcept;

    [[nodiscard]] CastlingRights castling_rights_mask(Square s) const noexcept;
    [[nodiscard]] CastlingRights castling_rights_mask(Square orgSq, Square dstSq) const noexcept;

    [[nodiscard]] CastlingRights castling_rights() const noexcept;

    [[nodiscard]] bool   has_castling_rights() const noexcept;
    [[nodiscard]] bool   has_castling_rights(Color c, CastlingSide cs) const noexcept;
    [[nodiscard]] bool   castling_full_path_clear(Color c, CastlingSide cs) const noexcept;
    [[nodiscard]] bool   castling_king_path_clear(Color c, CastlingSide cs) const noexcept;
    [[nodiscard]] Square castling_rook_sq(Color c, CastlingSide cs) const noexcept;
    [[nodiscard]] bool   castling_possible(Color c, CastlingSide cs) const noexcept;

    [[nodiscard]] Bitboard xslide_attackers_bb(Square s) const noexcept;
    [[nodiscard]] Bitboard slide_attackers_bb(Square s, Bitboard occupancyBB) const noexcept;
    [[nodiscard]] Bitboard slide_attackers_bb(Square s) const noexcept;
    [[nodiscard]] Bitboard attackers_bb(Square s, Bitboard occupancyBB) const noexcept;
    [[nodiscard]] Bitboard attackers_bb(Square s) const noexcept;

    [[nodiscard]] bool
    slide_attackers_exists(Square s, Bitboard attackersBB, Bitboard occupancyBB) const noexcept;
    [[nodiscard]] bool slide_attackers_exists(Square s, Bitboard attackersBB) const noexcept;
    [[nodiscard]] bool
    attackers_exists(Square s, Bitboard attackersBB, Bitboard occupancyBB) const noexcept;
    [[nodiscard]] bool attackers_exists(Square s, Bitboard attackersBB) const noexcept;

    [[nodiscard]] Bitboard blockers_bb(Square    s,
                                       Bitboard  attackersBB,
                                       Bitboard& ownPinnersBB,
                                       Bitboard& oppPinnersBB) const noexcept;

    // Attacks from a piece type
    template<PieceType PT>
    [[nodiscard]] Bitboard attacks_by_bb(Color c) const noexcept;

    // Doing and undoing moves
    DirtyBoard
    do_move(Move m, State& newSt, bool mayCheck = true, const Worker* worker = nullptr) noexcept;
    void undo_move(Move m) noexcept;
    void do_null_move(State& newSt) noexcept;
    void undo_null_move() noexcept;

    // Properties of moves
    [[nodiscard]] bool  legal(Move m) const noexcept;
    [[nodiscard]] bool  capture(Move m) const noexcept;
    [[nodiscard]] bool  capture_promo(Move m) const noexcept;
    [[nodiscard]] bool  check(Move m) const noexcept;
    [[nodiscard]] bool  dbl_check(Move m) const noexcept;
    [[nodiscard]] bool  fork(Move m) const noexcept;
    [[nodiscard]] Piece moved_pc(Move m) const noexcept;
    [[nodiscard]] Piece captured_pc(Move m) const noexcept;
    [[nodiscard]] auto  captured_pt(Move m) const noexcept;

    [[nodiscard]] Bitboard checkers_bb() const noexcept;
    [[nodiscard]] Bitboard checks_bb(PieceType pt) const noexcept;
    [[nodiscard]] Bitboard pinners_bb(Color c) const noexcept;
    [[nodiscard]] Bitboard pinners_bb() const noexcept;
    [[nodiscard]] Bitboard blockers_bb(Color c) const noexcept;
    [[nodiscard]] Bitboard blockers_bb() const noexcept;

    [[nodiscard]] Bitboard acc_attacks_bb() const noexcept;
    template<PieceType PT>
    [[nodiscard]] Bitboard acc_attacks_bb() const noexcept;
    [[nodiscard]] Bitboard acc_less_attacks_bb(PieceType pt) const noexcept;
    [[nodiscard]] Bitboard threats_bb() const noexcept;

    // Hash keys
    [[nodiscard]] Key raw_key() const noexcept;
    [[nodiscard]] Key key() const noexcept;
    [[nodiscard]] Key pawn_key(Color c) const noexcept;
    [[nodiscard]] Key pawn_key() const noexcept;
    [[nodiscard]] Key minor_key(Color c) const noexcept;
    [[nodiscard]] Key minor_key() const noexcept;
    [[nodiscard]] Key major_key(Color c) const noexcept;
    [[nodiscard]] Key major_key() const noexcept;
    [[nodiscard]] Key non_pawn_key(Color c) const noexcept;
    [[nodiscard]] Key non_pawn_key() const noexcept;

    [[nodiscard]] Key material_key() const noexcept;
    [[nodiscard]] Key move_key(Move m) const noexcept;

    [[nodiscard]] bool  has_non_pawn(Color c) const noexcept;
    [[nodiscard]] Value non_pawn_value(Color c) const noexcept;
    [[nodiscard]] Value non_pawn_value() const noexcept;

    // Other properties
    [[nodiscard]] u16 rule50_count() const noexcept;
    [[nodiscard]] u16 null_ply() const noexcept;
    [[nodiscard]] i16 repetition() const noexcept;

    [[nodiscard]] bool  has_castled(Color c) const noexcept;
    [[nodiscard]] bool  has_rule50_high() const noexcept;
    [[nodiscard]] Piece captured_pc() const noexcept;
    [[nodiscard]] Piece promoted_pc() const noexcept;
    [[nodiscard]] bool  bishop_paired(Color c) const noexcept;
    [[nodiscard]] bool  bishop_opposite() const noexcept;
    [[nodiscard]] bool  dtz_is_dtm() const noexcept;

    [[nodiscard]] usize bucket() const noexcept;

    [[nodiscard]] int   std_material() const noexcept;
    [[nodiscard]] Value material() const noexcept;

    // Static Exchange Evaluation:
    [[nodiscard]] auto see(Move m) const noexcept { return SEE(*this, m); }

    [[nodiscard]] bool is_repetition(i16 ply) const noexcept;
    [[nodiscard]] bool
    is_draw(i16 ply, bool useRule50 = true, bool useStalemate = false) const noexcept;
    [[nodiscard]] bool has_repeated() const noexcept;
    [[nodiscard]] bool is_upcoming_repetition(i16 ply) const noexcept;

    void  put(Square s, Piece pc, DirtyThreats* dts = nullptr) noexcept;
    Piece remove(Square s, DirtyThreats* dts = nullptr) noexcept;

    void flip() noexcept;
    void mirror() noexcept;

    // Position consistency check, for debugging
#if !defined(NDEBUG)
    [[nodiscard]] Key compute_key() const noexcept;
    [[nodiscard]] Key compute_minor_key() const noexcept;
    [[nodiscard]] Key compute_major_key() const noexcept;
    [[nodiscard]] Key compute_non_pawn_key() const noexcept;

    [[nodiscard]] bool _is_ok() const noexcept;
#endif

    // Used by NNUE
    [[nodiscard]] constexpr State* state() const noexcept;

    operator std::string() const noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept;

    void dump(std::ostream& os = std::cout) const noexcept;

    static inline bool Chess960 = false;

    static inline u16 DrawMoveCount = 50;

   private:
    struct Castlings final {
       public:
        Array<Bitboard, COLOR_NB, CASTLING_SIDE_NB> fullPathBB;
        Array<Bitboard, COLOR_NB, CASTLING_SIDE_NB> kingPathBB;
        Array<Square, COLOR_NB, CASTLING_SIDE_NB>   rookSq;
    };

    // SEE struct used to get a nice syntax for SEE comparisons.
    // Never use this type directly or store a value into a variable of this type,
    // instead use the syntax "pos.see(move) >= threshold" and similar for other comparisons.
    struct SEE final {
       public:
        constexpr SEE(const Position& p, Move m) noexcept :
            pos(p),
            move(m) {}

        [[nodiscard]] bool operator>=(int threshold) const noexcept;
        [[nodiscard]] bool operator>(int threshold) const noexcept;
        [[nodiscard]] bool operator<=(int threshold) const noexcept;
        [[nodiscard]] bool operator<(int threshold) const noexcept;

       private:
        SEE() noexcept                      = delete;
        SEE(const SEE&) noexcept            = delete;
        SEE& operator=(const SEE&) noexcept = delete;
        SEE(SEE&&) noexcept                 = delete;
        SEE& operator=(SEE&&) noexcept      = delete;

        const Position& pos;
        Move            move;
    };

    // Initialization helpers (used while setting up a position)
    void set_castling_rights(Color c, Square rookOrgSq) noexcept;
    void set_state() noexcept;
    void set_pinner_blocker() noexcept;
    void set_ext_state() noexcept;

    [[nodiscard]] bool see_ge(Move m, int threshold) const noexcept;

    template<bool MoveDone = true>
    bool
    enpassant_possible(Color ac, Square enPassantSq, Bitboard* epPawnsBBp = nullptr) const noexcept;

    // Other helpers
    Piece move(Square s1, Square s2, DirtyThreats* dts = nullptr) noexcept;
    Piece swap(Square s, Piece newPc, DirtyThreats* dts = nullptr) noexcept;

    template<bool Put, bool ComputeRay = true>
    void update_pc_threats(Square        s,
                           Piece         pc,
                           DirtyThreats* dts,
                           Bitboard      targetBB = FULL_BB) const noexcept;

    template<bool Do>
    void do_castling(Color       ac,
                     Square      kingOrgSq,
                     Square&     kingDstSq,
                     Square&     rookOrgSq,
                     Square&     rookDstSq,
                     DirtyBoard* db = nullptr) noexcept;

    void reset_en_passant_sq() noexcept;
    void reset_rule50_count() noexcept;

    static constexpr auto CASTLING_RIGHTS_INDICES = []() constexpr noexcept {
        Array<u8, SQUARE_NB> castlingRightsIndices{};

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
            castlingRightsIndices[s] = rank_of(s) == RANK_1 ? WHITE * FILE_NB + file_of(s)
                                     : rank_of(s) == RANK_8 ? BLACK * FILE_NB + file_of(s)
                                                            : NONE * FILE_NB;

        return castlingRightsIndices;
    }();

    PieceMap                                  pieceMap;
    Array<Bitboard, PIECE_TYPE_NB>            typeBBs;
    Array<Bitboard, COLOR_NB>                 colorBBs;
    Array<CastlingRights, COLOR_NB * FILE_NB> castlingRightsMasks;
    Castlings                                 castlings;
    State*                                    st;
    u16                                       gamePly;
    Color                                     activeColor;
};

//static_assert(sizeof(Position) == 248, "Position size must be 248 bytes");

inline const auto& Position::piece_map() const noexcept { return pieceMap; }

inline const auto& Position::type_bbs() const noexcept { return typeBBs; }

inline const auto& Position::color_bbs() const noexcept { return colorBBs; }

inline Piece Position::operator[](Square s) const noexcept { return pieceMap[s]; }

inline Bitboard Position::operator[](PieceType pt) const noexcept { return typeBBs[pt]; }

inline Bitboard Position::operator[](Color c) const noexcept { return colorBBs[c]; }

inline Piece Position::piece(Square s) const noexcept { return pieceMap[s]; }

inline bool Position::empty(Square s) const noexcept { return piece(s) == Piece::NO_PIECE; }

template<typename... PieceTypes>
inline Bitboard Position::pieces_bb(PieceTypes... pts) const noexcept {
    return (typeBBs[pts] | ...);
}

inline Bitboard Position::pieces_bb(Color c) const noexcept { return colorBBs[c]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces_bb(Color c, PieceTypes... pts) const noexcept {
    return pieces_bb(c) & pieces_bb(pts...);
}

inline Bitboard Position::pieces_bb(Piece pc) const noexcept {
    return pieces_bb(color_of(pc), type_of(pc));
}

inline Bitboard Position::pieces_bb() const noexcept { return typeBBs[ALL]; }

template<typename... PieceTypes>
inline u8 Position::count(PieceTypes... pts) const noexcept {
    return popcount(pieces_bb(pts...));
}

inline u8 Position::count(Color c) const noexcept { return popcount(pieces_bb(c)); }

template<typename... PieceTypes>
inline u8 Position::count(Color c, PieceTypes... pts) const noexcept {
    return popcount(pieces_bb(c, pts...));
}

inline u8 Position::count(Piece pc) const noexcept { return count(color_of(pc), type_of(pc)); }

inline u8 Position::count() const noexcept { return popcount(pieces_bb()); }

template<PieceType PT>
inline Square Position::square(Color c) const noexcept {
    assert(count(c, PT) == 1);

    return lsq(pieces_bb(c, PT));
}

inline Square Position::en_passant_sq() const noexcept { return st->enPassantSq; }

inline Square Position::captured_sq() const noexcept { return st->capturedSq; }

inline u16 Position::ply() const noexcept { return gamePly; }

inline Color Position::active_color() const noexcept { return activeColor; }

inline i32 Position::move_num() const noexcept {
    return 1 + (ply() - (active_color() == BLACK)) / 2;
}

inline CastlingRights Position::castling_rights_mask(Square s) const noexcept {
    auto sIdx = CASTLING_RIGHTS_INDICES[s];

    return sIdx < castlingRightsMasks.size() ? castlingRightsMasks[sIdx]
                                             : CastlingRights::NO_CASTLING;
}

inline CastlingRights Position::castling_rights_mask(Square orgSq, Square dstSq) const noexcept {
    return castling_rights_mask(orgSq) | castling_rights_mask(dstSq);
}

inline CastlingRights Position::castling_rights() const noexcept { return st->castlingRights; }

inline bool Position::has_castling_rights() const noexcept {
    return castling_rights() != CastlingRights::NO_CASTLING;
}

inline bool Position::has_castling_rights(Color c, CastlingSide cs) const noexcept {
    return (castling_rights() & make_cr(c, cs)) != CastlingRights::NO_CASTLING;
}

// Checks if squares between king and rook are empty
inline bool Position::castling_full_path_clear(Color c, CastlingSide cs) const noexcept {
    assert(is_ok(c) && is_ok(cs));

    return (castlings.fullPathBB[c][+cs] & pieces_bb()) == 0;
}

// Checks if the castling king path is attacked
inline bool Position::castling_king_path_clear(Color c, CastlingSide cs) const noexcept {
    assert(is_ok(c) && is_ok(cs));

    return (castlings.kingPathBB[c][+cs] & acc_attacks_bb<KING>()) == 0;
}

inline Square Position::castling_rook_sq(Color c, CastlingSide cs) const noexcept {
    assert(is_ok(c) && is_ok(cs));

    return castlings.rookSq[c][+cs];
}

inline bool Position::castling_possible(Color c, CastlingSide cs) const noexcept {
    assert(is_ok(c) && is_ok(cs));

    return has_castling_rights(c, cs)
        && is_ok(castling_rook_sq(c, cs))
        // Verify if the Rook blocks some checks (needed in case of Chess960).
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        && (blockers_bb(c) & castling_rook_sq(c, cs)) == 0  //
        && castling_full_path_clear(c, cs)                  //
        && castling_king_path_clear(c, cs);
}

// clang-format off

// Computes a bitboard of all x-ray sliding pieces which attack a given square.
inline Bitboard Position::xslide_attackers_bb(Square s) const noexcept {
    const auto [bAttacksBB, rAttacksBB] = attacks_bb_pair(s);
    return (pieces_bb(QUEEN, BISHOP) & bAttacksBB)
         | (pieces_bb(QUEEN, ROOK  ) & rAttacksBB);
}
// Computes a bitboard of all sliding pieces which attack a given square on occupancy.
inline Bitboard Position::slide_attackers_bb(Square s, Bitboard occupancyBB) const noexcept {
    const auto [bAttacksBB, rAttacksBB] = attacks_bb_pair(s, occupancyBB);
    return (pieces_bb(QUEEN, BISHOP) & bAttacksBB)
         | (pieces_bb(QUEEN, ROOK  ) & rAttacksBB);
}
inline Bitboard Position::slide_attackers_bb(Square s) const noexcept {
    return slide_attackers_bb(s, pieces_bb());
}
// Computes a bitboard of all pieces which attack a given square on occupancy
inline Bitboard Position::attackers_bb(Square s, Bitboard occupancyBB) const noexcept {
    return slide_attackers_bb(s, occupancyBB)
         | (pieces_bb(WHITE, PAWN) & attacks_bb<PAWN  >(s, BLACK))
         | (pieces_bb(BLACK, PAWN) & attacks_bb<PAWN  >(s, WHITE))
         | (pieces_bb(KNIGHT     ) & attacks_bb<KNIGHT>(s))
         | (pieces_bb(KING       ) & attacks_bb<KING  >(s));
}
inline Bitboard Position::attackers_bb(Square s) const noexcept {
    return attackers_bb(s, pieces_bb());
}

// Checks if there are any slide attackers to 's'
inline bool Position::slide_attackers_exists(Square s, Bitboard attackersBB, Bitboard occupancyBB) const noexcept {
    const auto [bAttacksBB, rAttacksBB] = attacks_bb_pair(s, occupancyBB);
    return ((attackersBB & pieces_bb(QUEEN, BISHOP) & bAttacksBB)
          | (attackersBB & pieces_bb(QUEEN, ROOK)   & rAttacksBB)) != 0;
}
inline bool Position::slide_attackers_exists(Square s, Bitboard attackersBB) const noexcept {
    return slide_attackers_exists(s, attackersBB, pieces_bb());
}
// Checks if there are any attackers to 's'
inline bool Position::attackers_exists(Square s, Bitboard attackersBB, Bitboard occupancyBB) const noexcept {
    return slide_attackers_exists(s, attackersBB, occupancyBB)
        || ((attackersBB & ((pieces_bb(WHITE, PAWN) & attacks_bb<PAWN  >(s, BLACK))
                          | (pieces_bb(BLACK, PAWN) & attacks_bb<PAWN  >(s, WHITE))))
          | (attackersBB & pieces_bb(KNIGHT       ) & attacks_bb<KNIGHT>(s))
          | (attackersBB & pieces_bb(KING         ) & attacks_bb<KING  >(s))) != 0;
}
inline bool Position::attackers_exists(Square s, Bitboard attackersBB) const noexcept {
    return attackers_exists(s, attackersBB, pieces_bb());
}

// clang-format on

// Computes the blockers that are pinned pieces to 's' from a set of attackers.
// Blockers are pieces that, when removed, would expose an x-ray attack to 's'.
// Pinners are also returned via the ownPinners and oppPinners reference.
inline Bitboard Position::blockers_bb(Square    s,
                                      Bitboard  attackersBB,
                                      Bitboard& ownPinnersBB,
                                      Bitboard& oppPinnersBB) const noexcept {
    Bitboard blockersBB = 0;

    // xSnipers are x-ray attackers that attack 's' when blockers are removed
    Bitboard xSnipersBB  = xslide_attackers_bb(s) & attackersBB;
    Bitboard occupancyBB = pieces_bb() ^ xSnipersBB;

    while (xSnipersBB != 0)
    {
        Square xSniperSq = pop_lsq(xSnipersBB);

        if (Bitboard blockerBB = between_bb(s, xSniperSq) & occupancyBB; exactly_one(blockerBB))
        {
            blockersBB |= blockerBB;

            if ((blockerBB & attackersBB) != 0)
                ownPinnersBB |= xSniperSq;
            else
                oppPinnersBB |= xSniperSq;
        }
    }

    return blockersBB;
}

// Computes attacks from a piece type for a given color.
template<PieceType PT>
inline Bitboard Position::attacks_by_bb(Color c) const noexcept {
    if constexpr (PT == PAWN)
        return pawn_attacks_bb(pieces_bb(c, PAWN), c);
    else
    {
        Bitboard attacksBB = 0;

        Bitboard occupancyBB = pieces_bb() ^ square<KING>(~c);

        Bitboard attackersBB = pieces_bb(c, PT);
        while (attackersBB != 0)
            attacksBB |= attacks_bb<PT>(pop_lsq(attackersBB), occupancyBB);

        return attacksBB;
    }
}

inline Bitboard Position::checkers_bb() const noexcept { return st->checkersBB; }

inline Bitboard Position::checks_bb(PieceType pt) const noexcept { return st->checksBB[pt]; }

inline Bitboard Position::pinners_bb(Color c) const noexcept { return st->pinnersBB[c]; }

inline Bitboard Position::pinners_bb() const noexcept {
    return pinners_bb(WHITE) | pinners_bb(BLACK);
}

inline Bitboard Position::blockers_bb(Color c) const noexcept { return st->blockersBB[c]; }

inline Bitboard Position::blockers_bb() const noexcept {
    return blockers_bb(WHITE) | blockers_bb(BLACK);
}

inline Bitboard Position::acc_attacks_bb() const noexcept {  //
    return st->accAttacksBB[ALL];
}
template<PieceType PT>
inline Bitboard Position::acc_attacks_bb() const noexcept {  //
    return st->accAttacksBB[PT];
}
inline Bitboard Position::acc_less_attacks_bb(PieceType pt) const noexcept {
    return st->accAttacksBB[pt == KNIGHT || pt == BISHOP ? PAWN : pt - 1];
}
inline Bitboard Position::threats_bb() const noexcept {
    return acc_attacks_bb<KING>() & ~acc_attacks_bb();
}

inline Key Position::raw_key() const noexcept { return st->key; }

inline Key Position::key() const noexcept { return raw_key() ^ Zobrist::mr50(rule50_count()); }

inline Key Position::pawn_key(Color c) const noexcept { return st->pawnKeys[c]; }

inline Key Position::pawn_key() const noexcept { return pawn_key(WHITE) ^ pawn_key(BLACK); }

inline Key Position::minor_key(Color c) const noexcept { return st->nonPawnKeys[c][0]; }

inline Key Position::minor_key() const noexcept { return minor_key(WHITE) ^ minor_key(BLACK); }

inline Key Position::major_key(Color c) const noexcept { return st->nonPawnKeys[c][1]; }

inline Key Position::major_key() const noexcept { return major_key(WHITE) ^ major_key(BLACK); }

inline Key Position::non_pawn_key(Color c) const noexcept {
    return minor_key(c) ^ major_key(c) ^ Zobrist::piece_square(c, KING, square<KING>(c));
}

inline Key Position::non_pawn_key() const noexcept {
    return non_pawn_key(WHITE) ^ non_pawn_key(BLACK);
}

inline Key Position::material_key() const noexcept {
    Key materialKey = 0;

    for (Color c : {WHITE, BLACK})
        for (PieceType pt : EX_KING_PIECE_TYPES)
            if (auto cnt = count(c, pt); cnt != 0)
                materialKey ^= Zobrist::piece_square(c, pt, Square(Zobrist::PAWN_OFFSET + cnt - 1));

    return materialKey;
}

inline bool Position::has_non_pawn(Color c) const noexcept {

    return std::any_of(NON_PAWN_PIECE_TYPES.begin(), NON_PAWN_PIECE_TYPES.end(),
                       [&](PieceType pt) -> bool { return pieces_bb(c, pt) != 0; });
}

inline Value Position::non_pawn_value(Color c) const noexcept {

    return std::accumulate(
      NON_PAWN_PIECE_TYPES.begin(), NON_PAWN_PIECE_TYPES.end(), VALUE_ZERO,
      [&](Value acc, PieceType pt) { return acc + piece_value(pt) * count(c, pt); });
}

inline Value Position::non_pawn_value() const noexcept {
    return non_pawn_value(WHITE) + non_pawn_value(BLACK);
}

inline u16 Position::rule50_count() const noexcept { return st->rule50Count; }

inline u16 Position::null_ply() const noexcept { return st->nullPly; }

inline i16 Position::repetition() const noexcept { return st->repetition; }

inline bool Position::has_castled(Color c) const noexcept { return st->hasCastleds[c]; }

inline bool Position::has_rule50_high() const noexcept { return st->hasRule50High; }

inline Piece Position::captured_pc() const noexcept { return st->capturedPc; }

inline Piece Position::promoted_pc() const noexcept { return st->promotedPc; }

inline bool Position::bishop_paired(Color c) const noexcept {
    Bitboard bishops = pieces_bb(c, BISHOP);
    return (bishops & color_bb<WHITE>())  //
        && (bishops & color_bb<BLACK>());
}

inline bool Position::bishop_opposite() const noexcept {
    return count(WHITE, BISHOP) == 1  //
        && count(BLACK, BISHOP) == 1
        && color_opposite(square<BISHOP>(WHITE), square<BISHOP>(BLACK));
}

inline bool Position::dtz_is_dtm() const noexcept {
    return count(PAWN) == 0 && (count() == 3 || (count() == 4 && pieces_bb(QUEEN, ROOK) == 0));
}

inline usize Position::bucket() const noexcept { return (count() - 1) / 4; }

inline int Position::std_material() const noexcept {
    return 1 * count(PAWN) + 3 * count(KNIGHT, BISHOP) + 5 * count(ROOK) + 9 * count(QUEEN);
}

inline Value Position::material() const noexcept {
    return VALUE_EVAL_PAWN * count(PAWN) + non_pawn_value();
}

inline bool Position::capture(const Move m) const noexcept {
    assert(legal(m));

    return (m.type() != MT::CASTLING && !empty(m.dst_sq())) || m.type() == MT::EN_PASSANT;
}

inline bool Position::capture_promo(const Move m) const noexcept {
    return capture(m)
        || (m.type() == MT::PROMOTION
            && (m.promotion_type() == QUEEN
                || (m.promotion_type() == KNIGHT && (checks_bb(KNIGHT) & m.dst_sq()) != 0)));
}

inline Piece Position::moved_pc(const Move m) const noexcept {
    assert(legal(m));

    return piece(m.org_sq());
}

inline Piece Position::captured_pc(const Move m) const noexcept {
    assert(legal(m));
    assert(m.type() != MT::CASTLING);

    return m.type() == MT::EN_PASSANT  //
           ? make_piece(~active_color(), PAWN)
           : piece(m.dst_sq());
}

inline auto Position::captured_pt(const Move m) const noexcept { return type_of(captured_pc(m)); }

inline void Position::reset_en_passant_sq() noexcept { st->enPassantSq = SQ_NONE; }

inline void Position::reset_rule50_count() noexcept { st->rule50Count = 0; }

inline void Position::put(Square s, Piece pc, DirtyThreats* dts) noexcept {
    assert(is_ok(s) && is_ok(pc) && empty(s));

    const Bitboard sBB = square_bb(s);

    pieceMap[s] = pc;
    colorBBs[color_of(pc)] |= sBB;
    typeBBs[ALL] |= typeBBs[type_of(pc)] |= sBB;

    if (dts != nullptr)
        update_pc_threats<true>(s, pc, dts);
}

inline Piece Position::remove(Square s, DirtyThreats* dts) noexcept {
    assert(is_ok(s) && !empty(s));

    const Bitboard sBB = square_bb(s);

    const Piece pc = piece(s);

    if (dts != nullptr)
        update_pc_threats<false>(s, pc, dts);

    pieceMap[s] = Piece::NO_PIECE;
    colorBBs[color_of(pc)] ^= sBB;
    typeBBs[type_of(pc)] ^= sBB;
    typeBBs[ALL] ^= sBB;

    return pc;
}

inline Piece Position::move(Square s1, Square s2, DirtyThreats* dts) noexcept {
    assert(is_ok(s1) && is_ok(s2) && s1 != s2 && !empty(s1));

    const Bitboard s1s2BB = square_bb(s1) | square_bb(s2);

    const Piece pc = piece(s1);

    if (dts != nullptr)
        update_pc_threats<false>(s1, pc, dts, s1s2BB);

    pieceMap[s1] = Piece::NO_PIECE;
    pieceMap[s2] = pc;
    colorBBs[color_of(pc)] ^= s1s2BB;
    typeBBs[type_of(pc)] ^= s1s2BB;
    typeBBs[ALL] ^= s1s2BB;

    if (dts != nullptr)
        update_pc_threats<true>(s2, pc, dts, s1s2BB);

    return pc;
}

inline Piece Position::swap(Square s, Piece newPc, DirtyThreats* dts) noexcept {

    const Piece oldPc = remove(s);

    if (dts != nullptr)
        update_pc_threats<false, false>(s, oldPc, dts);

    put(s, newPc);

    if (dts != nullptr)
        update_pc_threats<true, false>(s, newPc, dts);

    return oldPc;
}

template<bool Put>
inline void
DirtyThreats::add(Square sq, Square threatenedSq, Piece pc, Piece threatenedPc) noexcept {
    if constexpr (Put)
    {
        threateningBB |= sq;
        threatenedBB |= threatenedSq;
    }

    dtList.emplace_back(sq, threatenedSq, pc, threatenedPc, Put);
}

#if defined(USE_AVX512ICL)
// Given a DirtyThreat template and bit offsets to insert the piece type and square,
// write the threats present at the given bitboard.
template<int SqShift, int PcShift>
inline void write_multiple_dirties(const PieceMap& pieceMap,
                                   Bitboard        maskBB,
                                   DirtyThreat     templateDt,
                                   DirtyThreats*   dts) noexcept {
    const __m512i pieceVec = _mm512_loadu_si512(pieceMap.data());

    u8 maskCount = popcount(maskBB);
    assert(maskCount <= 16);

    auto* dtSpace = dts->dtList.make_space(maskCount);

    const __m512i templateVal = _mm512_set1_epi32(templateDt.raw());

    // Extract the list of squares and up convert to 32 bits.
    // There are never more than 16 incoming threats so this is sufficient.
    __m512i threatSquares = _mm512_maskz_compress_epi8(maskBB, ALL_SQUARES);
    threatSquares         = _mm512_cvtepi8_epi32(_mm512_castsi512_si128(threatSquares));

    __m512i threatPieces =
      _mm512_maskz_permutexvar_epi8(u64{0x1111111111111111}, threatSquares, pieceVec);

    // Shift the piece and square into place
    threatSquares = _mm512_slli_epi32(threatSquares, SqShift);
    threatPieces  = _mm512_slli_epi32(threatPieces, PcShift);

    // Combine into final dirty values (A | B | C = 254)
    const __m512i dirties =
      _mm512_ternarylogic_epi32(templateVal, threatSquares, threatPieces, 254);

    const __mmask16 storeMask = (u16{1} << maskCount) - 1;
    _mm512_mask_storeu_epi32(dtSpace, storeMask, dirties);
}
#endif

// Put newly threatened pieces
template<bool Put, bool ComputeRay>
inline void Position::update_pc_threats(Square                    s,
                                        Piece                     pc,
                                        DirtyThreats*             dts,
                                        [[maybe_unused]] Bitboard targetBB) const noexcept {

    Bitboard occupancyBB = pieces_bb();

    auto attacksBB = [&]() noexcept {
        Array<Bitboard, PIECE_TYPE_CNT> _;

        _[WHITE]  = attacks_bb<PAWN>(s, WHITE);
        _[BLACK]  = attacks_bb<PAWN>(s, BLACK);
        _[KNIGHT] = attacks_bb<KNIGHT>(s);
        _[BISHOP] = attacks_bb<BISHOP>(s, occupancyBB);
        _[ROOK]   = attacks_bb<ROOK>(s, occupancyBB);
        _[QUEEN]  = _[BISHOP] | _[ROOK];

        return _;
    }();

    Bitboard kings         = pieces_bb(KING);
    Bitboard exOccupancyBB = occupancyBB ^ kings;

    // clang-format off
    Bitboard slidersBB = (pieces_bb(QUEEN, BISHOP) & attacksBB[BISHOP])
                       | (pieces_bb(QUEEN, ROOK)   & attacksBB[ROOK]);
    // clang-format on

    auto process_sliders = [&](bool addDirectAttacks) noexcept {
        while (slidersBB != 0)
        {
            Square sliderSq = pop_lsq(slidersBB);
            Piece  sliderPc = piece(sliderSq);

            assert(sliderSq != s);
            assert(is_ok(sliderPc));

            Bitboard passRayBB    = pass_ray_bb(sliderSq, s);
            Bitboard discoveredBB = passRayBB & attacksBB[QUEEN] & exOccupancyBB;

            if (discoveredBB != 0 && (passRayBB & targetBB) != targetBB)
            {
                assert(!more_than_one(discoveredBB));
                Square threatenedSq = lsq(discoveredBB);
                Piece  threatenedPc = piece(threatenedSq);

                assert(is_ok(threatenedPc));

                dts->add<!Put>(sliderSq, threatenedSq, sliderPc, threatenedPc);
            }

            if (addDirectAttacks)
                dts->add<Put>(sliderSq, s, sliderPc, pc);
        }
    };

    if (type_of(pc) == KING)
    {
        if constexpr (ComputeRay)
        {
            process_sliders(false);
        }

        return;
    }

    // clang-format off
    Bitboard threatenedBB = (type_of(pc) == PAWN ? attacksBB[color_of(pc)]
                                                 : attacksBB[type_of(pc)])
                          & exOccupancyBB;

    Bitboard nonSlidersBB = (pieces_bb(WHITE, PAWN) & attacksBB[BLACK])
                          | (pieces_bb(BLACK, PAWN) & attacksBB[WHITE])
                          | (pieces_bb(KNIGHT)      & attacksBB[KNIGHT]);
    // clang-format on

#if defined(USE_AVX512ICL)
    if constexpr (Put)
    {
        dts->threatenedBB |= threatenedBB;
        // A bit may only be set if that square actually produces a threat, so we
        // must guard setting the square accordingly
        dts->threateningBB |= Bitboard{threatenedBB != 0} << s;
    }

    DirtyThreat templateDt1{s, SQUARE_ZERO, pc, Piece::NO_PIECE, Put};
    write_multiple_dirties<DirtyThreat::THREATENED_SQ_OFFSET, DirtyThreat::THREATENED_PC_OFFSET>(
      piece_map(), threatenedBB, templateDt1, dts);

    Bitboard attackersBB = slidersBB | nonSlidersBB;

    if constexpr (Put)
    {
        dts->threatenedBB |= Bitboard{attackersBB != 0} << s;
        dts->threateningBB |= attackersBB;
    }

    DirtyThreat templateDt2{SQUARE_ZERO, s, Piece::NO_PIECE, pc, Put};
    write_multiple_dirties<DirtyThreat::SQ_OFFSET, DirtyThreat::PC_OFFSET>(piece_map(), attackersBB,
                                                                           templateDt2, dts);
#else
    while (threatenedBB != 0)
    {
        Square threatenedSq = pop_lsq(threatenedBB);
        Piece  threatenedPc = piece(threatenedSq);

        assert(threatenedSq != s);
        assert(is_ok(threatenedPc));

        dts->add<Put>(s, threatenedSq, pc, threatenedPc);
    }
#endif

    if constexpr (ComputeRay)
    {
#if defined(USE_AVX512ICL)  // Direct threats were processed earlier (attackersBB)
        process_sliders(false);
#else
        process_sliders(true);
#endif
    }
    else
    {
        nonSlidersBB |= slidersBB;
    }

#if defined(USE_AVX512ICL)
#else
    while (nonSlidersBB != 0)
    {
        Square nonSliderSq = pop_lsq(nonSlidersBB);
        Piece  nonSliderPc = piece(nonSliderSq);

        assert(nonSliderSq != s);
        assert(is_ok(nonSliderPc));

        dts->add<Put>(nonSliderSq, s, nonSliderPc, pc);
    }
#endif
}

inline constexpr State* Position::state() const noexcept { return st; }

// Position::SEE
inline bool Position::SEE::operator>=(int threshold) const noexcept {
    return pos.see_ge(move, threshold);
}
inline bool Position::SEE::operator>(int threshold) const noexcept {
    return (*this >= threshold + 1);
}
inline bool Position::SEE::operator<=(int threshold) const noexcept { return !(*this > threshold); }
inline bool Position::SEE::operator<(int threshold) const noexcept { return !(*this >= threshold); }

inline i16 rule50_threshold(i16 r50 = -4) noexcept {
    assert(r50 >= -2 * Position::DrawMoveCount);

    return r50 + 2 * Position::DrawMoveCount;
}

}  // namespace DON

#endif  // #ifndef POSITION_H_INCLUDED
