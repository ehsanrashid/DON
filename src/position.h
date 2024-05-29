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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

#include "bitboard.h"
#include "types.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_architecture.h"

namespace DON {

class TranspositionTable;

// StateInfo struct stores information needed to restore a Position object to
// its previous state when retract a move. Whenever a move is made on the
// board (by calling Position::do_move), a StateInfo object must be passed.
struct StateInfo final {

    // Copied when making a move
    Key          pawnKey;
    Key          materialKey;
    Square       epSquare;
    Square       capSquare;
    std::uint8_t castlingRights;
    std::uint8_t rule50;
    std::uint8_t nullPly;  // Plies from Null-Move
    Square       kingSquare[COLOR_NB];
    bool         hasCastled[COLOR_NB];
    Value        nonPawnMaterial[COLOR_NB];

    // Not copied when making a move (will be recomputed anyhow)
    Key          key;
    Bitboard     checkers;
    Bitboard     checks[PIECE_TYPE_NB];
    Bitboard     pinners[COLOR_NB];
    Bitboard     blockers[COLOR_NB];
    Bitboard     attacks[COLOR_NB][PIECE_TYPE_NB];
    std::int16_t mobility[COLOR_NB];
    std::int8_t  repetition;
    Piece        capturedPiece;
    //Piece        promotedPiece;

    // Used by NNUE
    Eval::NNUE::Accumulator<Eval::NNUE::BigTransformedFeatureDimensions>   bigAccumulator;
    Eval::NNUE::Accumulator<Eval::NNUE::SmallTransformedFeatureDimensions> smallAccumulator;
    DirtyPiece                                                             dirtyPiece;

    StateInfo* previous;
};

// A list to keep track of the position states along the setup moves
// (from the start position to the position just before the search starts).
// Needed by 'draw by repetition' detection.
// Use a std::deque because pointers to elements are not invalidated upon list resizing.
using StateList    = std::deque<StateInfo>;
using StateListPtr = std::unique_ptr<StateList>;

// Position class stores information regarding the board representation as
// pieces, side to move, hash keys, castling info, etc.
// Important methods are do_move() and undo_move(),
// used by the search to update node info when traversing the search tree.
class Position final {
   public:
    static void init() noexcept;

    Position()                           = default;
    Position(const Position&)            = delete;
    Position& operator=(const Position&) = delete;

    // FEN string input/output
    Position&   set(std::string_view fenStr, StateInfo* si) noexcept;
    Position&   set(std::string_view code, Color c, StateInfo* si) noexcept;
    std::string fen(bool full = true) const noexcept;

    // Position representation
    Piece    piece_on(Square s) const noexcept;
    bool     empty_on(Square s) const noexcept;
    Bitboard pieces(PieceType pt = ALL_PIECE) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(PieceType pt, PieceTypes... pts) const noexcept;
    Bitboard pieces(Color c) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(Color c, PieceTypes... pts) const noexcept;
    template<PieceType PT>
    Bitboard pieces(Color c, Square s) const noexcept;

    std::uint8_t count(Color c, PieceType pt) const noexcept;
    template<PieceType PT>
    std::uint8_t count(Color c) const noexcept;
    template<PieceType PT>
    std::uint8_t count() const noexcept;

    template<PieceType PT>
    Square square(Color c) const noexcept;
    Square king_square(Color c) const noexcept;
    Square ep_square() const noexcept;
    Square cap_square() const noexcept;

    // Castling
    // CastlingRights castling_rights(Color c) const noexcept;
    bool   can_castle(CastlingRights cr) const noexcept;
    bool   castling_impeded(CastlingRights cr) const noexcept;
    Square castling_rook_square(CastlingRights cr) const noexcept;

    // Attackers
    Bitboard checkers() const noexcept;
    Bitboard checks(PieceType pt) const noexcept;
    Bitboard pinners(Color c) const noexcept;
    Bitboard blockers(Color c) const noexcept;
    Bitboard attacks(Color c, PieceType pt = ALL_PIECE) const noexcept;
    // Bitboard threatens(Color c) const noexcept;

    // Attacks to a given square
    Bitboard attackers_to(Square s, Bitboard occupied) const noexcept;
    Bitboard attackers_to(Square s) const noexcept;

    std::int16_t mobility(Color c) const noexcept;

   private:
    // Attacks from a piece type
    template<PieceType PT>
    Bitboard attacks_by(Color c, Bitboard target = ~0ULL, Bitboard occupied = 0ULL) const noexcept;
    // template<PieceType PT>
    // Bitboard attacks_by(Color c) const noexcept;
    // Bitboard attacks_by(Color c) const noexcept;

   public:
    // Properties of moves
    bool  legal(Move m) const noexcept;
    bool  pseudo_legal(Move m) const noexcept;
    bool  capture(Move m) const noexcept;
    bool  capture_stage(Move m) const noexcept;
    bool  gives_check(Move m) const noexcept;
    bool  gives_dbl_check(Move m) const noexcept;
    Piece moved_piece(Move m) const noexcept;
    Piece captured_piece(Move m) const noexcept;
    Piece captured_piece() const noexcept;
    // Piece promoted_piece() const noexcept;
    Piece prev_moved_piece(Move prevMove, Square prevDst) const noexcept;

    // Doing and undoing moves
    void do_move(Move m, StateInfo& newSt) noexcept;
    void do_move(Move m, StateInfo& newSt, bool givesCheck) noexcept;
    void undo_move(Move m) noexcept;
    void do_null_move(StateInfo& newSt, const TranspositionTable& tt) noexcept;
    void undo_null_move() noexcept;

    // Static Exchange Evaluation
    bool see_ge(Move m, int threshold = 0) const noexcept;

    // Hash keys
    Key key() const noexcept;
    Key pawn_key() const noexcept;
    Key material_key() const noexcept;
    Key move_key(Move m) const noexcept;

    // Other properties
    Color        side_to_move() const noexcept;
    std::int16_t game_ply() const noexcept;
    std::int16_t game_move() const noexcept;
    std::uint8_t rule50_count() const noexcept;
    std::uint8_t null_ply() const noexcept;
    bool         is_draw(std::int16_t ply) const noexcept;
    bool         has_game_cycle(std::int16_t ply) const noexcept;
    bool         has_repeated() const noexcept;
    Value        non_pawn_material(Color c) const noexcept;
    Value        non_pawn_material() const noexcept;
    bool         has_castled(Color c) const noexcept;
    bool         bishop_paired(Color c) const noexcept;
    bool         opposite_bishops() const noexcept;

    int   material() const noexcept;
    int   materials() const noexcept;
    Value evaluate() const noexcept;
    Value bonus() const noexcept;

    // Position consistency check, for debugging
    bool pos_is_ok() const noexcept;
    void flip() noexcept;

    // Used by NNUE
    StateInfo* state() const noexcept;
    StateInfo* prev_state() const noexcept;

    void put_piece(Piece pc, Square s) noexcept;
    void remove_piece(Square s) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept;

    static bool         Chess960;
    static std::uint8_t DrawMoveCount;

   private:
    // Initialization helpers (used while setting up a position)
    void set_castling_rights(Color c, Square rorg) noexcept;
    void set_state() noexcept;
    void set_ext_state() noexcept;
    bool can_enpassant(Color c, Square epSq, bool before = false) const noexcept;

    // Other helpers
    void move_piece(Square org, Square dst) noexcept;
    template<bool Do>
    void do_castling(Color c, Square org, Square& dst, Square& rorg, Square& rdst) noexcept;
    Key  adjust_key(Key k, bool before = false) const noexcept;

    // Data members
    Piece        board[SQUARE_NB];
    Bitboard     typeBB[PIECE_TYPE_NB];
    Bitboard     colorBB[COLOR_NB];
    std::uint8_t pieceCount[PIECE_NB];
    Bitboard     castlingPath[COLOR_NB * 2];
    Square       castlingRookSquare[COLOR_NB * 2];
    std::uint8_t castlingRightsMask[SQUARE_NB];
    std::int16_t gamePly;
    Color        sideToMove;
    StateInfo*   st;
};

inline Piece Position::piece_on(Square s) const noexcept {
    assert(is_ok(s));
    return board[s];
}

inline bool Position::empty_on(Square s) const noexcept { return piece_on(s) == NO_PIECE; }

inline Bitboard Position::pieces(PieceType pt) const noexcept { return typeBB[pt]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces(PieceType pt, PieceTypes... pts) const noexcept {
    return pieces(pt) | pieces(pts...);
}

inline Bitboard Position::pieces(Color c) const noexcept { return colorBB[c]; }

template<typename... PieceTypes>
inline Bitboard Position::pieces(Color c, PieceTypes... pts) const noexcept {
    return pieces(c) & pieces(pts...);
}

template<PieceType PT>
inline Bitboard Position::pieces(Color c, Square s) const noexcept {
    Bitboard pc = pieces(c, PT);
    // clang-format off
    switch (PT)
    {
    case KNIGHT : pc &= ~blockers(c); break;
    case BISHOP : pc &= ~blockers(c) | attacks_bb<BISHOP>(s); break;
    case ROOK :   pc &= ~blockers(c) | attacks_bb<ROOK>(s); break;
    default :;
    }
    // clang-format on
    return pc;
}

inline std::uint8_t Position::count(Color c, PieceType pt) const noexcept {
    return pieceCount[make_piece(c, pt)];
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

inline Square Position::king_square(Color c) const noexcept { return st->kingSquare[c]; }

inline Square Position::ep_square() const noexcept { return st->epSquare; }

inline Square Position::cap_square() const noexcept { return st->capSquare; }

// inline CastlingRights Position::castling_rights(Color c) const noexcept {
//     return c & CastlingRights(st->castlingRights);
// }

inline bool Position::can_castle(CastlingRights cr) const noexcept {
    return st->castlingRights & cr;
}

inline bool Position::castling_impeded(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return pieces() & PromotionRankBB & castlingPath[lsb(cr)];
}

inline Square Position::castling_rook_square(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return castlingRookSquare[lsb(cr)];
}

inline Bitboard Position::attackers_to(Square s) const noexcept {
    return attackers_to(s, pieces());
}

// template<PieceType PT>
// inline Bitboard Position::attacks_by(Color c) const noexcept {
//     return attacks_by<PT>(c, ~0ULL, pieces());
// }

// inline Bitboard Position::attacks_by(Color c) const noexcept {
//     return attacks_by<PAWN>(c) | attacks_by<KNIGHT>(c)  //
//          | attacks_by<BISHOP>(c) | attacks_by<ROOK>(c)  //
//          | attacks_by<QUEEN>(c) | attacks_bb<KING>(king_square(c));
// }

inline Bitboard Position::checkers() const noexcept { return st->checkers; }

inline Bitboard Position::checks(PieceType pt) const noexcept { return st->checks[pt]; }

inline Bitboard Position::pinners(Color c) const noexcept { return st->pinners[c]; }

inline Bitboard Position::blockers(Color c) const noexcept { return st->blockers[c]; }
// clang-format off
inline Bitboard Position::attacks(Color c, PieceType pt) const noexcept { return st->attacks[c][pt]; }
// clang-format on
// inline Bitboard Position::threatens(Color c) const noexcept { return st->attacks[c][EX_PIECE]; }

inline std::int16_t Position::mobility(Color c) const noexcept { return st->mobility[c]; }

inline Key Position::adjust_key(Key k, bool before) const noexcept {
    return st->rule50 < 14 - before ? k : k ^ make_key((st->rule50 - (14 - before)) / 8);
}

inline Key Position::key() const noexcept { return adjust_key(st->key); }

inline Key Position::pawn_key() const noexcept { return st->pawnKey; }

inline Key Position::material_key() const noexcept { return st->materialKey; }

inline Color Position::side_to_move() const noexcept { return sideToMove; }

inline std::int16_t Position::game_ply() const noexcept { return gamePly; }

inline std::int16_t Position::game_move() const noexcept {
    return 1 + (gamePly - (sideToMove == BLACK)) / 2;
}

inline std::uint8_t Position::rule50_count() const noexcept { return st->rule50; }

inline std::uint8_t Position::null_ply() const noexcept { return st->nullPly; }

inline Value Position::non_pawn_material(Color c) const noexcept { return st->nonPawnMaterial[c]; }

inline Value Position::non_pawn_material() const noexcept {
    return non_pawn_material(WHITE) + non_pawn_material(BLACK);
}

inline bool Position::has_castled(Color c) const noexcept { return st->hasCastled[c]; }

inline bool Position::bishop_paired(Color c) const noexcept {
    Bitboard bishops = pieces(c, BISHOP);
    return (bishops & ColorBB[WHITE]) && (bishops & ColorBB[BLACK]);
}

inline bool Position::opposite_bishops() const noexcept {
    return count<BISHOP>(WHITE) == 1 && count<BISHOP>(BLACK) == 1
        && opposite_color(square<BISHOP>(WHITE), square<BISHOP>(BLACK));
}

inline int Position::material() const noexcept {
    return 1 * count<PAWN>() + 3 * count<KNIGHT>() + 3 * count<BISHOP>()  //
         + 5 * count<ROOK>() + 9 * count<QUEEN>();
}
inline int Position::materials() const noexcept {
    return 200 * count<PAWN>() + 350 * count<KNIGHT>() + 400 * count<BISHOP>()  //
         + 640 * count<ROOK>() + 1200 * count<QUEEN>();
}

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the side to move. It can be divided by VALUE_PAWN to get
// an approximation of the material advantage on the board in terms of pawns.
inline Value Position::evaluate() const noexcept {
    return (count<PAWN>(sideToMove) - count<PAWN>(~sideToMove)) * VALUE_PAWN
         + (non_pawn_material(sideToMove) - non_pawn_material(~sideToMove))  //
         + bonus();
}

inline Value Position::bonus() const noexcept {
    // clang-format off
    return      (mobility(sideToMove) - mobility(~sideToMove)) / 4
         + 25 * (bishop_paired(sideToMove) - bishop_paired(~sideToMove))
         + 50 * ((can_castle( sideToMove & ANY_CASTLING) || has_castled( sideToMove))
               - (can_castle(~sideToMove & ANY_CASTLING) || has_castled(~sideToMove)));
    // clang-format on
}

inline bool Position::capture(Move m) const noexcept {
    assert(m.is_ok());
    return (m.type_of() != CASTLING && !empty_on(m.dst_sq()))
        || (m.type_of() == EN_PASSANT && ep_square() == m.dst_sq());
}

// Returns true if a move is generated from the capture stage, having also
// queen promotions covered, i.e. consistency with the capture stage move generation
// is needed to avoid the generation of duplicate moves.
inline bool Position::capture_stage(Move m) const noexcept {
    return capture(m) || m.promotion_type() == QUEEN;
}

inline Piece Position::moved_piece(Move m) const noexcept { return piece_on(m.org_sq()); }

inline Piece Position::captured_piece(Move m) const noexcept {
    assert(m.is_ok());
    return m.type_of() == EN_PASSANT && ep_square() == m.dst_sq()
           ? piece_on(m.dst_sq() - pawn_spush(sideToMove))
         : m.type_of() != CASTLING ? piece_on(m.dst_sq())
                                   : NO_PIECE;
}

inline Piece Position::captured_piece() const noexcept { return st->capturedPiece; }

// inline Piece Position::promoted_piece() const noexcept { return st->promotedPiece; }

inline Piece Position::prev_moved_piece(Move prevMove, Square prevDst) const noexcept {
    return prevMove.type_of() != PROMOTION ? piece_on(prevDst) : make_piece(~sideToMove, PAWN);
}

inline void Position::put_piece(Piece pc, Square s) noexcept {
    assert(is_ok(pc));

    board[s] = pc;
    typeBB[ALL_PIECE] |= typeBB[type_of(pc)] |= s;
    colorBB[color_of(pc)] |= s;
    ++pieceCount[pc];
    ++pieceCount[pc & 8];
}

inline void Position::remove_piece(Square s) noexcept {

    Piece pc = board[s];
    assert(is_ok(pc));
    board[s] = NO_PIECE;
    typeBB[ALL_PIECE] ^= s;
    typeBB[type_of(pc)] ^= s;
    colorBB[color_of(pc)] ^= s;
    --pieceCount[pc];
    --pieceCount[pc & 8];
}

inline void Position::move_piece(Square org, Square dst) noexcept {

    Piece pc = board[org];
    assert(is_ok(pc));
    board[org]      = NO_PIECE;
    board[dst]      = pc;
    Bitboard orgDst = org | dst;
    typeBB[ALL_PIECE] ^= orgDst;
    typeBB[type_of(pc)] ^= orgDst;
    colorBB[color_of(pc)] ^= orgDst;
}

inline void Position::do_move(Move m, StateInfo& newSt) noexcept {
    do_move(m, newSt, gives_check(m));
}

inline StateInfo* Position::state() const noexcept { return st; }

inline StateInfo* Position::prev_state() const noexcept { return st->previous; }

}  // namespace DON

#endif  // #ifndef POSITION_H_INCLUDED
