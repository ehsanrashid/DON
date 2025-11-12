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
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

#include "bitboard.h"
#include "misc.h"
#include "types.h"

namespace DON {

class TranspositionTable;

namespace Zobrist {

inline StdArray<Key, PIECE_NB, SQUARE_NB> psq{};
inline StdArray<Key, CASTLING_RIGHTS_NB>  castling{};
inline StdArray<Key, FILE_NB>             enpassant{};
inline Key                                turn{};

}  // namespace Zobrist

// State struct stores information needed to restore Position object
// to its previous state when retract any move.
struct State final {
    // --- Copied when making a move
    StdArray<Key, COLOR_NB>    pawnKey;
    StdArray<Key, COLOR_NB, 2> nonPawnKey;
    StdArray<Square, COLOR_NB> kingSq;
    StdArray<bool, COLOR_NB>   hasCastled;

    Square         epSq;
    Square         capSq;
    CastlingRights castlingRights;
    std::uint8_t   rule50Count;
    std::uint8_t   nullPly;  // Plies from Null-Move
    bool           hasRule50High;

    // --- Not copied when making a move (will be recomputed anyhow)
    Key                                         key;
    Bitboard                                    checkers;
    StdArray<Bitboard, PIECE_TYPE_NB>           checks;
    StdArray<Bitboard, COLOR_NB>                pinners;
    StdArray<Bitboard, COLOR_NB>                blockers;
    StdArray<Bitboard, COLOR_NB, PIECE_TYPE_NB> attacks;
    std::int16_t                                repetition;
    Piece                                       capturedPiece;
    Piece                                       promotedPiece;

    State* preSt;
};

inline constexpr std::uint8_t R50_OFFSET = 14;
inline constexpr std::uint8_t R50_FACTOR = 8;

// Position class stores information regarding the board representation as
// pieces, active color, hash keys, castling info, etc. (Size = 192)
// Important methods are do_move() and undo_move(),
// used by the search to update node info when traversing the search tree.
class Position final {
   public:
    using PieceArray = StdArray<Piece, SQUARE_NB>;

    static void init() noexcept;

    static inline bool         Chess960      = false;
    static inline std::uint8_t DrawMoveCount = 50;

    Position() noexcept = default;

   private:
    Position(const Position&) noexcept            = delete;
    Position(Position&&) noexcept                 = delete;
    Position& operator=(const Position&) noexcept = default;
    Position& operator=(Position&&) noexcept      = delete;

   public:
    // FEN string input/output
    void        set(std::string_view fens, State* const newSt) noexcept;
    void        set(std::string_view code, Color c, State* const newSt) noexcept;
    void        set(const Position& pos, State* const newSt) noexcept;
    std::string fen(bool full = true) const noexcept;

    // Position representation
    [[nodiscard]] const PieceArray& piece_arr() const noexcept;

    Piece    piece_on(Square s) const noexcept;
    bool     empty_on(Square s) const noexcept;
    Bitboard pieces() const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(PieceTypes... pts) const noexcept;
    Bitboard pieces(Color c) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(Color c, PieceTypes... pts) const noexcept;
    template<PieceType PT>
    Bitboard pieces(Color c) const noexcept;

    std::uint8_t count(Piece pc) const noexcept;
    std::uint8_t count(Color c, PieceType pt) const noexcept;
    template<PieceType PT>
    std::uint8_t count(Color c) const noexcept;
    template<PieceType PT>
    std::uint8_t count() const noexcept;

    template<PieceType PT>
    Square square(Color c) const noexcept;
    Square king_sq(Color c) const noexcept;
    Square ep_sq() const noexcept;
    Square cap_sq() const noexcept;

    CastlingRights castling_rights() const noexcept;

    bool   can_castle(CastlingRights cr) const noexcept;
    bool   castling_impeded(CastlingRights cr) const noexcept;
    Square castling_rook_sq(CastlingRights cr) const noexcept;
    auto   castling_rights_mask(Square org, Square dst) const noexcept;

    // Other info
    Bitboard checkers() const noexcept;
    Bitboard checks(PieceType pt) const noexcept;
    Bitboard pinners(Color c) const noexcept;
    Bitboard pinners() const noexcept;
    Bitboard blockers(Color c) const noexcept;

    template<PieceType PT>
    Bitboard attacks(Color c) const noexcept;
    Bitboard attacks_lesser(Color c, PieceType pt) const noexcept;
    Piece    captured_piece() const noexcept;
    Piece    promoted_piece() const noexcept;

    // Slide attacker to a given square
    Bitboard xslide_attackers_to(Square s) const noexcept;
    Bitboard slide_attackers_to(Square s, Bitboard occupied) const noexcept;
    Bitboard slide_attackers_to(Square s) const noexcept;
    // All attackers to a given square
    Bitboard attackers_to(Square s, Bitboard occupied) const noexcept;
    Bitboard attackers_to(Square s) const noexcept;

    bool has_attackers_to(Bitboard attackers, Square s, Bitboard occupied) const noexcept;
    bool has_attackers_to(Bitboard attackers, Square s) const noexcept;

    // Attacks from a piece type
    template<PieceType PT>
    Bitboard attacks_by(Color c) const noexcept;

    // Doing and undoing moves
    DirtyBoard
    do_move(Move m, State& newSt, bool inCheck, const TranspositionTable* tt = nullptr) noexcept;
    DirtyBoard do_move(Move m, State& newSt, const TranspositionTable* tt = nullptr) noexcept;
    void       undo_move(Move m) noexcept;
    void       do_null_move(State& newSt, const TranspositionTable* tt = nullptr) noexcept;
    void       undo_null_move() noexcept;

    // Properties of moves
    bool  pseudo_legal(Move m) const noexcept;
    bool  legal(Move m) const noexcept;
    bool  capture(Move m) const noexcept;
    bool  capture_promo(Move m) const noexcept;
    bool  check(Move m) const noexcept;
    bool  dbl_check(Move m) const noexcept;
    bool  fork(Move m) const noexcept;
    Piece moved_piece(Move m) const noexcept;
    Piece captured_piece(Move m) const noexcept;
    auto  captured(Move m) const noexcept;

    // Hash keys
    Key key(std::int8_t r50 = 0) const noexcept;
    Key pawn_key(Color c) const noexcept;
    Key pawn_key() const noexcept;
    Key minor_key(Color c) const noexcept;
    Key minor_key() const noexcept;
    Key major_key(Color c) const noexcept;
    Key major_key() const noexcept;
    Key non_pawn_key(Color c) const noexcept;
    Key non_pawn_key() const noexcept;

    Key compute_material_key() const noexcept;
    Key compute_move_key(Move m) const noexcept;

    // Static Exchange Evaluation
    [[nodiscard]] auto see(Move m) const noexcept { return SEE(*this, m); }

    // Other properties
    Color        active_color() const noexcept;
    std::int16_t ply() const noexcept;
    std::int32_t move_num() const noexcept;
    std::int32_t phase() const noexcept;

    std::int16_t rule50_count() const noexcept;
    std::int16_t null_ply() const noexcept;
    std::int16_t repetition() const noexcept;

    bool is_repetition(std::int16_t ply) const noexcept;
    bool is_draw(std::int16_t ply,
                 bool         rule50Enabled    = true,
                 bool         stalemateEnabled = false) const noexcept;
    bool has_repeated() const noexcept;
    bool is_upcoming_repetition(std::int16_t ply) const noexcept;

    Value non_pawn_value(Color c) const noexcept;
    Value non_pawn_value() const noexcept;
    bool  has_non_pawn(Color c) const noexcept;
    bool  has_castled(Color c) const noexcept;
    bool  has_rule50_high() const noexcept;
    bool  bishop_paired(Color c) const noexcept;
    bool  bishop_opposite() const noexcept;

    std::size_t bucket() const noexcept;

    int   std_material() const noexcept;
    Value material() const noexcept;
    Value evaluate() const noexcept;
    Value bonus() const noexcept;

    void  put_piece(Square s, Piece pc, DirtyThreats* const dts = nullptr) noexcept;
    Piece remove_piece(Square s, DirtyThreats* const dts = nullptr) noexcept;

    void flip() noexcept;
    void mirror() noexcept;

    // Position consistency check, for debugging
#if !defined(NDEBUG)
    Key compute_key() const noexcept;
    Key compute_minor_key() const noexcept;
    Key compute_major_key() const noexcept;
    Key compute_non_pawn_key() const noexcept;

    bool pos_is_ok() const noexcept;
#endif

    // Used by NNUE
    State* state() const noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept;

   private:
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
    bool can_enpassant(Color ac, Square epSq, Bitboard* const epAttackers = nullptr) const noexcept;

    // Other helpers
    Piece move_piece(Square s1, Square s2, DirtyThreats* const dts = nullptr) noexcept;
    Piece swap_piece(Square s, Piece newPc, DirtyThreats* const dts = nullptr) noexcept;

    template<bool PutPiece, bool ComputeRay = true>
    void update_piece_threats(Piece pc, Square s, DirtyThreats* const dts) noexcept;

    template<bool Do>
    void do_castling(Color               ac,
                     Square              org,
                     Square&             dst,
                     Square&             rOrg,
                     Square&             rDst,
                     DirtyPiece* const   dp  = nullptr,
                     DirtyThreats* const dts = nullptr) noexcept;

    Key adjust_key(Key k, std::int8_t r50 = 0) const noexcept;

    void reset_ep_sq() noexcept;
    void reset_rule50_count() noexcept;
    void reset_repetitions() noexcept;

    // Static Exchange Evaluation
    bool see_ge(Move m, int threshold) const noexcept;

    // Data members
    PieceArray                                      pieceArr;
    StdArray<Bitboard, PIECE_TYPE_NB>               typeBB;
    StdArray<Bitboard, COLOR_NB>                    colorBB;
    StdArray<std::uint8_t, PIECE_NB>                pieceCount;
    StdArray<Bitboard, COLOR_NB * CASTLING_SIDE_NB> castlingPath;
    StdArray<Square, COLOR_NB * CASTLING_SIDE_NB>   castlingRookSq;
    StdArray<std::uint8_t, COLOR_NB * FILE_NB + 1>  castlingRightsMask;
    Color                                           activeColor;
    std::int16_t                                    gamePly;
    State*                                          st;
};

inline const Position::PieceArray& Position::piece_arr() const noexcept { return pieceArr; }

inline Piece Position::piece_on(Square s) const noexcept {
    assert(is_ok(s));
    return pieceArr[s];
}

inline bool Position::empty_on(Square s) const noexcept { return piece_on(s) == NO_PIECE; }

inline Bitboard Position::pieces() const noexcept { return typeBB[ALL_PIECE]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces(PieceTypes... pts) const noexcept {
    return (typeBB[pts] | ...);
}

inline Bitboard Position::pieces(Color c) const noexcept { return colorBB[c]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces(Color c, PieceTypes... pts) const noexcept {
    return pieces(c) & pieces(pts...);
}

template<PieceType PT>
inline Bitboard Position::pieces(Color c) const noexcept {
    if constexpr (PT == KNIGHT)
        return pieces(c, KNIGHT) & (~blockers(c));
    if constexpr (PT == BISHOP)
        return pieces(c, BISHOP) & (~blockers(c) | attacks_bb<BISHOP>(king_sq(c)));
    if constexpr (PT == ROOK)
        return pieces(c, ROOK) & (~blockers(c) | attacks_bb<ROOK>(king_sq(c)));
    return pieces(c, PT);
}

inline std::uint8_t Position::count(Piece pc) const noexcept { return pieceCount[pc]; }

inline std::uint8_t Position::count(Color c, PieceType pt) const noexcept {
    return count(make_piece(c, pt));
}

template<PieceType PT>
inline std::uint8_t Position::count(Color c) const noexcept {
    return count(c, PT);
}

template<PieceType PT>
inline std::uint8_t Position::count() const noexcept {
    return count<PT>(WHITE) + count<PT>(BLACK);
}

template<PieceType PT>
inline Square Position::square(Color c) const noexcept {
    assert(count<PT>(c) == 1);
    return lsb(pieces(c, PT));
}

inline Square Position::king_sq(Color c) const noexcept { return st->kingSq[c]; }

inline Square Position::ep_sq() const noexcept { return st->epSq; }

inline Square Position::cap_sq() const noexcept { return st->capSq; }

inline CastlingRights Position::castling_rights() const noexcept { return st->castlingRights; }

inline bool Position::can_castle(CastlingRights cr) const noexcept {
    return castling_rights() & int(cr);
}

inline bool Position::castling_impeded(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return pieces() & castlingPath[lsb(cr)];
}

inline Square Position::castling_rook_sq(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return castlingRookSq[lsb(cr)];
}

inline auto Position::castling_rights_mask(Square org, Square dst) const noexcept {
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

    return castlingRightsMask[Indices[org]] | castlingRightsMask[Indices[dst]];
}

// clang-format off

inline Bitboard Position::xslide_attackers_to(Square s) const noexcept {
    return (pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s))
         | (pieces(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s));
}
inline Bitboard Position::slide_attackers_to(Square s, Bitboard occupied) const noexcept {
    return (pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s) ? pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupied) : 0)
         | (pieces(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s) ? pieces(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s, occupied) : 0);
}
inline Bitboard Position::slide_attackers_to(Square s) const noexcept {
    return slide_attackers_to(s, pieces());
}
// Computes a bitboard of all pieces which attack a given square.
// Slider attacks use the occupied bitboard to indicate occupancy.
inline Bitboard Position::attackers_to(Square s, Bitboard occupied) const noexcept {
    return slide_attackers_to(s, occupied)
         | (pieces(WHITE, PAWN) & attacks_bb<PAWN  >(s, BLACK))
         | (pieces(BLACK, PAWN) & attacks_bb<PAWN  >(s, WHITE))
         | (pieces(KNIGHT     ) & attacks_bb<KNIGHT>(s))
         | (pieces(KING       ) & attacks_bb<KING  >(s));
}
inline Bitboard Position::attackers_to(Square s) const noexcept {
    return attackers_to(s, pieces());
}

inline bool Position::has_attackers_to(Bitboard attackers, Square s, Bitboard occupied) const noexcept {
    return ((attackers & pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s))
         && (attackers & pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupied)))
        || ((attackers & pieces(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s))
         && (attackers & pieces(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s, occupied)))
        ||  (attackers & ((pieces(WHITE, PAWN) & attacks_bb<PAWN  >(s, BLACK))
                        | (pieces(BLACK, PAWN) & attacks_bb<PAWN  >(s, WHITE))))
        ||  (attackers & pieces(KNIGHT       ) & attacks_bb<KNIGHT>(s))
        ||  (attackers & pieces(KING         ) & attacks_bb<KING  >(s));
}
inline bool Position::has_attackers_to(Bitboard attackers, Square s) const noexcept {
    return has_attackers_to(attackers, s, pieces());
}

// clang-format on

// Computes attacks from a piece type for a given color.
template<PieceType PT>
inline Bitboard Position::attacks_by(Color c) const noexcept {
    if constexpr (PT == PAWN)
        return pawn_attacks_bb(pieces(c, PAWN), c);
    else
    {
        Bitboard attacks = 0;
        Bitboard pc      = pieces(c, PT);
        while (pc)
            attacks |= attacks_bb<PT>(pop_lsb(pc), pieces());
        return attacks;
    }
}

inline Bitboard Position::checkers() const noexcept { return st->checkers; }

inline Bitboard Position::checks(PieceType pt) const noexcept { return st->checks[pt]; }

inline Bitboard Position::pinners(Color c) const noexcept { return st->pinners[c]; }

inline Bitboard Position::pinners() const noexcept { return pinners(WHITE) | pinners(BLACK); }

inline Bitboard Position::blockers(Color c) const noexcept { return st->blockers[c]; }

// clang-format off

template<PieceType PT>
inline Bitboard Position::attacks(Color c) const noexcept { return st->attacks[c][PT]; }
inline Bitboard Position::attacks_lesser(Color c, PieceType pt) const noexcept {
    return st->attacks[c][pt == KNIGHT || pt == BISHOP ? PAWN : pt - 1];
}

inline Piece Position::captured_piece() const noexcept { return st->capturedPiece; }

inline Piece Position::promoted_piece() const noexcept { return st->promotedPiece; }

inline Key Position::adjust_key(Key k, std::int8_t r50) const noexcept {
    std::int16_t idx = st->rule50Count + r50 - R50_OFFSET;
    return idx < 0 ? k : k ^ make_hash(idx / R50_FACTOR);
}

inline Key Position::key(std::int8_t r50) const noexcept { return adjust_key(st->key, r50); }

inline Key Position::pawn_key(Color c) const noexcept { return st->pawnKey[c]; }

inline Key Position::pawn_key() const noexcept { return pawn_key(WHITE) ^ pawn_key(BLACK); }

inline Key Position::minor_key(Color c) const noexcept { return st->nonPawnKey[c][0]; }

inline Key Position::minor_key() const noexcept { return minor_key(WHITE) ^ minor_key(BLACK); }

inline Key Position::major_key(Color c) const noexcept { return st->nonPawnKey[c][1]; }

inline Key Position::major_key() const noexcept { return major_key(WHITE) ^ major_key(BLACK); }

inline Key Position::non_pawn_key(Color c) const noexcept { return minor_key(c) ^ major_key(c) ^ Zobrist::psq[make_piece(c, KING)][king_sq(c)]; }

inline Key Position::non_pawn_key() const noexcept { return non_pawn_key(WHITE) ^ non_pawn_key(BLACK); }

inline Value Position::non_pawn_value(Color c) const noexcept {
    Value nonPawnValue = VALUE_ZERO;

    for (Piece pc : NonPawnPieces[c])
        nonPawnValue += PIECE_VALUE[type_of(pc)] * count(pc);

    return nonPawnValue;
}

inline Value Position::non_pawn_value() const noexcept { return non_pawn_value(WHITE) + non_pawn_value(BLACK); }

inline bool Position::has_non_pawn(Color c) const noexcept {

    for (Piece pc : NonPawnPieces[c])
        if (count(pc))
            return true;
    return false;
}

// clang-format on

inline Color Position::active_color() const noexcept { return activeColor; }

inline std::int16_t Position::ply() const noexcept { return gamePly; }

inline std::int32_t Position::move_num() const noexcept {
    return 1 + (ply() - (active_color() == BLACK)) / 2;
}

inline std::int32_t Position::phase() const noexcept {
    return std::max(24 - count<KNIGHT>() - count<BISHOP>() - 2 * count<ROOK>() - 4 * count<QUEEN>(),
                    0);
}

inline std::int16_t Position::rule50_count() const noexcept { return st->rule50Count; }

inline std::int16_t Position::null_ply() const noexcept { return st->nullPly; }

inline std::int16_t Position::repetition() const noexcept { return st->repetition; }

inline bool Position::has_castled(Color c) const noexcept { return st->hasCastled[c]; }

inline bool Position::has_rule50_high() const noexcept { return st->hasRule50High; }

inline bool Position::bishop_paired(Color c) const noexcept {
    return (pieces(c, BISHOP) & color_bb<WHITE>()) && (pieces(c, BISHOP) & color_bb<BLACK>());
}

inline bool Position::bishop_opposite() const noexcept {
    return count<BISHOP>(WHITE) == 1 && count<BISHOP>(BLACK) == 1
        && color_opposite(square<BISHOP>(WHITE), square<BISHOP>(BLACK));
}

inline std::size_t Position::bucket() const noexcept { return (count<ALL_PIECE>() - 1) / 4; }

inline int Position::std_material() const noexcept {
    return 1 * count<PAWN>()                          //
         + 3 * count<KNIGHT>() + 3 * count<BISHOP>()  //
         + 5 * count<ROOK>()                          //
         + 8 * count<QUEEN>();
}

inline Value Position::material() const noexcept { return 535 * count<PAWN>() + non_pawn_value(); }

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the side to move. It can be divided by VALUE_PAWN to get
// an approximation of the material advantage on the board in terms of pawns.
inline Value Position::evaluate() const noexcept {
    Color ac = active_color();
    return VALUE_PAWN * (count<PAWN>(ac) - count<PAWN>(~ac))
         + (non_pawn_value(ac) - non_pawn_value(~ac));
}

inline Value Position::bonus() const noexcept {
    Color ac = active_color();
    // clang-format off
    return (+20 * (bishop_paired(ac) - bishop_paired(~ac))
            +56 * ((can_castle( ac & ANY_CASTLING) || has_castled( ac))
                 - (can_castle(~ac & ANY_CASTLING) || has_castled(~ac))))
         * (1.0 - 4.1250e-2 * phase());
    // clang-format on
}

inline bool Position::capture(Move m) const noexcept {
    assert(pseudo_legal(m));
    return (m.type_of() != CASTLING && !empty_on(m.dst_sq())) || m.type_of() == EN_PASSANT;
}

inline bool Position::capture_promo(Move m) const noexcept {
    return capture(m) || m.type_of() == PROMOTION;
}

inline Piece Position::moved_piece(Move m) const noexcept {
    assert(pseudo_legal(m));
    return piece_on(m.org_sq());
}

inline Piece Position::captured_piece(Move m) const noexcept {
    assert(pseudo_legal(m));
    assert(m.type_of() != CASTLING);
    return m.type_of() == EN_PASSANT ? make_piece(~active_color(), PAWN) : piece_on(m.dst_sq());
}

inline auto Position::captured(Move m) const noexcept { return type_of(captured_piece(m)); }

inline void Position::reset_ep_sq() noexcept { st->epSq = SQ_NONE; }

inline void Position::reset_rule50_count() noexcept { st->rule50Count = 0; }

inline void Position::reset_repetitions() noexcept {
    auto* cSt = st;
    while (cSt != nullptr)
    {
        cSt->repetition = 0;

        cSt = cSt->preSt;
    }
}

inline void Position::put_piece(Square s, Piece pc, DirtyThreats* const dts) noexcept {
    assert(is_ok(s) && is_ok(pc));

    pieceArr[s]  = pc;
    Bitboard sBB = square_bb(s);
    typeBB[ALL_PIECE] |= typeBB[type_of(pc)] |= sBB;
    colorBB[color_of(pc)] |= sBB;
    ++pieceCount[pc];
    ++pieceCount[pc & PIECE_TYPE_NB];

    if (dts != nullptr)
        update_piece_threats<true>(pc, s, dts);
}

inline Piece Position::remove_piece(Square s, DirtyThreats* const dts) noexcept {
    assert(is_ok(s));

    Piece pc = piece_on(s);
    assert(is_ok(pc) && count(pc));

    if (dts != nullptr)
        update_piece_threats<false>(pc, s, dts);

    pieceArr[s]  = NO_PIECE;
    Bitboard sBB = square_bb(s);
    typeBB[ALL_PIECE] ^= sBB;
    typeBB[type_of(pc)] ^= sBB;
    colorBB[color_of(pc)] ^= sBB;
    --pieceCount[pc];
    --pieceCount[pc & PIECE_TYPE_NB];
    return pc;
}

inline Piece Position::move_piece(Square s1, Square s2, DirtyThreats* const dts) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    Piece pc = piece_on(s1);
    assert(is_ok(pc));

    if (dts != nullptr)
        update_piece_threats<false>(pc, s1, dts);

    pieceArr[s1]    = NO_PIECE;
    pieceArr[s2]    = pc;
    Bitboard s1s2BB = make_bitboard(s1, s2);
    typeBB[ALL_PIECE] ^= s1s2BB;
    typeBB[type_of(pc)] ^= s1s2BB;
    colorBB[color_of(pc)] ^= s1s2BB;

    if (dts != nullptr)
        update_piece_threats<true>(pc, s2, dts);

    return pc;
}

inline Piece Position::swap_piece(Square s, Piece newPc, DirtyThreats* const dts) noexcept {

    Piece oldPc = remove_piece(s);

    if (dts != nullptr)
        update_piece_threats<false, false>(oldPc, s, dts);

    put_piece(s, newPc);

    if (dts != nullptr)
        update_piece_threats<true, false>(newPc, s, dts);

    return oldPc;
}

template<bool PutPiece>
inline void
DirtyThreats::add(Piece pc, Piece threatenedPc, Square sq, Square threatenedSq) noexcept {
    if (PutPiece)
    {
        threateningBB |= sq;
        threatenedBB |= threatenedSq;
    }

    list.push_back({pc, threatenedPc, sq, threatenedSq, PutPiece});
}

// Add newly threatened pieces
template<bool PutPiece, bool ComputeRay>
inline void Position::update_piece_threats(Piece pc, Square s, DirtyThreats* const dts) noexcept {
    Bitboard occupied = pieces();

    Bitboard rAttacks = attacks_bb<ROOK>(s, occupied);
    Bitboard bAttacks = attacks_bb<BISHOP>(s, occupied);
    Bitboard qAttacks = rAttacks | bAttacks;

    Bitboard threatened;

    switch (type_of(pc))
    {
    case PAWN :
        threatened = attacks_bb<PAWN>(s, color_of(pc));
        break;
    case KNIGHT :
        threatened = attacks_bb<KNIGHT>(s);
        break;
    case BISHOP :
        threatened = bAttacks;
        break;
    case ROOK :
        threatened = rAttacks;
        break;
    case QUEEN :
        threatened = qAttacks;
        break;
    case KING :
        threatened = attacks_bb<KING>(s);
        break;
    default :
        assert(false);
        threatened = 0;
    }

    threatened &= occupied;

    while (threatened)
    {
        Square threatenedSq = pop_lsb(threatened);
        Piece  threatenedPc = piece_on(threatenedSq);

        assert(threatenedSq != s);
        assert(is_ok(threatenedPc));

        dts->add<PutPiece>(pc, threatenedPc, s, threatenedSq);
    }

    Bitboard sliders = (pieces(QUEEN, BISHOP) & bAttacks)  //
                     | (pieces(QUEEN, ROOK) & rAttacks);

    Bitboard incomingThreats = (attacks_bb<KNIGHT>(s) & pieces(KNIGHT))            //
                             | (attacks_bb<PAWN>(s, WHITE) & pieces(BLACK, PAWN))  //
                             | (attacks_bb<PAWN>(s, BLACK) & pieces(WHITE, PAWN))  //
                             | (attacks_bb<KING>(s) & pieces(KING));

    while (sliders)
    {
        Square sliderSq = pop_lsb(sliders);
        Piece  sliderPc = piece_on(sliderSq);

        Bitboard ray = pass_ray_bb(sliderSq, s) & ~between_bb(sliderSq, s);
        threatened   = ray & qAttacks & occupied;

        assert(!more_than_one(threatened));
        if (ComputeRay && threatened)
        {
            Square threatenedSq = lsb(threatened);
            Piece  threatenedPc = piece_on(threatenedSq);

            dts->add<!PutPiece>(sliderPc, threatenedPc, sliderSq, threatenedSq);
        }
        dts->add<PutPiece>(sliderPc, pc, sliderSq, s);
    }

    // Add threats of sliders that were already threatening s,
    // sliders are already handled in the loop above
    while (incomingThreats)
    {
        Square srcSq = pop_lsb(incomingThreats);
        Piece  srcPc = piece_on(srcSq);

        assert(srcSq != s);
        assert(is_ok(srcPc));

        dts->add<PutPiece>(srcPc, pc, srcSq, s);
    }
}

inline DirtyBoard Position::do_move(Move m, State& newSt, const TranspositionTable* tt) noexcept {
    return do_move(m, newSt, check(m), tt);
}

inline State* Position::state() const noexcept { return st; }

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
