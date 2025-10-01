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
#include <deque>
#include <iosfwd>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>

#include "bitboard.h"
#include "types.h"

namespace DON {

namespace Zobrist {

extern Key psq[PIECE_NB][SQUARE_NB];
extern Key castling[CASTLING_RIGHTS_NB];
extern Key enpassant[FILE_NB];
extern Key turn;

void init() noexcept;
}  // namespace Zobrist

// State struct stores information needed to restore Position object
// to its previous state when retract any move.
struct State final {
    // --- Copied when making a move
    Key   pawnKey[COLOR_NB];
    Key   groupKey[COLOR_NB][2];
    Value nonPawnMaterial[COLOR_NB];

    Square         kingSq[COLOR_NB];
    bool           castled[COLOR_NB];
    Square         epSq;
    Square         capSq;
    CastlingRights castlingRights;
    std::uint8_t   rule50Count;
    std::uint8_t   nullPly;  // Plies from Null-Move
    bool           rule50High;

    // --- Not copied when making a move (will be recomputed anyhow)
    Key         key;
    Bitboard    checkers;
    Bitboard    checks[PIECE_TYPE_NB];
    Bitboard    pinners[COLOR_NB];
    Bitboard    blockers[COLOR_NB];
    Bitboard    attacks[COLOR_NB][PIECE_TYPE_NB];
    std::int8_t repetition;
    Piece       capturedPiece;
    Piece       promotedPiece;

    State* preSt;
};

// A list to keep track of the position states along the setup moves
// (from the start position to the position just before the search starts).
// Needed by 'draw by repetition' detection.
// Use a std::deque because pointers to elements are not invalidated upon list resizing.
using StateList    = std::deque<State>;
using StateListPtr = std::unique_ptr<StateList>;

constexpr std::uint8_t R50Offset = 14U;
constexpr std::uint8_t R50Factor = 8U;

extern std::uint8_t DrawMoveCount;

extern bool Chess960;

inline std::uint8_t rule50_threshold(std::int8_t r50 = -4) noexcept {
    assert(r50 >= -2 * DrawMoveCount);
    return r50 + 2 * DrawMoveCount;
}

// Position class stores information regarding the board representation as
// pieces, active color, hash keys, castling info, etc. (Size = 192)
// Important methods are do_move() and undo_move(),
// used by the search to update node info when traversing the search tree.
class Position final {
   public:
    class Board final {
       public:
        struct Cardinal final {
           public:
            Cardinal() noexcept :
                rankPieces(0) {}

            constexpr void piece_on(File f, Piece pc) noexcept {
                auto shift = f << 2;
                rankPieces = (rankPieces & ~(0xF << shift)) | (pc << shift);
            }

            constexpr Piece piece_on(File f) const noexcept {
                return Piece((rankPieces >> (f << 2)) & 0xF);
            }

            std::uint8_t count(Piece pc) const noexcept {
                std::uint8_t cnt = 0;
                for (File f = FILE_A; f <= FILE_H; ++f)
                    cnt += piece_on(f) == pc;
                return cnt;
            }

            friend std::ostream& operator<<(std::ostream& os, const Cardinal& cardinal) noexcept;

           private:
            std::uint32_t rankPieces;
        };

        Board() noexcept                        = default;
        Board(const Board&) noexcept            = delete;
        Board(Board&&) noexcept                 = delete;
        Board& operator=(const Board&) noexcept = default;
        Board& operator=(Board&&) noexcept      = delete;

        constexpr void piece_on(Square s, Piece pc) noexcept {
            cardinals[rank_of(s)].piece_on(file_of(s), pc);
        }
        constexpr Piece piece_on(Square s) const noexcept {
            return cardinals[rank_of(s)].piece_on(file_of(s));
        }

        std::uint8_t count(Piece pc) const noexcept {
            return std::accumulate(
              std::begin(cardinals), std::end(cardinals), 0,
              [=](std::uint8_t cnt, const Cardinal& cardinal) { return cnt + cardinal.count(pc); });
        }

        friend std::ostream& operator<<(std::ostream& os, const Board& board) noexcept;

       private:
        Cardinal cardinals[RANK_NB];
    };

    static void init() noexcept;

    Position() noexcept = default;

   private:
    Position(const Position&) noexcept            = delete;
    Position(Position&&) noexcept                 = delete;
    Position& operator=(const Position&) noexcept = default;
    Position& operator=(Position&&) noexcept      = delete;

   public:
    // FEN string input/output
    void        set(std::string_view fenStr, State* const newSt) noexcept;
    void        set(std::string_view code, Color c, State* const newSt) noexcept;
    void        set(const Position& pos, State* const newSt) noexcept;
    std::string fen(bool full = true) const noexcept;

    // Position representation
    Piece    piece_on(Square s) const noexcept;
    bool     empty_on(Square s) const noexcept;
    Bitboard pieces() const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(PieceTypes... pts) const noexcept;
    Bitboard pieces(Color c) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(Color c, PieceTypes... pts) const noexcept;
    template<PieceType PT>
    Bitboard pieces(Color c, Square s, Bitboard blockers) const noexcept;
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

    bool exist_attackers_to(Square s, Bitboard attackers, Bitboard occupied) const noexcept;
    bool exist_attackers_to(Square s, Bitboard attackers) const noexcept;

    // Attacks from a piece type
    template<PieceType PT>
    Bitboard attacks_by(Color c) const noexcept;

   public:
    // Doing and undoing moves
    DirtyPiece do_move(const Move& m, State& newSt, bool check) noexcept;
    DirtyPiece do_move(const Move& m, State& newSt) noexcept;
    void       undo_move(const Move& m) noexcept;
    void       do_null_move(State& newSt) noexcept;
    void       undo_null_move() noexcept;

    // Properties of moves
    bool  pseudo_legal(const Move& m) const noexcept;
    bool  legal(const Move& m) const noexcept;
    bool  capture(const Move& m) const noexcept;
    bool  capture_promo(const Move& m) const noexcept;
    bool  check(const Move& m) const noexcept;
    bool  dbl_check(const Move& m) const noexcept;
    bool  fork(const Move& m) const noexcept;
    Piece moved_piece(const Move& m) const noexcept;
    Piece captured_piece(const Move& m) const noexcept;
    auto  captured(const Move& m) const noexcept;

    // Hash keys
    Key key(std::int16_t ply = 0) const noexcept;
    Key pawn_key(Color c) const noexcept;
    Key pawn_key() const noexcept;
    Key minor_key(Color c) const noexcept;
    Key minor_key() const noexcept;
    Key major_key(Color c) const noexcept;
    Key major_key() const noexcept;
    Key non_pawn_key(Color c) const noexcept;
    Key non_pawn_key() const noexcept;
    Key material_key() const noexcept;
    Key move_key(const Move& m) const noexcept;

    // Static Exchange Evaluation
    auto see(const Move& m) const noexcept { return SEE(*this, m); }

    // Other properties
    Color        active_color() const noexcept;
    std::int16_t ply() const noexcept;
    std::int32_t move_num() const noexcept;
    std::int32_t phase() const noexcept;

    std::int16_t rule50_count() const noexcept;
    std::int16_t null_ply() const noexcept;
    std::int16_t repetition() const noexcept;

    bool rule50_high() const noexcept;
    bool is_repetition(std::int16_t ply) const noexcept;
    bool is_draw(std::int16_t ply, bool rule50Use = true, bool stalemateUse = false) const noexcept;
    bool has_repetition() const noexcept;
    bool upcoming_repetition(std::int16_t ply) const noexcept;

    Value non_pawn_material(Color c) const noexcept;
    Value non_pawn_material() const noexcept;
    bool  castled(Color c) const noexcept;
    bool  bishop_paired(Color c) const noexcept;
    bool  bishop_opposite() const noexcept;

    int std_material() const noexcept;

    std::size_t bucket() const noexcept;

    Value material() const noexcept;
    Value evaluate() const noexcept;
    Value bonus() const noexcept;

    void put_piece(Square s, Piece pc) noexcept;
    void remove_piece(Square s) noexcept;

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

        constexpr SEE(const Position& p, const Move& m) noexcept :
            pos(p),
            move(m) {}

        bool operator>=(int threshold) const noexcept;
        bool operator>(int threshold) const noexcept;
        bool operator<=(int threshold) const noexcept;
        bool operator<(int threshold) const noexcept;

       private:
        const Position& pos;
        const Move&     move;
    };

    // Initialization helpers (used while setting up a position)
    void set_castling_rights(Color c, Square rOrg) noexcept;
    void set_state() noexcept;
    void set_ext_state() noexcept;

    bool can_enpassant(Color           ac,
                       Square          epSq,
                       bool            before      = false,
                       Bitboard* const epAttackers = nullptr) const noexcept;

    // Other helpers
    void move_piece(Square s1, Square s2) noexcept;

    template<bool Do>
    void do_castling(Color             ac,
                     Square            org,
                     Square&           dst,
                     Square&           rOrg,
                     Square&           rDst,
                     DirtyPiece* const dp = nullptr) noexcept;

    Key adjust_key(Key k, std::int16_t ply = 0) const noexcept;

    void reset_ep_sq() noexcept;
    void reset_rule50_count() noexcept;
    void reset_repetitions() noexcept;

    // Static Exchange Evaluation
    bool see_ge(const Move& m, int threshold) const noexcept;

    // Data members
    Board        board;
    Bitboard     typeBB[PIECE_TYPE_NB];
    Bitboard     colorBB[COLOR_NB];
    std::uint8_t pieceCount[PIECE_NB];
    Bitboard     castlingPath[COLOR_NB * CASTLING_SIDE_NB];
    Square       castlingRookSq[COLOR_NB * CASTLING_SIDE_NB];
    std::uint8_t castlingRightsMask[COLOR_NB * FILE_NB + 1];
    Color        activeColor;
    std::int16_t gamePly;
    State*       st;
};

inline Piece Position::piece_on(Square s) const noexcept {
    assert(is_ok(s));
    return board.piece_on(s);
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
inline Bitboard Position::pieces(Color c, Square s, Bitboard blockers) const noexcept {
    switch (PT)
    {
    case KNIGHT :
        return pieces(c, KNIGHT) & (~blockers);
    case BISHOP :
        return pieces(c, BISHOP) & (~blockers | attacks_bb<BISHOP>(s));
    case ROOK :
        return pieces(c, ROOK) & (~blockers | attacks_bb<ROOK>(s));
    default :
        return pieces(c, PT);
    }
}

template<PieceType PT>
inline Bitboard Position::pieces(Color c, Square s) const noexcept {
    return pieces<PT>(c, s, blockers(c));
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
    static constexpr auto Indices = []() {
        std::array<std::size_t, SQUARE_NB> indices{};
        for (std::size_t s = 0; s < indices.size(); ++s)
        {
            auto rank  = rank_of(Square(s));
            auto file  = file_of(Square(s));
            indices[s] = rank == RANK_1 ? WHITE * FILE_NB + file
                       : rank == RANK_8 ? BLACK * FILE_NB + file
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

inline bool Position::exist_attackers_to(Square s, Bitboard attackers, Bitboard occupied) const noexcept {
    return  (attackers & pieces(KNIGHT       ) & attacks_bb<KNIGHT>(s))
        || ((attackers & pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s))
         && (attackers & pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupied)))
        || ((attackers & pieces(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s))
         && (attackers & pieces(QUEEN, ROOK  ) & attacks_bb<ROOK  >(s, occupied)))
        ||  (attackers & pieces(KING         ) & attacks_bb<KING  >(s))
        ||  (attackers & ((pieces(WHITE, PAWN) & attacks_bb<PAWN  >(s, BLACK))
                        | (pieces(BLACK, PAWN) & attacks_bb<PAWN  >(s, WHITE))));
}
inline bool Position::exist_attackers_to(Square s, Bitboard attackers) const noexcept {
    return exist_attackers_to(s, attackers, pieces());
}

// clang-format on

// Computes attacks from a piece type for a given color.
template<PieceType PT>
inline Bitboard Position::attacks_by(Color c) const noexcept {
    if constexpr (PT == PAWN)
    {
        return attacks_pawn_bb(pieces(c, PAWN), c);
    }
    else
    {
        Bitboard attacks = 0;

        Square   kingSq = king_sq(c);
        Bitboard pc     = pieces<PT>(c, kingSq, blockers(c));
        while (pc)
        {
            Square   s = pop_lsb(pc);
            Bitboard b = attacks_bb<PT>(s, pieces());
            if (PT != KNIGHT && (blockers(c) & s))
                b &= line_bb(kingSq, s);
            attacks |= b;
        }
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

inline Key Position::adjust_key(Key k, std::int16_t ply) const noexcept {
    return st->rule50Count + ply - R50Offset < 0
           ? k
           : k ^ make_hash((st->rule50Count + ply - R50Offset) / R50Factor);
}

inline Key Position::key(std::int16_t ply) const noexcept { return adjust_key(st->key, ply); }

inline Key Position::pawn_key(Color c) const noexcept { return st->pawnKey[c]; }

inline Key Position::pawn_key() const noexcept { return pawn_key(WHITE) ^ pawn_key(BLACK); }

inline Key Position::minor_key(Color c) const noexcept { return st->groupKey[c][0]; }

inline Key Position::minor_key() const noexcept { return minor_key(WHITE) ^ minor_key(BLACK); }

inline Key Position::major_key(Color c) const noexcept { return st->groupKey[c][1]; }

inline Key Position::major_key() const noexcept { return major_key(WHITE) ^ major_key(BLACK); }

inline Key Position::non_pawn_key(Color c) const noexcept { return minor_key(c) ^ major_key(c) ^ Zobrist::psq[make_piece(c, KING)][king_sq(c)]; }

inline Key Position::non_pawn_key() const noexcept { return non_pawn_key(WHITE) ^ non_pawn_key(BLACK); }

inline Value Position::non_pawn_material(Color c) const noexcept { return st->nonPawnMaterial[c]; }

inline Value Position::non_pawn_material() const noexcept { return non_pawn_material(WHITE) + non_pawn_material(BLACK); }

// clang-format on

inline Color Position::active_color() const noexcept { return activeColor; }

inline std::int16_t Position::ply() const noexcept { return gamePly; }

inline std::int32_t Position::move_num() const noexcept {
    return 1 + (ply() - (active_color() == BLACK)) / 2;
}

inline std::int32_t Position::phase() const noexcept {
    constexpr int MaxPhase = 24;

    return std::max(
      MaxPhase - count<KNIGHT>() - count<BISHOP>() - 2 * count<ROOK>() - 4 * count<QUEEN>(), 0);
}

inline std::int16_t Position::rule50_count() const noexcept { return st->rule50Count; }

inline std::int16_t Position::null_ply() const noexcept { return st->nullPly; }

inline std::int16_t Position::repetition() const noexcept { return st->repetition; }

inline bool Position::rule50_high() const noexcept { return st->rule50High; }

inline bool Position::castled(Color c) const noexcept { return st->castled[c]; }

inline bool Position::bishop_paired(Color c) const noexcept {
    return (pieces(c, BISHOP) & COLOR_BB[WHITE]) && (pieces(c, BISHOP) & COLOR_BB[BLACK]);
}

inline bool Position::bishop_opposite() const noexcept {
    return count<BISHOP>(WHITE) == 1 && count<BISHOP>(BLACK) == 1
        && color_opposite(square<BISHOP>(WHITE), square<BISHOP>(BLACK));
}

inline int Position::std_material() const noexcept {
    return 1.0469f * count<PAWN>() + 2.8438f * count<KNIGHT>() + 2.8438f * count<BISHOP>()
         + 5.2656f * count<ROOK>() + 8.6406f * count<QUEEN>();
}

inline std::size_t Position::bucket() const noexcept { return (count<ALL_PIECE>() - 1) / 4; }

inline Value Position::material() const noexcept {
    return 535 * count<PAWN>() + non_pawn_material();
}

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
    return (+20 * (bishop_paired(ac) - bishop_paired(~ac))
            +56 * ((can_castle(ac & ANY_CASTLING) || castled(ac))
                 - (can_castle(~ac & ANY_CASTLING) || castled(~ac))))
         * (1.0f - 4.1250e-2f * phase());
    // clang-format on
}

inline bool Position::capture(const Move& m) const noexcept {
    assert(pseudo_legal(m));
    return (m.type_of() != CASTLING && !empty_on(m.dst_sq())) || m.type_of() == EN_PASSANT;
}

inline bool Position::capture_promo(const Move& m) const noexcept {
    return capture(m) || m.type_of() == PROMOTION;
}

inline Piece Position::moved_piece(const Move& m) const noexcept {
    assert(pseudo_legal(m));
    return piece_on(m.org_sq());
}

inline Piece Position::captured_piece(const Move& m) const noexcept {
    assert(pseudo_legal(m));
    assert(m.type_of() != CASTLING);
    return m.type_of() == EN_PASSANT ? make_piece(~active_color(), PAWN) : piece_on(m.dst_sq());
}

inline auto Position::captured(const Move& m) const noexcept { return type_of(captured_piece(m)); }

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

inline DirtyPiece Position::do_move(const Move& m, State& newSt) noexcept {
    return do_move(m, newSt, check(m));
}

inline State* Position::state() const noexcept { return st; }

// Position::SEE
inline bool Position::SEE::operator>=(int threshold) const noexcept {
    return pos.see_ge(move, threshold);
}
inline bool Position::SEE::operator>(int threshold) const noexcept {
    return *this >= 1 + threshold;
}
inline bool Position::SEE::operator<=(int threshold) const noexcept { return !(*this > threshold); }
inline bool Position::SEE::operator<(int threshold) const noexcept { return !(*this >= threshold); }

}  // namespace DON

#endif  // #ifndef POSITION_H_INCLUDED
