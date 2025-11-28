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
#include <initializer_list>
#include <iosfwd>
#include <string>
#include <string_view>

#include "bitboard.h"
#include "misc.h"
#include "types.h"

namespace DON {

class TranspositionTable;

struct Zobrist final {
   public:
    static void init() noexcept;

    static Key piece_square(Piece pc, Square s) noexcept {
        assert(is_ok(pc) && is_ok(s));
        return PieceSquare[pc][s];
    }

    static Key castling(CastlingRights cr) noexcept { return Castling[cr]; }

    static Key enpassant(Square ep) noexcept {
        assert(is_ok(ep));
        return Enpassant[file_of(ep)];
    }

    static Key turn() noexcept { return Turn; }

    static Key mr50(std::int16_t rule50Count) noexcept {
        std::int16_t idx = rule50Count - R50Offset;
        return idx < 0 ? 0 : MR50[std::min(idx / R50Factor, int(MR50.size()) - 1)];
    }

    static constexpr std::size_t PawnOffset = 8;

   private:
    Zobrist() noexcept                          = delete;
    Zobrist(const Zobrist&) noexcept            = delete;
    Zobrist(Zobrist&&) noexcept                 = delete;
    Zobrist& operator=(const Zobrist&) noexcept = delete;
    Zobrist& operator=(Zobrist&&) noexcept      = delete;

    static inline StdArray<Key, PIECE_NB, SQUARE_NB> PieceSquare{};
    static inline StdArray<Key, CASTLING_RIGHTS_NB>  Castling{};
    static inline StdArray<Key, FILE_NB>             Enpassant{};
    static inline Key                                Turn{};

    static constexpr std::uint8_t R50Offset = 14;
    static constexpr std::uint8_t R50Factor = 8;

    static inline StdArray<Key, (MAX_PLY + 1 - R50Offset) / R50Factor + 2> MR50{};
};

// State struct stores information needed to restore Position object
// to its previous state when retract any move.
struct State final {
   public:
    State() noexcept = default;

    void clear() noexcept;

    // --- Copied when making a move
    StdArray<Key, COLOR_NB>    pawnKey;
    StdArray<Key, COLOR_NB, 2> nonPawnKey;
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

// Position class stores information regarding the board representation as
// pieces, active color, hash keys, castling info, etc. (Size = 760)
// Important methods are do_move() and undo_move(),
// used by the search to update node info when traversing the search tree.
class Position final {
   public:
    static void init() noexcept;

    static inline bool         Chess960      = false;
    static inline std::uint8_t DrawMoveCount = 50;

    Position() noexcept = default;

   private:
    Position(const Position&) noexcept = delete;
    Position(Position&&) noexcept      = delete;
    Position& operator=(const Position& pos) noexcept;
    Position& operator=(Position&&) noexcept = delete;

   public:
    void clear() noexcept;

    // FEN string input/output
    void        set(std::string_view fens, State* const newSt) noexcept;
    void        set(std::string_view code, Color c, State* const newSt) noexcept;
    void        set(const Position& pos, State* const newSt) noexcept;
    std::string fen(bool full = true) const noexcept;

    // Position representation
    [[nodiscard]] const auto& piece_map() const noexcept;
    [[nodiscard]] const auto& color_bb() const noexcept;
    [[nodiscard]] const auto& type_bb() const noexcept;
    [[nodiscard]] const auto& piece_lists() const noexcept;

    [[nodiscard]] Piece    operator[](Square s) const noexcept;
    [[nodiscard]] Bitboard operator[](Color c) const noexcept;
    [[nodiscard]] Bitboard operator[](PieceType pt) const noexcept;

    Piece piece_on(Square s) const noexcept;
    bool  empty_on(Square s) const noexcept;

    Bitboard pieces(Color c) const noexcept;
    Bitboard pieces() const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(PieceTypes... pts) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(Color c, PieceTypes... pts) const noexcept;
    template<PieceType PT>
    Bitboard pieces(Color c) const noexcept;

    [[nodiscard]] const auto& piece_list(Color c, PieceType pt) const noexcept;
    template<PieceType PT>
    [[nodiscard]] const auto& piece_list(Color c) const noexcept;
    [[nodiscard]] const auto& piece_list(Piece pc) const noexcept;

    [[nodiscard]] auto& piece_list(Color c, PieceType pt) noexcept;
    template<PieceType PT>
    [[nodiscard]] auto& piece_list(Color c) noexcept;
    [[nodiscard]] auto& piece_list(Piece pc) noexcept;

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
    Bitboard less_attacks(Color c, PieceType pt) const noexcept;
    Bitboard threats(Color c) const noexcept;

    Piece captured_piece() const noexcept;
    Piece promoted_piece() const noexcept;

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

    // clang-format off
    // Doing and undoing moves
    DirtyBoard do_move(Move m, State& newSt, bool isCheck, const TranspositionTable* const tt = nullptr) noexcept;
    DirtyBoard do_move(Move m, State& newSt, const TranspositionTable* const tt = nullptr) noexcept;
    void       undo_move(Move m) noexcept;
    void       do_null_move(State& newSt, const TranspositionTable* const tt = nullptr) noexcept;
    void       undo_null_move() noexcept;
    // clang-format on

    // Properties of moves
    bool  pseudo_legal(Move m) const noexcept;
    bool  legal(Move m) const noexcept;
    bool  capture(Move m) const noexcept;
    bool  capture_queenpromo(Move m) const noexcept;
    bool  check(Move m) const noexcept;
    bool  dbl_check(Move m) const noexcept;
    bool  fork(Move m) const noexcept;
    Piece moved_piece(Move m) const noexcept;
    Piece captured_piece(Move m) const noexcept;
    auto  captured(Move m) const noexcept;

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
    Color        active_color() const noexcept;
    std::int16_t ply() const noexcept;
    std::int32_t move_num() const noexcept;

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

    bool _is_ok() const noexcept;
#endif

    // Used by NNUE
    State* state() const noexcept;

    operator std::string() const noexcept;

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

    template<bool Add, bool ComputeRay = true>
    void update_piece_threats(Piece pc, Square s, DirtyThreats* const dts) noexcept;

    template<bool Do>
    void do_castling(Color             ac,
                     Square            org,
                     Square&           dst,
                     Square&           rOrg,
                     Square&           rDst,
                     DirtyBoard* const db = nullptr) noexcept;

    void reset_ep_sq() noexcept;
    void reset_rule50_count() noexcept;
    void reset_repetitions() noexcept;

    // Static Exchange Evaluation
    bool see_ge(Move m, int threshold) const noexcept;

    static constexpr std::size_t InvalidIndex = 64;

    StdArray<FixedVector<Square, 20>, COLOR_NB>    pawnLists;
    StdArray<FixedVector<Square, 16>, COLOR_NB, 4> nonPawnLists;
    StdArray<FixedVector<Square, 01>, COLOR_NB>    kingLists;

    StdArray<IFixedVector<Square>*, COLOR_NB, PIECE_TYPE_NB - 2> pieceLists{
      {{
         &pawnLists[WHITE],        //
         &nonPawnLists[WHITE][0],  //
         &nonPawnLists[WHITE][1],  //
         &nonPawnLists[WHITE][2],  //
         &nonPawnLists[WHITE][3],  //
         &kingLists[WHITE]         //
       },
       {
         &pawnLists[BLACK],        //
         &nonPawnLists[BLACK][0],  //
         &nonPawnLists[BLACK][1],  //
         &nonPawnLists[BLACK][2],  //
         &nonPawnLists[BLACK][3],  //
         &kingLists[BLACK]         //
       }}};

    StdArray<std::uint8_t, SQUARE_NB>               squareIndex;
    StdArray<Piece, SQUARE_NB>                      pieceMap;
    StdArray<Bitboard, COLOR_NB>                    colorBB;
    StdArray<Bitboard, PIECE_TYPE_NB>               typeBB;
    StdArray<std::uint8_t, COLOR_NB>                pieceCount;
    StdArray<Bitboard, COLOR_NB * CASTLING_SIDE_NB> castlingPath;
    StdArray<Square, COLOR_NB * CASTLING_SIDE_NB>   castlingRookSq;
    StdArray<std::uint8_t, COLOR_NB * FILE_NB + 1>  castlingRightsMask;
    Color                                           activeColor;
    std::int16_t                                    gamePly;
    State*                                          st;
};

//static_assert(sizeof(Position) == 760, "Position size");

inline const auto& Position::piece_map() const noexcept { return pieceMap; }

inline const auto& Position::color_bb() const noexcept { return colorBB; }

inline const auto& Position::type_bb() const noexcept { return typeBB; }

inline const auto& Position::piece_lists() const noexcept { return pieceLists; }

inline Piece Position::operator[](Square s) const noexcept { return pieceMap[s]; }

inline Bitboard Position::operator[](Color c) const noexcept { return colorBB[c]; }

inline Bitboard Position::operator[](PieceType pt) const noexcept { return typeBB[pt]; }

inline Piece Position::piece_on(Square s) const noexcept { return pieceMap[s]; }

inline bool Position::empty_on(Square s) const noexcept { return piece_on(s) == NO_PIECE; }

inline Bitboard Position::pieces(Color c) const noexcept { return colorBB[c]; }

inline Bitboard Position::pieces() const noexcept { return typeBB[NO_PIECE_TYPE]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces(PieceTypes... pts) const noexcept {
    return (typeBB[pts] | ...);
}

template<typename... PieceTypes>
inline Bitboard Position::pieces(Color c, PieceTypes... pts) const noexcept {
    return pieces(c) & pieces(pts...);
}

template<PieceType PT>
inline Bitboard Position::pieces(Color c) const noexcept {
    return pieces(c, PT);
}

inline const auto& Position::piece_list(Color c, PieceType pt) const noexcept {
    return *pieceLists[c][pt - 1];
}

template<PieceType PT>
inline const auto& Position::piece_list(Color c) const noexcept {
    return piece_list(c, PT);
}

inline const auto& Position::piece_list(Piece pc) const noexcept {
    return piece_list(color_of(pc), type_of(pc));
}

inline auto& Position::piece_list(Color c, PieceType pt) noexcept {  //
    return *pieceLists[c][pt - 1];
}

template<PieceType PT>
inline auto& Position::piece_list(Color c) noexcept {
    return piece_list(c, PT);
}

inline auto& Position::piece_list(Piece pc) noexcept {
    return piece_list(color_of(pc), type_of(pc));
}

inline std::uint8_t Position::count(Color c, PieceType pt) const noexcept {
    return (*pieceLists[c][pt - 1]).size();
}

template<PieceType PT>
inline std::uint8_t Position::count(Color c) const noexcept {
    return count(c, PT);
}

inline std::uint8_t Position::count(Piece pc) const noexcept {
    return count(color_of(pc), type_of(pc));
}

inline std::uint8_t Position::count(Color c) const noexcept { return pieceCount[c]; }

inline std::uint8_t Position::count() const noexcept { return count(WHITE) + count(BLACK); }

template<PieceType PT>
inline std::uint8_t Position::count() const noexcept {
    return count<PT>(WHITE) + count<PT>(BLACK);
}

template<PieceType PT>
inline Square Position::square(Color c) const noexcept {
    assert(count<PT>(c) == 1);
    return piece_list<PT>(c)[0];
}

inline Square Position::ep_sq() const noexcept { return st->epSq; }

inline Square Position::cap_sq() const noexcept { return st->capSq; }

inline CastlingRights Position::castling_rights() const noexcept { return st->castlingRights; }

inline bool Position::can_castle(CastlingRights cr) const noexcept {
    return castling_rights() & int(cr);
}

inline bool Position::castling_impeded(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return pieces() & castlingPath[cr_lsb(cr)];
}

inline Square Position::castling_rook_sq(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return castlingRookSq[cr_lsb(cr)];
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
inline Bitboard Position::less_attacks(Color c, PieceType pt) const noexcept {
    return st->attacks[c][pt == KNIGHT || pt == BISHOP ? PAWN : pt - 1];
}
inline Bitboard Position::threats(Color c) const noexcept {
    return st->attacks[c][KING] & ~st->attacks[~c][KING];
}

inline Piece Position::captured_piece() const noexcept { return st->capturedPiece; }

inline Piece Position::promoted_piece() const noexcept { return st->promotedPiece; }

inline Key Position::key(std::int16_t r50) const noexcept { return st->key ^ Zobrist::mr50(rule50_count() + r50); }

inline Key Position::pawn_key(Color c) const noexcept { return st->pawnKey[c]; }

inline Key Position::pawn_key() const noexcept { return pawn_key(WHITE) ^ pawn_key(BLACK); }

inline Key Position::minor_key(Color c) const noexcept { return st->nonPawnKey[c][0]; }

inline Key Position::minor_key() const noexcept { return minor_key(WHITE) ^ minor_key(BLACK); }

inline Key Position::major_key(Color c) const noexcept { return st->nonPawnKey[c][1]; }

inline Key Position::major_key() const noexcept { return major_key(WHITE) ^ major_key(BLACK); }

inline Key Position::non_pawn_key(Color c) const noexcept {
    Square kingSq = square<KING>(c);
    return minor_key(c) ^ major_key(c) ^ Zobrist::piece_square(piece_on(kingSq), kingSq);
}

inline Key Position::non_pawn_key() const noexcept { return non_pawn_key(WHITE) ^ non_pawn_key(BLACK); }

inline Key Position::material_key() const noexcept {
    Key materialKey = 0;

    for (Color c : {WHITE, BLACK})
        for (Piece pc : Pieces[c])
            if (type_of(pc) != KING && count(pc))
                materialKey ^= Zobrist::piece_square(pc, Square(Zobrist::PawnOffset + count(pc) - 1));

    return materialKey;
}

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

inline std::int16_t Position::rule50_count() const noexcept { return st->rule50Count; }

inline std::int16_t Position::null_ply() const noexcept { return st->nullPly; }

inline std::int16_t Position::repetition() const noexcept { return st->repetition; }

inline bool Position::has_castled(Color c) const noexcept { return st->hasCastled[c]; }

inline bool Position::has_rule50_high() const noexcept { return st->hasRule50High; }

inline bool Position::bishop_paired(Color c) const noexcept {
    return (pieces(c, BISHOP) & colors_bb<WHITE>()) && (pieces(c, BISHOP) & colors_bb<BLACK>());
}

inline bool Position::bishop_opposite() const noexcept {
    return count<BISHOP>(WHITE) == 1 && count<BISHOP>(BLACK) == 1
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
    assert(pseudo_legal(m));
    return (m.type_of() != CASTLING && !empty_on(m.dst_sq())) || m.type_of() == EN_PASSANT;
}

inline bool Position::capture_queenpromo(Move m) const noexcept {
    return capture(m) || m.promotion_type() == QUEEN;  // m.type_of() == PROMOTION must be true here
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
    Bitboard sbb = square_bb(s);

    auto c  = color_of(pc);
    auto pt = type_of(pc);

    pieceMap[s] = pc;
    colorBB[c] |= sbb;
    typeBB[NO_PIECE_TYPE] |= typeBB[pt] |= sbb;
    auto& pieceList = piece_list(c, pt);
    squareIndex[s]  = pieceList.size();
    pieceList.push_back(s);
    ++pieceCount[c];

    if (dts != nullptr)
        update_piece_threats<true>(pc, s, dts);
}

inline Piece Position::remove_piece(Square s, DirtyThreats* const dts) noexcept {
    assert(is_ok(s));
    Bitboard sbb = square_bb(s);

    Piece pc = piece_on(s);
    auto  c  = color_of(pc);
    auto  pt = type_of(pc);
    assert(is_ok(pc) && count(c, pt));

    if (dts != nullptr)
        update_piece_threats<false>(pc, s, dts);

    pieceMap[s] = NO_PIECE;
    colorBB[c] ^= sbb;
    typeBB[pt] ^= sbb;
    typeBB[NO_PIECE_TYPE] ^= sbb;
    auto  idx       = squareIndex[s];
    auto& pieceList = piece_list(c, pt);
    assert(idx < pieceList.capacity());
    Square sq       = pieceList.back();
    pieceList[idx]  = sq;
    squareIndex[sq] = idx;
    //squareIndex[s]    = InvalidIndex;
    pieceList.pop_back();
    --pieceCount[c];

    return pc;
}

inline Piece Position::move_piece(Square s1, Square s2, DirtyThreats* const dts) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    Bitboard s1s2bb = make_bb(s1, s2);

    Piece pc = piece_on(s1);
    auto  c  = color_of(pc);
    auto  pt = type_of(pc);
    assert(is_ok(pc) && count(c, pt));

    if (dts != nullptr)
        update_piece_threats<false>(pc, s1, dts);

    pieceMap[s1] = NO_PIECE;
    pieceMap[s2] = pc;
    colorBB[c] ^= s1s2bb;
    typeBB[pt] ^= s1s2bb;
    typeBB[NO_PIECE_TYPE] ^= s1s2bb;
    auto  idx       = squareIndex[s1];
    auto& pieceList = piece_list(c, pt);
    assert(idx < pieceList.capacity());
    pieceList[idx]  = s2;
    squareIndex[s2] = idx;
    //squareIndex[s1]  = InvalidIndex;

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
inline void Position::update_piece_threats(Piece pc, Square s, DirtyThreats* const dts) noexcept {

    Bitboard occupied = pieces();

    const auto attacks = [&]() noexcept {
        StdArray<Bitboard, 7> _{};
        _[WHITE]  = attacks_bb<PAWN>(s, WHITE);
        _[BLACK]  = attacks_bb<PAWN>(s, BLACK);
        _[KNIGHT] = attacks_bb<KNIGHT>(s);
        _[BISHOP] = attacks_bb<BISHOP>(s, occupied);
        _[ROOK]   = attacks_bb<ROOK>(s, occupied);
        _[QUEEN]  = _[BISHOP] | _[ROOK];
        _[KING]   = attacks_bb<KING>(s);
        return _;
    }();

    Bitboard threatened = (type_of(pc) == PAWN ? attacks[color_of(pc)]  //
                                               : attacks[type_of(pc)])
                        & occupied;
    while (threatened)
    {
        Square threatenedSq = pop_lsb(threatened);
        Piece  threatenedPc = piece_on(threatenedSq);

        assert(threatenedSq != s);
        assert(is_ok(threatenedPc));

        dts->add<Add>(s, threatenedSq, pc, threatenedPc);
    }

    // clang-format off
    Bitboard sliders = (pieces(QUEEN, BISHOP) & attacks[BISHOP])
                     | (pieces(QUEEN, ROOK)   & attacks[ROOK]);
    // clang-format on
    while (sliders)
    {
        Square sliderSq = pop_lsb(sliders);
        Piece  sliderPc = piece_on(sliderSq);

        assert(is_ok(sliderPc));

        if constexpr (ComputeRay)
        {
            Bitboard discovered = pass_ray_bb(sliderSq, s) & attacks[QUEEN] & occupied;

            if (discovered)
            {
                assert(!more_than_one(discovered));
                Square threatenedSq = lsb(discovered);
                Piece  threatenedPc = piece_on(threatenedSq);

                assert(is_ok(threatenedPc));

                dts->add<!Add>(sliderSq, threatenedSq, sliderPc, threatenedPc);
            }
        }

        dts->add<Add>(sliderSq, s, sliderPc, pc);
    }

    // clang-format off
    Bitboard nonSliders = (pieces(WHITE, PAWN) & attacks[BLACK])
                        | (pieces(BLACK, PAWN) & attacks[WHITE])
                        | (pieces(KNIGHT)      & attacks[KNIGHT])
                        | (pieces(KING)        & attacks[KING]);
    // clang-format on
    while (nonSliders)
    {
        Square nonSliderSq = pop_lsb(nonSliders);
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
