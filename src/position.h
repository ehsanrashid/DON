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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <execution>
#include <iosfwd>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>

#include "bitboard.h"
#include "types.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_architecture.h"

namespace DON {

using Mobility = std::int16_t;

// State struct stores information needed to restore a Position object to
// its previous state when retract a move.
struct State final {
    // Copied when making a move
    Key            pawnKey[COLOR_NB];
    Key            nonPawnKey[COLOR_NB];
    Key            pieceKey[2];
    Key            materialKey;
    Square         epSquare;
    Square         capSquare;
    CastlingRights castlingRights;
    std::uint8_t   rule50;
    std::uint8_t   nullPly;  // Plies from Null-Move
    bool           rule50High;
    Square         kingSquare[COLOR_NB];
    bool           castled[COLOR_NB];
    Value          nonPawnMaterial[COLOR_NB];

    // Not copied when making a move (will be recomputed anyhow)
    Key         key;
    Bitboard    checkers;
    Bitboard    checks[PIECE_TYPE_NB - 2];
    Bitboard    pinners[COLOR_NB];
    Bitboard    blockers[COLOR_NB];
    Bitboard    attacks[COLOR_NB][PIECE_TYPE_NB];
    Mobility    mobility[COLOR_NB];
    std::int8_t repetition;
    Piece       capturedPiece;
    Piece       promotedPiece;

    // Used by NNUE
    Eval::NNUE::Accumulator<Eval::NNUE::BigTransformedFeatureDimensions>   bigAccumulator;
    Eval::NNUE::Accumulator<Eval::NNUE::SmallTransformedFeatureDimensions> smallAccumulator;
    DirtyPiece                                                             dirtyPiece;

    State* preState;
    State* nxtState;
};

// A list to keep track of the position states along the setup moves
// (from the start position to the position just before the search starts).
// Needed by 'draw by repetition' detection.
// Use a std::deque because pointers to elements are not invalidated upon list resizing.
using StateList    = std::deque<State>;
using StateListPtr = std::unique_ptr<StateList>;

// Position class stores information regarding the board representation as
// pieces, side to move, hash keys, castling info, etc. (Size = 192)
// Important methods are do_move() and undo_move(),
// used by the search to update node info when traversing the search tree.
class Position final {
   public:
    class Board final {
       public:
        Board() noexcept                        = default;
        Board(const Board&) noexcept            = delete;
        Board(Board&&) noexcept                 = delete;
        Board& operator=(const Board&) noexcept = default;
        Board& operator=(Board&&) noexcept      = delete;

        /*
        void init() noexcept {
            std::fill(std::begin(cardinals), std::end(cardinals), Cardinal());
            //std::fill(std::begin(pieces), std::end(pieces), NO_PIECE);
        }
        */

        constexpr void piece_on(Square s, Piece pc) noexcept {
            cardinals[rank_of(s)].piece_on(file_of(s), pc);
            //pieces[s] = pc;
        }
        constexpr Piece piece_on(Square s) const noexcept {
            return cardinals[rank_of(s)].piece_on(file_of(s));
            //return pieces[s];
        }

        std::uint8_t count(Piece pc) const noexcept {
            return std::accumulate(
              std::begin(cardinals), std::end(cardinals), 0,
              [=](std::uint8_t cnt, const Cardinal& cardinal) { return cnt + cardinal.count(pc); });
            //return std::count(std::execution::unseq, std::begin(pieces), std::end(pieces), pc);
        }

        friend std::ostream& operator<<(std::ostream& os, const Board& board) noexcept;

       private:
        struct Cardinal final {
           public:
            Cardinal() noexcept :
                rankP(0) {}

            constexpr void piece_on(File f, Piece pc) noexcept {
                auto x = f << 2;
                rankP  = (rankP & ~(0xFu << x)) | (pc << x);
            }

            constexpr Piece piece_on(File f) const noexcept {
                return Piece((rankP >> (f << 2)) & 0xFu);
            }

            std::uint8_t count(Piece pc) const noexcept {
                std::uint8_t cnt = 0;
                for (File f = FILE_A; f <= FILE_H; ++f)
                    cnt += piece_on(f) == pc;
                return cnt;
            }

           private:
            std::uint32_t rankP;
        };

        Cardinal cardinals[RANK_NB];
        //Piece pieces[SQUARE_NB];
    };

    static void init() noexcept;

    static auto rule50_threshold() noexcept;

    static bool         Chess960;
    static std::uint8_t DrawMoveCount;

    Position() noexcept = default;

   private:
    Position(const Position&) noexcept            = delete;
    Position(Position&&) noexcept                 = delete;
    Position& operator=(const Position&) noexcept = default;
    Position& operator=(Position&&) noexcept      = delete;

   public:
    // FEN string input/output
    void        set(std::string_view fenStr, State* newSt) noexcept;
    void        set(std::string_view code, Color c, State* newSt) noexcept;
    void        set(const Position& pos, State* newSt) noexcept;
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

    std::uint8_t count(Piece pc) const noexcept;
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
    CastlingRights castling_rights() const noexcept;
    //CastlingRights castling_rights(Color c) const noexcept;
    bool   can_castle(CastlingRights cr) const noexcept;
    bool   castling_impeded(CastlingRights cr) const noexcept;
    Square castling_rook_square(CastlingRights cr) const noexcept;
    auto   castling_rights_mask(Square s1, Square s2) const noexcept;

    // ExState Info
    Bitboard checkers() const noexcept;
    Bitboard checks(PieceType pt) const noexcept;
    Bitboard pinners(Color c) const noexcept;
    Bitboard pinners() const noexcept;
    Bitboard blockers(Color c) const noexcept;
    Bitboard attacks(Color c, PieceType pt = KING) const noexcept;
    Bitboard threatens(Color c) const noexcept;
    Mobility mobility(Color c) const noexcept;
    Piece    captured_piece() const noexcept;
    Piece    promoted_piece() const noexcept;

    // Non-Slide attacker to a given sqaure
    Bitboard fix_attackers_to(Square s) const noexcept;
    // Slide attacker to a given sqaure
    Bitboard xslide_attackers_to(Square s) const noexcept;
    Bitboard slide_attackers_to(Square s, Bitboard occupied) const noexcept;
    Bitboard slide_attackers_to(Square s) const noexcept;
    // All attackers to a given square
    Bitboard attackers_to(Square s, Bitboard occupied) const noexcept;
    Bitboard attackers_to(Square s) const noexcept;

   private:
    // Attacks from a piece type
    template<PieceType PT>
    Bitboard attacks_by(Color c, Bitboard target = ~0ull, Bitboard occupied = 0ull) noexcept;

   public:
    // Doing and undoing moves
    void do_move(Move m, State& newSt, bool check) noexcept;
    void do_move(Move m, State& newSt) noexcept;
    void undo_move(Move m) noexcept;
    void do_null_move(State& newSt) noexcept;
    void undo_null_move() noexcept;

    // Properties of moves
    bool  legal(Move m) const noexcept;
    bool  pseudo_legal(Move m) const noexcept;
    bool  capture(Move m) const noexcept;
    bool  capture_promo(Move m) const noexcept;
    bool  check(Move m) const noexcept;
    bool  dbl_check(Move m) const noexcept;
    bool  fork(Move m) const noexcept;
    Piece moved_piece(Move m) const noexcept;
    Piece ex_moved_piece(Move m) const noexcept;
    Piece captured_piece(Move m) const noexcept;
    Piece prev_ex_moved_piece(Move pm) const noexcept;
    auto  captured(Move m) const noexcept;
    auto  ex_captured(Move m) const noexcept;

    // Hash keys
    Key key(std::int16_t ply = 0) const noexcept;
    Key pawn_key(Color c) const noexcept;
    Key pawn_key() const noexcept;
    Key non_pawn_key(Color c) const noexcept;
    Key non_pawn_key() const noexcept;
    Key major_key() const noexcept;
    Key minor_key() const noexcept;
    Key material_key() const noexcept;
    Key move_key(Move m) const noexcept;
    Key move_key() const noexcept;

    // Static Exchange Evaluation
    auto see(Move m) const noexcept { return SEE(*this, m); }

    // Other properties
    auto active_color() const noexcept;
    auto game_ply() const noexcept;
    auto game_move() const noexcept;
    auto game_phase() const noexcept;
    auto rule50_count() const noexcept;
    bool rule50_high() const noexcept;
    auto null_ply() const noexcept;
    auto repetition() const noexcept;
    bool is_draw(std::int16_t ply, bool checkStalemate = false) const noexcept;
    bool has_repeated() const noexcept;
    bool upcoming_repetition(std::int16_t ply) const noexcept;

    Value non_pawn_material(Color c) const noexcept;
    Value non_pawn_material() const noexcept;
    bool  castled(Color c) const noexcept;
    bool  bishop_paired(Color c) const noexcept;
    bool  bishop_opposite() const noexcept;

    short material() const noexcept;
    Value evaluate() const noexcept;
    Value bonus() const noexcept;

    void put_piece(Square s, Piece pc) noexcept;
    void remove_piece(Square s) noexcept;

    // Position consistency check, for debugging
    void flip() noexcept;
#if !defined(NDEBUG)
    Key  compute_key() const noexcept;
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

        SEE(const Position& p, Move m) noexcept :
            pos(p),
            move(m) {}

        bool operator>=(Value threshold) const noexcept;
        bool operator>(Value threshold) const noexcept;
        bool operator<=(Value threshold) const noexcept;
        bool operator<(Value threshold) const noexcept;

       private:
        const Position& pos;
        Move            move;
    };

    // Initialization helpers (used while setting up a position)
    void set_castling_rights(Color c, Square rorg) noexcept;
    void set_state() noexcept;
    void set_ext_state() noexcept;
    bool can_enpassant(Color ac, Square epSq, bool before = false) const noexcept;

    // Other helpers
    void move_piece(Square s1, Square s2) noexcept;
    template<bool Do>
    void do_castling(Color ac, Square org, Square& dst, Square& rorg, Square& rdst) noexcept;

    Key adjust_key(Key k, std::int16_t ply = 0) const noexcept;

    void reset_ep_square() noexcept;
    void reset_rule50_count() noexcept;
    void reset_repetitions() noexcept;

    // Static Exchange Evaluation
    bool see_ge(Move m, Value threshold) const noexcept;

    // Data members
    Board        board;
    Bitboard     typeBB[PIECE_TYPE_NB];
    Bitboard     colorBB[COLOR_NB];
    std::uint8_t pieceCount[PIECE_NB];
    Bitboard     castlingPath[COLOR_NB * CASTLING_SIDE_NB];
    Square       castlingRookSquare[COLOR_NB * CASTLING_SIDE_NB];
    std::uint8_t castlingRightsMask[COLOR_NB * FILE_NB];
    std::int16_t gamePly;
    Color        activeColor;
    State*       st;
};

inline auto Position::rule50_threshold() noexcept { return -10 + 2 * DrawMoveCount; }

inline Piece Position::piece_on(Square s) const noexcept {
    assert(is_ok(s));
    return board.piece_on(s);
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
    case KNIGHT : return pc & (~blockers(c));
    case BISHOP : return pc & (~blockers(c) | attacks_bb<BISHOP>(s));
    case ROOK :   return pc & (~blockers(c) | attacks_bb<ROOK>  (s));
    default :     return pc;
    }
    // clang-format on
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

inline Square Position::king_square(Color c) const noexcept { return st->kingSquare[c]; }

inline Square Position::ep_square() const noexcept { return st->epSquare; }

inline Square Position::cap_square() const noexcept { return st->capSquare; }

inline CastlingRights Position::castling_rights() const noexcept { return st->castlingRights; }
//inline CastlingRights Position::castling_rights(Color c) const noexcept { return c & castling_rights(); }

inline bool Position::can_castle(CastlingRights cr) const noexcept {
    return castling_rights() & cr;
}

inline bool Position::castling_impeded(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return pieces() & PROMOTION_RANK_BB & castlingPath[lsb(cr)];
}

inline Square Position::castling_rook_square(CastlingRights cr) const noexcept {
    assert(cr == WHITE_OO || cr == WHITE_OOO || cr == BLACK_OO || cr == BLACK_OOO);
    return castlingRookSquare[lsb(cr)];
}

inline auto Position::castling_rights_mask(Square s1, Square s2) const noexcept {
    return bool(PROMOTION_RANK_BB & s1)
           * castlingRightsMask[((1 + rank_of(s1)) & int(RANK_NB)) + file_of(s1)]
         | bool(PROMOTION_RANK_BB & s2)
             * castlingRightsMask[((1 + rank_of(s2)) & int(RANK_NB)) + file_of(s2)];
}

// clang-format off
inline Bitboard Position::fix_attackers_to(Square s) const noexcept {
    return (pieces(WHITE, PAWN) & pawn_attacks_bb<BLACK>(s))
         | (pieces(BLACK, PAWN) & pawn_attacks_bb<WHITE>(s))
         | (pieces(KNIGHT)      & attacks_bb<KNIGHT>(s))
         | (pieces(KING)        & attacks_bb<KING>  (s));
}
inline Bitboard Position::xslide_attackers_to(Square s) const noexcept {
    return (pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s))
         | (pieces(QUEEN, ROOK)   & attacks_bb<ROOK>  (s));
}
inline Bitboard Position::slide_attackers_to(Square s, Bitboard occupied) const noexcept {
    return (pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupied))
         | (pieces(QUEEN, ROOK)   & attacks_bb<ROOK>  (s, occupied));
}
inline Bitboard Position::slide_attackers_to(Square s) const noexcept {
    return slide_attackers_to(s, pieces());
}
// Computes a bitboard of all pieces which attack a given square.
// Slider attacks use the occupied bitboard to indicate occupancy.
inline Bitboard Position::attackers_to(Square s, Bitboard occupied) const noexcept {
    return fix_attackers_to(s)
         | slide_attackers_to(s, occupied);
}
inline Bitboard Position::attackers_to(Square s) const noexcept {
    return attackers_to(s, pieces());
}
// clang-format on

inline Bitboard Position::checkers() const noexcept { return st->checkers; }

inline Bitboard Position::checks(PieceType pt) const noexcept { return st->checks[pt - 1]; }
// clang-format off
inline Bitboard Position::pinners(Color c) const noexcept { return st->pinners[c]; }

inline Bitboard Position::pinners() const noexcept { return st->pinners[WHITE] | st->pinners[BLACK]; }

inline Bitboard Position::blockers(Color c) const noexcept { return st->blockers[c]; }

inline Bitboard Position::attacks(Color c, PieceType pt) const noexcept { return st->attacks[c][pt]; }

inline Bitboard Position::threatens(Color c) const noexcept { return st->attacks[c][EX_PIECE]; }

inline Mobility Position::mobility(Color c) const noexcept { return st->mobility[c]; }

inline Piece Position::captured_piece() const noexcept { return st->capturedPiece; }

inline Piece Position::promoted_piece() const noexcept { return st->promotedPiece; }

inline Key Position::adjust_key(Key k, std::int16_t ply) const noexcept {
    return k ^ (st->rule50 + ply >= 14) * make_hash((st->rule50 + ply - 14) / 8);
}

inline Key Position::key(std::int16_t ply) const noexcept { return adjust_key(st->key, ply); }

inline Key Position::pawn_key(Color c) const noexcept { return st->pawnKey[c]; }

inline Key Position::pawn_key() const noexcept { return pawn_key(WHITE) ^ pawn_key(BLACK); }

inline Key Position::non_pawn_key(Color c) const noexcept { return st->nonPawnKey[c]; }

inline Key Position::non_pawn_key() const noexcept { return non_pawn_key(WHITE) ^ non_pawn_key(BLACK); }

inline Key Position::minor_key() const noexcept { return st->pieceKey[0]; }

inline Key Position::major_key() const noexcept { return st->pieceKey[1]; }

inline Key Position::material_key() const noexcept { return st->materialKey; }

inline auto Position::active_color() const noexcept { return activeColor; }

inline auto Position::game_ply() const noexcept { return gamePly; }

inline auto Position::game_move() const noexcept {
    return 1 + (game_ply() - (active_color() == BLACK)) / 2;
}

inline auto Position::game_phase() const noexcept {
    return std::max(24 - count<KNIGHT>() - count<BISHOP>() - 2 * count<ROOK>() - 4 * count<QUEEN>(), 0);
}

inline auto Position::rule50_count() const noexcept { return st->rule50; }

inline bool Position::rule50_high() const noexcept { return st->rule50High; }

inline auto Position::null_ply() const noexcept { return st->nullPly; }

inline auto Position::repetition() const noexcept { return st->repetition; }

inline Value Position::non_pawn_material(Color c) const noexcept { return st->nonPawnMaterial[c]; }

inline Value Position::non_pawn_material() const noexcept {
    return non_pawn_material(WHITE) + non_pawn_material(BLACK);
}

inline bool Position::castled(Color c) const noexcept { return st->castled[c]; }

inline bool Position::bishop_paired(Color c) const noexcept {
    return (pieces(c, BISHOP) & COLOR_BB[WHITE]) && (pieces(c, BISHOP) & COLOR_BB[BLACK]);
}

inline bool Position::bishop_opposite() const noexcept {
    return count<BISHOP>(WHITE) == 1 && count<BISHOP>(BLACK) == 1
        && color_opposite(square<BISHOP>(WHITE), square<BISHOP>(BLACK));
}

inline short Position::material() const noexcept {
    return 1 * count<PAWN>() + 3 * count<KNIGHT>() + 3 * count<BISHOP>() + 5 * count<ROOK>() + 9 * count<QUEEN>();
}
// clang-format on

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the side to move. It can be divided by VALUE_PAWN to get
// an approximation of the material advantage on the board in terms of pawns.
inline Value Position::evaluate() const noexcept {
    Color ac = active_color();
    return VALUE_PAWN * (count<PAWN>(ac) - count<PAWN>(~ac))
         + (non_pawn_material(ac) - non_pawn_material(~ac));
}

inline Value Position::bonus() const noexcept {
    Color ac = active_color();
    // clang-format off
    return  1 * (mobility(ac) - mobility(~ac))
         + 30 * (bishop_paired(ac) - bishop_paired(~ac))
         + 70 * ((can_castle( ac & ANY_CASTLING) || castled( ac))
               - (can_castle(~ac & ANY_CASTLING) || castled(~ac)));
    // clang-format on
}

inline bool Position::capture(Move m) const noexcept {
    return (m.type_of() != CASTLING && !empty_on(m.dst_sq())) || m.type_of() == EN_PASSANT;
}

inline bool Position::capture_promo(Move m) const noexcept {
    return capture(m) || m.type_of() == PROMOTION;
}

inline Piece Position::moved_piece(Move m) const noexcept { return piece_on(m.org_sq()); }
inline Piece Position::ex_moved_piece(Move m) const noexcept {
    return m.type_of() != CASTLING ? moved_piece(m) : make_piece(active_color(), EX_PIECE);
}

inline Piece Position::captured_piece(Move m) const noexcept {
    assert(m.is_ok());
    return m.type_of() == EN_PASSANT ? make_piece(~active_color(), PAWN)
         : m.type_of() != CASTLING   ? piece_on(m.dst_sq())
                                     : NO_PIECE;
}

inline Piece Position::prev_ex_moved_piece(Move pm) const noexcept {
    return pm.type_of() != CASTLING ? piece_on(pm.dst_sq()) : make_piece(~active_color(), EX_PIECE);
}

inline auto Position::captured(Move m) const noexcept { return type_of(captured_piece(m)); }

inline auto Position::ex_captured(Move m) const noexcept {
    auto capture = captured(m);
    if (capture == NO_PIECE_TYPE && m.type_of() == PROMOTION)
        return m.promotion_type();
    return capture;
}


inline void Position::put_piece(Square s, Piece pc) noexcept {
    assert(is_ok(s) && is_ok(pc));

    board.piece_on(s, pc);
    typeBB[ALL_PIECE] |= typeBB[type_of(pc)] |= s;
    colorBB[color_of(pc)] |= s;
    ++pieceCount[pc];
    ++pieceCount[pc & PIECE_TYPE_NB];
}

inline void Position::remove_piece(Square s) noexcept {
    assert(is_ok(s));

    Piece pc = board.piece_on(s);
    assert(is_ok(pc));
    board.piece_on(s, NO_PIECE);
    typeBB[ALL_PIECE] ^= s;
    typeBB[type_of(pc)] ^= s;
    colorBB[color_of(pc)] ^= s;
    --pieceCount[pc];
    --pieceCount[pc & PIECE_TYPE_NB];
}

inline void Position::move_piece(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    Piece pc = board.piece_on(s1);
    assert(is_ok(pc));
    board.piece_on(s1, NO_PIECE);
    board.piece_on(s2, pc);
    Bitboard s1s2 = s1 | s2;
    typeBB[ALL_PIECE] ^= s1s2;
    typeBB[type_of(pc)] ^= s1s2;
    colorBB[color_of(pc)] ^= s1s2;
}

inline void Position::do_move(Move m, State& newSt) noexcept { do_move(m, newSt, check(m)); }

inline State* Position::state() const noexcept { return st; }

// Position::SEE
inline bool Position::SEE::operator>=(Value threshold) const noexcept {  //
    return pos.see_ge(move, threshold);
}
inline bool Position::SEE::operator>(Value threshold) const noexcept {  //
    return *this >= threshold + 1;
}
inline bool Position::SEE::operator<=(Value threshold) const noexcept {  //
    return !(*this > threshold);
}
inline bool Position::SEE::operator<(Value threshold) const noexcept {  //
    return !(*this >= threshold);
}

}  // namespace DON

#endif  // #ifndef POSITION_H_INCLUDED
