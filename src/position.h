/*
  DON, a UCI chess playing engine derived from Stockfish

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
#include <cstddef>  // IWYU pragma: keep
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "bitboard.h"
#include "misc.h"
#include "types.h"

namespace DON {

class TranspositionTable;

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

    static Key castling(CastlingRights cr) noexcept { return Castling[cr]; }

    static Key enpassant(Square enPassantSq) noexcept {
        assert(is_ok(enPassantSq));
        return Enpassant[file_of(enPassantSq)];
    }

    static Key turn() noexcept { return Turn; }

    static Key mr50(std::int16_t rule50Count) noexcept {
        std::int16_t idx = rule50Count - R50_OFFSET;
        return idx < 0 ? 0 : MR50[std::min(idx / R50_FACTOR, int(MR50.size()) - 1)];
    }

    static constexpr std::size_t PAWN_OFFSET = 8;

   private:
    Zobrist() noexcept                          = delete;
    Zobrist(const Zobrist&) noexcept            = delete;
    Zobrist(Zobrist&&) noexcept                 = delete;
    Zobrist& operator=(const Zobrist&) noexcept = delete;
    Zobrist& operator=(Zobrist&&) noexcept      = delete;

    static inline StdArray<Key, COLOR_NB, PIECE_TYPE_NB - 1, SQUARE_NB> PieceSquare{};
    static inline StdArray<Key, CASTLING_RIGHTS_NB>                     Castling{};
    static inline StdArray<Key, FILE_NB>                                Enpassant{};
    static inline Key                                                   Turn{};

    static constexpr std::uint8_t R50_OFFSET = 14;
    static constexpr std::uint8_t R50_FACTOR = 8;

    static inline StdArray<Key, (MAX_PLY + 1 - R50_OFFSET) / R50_FACTOR + 2> MR50{};
};

// State struct stores information needed to restore Position object
// to its previous state when retract any move.
struct State final {
   private:
    constexpr State(State&&) noexcept            = delete;
    constexpr State& operator=(State&&) noexcept = delete;

   public:
    State() noexcept                                     = default;
    constexpr State(const State&) noexcept               = default;
    constexpr State& operator=(const State& st) noexcept = default;

    void clear() noexcept;

    // --- Copied when making a move
    StdArray<Key, COLOR_NB>    pawnKey;
    StdArray<Key, COLOR_NB, 2> nonPawnKey;
    StdArray<bool, COLOR_NB>   hasCastled;

    Square         enPassantSq;
    Square         capturedSq;
    CastlingRights castlingRights;
    std::uint8_t   rule50Count;
    std::uint8_t   nullPly;  // Plies from Null-Move
    bool           hasRule50High;

    // --- Not copied when making a move (will be recomputed anyhow)
    Key                                         key;
    Bitboard                                    checkersBB;
    StdArray<Bitboard, PIECE_TYPE_NB>           checksBB;
    StdArray<Bitboard, COLOR_NB>                pinnersBB;
    StdArray<Bitboard, COLOR_NB>                blockersBB;
    StdArray<Bitboard, COLOR_NB, PIECE_TYPE_NB> attacksAccBB;
    std::int16_t                                repetition;
    Piece                                       capturedPc;
    Piece                                       promotedPc;
    const State*                                preSt;

    // Copy relevant fields from the state.
    // excluding those that will recomputed from scratch anyway and
    // then switch the state pointer to point to the new state.
    template<typename T = Key>
    void switch_to_prefix(const State* st, T State::* member = &State::key) noexcept {
        // Compute offset dynamically for this object
        std::size_t size = reinterpret_cast<const char*>(&(st->*member))  //
                         - reinterpret_cast<const char*>(st);

        //// Defensive clamp (shouldn't be needed if member belongs to State)
        //if (size > sizeof(*this))
        //    size = sizeof(*this);

        std::memcpy(this, st, size);

        preSt = st;
    }
};

static_assert(std::is_standard_layout_v<State> && std::is_trivially_copyable_v<State>,
              "State must be standard-layout and trivially copyable");
//static_assert(sizeof(State) == 312, "State size");

// Position class stores information regarding the board representation as
// pieces, active color, hash keys, castling info, etc. (Size = 464)
// Important methods are do_move() and undo_move(),
// used by the search to update node info when traversing the search tree.
class Position final {
   public:
    static void init() noexcept;

    Position() noexcept;
    Position(const Position&) noexcept            = default;
    Position& operator=(const Position&) noexcept = default;

   private:
    constexpr Position(Position&&) noexcept            = delete;
    constexpr Position& operator=(Position&&) noexcept = delete;

    void construct() noexcept;

    void clear() noexcept;

   public:
    // FEN string input/output
    void        set(std::string_view fens, State* const newSt) noexcept;
    void        set(std::string_view code, Color c, State* const newSt) noexcept;
    void        set(const Position& pos, State* const newSt) noexcept;
    std::string fen(bool full = true) const noexcept;

    // Position representation
    [[nodiscard]] const auto& piece_map() const noexcept;
    [[nodiscard]] const auto& type_bbs() const noexcept;
    [[nodiscard]] const auto& color_bbs() const noexcept;
    [[nodiscard]] const auto& piece_list() const noexcept;

    [[nodiscard]] Piece    operator[](Square s) const noexcept;
    [[nodiscard]] Bitboard operator[](PieceType pt) const noexcept;
    [[nodiscard]] Bitboard operator[](Color c) const noexcept;

    Piece piece_on(Square s) const noexcept;
    bool  empty_on(Square s) const noexcept;

    Bitboard pieces_bb() const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces_bb(PieceTypes... pts) const noexcept;
    Bitboard pieces_bb(Color c) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces_bb(Color c, PieceTypes... pts) const noexcept;

    [[nodiscard]] const auto& squares(Color c, PieceType pt) const noexcept;
    template<PieceType PT>
    [[nodiscard]] const auto& squares(Color c) const noexcept;
    [[nodiscard]] const auto& squares(Piece pc) const noexcept;
    auto                      squares(Color c, std::size_t& n) const noexcept;
    auto                      squares(std::size_t& n) const noexcept;

    std::uint8_t count(Color c, PieceType pt) const noexcept;
    template<PieceType PT>
    std::uint8_t count(Color c) const noexcept;
    std::uint8_t count(Piece pc) const noexcept;
    std::uint8_t count(Color c) const noexcept;
    std::uint8_t count() const noexcept;
    template<PieceType PT>
    std::uint8_t count() const noexcept;

    template<PieceType PT>
    Square square(Color c) const noexcept;

    Square en_passant_sq() const noexcept;
    Square captured_sq() const noexcept;

    std::int16_t ply() const noexcept;
    Color        active_color() const noexcept;
    std::int32_t move_num() const noexcept;

    CastlingRights castling_rights() const noexcept;

    auto castling_rights_mask(Square orgSq, Square dstSq) const noexcept;

    bool   castling_has_rights(CastlingRights cr) const noexcept;
    Square castling_rook_sq(CastlingRights cr) const noexcept;
    bool   castling_full_path_clear(CastlingRights cr) const noexcept;
    bool   castling_king_path_attackers_exists(Color c, CastlingRights cr) const noexcept;
    bool   castling_possible(Color c, CastlingRights cr) const noexcept;

    Bitboard xslide_attackers_bb(Square s) const noexcept;
    Bitboard slide_attackers_bb(Square s, Bitboard occupancyBB) const noexcept;
    Bitboard slide_attackers_bb(Square s) const noexcept;
    Bitboard attackers_bb(Square s, Bitboard occupancyBB) const noexcept;
    Bitboard attackers_bb(Square s) const noexcept;

    bool attackers_exists(Square s, Bitboard attackersBB, Bitboard occupancyBB) const noexcept;
    bool attackers_exists(Square s, Bitboard attackersBB) const noexcept;

    Bitboard blockers_bb(Square    s,
                         Bitboard  attackersBB,
                         Bitboard& ownPinnersBB,
                         Bitboard& oppPinnersBB) const noexcept;

    // Attacks from a piece type
    template<PieceType PT>
    Bitboard attacks_by_bb(Color c) const noexcept;

    // clang-format off
    // Doing and undoing moves
    DirtyBoard do_move(Move m, State& newSt, bool isCheck, const TranspositionTable* const tt = nullptr) noexcept;
    DirtyBoard do_move(Move m, State& newSt, const TranspositionTable* const tt = nullptr) noexcept;
    void       undo_move(Move m) noexcept;
    void       do_null_move(State& newSt, const TranspositionTable* const tt = nullptr) noexcept;
    void       undo_null_move() noexcept;
    // clang-format on

    // Properties of moves
    bool  legal(Move m) const noexcept;
    bool  capture(Move m) const noexcept;
    bool  capture_queenpromo(Move m) const noexcept;
    bool  check(Move m) const noexcept;
    bool  dbl_check(Move m) const noexcept;
    bool  fork(Move m) const noexcept;
    Piece moved_pc(Move m) const noexcept;
    Piece captured_pc(Move m) const noexcept;
    auto  captured_pt(Move m) const noexcept;

    Bitboard checkers_bb() const noexcept;
    Bitboard checks_bb(PieceType pt) const noexcept;
    Bitboard pinners_bb(Color c) const noexcept;
    Bitboard pinners_bb() const noexcept;
    Bitboard blockers_bb(Color c) const noexcept;
    Bitboard blockers_bb() const noexcept;

    template<PieceType PT>
    Bitboard acc_attacks_bb(Color c) const noexcept;
    Bitboard acc_less_attacks_bb(Color c, PieceType pt) const noexcept;
    Bitboard threats_bb(Color c) const noexcept;

    Piece captured_pc() const noexcept;
    Piece promoted_pc() const noexcept;

    // Hash keys
    Key key(std::int16_t r50 = 0) const noexcept;
    Key pawn_key(Color c) const noexcept;
    Key pawn_key() const noexcept;
    Key minor_key(Color c) const noexcept;
    Key minor_key() const noexcept;
    Key major_key(Color c) const noexcept;
    Key major_key() const noexcept;
    Key non_pawn_key(Color c) const noexcept;
    Key non_pawn_key() const noexcept;

    Key material_key() const noexcept;
    Key move_key(Move m) const noexcept;

    Value non_pawn_value(Color c) const noexcept;
    Value non_pawn_value() const noexcept;
    bool  has_non_pawn(Color c) const noexcept;

    // Other properties
    std::int16_t rule50_count() const noexcept;
    std::int16_t null_ply() const noexcept;
    std::int16_t repetition() const noexcept;

    bool has_castled(Color c) const noexcept;
    bool has_rule50_high() const noexcept;
    bool bishop_paired(Color c) const noexcept;
    bool bishop_opposite() const noexcept;

    std::size_t bucket() const noexcept;

    int   std_material() const noexcept;
    Value material() const noexcept;
    Value evaluate() const noexcept;

    // Static Exchange Evaluation
    [[nodiscard]] auto see(Move m) const noexcept { return SEE(*this, m); }

    bool is_repetition(std::int16_t ply) const noexcept;
    bool
    is_draw(std::int16_t ply, bool rule50Active = true, bool chkStalemate = false) const noexcept;
    bool has_repeated() const noexcept;
    bool is_upcoming_repetition(std::int16_t ply) const noexcept;

    void  put_pc(Square s, Piece pc, DirtyThreats* const dts = nullptr) noexcept;
    Piece remove_pc(Square s, DirtyThreats* const dts = nullptr) noexcept;

    void flip() noexcept;
    void mirror() noexcept;

    // Position consistency check, for debugging
#if !defined(NDEBUG)
    Key compute_key() const noexcept;
    Key compute_minor_key() const noexcept;
    Key compute_major_key() const noexcept;
    Key compute_non_pawn_key() const noexcept;

    bool _is_ok() const noexcept;
#endif

    constexpr Square*       base(Color c) noexcept;
    constexpr const Square* base(Color c) const noexcept;

    // Used by NNUE
    constexpr State* state() const noexcept;

    operator std::string() const noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept;

    void dump(std::ostream& os = std::cout) const noexcept;

    static constexpr StdArray<std::size_t, PIECES> CAPACITY  //
      {11, 13, 13, 13, 13, 1};

    static inline bool Chess960 = false;

    static inline std::uint8_t DrawMoveCount = 50;

   private:
    struct Castling final {
       public:
        void clear() noexcept {
            rookSq = SQ_NONE;
            std::memset(fullPathSqs.data(), SQ_NONE, sizeof(fullPathSqs));
            fullPathLen = 0;
            std::memset(kingPathSqs.data(), SQ_NONE, sizeof(kingPathSqs));
            kingPathLen = 0;
        }

        Square              rookSq = SQ_NONE;
        StdArray<Square, 5> fullPathSqs;
        std::uint8_t        fullPathLen = 0;
        StdArray<Square, 5> kingPathSqs;
        std::uint8_t        kingPathLen = 0;
    };

    // SEE struct used to get a nice syntax for SEE comparisons.
    // Never use this type directly or store a value into a variable of this type,
    // instead use the syntax "pos.see(move) >= threshold" and similar for other comparisons.
    struct SEE final {
       public:
        SEE() noexcept                      = delete;
        SEE(const SEE&) noexcept            = delete;
        SEE(SEE&&) noexcept                 = delete;
        SEE& operator=(const SEE&) noexcept = delete;
        SEE& operator=(SEE&&) noexcept      = delete;

        constexpr SEE(const Position& p, Move m) noexcept :
            pos(p),
            move(m) {}

        [[nodiscard]] bool operator>=(int threshold) const noexcept;
        [[nodiscard]] bool operator>(int threshold) const noexcept;
        [[nodiscard]] bool operator<=(int threshold) const noexcept;
        [[nodiscard]] bool operator<(int threshold) const noexcept;

       private:
        const Position& pos;
        Move            move;
    };

    // Initialization helpers (used while setting up a position)
    void set_castling_rights(Color c, Square rOrg) noexcept;
    void set_state() noexcept;
    void set_ext_state() noexcept;

    template<bool After = true>
    bool
    can_enpassant(Color ac, Square epSq, Bitboard* const epAttackersBB = nullptr) const noexcept;

    // Other helpers
    Piece move_pc(Square s1, Square s2, DirtyThreats* const dts = nullptr) noexcept;
    Piece swap_pc(Square s, Piece newPc, DirtyThreats* const dts = nullptr) noexcept;

    template<bool Add, bool ComputeRay = true>
    void update_pc_threats(Piece pc, Square s, DirtyThreats* const dts) noexcept;

    template<bool Do>
    void do_castling(Color             ac,
                     Square            orgSq,
                     Square&           dstSq,
                     Square&           rOrgSq,
                     Square&           rDstSq,
                     DirtyBoard* const db = nullptr) noexcept;

    void reset_en_passant_sq() noexcept;
    void reset_rule50_count() noexcept;

    // Static Exchange Evaluation
    bool see_ge(Move m, int threshold) const noexcept;

    static constexpr std::size_t TOTAL_CAPACITY = []() constexpr {
        std::size_t totalCapacity = 0;
        for (std::size_t i = 0; i < PIECES; ++i)
            totalCapacity += CAPACITY[i];
        return totalCapacity;
    }();
    static constexpr auto OFFSET = []() constexpr {
        StdArray<std::size_t, PIECES> offset{};
        offset[0] = 0;
        for (std::size_t i = 1; i < PIECES; ++i)
            offset[i] = offset[i - 1] + CAPACITY[i - 1];
        return offset;
    }();

    static constexpr StdArray<std::uint8_t, CASTLING_RIGHTS_NB> BIT{
      4, 0, 1, 4, 2, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4  //
    };

    static constexpr std::uint8_t INDEX_NONE = SQUARE_NB;

    // Backing Square Table: [COLOR_NB][TOTAL_CAPACITY]
    StdArray<Square, COLOR_NB, TOTAL_CAPACITY> squaresTable;
    // Generic CountTableView slices
    StdArray<CountTableView<Square>, COLOR_NB, 1 + PIECES> pieceList;

    StdArray<std::uint8_t, SQUARE_NB>               indexMap;
    StdArray<Piece, SQUARE_NB>                      pieceMap;
    StdArray<Bitboard, PIECE_TYPE_NB>               typeBBs;
    StdArray<Bitboard, COLOR_NB>                    colorBBs;
    StdArray<std::uint8_t, COLOR_NB>                pieceCounts;
    StdArray<std::uint8_t, COLOR_NB * FILE_NB>      castlingRightsMasks;
    StdArray<Castling, COLOR_NB * CASTLING_SIDE_NB> castlings;
    State*                                          st;
    std::int16_t                                    gamePly;
    Color                                           activeColor;
};

//static_assert(sizeof(Position) == 464, "Position size");

inline const auto& Position::piece_map() const noexcept { return pieceMap; }

inline const auto& Position::type_bbs() const noexcept { return typeBBs; }

inline const auto& Position::color_bbs() const noexcept { return colorBBs; }

inline const auto& Position::piece_list() const noexcept { return pieceList; }

inline Piece Position::operator[](Square s) const noexcept { return pieceMap[s]; }

inline Bitboard Position::operator[](PieceType pt) const noexcept { return typeBBs[pt]; }

inline Bitboard Position::operator[](Color c) const noexcept { return colorBBs[c]; }

inline Piece Position::piece_on(Square s) const noexcept { return pieceMap[s]; }

inline bool Position::empty_on(Square s) const noexcept { return piece_on(s) == NO_PIECE; }

inline Bitboard Position::pieces_bb() const noexcept { return typeBBs[NO_PIECE_TYPE]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces_bb(PieceTypes... pts) const noexcept {
    return (typeBBs[pts] | ...);
}

inline Bitboard Position::pieces_bb(Color c) const noexcept { return colorBBs[c]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces_bb(Color c, PieceTypes... pts) const noexcept {
    return pieces_bb(c) & pieces_bb(pts...);
}

inline const auto& Position::squares(Color c, PieceType pt) const noexcept {
    return pieceList[c][pt];
}

template<PieceType PT>
inline const auto& Position::squares(Color c) const noexcept {
    return squares(c, PT);
}

inline const auto& Position::squares(Piece pc) const noexcept {
    return squares(color_of(pc), type_of(pc));
}

inline auto Position::squares(Color c, std::size_t& n) const noexcept {
    StdArray<Square, SQUARE_NB> orgSqs;

    n = 0;
    for (PieceType pt : PIECE_TYPES)
    {
        const auto& pL    = squares(c, pt);
        const auto  count = pL.count();
        if (count)
        {
            const auto* pB = base(c);
            std::memcpy(orgSqs.data() + n, pL.data(pB), count * sizeof(Square));
            n += count;
        }
    }

    return orgSqs;
}

inline auto Position::squares(std::size_t& n) const noexcept {
    StdArray<Square, SQUARE_NB> orgSqs;

    n = 0;
    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            const auto& pL    = squares(c, pt);
            const auto  count = pL.count();
            if (count)
            {
                const auto* pB = base(c);
                std::memcpy(orgSqs.data() + n, pL.data(pB), count * sizeof(Square));
                n += count;
            }
        }

    return orgSqs;
}

inline std::uint8_t Position::count(Color c, PieceType pt) const noexcept {
    return squares(c, pt).count();
}

template<PieceType PT>
inline std::uint8_t Position::count(Color c) const noexcept {
    return count(c, PT);
}

inline std::uint8_t Position::count(Piece pc) const noexcept {
    return count(color_of(pc), type_of(pc));
}

inline std::uint8_t Position::count(Color c) const noexcept { return pieceCounts[c]; }

inline std::uint8_t Position::count() const noexcept { return count(WHITE) + count(BLACK); }

template<PieceType PT>
inline std::uint8_t Position::count() const noexcept {
    return count<PT>(WHITE) + count<PT>(BLACK);
}

template<PieceType PT>
inline Square Position::square(Color c) const noexcept {
    assert(count<PT>(c) == 1);
    return squares<PT>(c).at(0, base(c));
}

inline Square Position::en_passant_sq() const noexcept { return st->enPassantSq; }

inline Square Position::captured_sq() const noexcept { return st->capturedSq; }

inline std::int16_t Position::ply() const noexcept { return gamePly; }

inline Color Position::active_color() const noexcept { return activeColor; }

inline std::int32_t Position::move_num() const noexcept {
    return 1 + (ply() - (active_color() == BLACK)) / 2;
}

inline CastlingRights Position::castling_rights() const noexcept { return st->castlingRights; }

inline auto Position::castling_rights_mask(Square orgSq, Square dstSq) const noexcept {
    constexpr auto Indices = []() constexpr {
        StdArray<std::uint8_t, SQUARE_NB> indices{};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            auto rank  = rank_of(s);
            auto file  = file_of(s);
            indices[s] = (rank == RANK_1) ? WHITE * FILE_NB + file
                       : (rank == RANK_8) ? BLACK * FILE_NB + file
                                          : COLOR_NB * FILE_NB;
        }
        return indices;
    }();

    auto orgIdx = Indices[orgSq];
    auto dstIdx = Indices[dstSq];

    return (orgIdx < castlingRightsMasks.size() ? castlingRightsMasks[orgIdx] : 0)
         | (dstIdx < castlingRightsMasks.size() ? castlingRightsMasks[dstIdx] : 0);
}

inline bool Position::castling_has_rights(CastlingRights cr) const noexcept {
    return castling_rights() & int(cr);
}

inline Square Position::castling_rook_sq(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return castlings[BIT[cr]].rookSq;
}

// Checks if squares between king and rook are empty
inline bool Position::castling_full_path_clear(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);

    const auto& castling = castlings[BIT[cr]];

    for (std::size_t len = 0; len < castling.fullPathLen; ++len)
        if (!empty_on(castling.fullPathSqs[len]))
            return false;

    return true;
}

// Checks if the castling king path is attacked
inline bool Position::castling_king_path_attackers_exists(Color          c,
                                                          CastlingRights cr) const noexcept {
    assert((c == WHITE && (cr == WHITE_OO || cr == WHITE_OOO))
           || (c == BLACK && (cr == BLACK_OO || cr == BLACK_OOO)));

    Bitboard attackersBB = pieces_bb(~c);

    const auto& castling = castlings[BIT[cr]];

    for (std::size_t len = 0; len < castling.kingPathLen; ++len)
        if (attackers_exists(castling.kingPathSqs[len], attackersBB))
            return true;

    return false;
}

inline bool Position::castling_possible(Color c, CastlingRights cr) const noexcept {
    assert((c == WHITE && (cr == WHITE_OO || cr == WHITE_OOO))
           || (c == BLACK && (cr == BLACK_OO || cr == BLACK_OOO)));

    return castling_has_rights(cr)  //
        // Verify if the Rook blocks some checks (needed in case of Chess960).
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        && !(blockers_bb(c) & castling_rook_sq(cr))  //
        && castling_full_path_clear(cr)              //
        && !castling_king_path_attackers_exists(c, cr);
}

// clang-format off

// Computes a bitboard of all x-ray sliding pieces which attack a given square.
inline Bitboard Position::xslide_attackers_bb(Square s) const noexcept {
    return (pieces_bb(QUEEN, BISHOP) & attacks_bb<BISHOP>(s))
         | (pieces_bb(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s));
}
// Computes a bitboard of all sliding pieces which attack a given square on occupancy.
inline Bitboard Position::slide_attackers_bb(Square s, Bitboard occupancyBB) const noexcept {
    return (pieces_bb(QUEEN, BISHOP) & attacks_bb<BISHOP>(s) ? pieces_bb(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupancyBB) : 0)
         | (pieces_bb(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s) ? pieces_bb(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s, occupancyBB) : 0);
}
inline Bitboard Position::slide_attackers_bb(Square s) const noexcept {
    return slide_attackers_bb(s, pieces_bb());
}
// Computes a bitboard of all pieces which attack a given square on occupancy.
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
// Checks if there are any attackers to a given square from a set of attackers.
inline bool Position::attackers_exists(Square s, Bitboard attackersBB, Bitboard occupancyBB) const noexcept {
    return ((attackersBB & pieces_bb(QUEEN, BISHOP) & attacks_bb<BISHOP>(s))
         && (attackersBB & pieces_bb(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupancyBB)))
        || ((attackersBB & pieces_bb(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s))
         && (attackersBB & pieces_bb(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s, occupancyBB)))
        ||  (attackersBB & ((pieces_bb(WHITE, PAWN) & attacks_bb<PAWN  >(s, BLACK))
                          | (pieces_bb(BLACK, PAWN) & attacks_bb<PAWN  >(s, WHITE))))
        ||  (attackersBB & pieces_bb(KNIGHT       ) & attacks_bb<KNIGHT>(s))
        ||  (attackersBB & pieces_bb(KING         ) & attacks_bb<KING  >(s));
}
inline bool Position::attackers_exists(Square s, Bitboard attackersBB) const noexcept {
    return attackers_exists(s, attackersBB, pieces_bb());
}

// Computes the blockers that are pinned pieces to a given square 's' from a set of attackers.
// Blockers are pieces that, when removed, would expose an x-ray attack to 's'.
// Pinners are also returned via the ownPinners and oppPinners reference.
inline Bitboard Position::blockers_bb(Square s, Bitboard attackersBB, Bitboard& ownPinnersBB, Bitboard& oppPinnersBB) const noexcept {
    Bitboard blockersBB = 0;

    // xSnipers are x-ray attackers that attack 's' when blockers are removed
    Bitboard xSnipersBB  = xslide_attackers_bb(s) & attackersBB;
    Bitboard occupancyBB = pieces_bb() ^ xSnipersBB;

    while (xSnipersBB)
    {
        Square xSniperSq = pop_lsb(xSnipersBB);

        Bitboard blockerBB = between_bb(s, xSniperSq) & occupancyBB;

        if (exactly_one(blockerBB))
        {
            blockersBB |= blockerBB;

            if (blockerBB & attackersBB)
                ownPinnersBB |= xSniperSq;
            else
                oppPinnersBB |= xSniperSq;
        }
    }

    return blockersBB;
}

// clang-format on

// Computes attacks from a piece type for a given color.
template<PieceType PT>
inline Bitboard Position::attacks_by_bb(Color c) const noexcept {
    if constexpr (PT == PAWN)
        return pawn_attacks_bb(pieces_bb(c, PAWN), c);
    else
    {
        Bitboard attacksBB = 0;

        const auto& pL = squares<PT>(c);
        const auto* pB = base(c);
        for (const Square* orgSq = pL.begin(pB); orgSq != pL.end(pB); ++orgSq)
            attacksBB |= attacks_bb<PT>(*orgSq, pieces_bb());

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

template<PieceType PT>
inline Bitboard Position::acc_attacks_bb(Color c) const noexcept {
    return st->attacksAccBB[c][PT];
}
inline Bitboard Position::acc_less_attacks_bb(Color c, PieceType pt) const noexcept {
    return st->attacksAccBB[c][pt == KNIGHT || pt == BISHOP ? PAWN : pt - 1];
}
inline Bitboard Position::threats_bb(Color c) const noexcept {
    return st->attacksAccBB[c][KING] & ~st->attacksAccBB[~c][KING];
}

inline Piece Position::captured_pc() const noexcept { return st->capturedPc; }

inline Piece Position::promoted_pc() const noexcept { return st->promotedPc; }

inline Key Position::key(std::int16_t r50) const noexcept {
    return st->key ^ Zobrist::mr50(rule50_count() + r50);
}

inline Key Position::pawn_key(Color c) const noexcept { return st->pawnKey[c]; }

inline Key Position::pawn_key() const noexcept { return pawn_key(WHITE) ^ pawn_key(BLACK); }

inline Key Position::minor_key(Color c) const noexcept { return st->nonPawnKey[c][0]; }

inline Key Position::minor_key() const noexcept { return minor_key(WHITE) ^ minor_key(BLACK); }

inline Key Position::major_key(Color c) const noexcept { return st->nonPawnKey[c][1]; }

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
        for (PieceType pt : PIECE_TYPES)
        {
            if (pt == KING || !count(c, pt))
                continue;

            Square s = Square(Zobrist::PAWN_OFFSET + count(c, pt) - 1);

            materialKey ^= Zobrist::piece_square(c, pt, s);
        }

    return materialKey;
}

inline Value Position::non_pawn_value(Color c) const noexcept {
    Value nonPawnValue = VALUE_ZERO;

    for (PieceType pt : NON_PAWN_PIECE_TYPES)
        nonPawnValue += piece_value(pt) * count(c, pt);

    return nonPawnValue;
}

inline Value Position::non_pawn_value() const noexcept {
    return non_pawn_value(WHITE) + non_pawn_value(BLACK);
}

inline bool Position::has_non_pawn(Color c) const noexcept {

    for (PieceType pt : NON_PAWN_PIECE_TYPES)
        if (count(c, pt))
            return true;
    return false;
}

inline std::int16_t Position::rule50_count() const noexcept { return st->rule50Count; }

inline std::int16_t Position::null_ply() const noexcept { return st->nullPly; }

inline std::int16_t Position::repetition() const noexcept { return st->repetition; }

inline bool Position::has_castled(Color c) const noexcept { return st->hasCastled[c]; }

inline bool Position::has_rule50_high() const noexcept { return st->hasRule50High; }

inline bool Position::bishop_paired(Color c) const noexcept {
    Bitboard bishops = pieces_bb(c, BISHOP);
    return (bishops & color_bb<WHITE>())  //
        && (bishops & color_bb<BLACK>());
}

inline bool Position::bishop_opposite() const noexcept {
    return count<BISHOP>(WHITE) == 1  //
        && count<BISHOP>(BLACK) == 1
        && color_opposite(square<BISHOP>(WHITE), square<BISHOP>(BLACK));
}

inline std::size_t Position::bucket() const noexcept { return (count() - 1) / 4; }

inline int Position::std_material() const noexcept {
    return 1 * count<PAWN>() + 3 * (count<KNIGHT>() + count<BISHOP>()) + 5 * count<ROOK>()
         + 9 * count<QUEEN>();
}

inline Value Position::material() const noexcept { return 534 * count<PAWN>() + non_pawn_value(); }

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the side to move. It can be divided by VALUE_PAWN to get
// an approximation of the material advantage on the board in terms of pawns.
inline Value Position::evaluate() const noexcept {
    Color ac = active_color();
    return VALUE_PAWN * (count<PAWN>(ac) - count<PAWN>(~ac))
         + (non_pawn_value(ac) - non_pawn_value(~ac));
}

inline bool Position::capture(Move m) const noexcept {
    assert(legal(m));
    return (m.type_of() != CASTLING && !empty_on(m.dst_sq())) || m.type_of() == EN_PASSANT;
}

inline bool Position::capture_queenpromo(Move m) const noexcept {
    return capture(m) || m.promotion_type() == QUEEN;  // m.type_of() == PROMOTION must be true here
}

inline Piece Position::moved_pc(Move m) const noexcept {
    assert(legal(m));
    return piece_on(m.org_sq());
}

inline Piece Position::captured_pc(Move m) const noexcept {
    assert(legal(m));
    assert(m.type_of() != CASTLING);
    return m.type_of() == EN_PASSANT ? make_piece(~active_color(), PAWN) : piece_on(m.dst_sq());
}

inline auto Position::captured_pt(Move m) const noexcept { return type_of(captured_pc(m)); }

inline void Position::reset_en_passant_sq() noexcept { st->enPassantSq = SQ_NONE; }

inline void Position::reset_rule50_count() noexcept { st->rule50Count = 0; }

inline void Position::put_pc(Square s, Piece pc, DirtyThreats* const dts) noexcept {
    assert(is_ok(s) && is_ok(pc));
    Bitboard sBB = square_bb(s);

    auto c  = color_of(pc);
    auto pt = type_of(pc);

    pieceMap[s] = pc;
    colorBBs[c] |= sBB;
    typeBBs[NO_PIECE_TYPE] |= typeBBs[pt] |= sBB;
    auto& pL = pieceList[c][pt];
    auto* pB = base(c);

    indexMap[s] = pL.count();
    pL.push_back(s, pB);
    ++pieceCounts[c];

    if (dts != nullptr)
        update_pc_threats<true>(pc, s, dts);
}

inline Piece Position::remove_pc(Square s, DirtyThreats* const dts) noexcept {
    assert(is_ok(s));
    Bitboard sBB = square_bb(s);

    Piece pc = piece_on(s);
    auto  c  = color_of(pc);
    auto  pt = type_of(pc);
    assert(is_ok(pc) && count(c, pt));

    if (dts != nullptr)
        update_pc_threats<false>(pc, s, dts);

    pieceMap[s] = NO_PIECE;
    colorBBs[c] ^= sBB;
    typeBBs[pt] ^= sBB;
    typeBBs[NO_PIECE_TYPE] ^= sBB;
    auto  idx = indexMap[s];
    auto& pL  = pieceList[c][pt];
    auto* pB  = base(c);
    assert(idx < pL.size());
    Square sq    = pL.back(pB);
    indexMap[sq] = idx;
    //indexMap[s]  = INDEX_NONE;
    pL.at(idx, pB) = sq;
    pL.pop_back();
    assert(pieceCounts[c] != 0);
    --pieceCounts[c];

    return pc;
}

inline Piece Position::move_pc(Square s1, Square s2, DirtyThreats* const dts) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    Bitboard s1s2BB = make_bb(s1, s2);

    Piece pc = piece_on(s1);
    auto  c  = color_of(pc);
    auto  pt = type_of(pc);
    assert(is_ok(pc) && count(c, pt));

    if (dts != nullptr)
        update_pc_threats<false>(pc, s1, dts);

    pieceMap[s1] = NO_PIECE;
    pieceMap[s2] = pc;
    colorBBs[c] ^= s1s2BB;
    typeBBs[pt] ^= s1s2BB;
    typeBBs[NO_PIECE_TYPE] ^= s1s2BB;
    auto  idx = indexMap[s1];
    auto& pL  = pieceList[c][pt];
    auto* pB  = base(c);
    assert(idx < pL.size());
    indexMap[s2] = idx;
    //indexMap[s1] = INDEX_NONE;
    pL.at(idx, pB) = s2;

    if (dts != nullptr)
        update_pc_threats<true>(pc, s2, dts);

    return pc;
}

inline Piece Position::swap_pc(Square s, Piece newPc, DirtyThreats* const dts) noexcept {

    Piece oldPc = remove_pc(s);

    if (dts != nullptr)
        update_pc_threats<false, false>(oldPc, s, dts);

    put_pc(s, newPc);

    if (dts != nullptr)
        update_pc_threats<true, false>(newPc, s, dts);

    return oldPc;
}

template<bool Add>
inline void
DirtyThreats::add(Square sq, Square threatenedSq, Piece pc, Piece threatenedPc) noexcept {
    if constexpr (Add)
    {
        threateningBB |= sq;
        threatenedBB |= threatenedSq;
    }

    list.push_back({sq, threatenedSq, pc, threatenedPc, Add});
}

// Add newly threatened pieces
template<bool Add, bool ComputeRay>
inline void Position::update_pc_threats(Piece pc, Square s, DirtyThreats* const dts) noexcept {

    Bitboard occupancyBB = pieces_bb();

    const auto attacks = [&]() noexcept {
        StdArray<Bitboard, 7> _;
        _[WHITE]  = attacks_bb<PAWN>(s, WHITE);
        _[BLACK]  = attacks_bb<PAWN>(s, BLACK);
        _[KNIGHT] = attacks_bb<KNIGHT>(s);
        _[BISHOP] = attacks_bb<BISHOP>(s, occupancyBB);
        _[ROOK]   = attacks_bb<ROOK>(s, occupancyBB);
        _[QUEEN]  = _[BISHOP] | _[ROOK];
        _[KING]   = attacks_bb<KING>(s);
        return _;
    }();

    Bitboard threatenedBB = (type_of(pc) == PAWN ? attacks[color_of(pc)]  //
                                                 : attacks[type_of(pc)])
                          & occupancyBB;
    while (threatenedBB != 0)
    {
        Square threatenedSq = pop_lsb(threatenedBB);
        Piece  threatenedPc = piece_on(threatenedSq);

        assert(threatenedSq != s);
        assert(is_ok(threatenedPc));

        dts->add<Add>(s, threatenedSq, pc, threatenedPc);
    }

    // clang-format off
    Bitboard slidersBB = (pieces_bb(QUEEN, BISHOP) & attacks[BISHOP])
                     | (pieces_bb(QUEEN, ROOK)   & attacks[ROOK]);
    // clang-format on
    while (slidersBB != 0)
    {
        Square sliderSq = pop_lsb(slidersBB);
        Piece  sliderPc = piece_on(sliderSq);

        assert(is_ok(sliderPc));

        if constexpr (ComputeRay)
        {
            Bitboard discoveredBB = pass_ray_bb(sliderSq, s) & attacks[QUEEN] & occupancyBB;

            if (discoveredBB != 0)
            {
                assert(!more_than_one(discoveredBB));
                Square threatenedSq = lsb(discoveredBB);
                Piece  threatenedPc = piece_on(threatenedSq);

                assert(is_ok(threatenedPc));

                dts->add<!Add>(sliderSq, threatenedSq, sliderPc, threatenedPc);
            }
        }

        dts->add<Add>(sliderSq, s, sliderPc, pc);
    }

    // clang-format off
    Bitboard nonSlidersBB = (pieces_bb(WHITE, PAWN) & attacks[BLACK])
                        | (pieces_bb(BLACK, PAWN) & attacks[WHITE])
                        | (pieces_bb(KNIGHT)      & attacks[KNIGHT])
                        | (pieces_bb(KING)        & attacks[KING]);
    // clang-format on
    while (nonSlidersBB != 0)
    {
        Square nonSliderSq = pop_lsb(nonSlidersBB);
        Piece  nonSliderPc = piece_on(nonSliderSq);

        assert(nonSliderSq != s);
        assert(is_ok(nonSliderPc));

        dts->add<Add>(nonSliderSq, s, nonSliderPc, pc);
    }
}

inline DirtyBoard
Position::do_move(Move m, State& newSt, const TranspositionTable* const tt) noexcept {
    return do_move(m, newSt, check(m), tt);
}

inline constexpr Square* Position::base(Color c) noexcept {  //
    return squaresTable[c].data();
}
inline constexpr const Square* Position::base(Color c) const noexcept {
    return squaresTable[c].data();
}

inline constexpr State* Position::state() const noexcept { return st; }

// Position::SEE
inline bool Position::SEE::operator>=(int threshold) const noexcept {
    return pos.see_ge(move, threshold);
}
inline bool Position::SEE::operator>(int threshold) const noexcept {
    return (*this >= 1 + threshold);
}
inline bool Position::SEE::operator<=(int threshold) const noexcept { return !(*this > threshold); }
inline bool Position::SEE::operator<(int threshold) const noexcept { return !(*this >= threshold); }

inline std::uint8_t rule50_threshold(std::int8_t r50 = -4) noexcept {
    assert(r50 >= -2 * Position::DrawMoveCount);
    return r50 + 2 * Position::DrawMoveCount;
}

}  // namespace DON

#endif  // #ifndef POSITION_H_INCLUDED
