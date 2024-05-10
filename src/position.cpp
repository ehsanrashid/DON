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

#include "position.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "misc.h"
#include "movegen.h"
#include "tt.h"
#include "uci.h"
#include "nnue/nnue_common.h"
#include "syzygy/tbprobe.h"

namespace DON {

namespace Zobrist {

Key psq[PIECE_NB][SQUARE_NB];
Key castling[CASTLING_RIGHTS_NB];
Key enpassant[FILE_NB];
Key side, pawn;
}  // namespace Zobrist

namespace {

constexpr Piece Pieces[12]{W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
                           B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING};

constexpr std::uint8_t MobilityWeight[PIECE_TYPE_NB]{0, 1, 10, 12, 16, 14, 1, 0};

// Implements Marcel van Kervinck's cuckoo algorithm to detect repetition of positions
// for 3-fold repetition draws. The algorithm uses two hash tables with Zobrist hashes
// to allow fast detection of recurring positions. For details see:
// http://web.archive.org/web/20201107002606/https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
struct Cuckoo final {
    Key  key;
    Move move;
};

std::array<Cuckoo, 0X2000> Cuckoos;

// First and second hash functions for indexing the cuckoo tables
constexpr std::uint16_t H1(Key k) noexcept { return std::uint16_t(k >> 00) & (Cuckoos.size() - 1); }
constexpr std::uint16_t H2(Key k) noexcept { return std::uint16_t(k >> 16) & (Cuckoos.size() - 1); }

}  // namespace

bool         Position::Chess960      = false;
std::uint8_t Position::DrawMoveCount = 50;

// Initializes at startup the various arrays used to compute hash keys
void Position::init() noexcept {
    PRNG rng(0x105524ULL);

    for (std::uint8_t pc : {0, 7, 8, 15})
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
            Zobrist::psq[pc][s] = 0ULL;
    for (Piece pc : Pieces)
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
            Zobrist::psq[pc][s] = rng.rand<Key>();

    for (std::uint8_t cr = NO_CASTLING; cr <= ANY_CASTLING; ++cr)
    {
        Zobrist::castling[cr] = 0ULL;

        Bitboard b = cr;
        while (b)
        {
            Key k = Zobrist::castling[1U << pop_lsb(b)];
            Zobrist::castling[cr] ^= k ? k : rng.rand<Key>();
        }
    }

    for (File f = FILE_A; f <= FILE_H; ++f)
        Zobrist::enpassant[f] = rng.rand<Key>();

    Zobrist::side = rng.rand<Key>();
    Zobrist::pawn = rng.rand<Key>();

    // Prepare the cuckoo tables
    Cuckoos.fill({0, Move::None()});
    [[maybe_unused]] std::uint16_t count = 0;
    for (Piece pc : Pieces)
    {
        if (type_of(pc) == PAWN)
            continue;
        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
            for (Square s2 = Square(int(s1) + 1); s2 <= SQ_H8; ++s2)
                if (attacks_bb(type_of(pc), s1, 0) & s2)
                {
                    Cuckoo ck{Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side,
                              Move(s1, s2)};

                    std::uint16_t i = H1(ck.key);
                    while (true)
                    {
                        std::swap(Cuckoos[i], ck);
                        if (!ck.move)  // Arrived at empty slot?
                            break;
                        i ^= H1(ck.key) ^ H2(ck.key);  // Push victim to alternative slot
                    }
                    ++count;
                }
    }
    assert(count == 3668);
}

// Initializes the position object with the given FEN string.
// This function is not very robust - make sure that input FENs are correct,
// this is assumed to be the responsibility of the GUI.
Position& Position::set(std::string_view fenStr, StateInfo* si) noexcept {
    /*
   A FEN string defines a particular position using only the ASCII character set.

   A FEN string contains six fields separated by a space. The fields are:

   1) Piece placement (from white's perspective). Each rank is described, starting
      with rank 8 and ending with rank 1. Within each rank, the contents of each
      square are described from file A through file H. Following the Standard
      Algebraic Notation (SAN), each piece is identified by a single letter taken
      from the standard English names. White pieces are designated using upper-case
      letters ("PNBRQK") whilst Black uses lowercase ("pnbrqk"). Blank squares are
      noted using digits 1 through 8 (the number of blank squares), and "/"
      separates ranks.

   2) Active color. "w" means white moves next, "b" means black.

   3) Castling availability. If neither side can castle, this is "-". Otherwise,
      this has one or more letters: "K" (White can castle kingside), "Q" (White
      can castle queenside), "k" (Black can castle kingside), and/or "q" (Black
      can castle queenside).

   4) En-passant target square (in algebraic notation). If there's no en-passant
      target square, this is "-". If a pawn has just made a 2-square move, this
      is the position "behind" the pawn. Following X-FEN standard, this is recorded
      only if there is a pawn in position to make an en-passant capture, and if
      there really is a pawn that might have advanced two squares.

   5) Halfmove clock. This is the number of halfmoves since the last pawn advance
      or capture. This is used to determine if a draw can be claimed under the
      fifty-move rule.

   6) Fullmove number. The number of the full move. It starts at 1, and is
      incremented after Black's move.
*/
    assert(!fenStr.empty());

    std::memset(this, 0, sizeof(Position));
    std::fill(std::begin(castlingRookSquare), std::end(castlingRookSquare), SQ_NONE);
    std::memset(si, 0, sizeof(StateInfo));
    st = si;

    st->epSquare = st->capSquare = SQ_NONE;
    st->kingSquare[WHITE] = st->kingSquare[BLACK] = SQ_NONE;

    std::istringstream iss(fenStr.data());
    iss >> std::noskipws;
    std::uint8_t token;

    [[maybe_unused]] std::uint8_t piecesCount = 0;

    Square sq = SQ_A8;
    // 1. Piece placement
    while ((iss >> token) && !std::isspace(token))
    {
        if (std::isdigit(token))
        {
            int files = token - '0';
            assert(1 <= files && files <= 8 - file_of(sq) && "Position::set(): Invalid Files");
            sq += files * EAST;  // Advance the given number of files
        }
        else if (token == '/')
        {
            assert(file_of(sq) == FILE_A);
            assert(rank_of(sq) >= RANK_3 || rank_of(sq) == RANK_1);
            sq += SOUTH_2;
        }
        else if (Piece pc; (pc = UCI::piece(token)) != NO_PIECE)
        {
            assert(++piecesCount <= 32 && "Position::set(): Number of Pieces");
            assert(empty_on(sq) && "Position::set(): Square not empty");
            put_piece(pc, sq);
            if (type_of(pc) == KING)
                st->kingSquare[color_of(pc)] = sq;
            ++sq;
        }
        else
            assert(false && "Position::set(): Invalid Piece");
    }
    assert(pieceCount[W_KING] == 1 && pieceCount[B_KING] == 1);
    assert(pieceCount[W_PAWN] <= 8 && pieceCount[B_PAWN] <= 8);
    assert(distance(king_square(WHITE), king_square(BLACK)) > 1);

    iss >> std::ws;

    // 2. Active color
    iss >> token;
    token = std::uint8_t(std::tolower(token));
    assert((token == 'w' || token == 'b') && "Position::set(): Invalid Color");
    sideToMove = token == 'w' ? WHITE : token == 'b' ? BLACK : COLOR_NB;

    iss >> std::ws;

    // 3. Castling availability. Compatible with 3 standards: Normal FEN standard,
    // Shredder-FEN that uses the letters of the columns on which the rooks began
    // the game instead of KQkq and also X-FEN standard that, in case of Chess960,
    // if an inner rook is associated with the castling right, the castling tag is
    // replaced by the file letter of the involved rook, as for the Shredder-FEN.
    [[maybe_unused]] std::uint8_t castlingRightsCount = 0;
    while ((iss >> token) && !std::isspace(token))
    {
        if (token == '-')
            continue;
        assert(++castlingRightsCount <= 4 && "Position::set(): Number of Castling Rights");

        Color c = std::isupper(token) ? WHITE : BLACK;
        if (relative_rank(c, king_square(c)) != RANK_1)
        {
            assert(false && "Position::set(): Missing King on RANK_1");
            continue;
        }
        if (!(pieces(c, ROOK) & relative_rank(c, RANK_1)))
        {
            assert(false && "Position::set(): Missing Rook on RANK_1");
            continue;
        }

        token = std::uint8_t(std::toupper(token));

        Square rsq = SQ_NONE;
        if (token == 'K')
        {
            rsq = relative_square(c, SQ_H1);
            while (file_of(rsq) >= FILE_C && !(pieces(c, ROOK) & rsq) && rsq != king_square(c))
                --rsq;
        }
        else if (token == 'Q')
        {
            rsq = relative_square(c, SQ_A1);
            while (file_of(rsq) <= FILE_F && !(pieces(c, ROOK) & rsq) && rsq != king_square(c))
                ++rsq;
        }
        else if ('A' <= token && token <= 'H')
        {
            rsq = make_square(File(token - 'A'), relative_rank(c, RANK_1));
        }
        else
        {
            assert(false && "Position::set(): Invalid Castling Rights");
            continue;
        }

        if (!(is_ok(rsq) && (pieces(c, ROOK) & rsq)))
        {
            assert(false && "Position::set(): Missing Castling Rook");
            continue;
        }

        set_castling_rights(c, rsq);
    }
    // if (!can_castle(WHITE_CASTLING)
    //    && ((king_square(WHITE) == SQ_C1
    //         && !(pieces(WHITE, ROOK) & SQ_A1))
    //        || (king_square(WHITE) == SQ_G1
    //            && !(pieces(WHITE, ROOK) & SQ_H1))))
    //    st->hasCastled[WHITE] = true;
    // if (!can_castle(BLACK_CASTLING)
    //    && ((king_square(BLACK) == SQ_C8
    //         && !(pieces(BLACK, ROOK) & SQ_A8))
    //        || (king_square(BLACK) == SQ_G8
    //            && !(pieces(BLACK, ROOK) & SQ_H8))))
    //    st->hasCastled[BLACK] = true;

    iss >> std::ws;

    // 4. En-passant square.
    // Ignore if square is invalid or not on side to move relative rank 6.
    bool enpassant = false;
    iss >> token;
    if (token != '-')
    {
        std::uint8_t file = std::uint8_t(std::tolower(token)), rank;
        iss >> rank;
        if ('a' <= file && file <= 'h' && rank == (sideToMove == WHITE ? '6' : '3'))
        {
            st->epSquare = make_square(File(file - 'a'), Rank(rank - '1'));

            // En-passant square will be considered only if
            // a) there is an enemy pawn in front of epSquare
            // b) there is no piece on epSquare or behind epSquare
            // c) side to move have a pawn threatening epSquare
            // d) there is no enemy Bishop, Rook or Queen pinning
            enpassant = (pieces(~sideToMove, PAWN) & (st->epSquare - pawn_spush(sideToMove)))
                     && !(pieces() & (st->epSquare | (st->epSquare + pawn_spush(sideToMove))))
                     && (pieces(sideToMove, PAWN) & pawn_attacks_bb(~sideToMove, st->epSquare))
                     && can_enpassant(sideToMove, st->epSquare);
        }
        else
            assert(false && "Position::set(): Invalid En-passant square");
    }

    // 5-6. Halfmove clock and fullmove number
    std::int16_t rule50   = 0;
    std::int16_t fullmove = 1;
    iss >> std::skipws >> rule50 >> fullmove;

    st->rule50 = std::max<std::int16_t>(rule50, 0);
    // Convert from fullmove starting from 1 to gamePly starting from 0,
    // handle also common incorrect FEN with fullmove = 0.
    gamePly = std::max(2 * (fullmove - 1), 0) + (sideToMove == BLACK);

    // Reset illegal values
    if (is_ok_ep(st->epSquare))
    {
        st->rule50 = 0;
        if (!enpassant)
            st->epSquare = SQ_NONE;
    }
    assert(st->rule50 <= std::min<std::int16_t>(gamePly, 100));

    set_state();

    assert(pos_is_ok());

    return *this;
}

// Overload to initialize the position object with the given endgame code string
// like "KBPKN". It's mainly a helper to get the material key out of an endgame code.
Position& Position::set(std::string_view code, Color c, StateInfo* si) noexcept {
    assert(!code.empty() && code[0] == 'K');

    std::string sides[COLOR_NB]{
      std::string(code.substr(code.find('K', 1))),                                // Weak
      std::string(code.substr(0, std::min(code.find('v'), code.find('K', 1))))};  // Strong

    assert(0 < sides[WHITE].length() && sides[WHITE].length() < 8);
    assert(0 < sides[BLACK].length() && sides[BLACK].length() < 8);

    sides[c] = to_lower(sides[c]);

    std::string fenStr = "8/" + sides[WHITE] + char(8 - sides[WHITE].length() + '0') + "/8/8/8/8/"
                       + sides[BLACK] + char(8 - sides[BLACK].length() + '0') + "/8 w - - 0 1";

    return set(fenStr, si);
}

// Returns a FEN representation of the position.
// In case of Chess960 the Shredder-FEN notation is used.
// This is mainly a debugging function.
std::string Position::fen(bool full) const noexcept {
    std::ostringstream oss;

    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            std::uint8_t emptyCount = 0;
            while (f <= FILE_H && empty_on(make_square(f, r)))
            {
                ++emptyCount;
                ++f;
            }
            if (emptyCount)
                oss << int(emptyCount);

            if (f <= FILE_H)
                oss << UCI::piece(piece_on(make_square(f, r)));
        }

        if (r > RANK_1)
            oss << '/';
    }

    oss << (sideToMove == WHITE ? " w " : sideToMove == BLACK ? " b " : " - ");

    if (can_castle(ANY_CASTLING))
    {
        if (can_castle(WHITE_OO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(WHITE_OO)), false) : 'K');
        if (can_castle(WHITE_OOO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(WHITE_OOO)), false) : 'Q');
        if (can_castle(BLACK_OO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(BLACK_OO)), true) : 'k');
        if (can_castle(BLACK_OOO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(BLACK_OOO)), true) : 'q');
    }
    else
    {
        oss << '-';
    }

    oss << ' ' << (is_ok_ep(ep_square()) ? UCI::square(ep_square()) : "-");
    if (full)
        oss << ' ' << int(rule50_count()) << ' ' << game_move();

    return oss.str();
}

// Sets castling rights given the corresponding color and the rook starting square.
void Position::set_castling_rights(Color c, Square rorg) noexcept {
    assert(relative_rank(c, rorg) == RANK_1 && (pieces(c, ROOK) & rorg)
           && !castlingRightsMask[rorg]);
    Square korg = king_square(c);
    assert(relative_rank(c, korg) == RANK_1 && (pieces(c, KING) & korg));

    CastlingRights cr    = castling_rights(c, korg, rorg);
    std::uint8_t   crlsb = lsb(cr);
    assert(0 <= crlsb && crlsb < 4);
    assert(!is_ok(castlingRookSquare[crlsb]));

    st->castlingRights |= cr;
    castlingRightsMask[korg] |= cr;
    castlingRightsMask[rorg]  = cr;
    castlingRookSquare[crlsb] = rorg;

    Square kdst = king_castle_sq(c, korg, rorg);
    Square rdst = rook_castle_sq(c, korg, rorg);

    castlingPath[crlsb] = (between_bb(korg, kdst) | between_bb(rorg, rdst)) & ~(korg | rorg);
}

// Computes the hash keys of the position, and other data
// that once computed is updated incrementally as moves are made.
// The function is only used when a new position is set up.
void Position::set_state() noexcept {
    assert(st->key == 0 && st->materialKey == 0);
    assert(st->nonPawnMaterial[WHITE] == VALUE_ZERO);
    assert(st->nonPawnMaterial[BLACK] == VALUE_ZERO);

    st->pawnKey = Zobrist::pawn;
    for (Bitboard occ = pieces(); occ;)
    {
        Square s  = pop_lsb(occ);
        Piece  pc = piece_on(s);
        st->key ^= Zobrist::psq[pc][s];

        if (type_of(pc) == PAWN)
            st->pawnKey ^= Zobrist::psq[pc][s];

        else if (type_of(pc) != KING)
            st->nonPawnMaterial[color_of(pc)] += PieceValue[pc];
    }

    if (is_ok_ep(ep_square()))
        st->key ^= Zobrist::enpassant[file_of(ep_square())];

    st->key ^= Zobrist::castling[st->castlingRights];

    if (sideToMove == BLACK)
        st->key ^= Zobrist::side;

    for (Piece pc : Pieces)
        for (std::uint8_t cnt = 0; cnt < pieceCount[pc]; ++cnt)
            st->materialKey ^= Zobrist::psq[pc][cnt];

    st->checkers = attackers_to(king_square(sideToMove)) & pieces(~sideToMove);

    set_ext_state();
}

// Set extra state to detect if a move gives check
void Position::set_ext_state() noexcept {

    Bitboard occupied = pieces();

    Square ksq = king_square(~sideToMove);
    // clang-format off
    st->checks[ALL_PIECE]                   = 0;
    st->checks[PAWN]                        = pawn_attacks_bb(~sideToMove, ksq);
    st->checks[KNIGHT]                      = attacks_bb<KNIGHT>(ksq);
    st->checks[QUEEN] = st->checks[BISHOP]  = attacks_bb<BISHOP>(ksq, occupied);
    st->checks[QUEEN] |= st->checks[ROOK]   = attacks_bb<ROOK>  (ksq, occupied);
    st->checks[KING] = st->checks[EX_PIECE] = 0;
    // clang-format on

    Bitboard pinners = st->pinners[WHITE] = st->pinners[BLACK] = 0;
    for (Color c : {WHITE, BLACK})
    {
        Color xc = ~c;

        ksq = king_square(c);

        // Calculates st->blockers[c] and st->pinners[],
        // which store respectively the pieces preventing king of color c from being in check
        // and the slider pieces of color xc pinning pieces of color c to the king.
        st->blockers[c] = 0;

        // Snipers are sliders that attack 'ksq' when a piece and other snipers are removed
        Bitboard snipers = pieces(xc)
                         & ((pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(ksq))
                            | (pieces(QUEEN, ROOK) & attacks_bb<ROOK>(ksq)));
        Bitboard occ = occupied ^ snipers;

        while (snipers)
        {
            Square sniper = pop_lsb(snipers);

            Bitboard b = between_bb(ksq, sniper) & occ;
            if (b && !more_than_one(b))
            {
                st->blockers[c] |= b;
                pinners |= st->pinners[b & pieces(c) ? xc : c] |= sniper;
            }
        }
        // clang-format off
        st->mobility[c] = MobilityWeight[PAWN] * popcount(pawn_push_bb(c, pieces(c, PAWN) & ~(st->blockers[c] & pieces(PAWN) & attacks_bb<QUEEN>(ksq, occupied))) & ~occupied);
        st->attacks[c][PAWN] = attacks_by<PAWN>(c, pieces(xc), occupied);
        // clang-format on
    }
    for (Color c : {WHITE, BLACK})
    {
        // clang-format off
        Bitboard target = ~(st->attacks[~c][PAWN]
                          | (pieces(~c) & pinners)
                          | (pieces( c) & (st->blockers[c]
                                         | pieces(QUEEN, KING)
                                         | (pieces(PAWN) & (LowRankBB[c] | (pawn_push_bb(~c, occupied) & ~pawn_attacks_bb(~c, pieces(~c) & ~pieces(KING))))))));

        st->attacks[c][KNIGHT]    = st->attacks[c][PAWN]   | attacks_by<KNIGHT>(c, target);
        st->attacks[c][BISHOP]    = st->attacks[c][KNIGHT] | attacks_by<BISHOP>(c, target, occupied ^ ((pieces(c, QUEEN, BISHOP) & ~st->blockers[c]) | (pieces(~c, KING, QUEEN, ROOK) & ~pinners)));
        st->attacks[c][ROOK]      = st->attacks[c][BISHOP] | attacks_by<ROOK>  (c, target, occupied ^ ((pieces(c, QUEEN, ROOK)   & ~st->blockers[c]) | (pieces(~c, KING, QUEEN) & ~pinners)));
        st->attacks[c][QUEEN]     = st->attacks[c][ROOK]   | attacks_by<QUEEN> (c, target, occupied ^ ((pieces(c, QUEEN)         & ~st->blockers[c]) | (pieces(~c, KING))));
        st->attacks[c][KING]      = st->attacks[c][QUEEN]  | attacks_by<KING>  (c, target);
        st->attacks[c][ALL_PIECE] = st->attacks[c][KING];
        st->attacks[~c][EX_PIECE] = (pieces(~c, KNIGHT, BISHOP) & st->attacks[c][PAWN])
                                  | (pieces(~c, ROOK)           & st->attacks[c][MINOR])
                                  | (pieces(~c, QUEEN)          & st->attacks[c][ROOK]);
        // clang-format on
    }
}

// Check can en-passant
bool Position::can_enpassant(Color c, Square epSq, bool before) const noexcept {
    assert(is_ok_ep(epSq) && relative_rank(c, epSq) == RANK_6);

    // En-passant attackers
    Bitboard attackers = pieces(c, PAWN) & pawn_attacks_bb(~c, epSq);
    if (!attackers)
        return false;

    Square cap = before ? epSq + pawn_spush(c) : epSq - pawn_spush(c);
    assert(pieces(~c, PAWN) & cap);

    Square ksq = king_square(c);

    Bitboard qB = pieces(~c, QUEEN, BISHOP) & attacks_bb<BISHOP>(ksq);
    Bitboard qR = pieces(~c, QUEEN, ROOK) & attacks_bb<ROOK>(ksq);

    // Check en-passant is legal for the position
    Bitboard occupied = (pieces() ^ cap) | epSq;
    while (attackers)
        if (Bitboard occ = occupied ^ pop_lsb(attackers);
            !(qB & attacks_bb<BISHOP>(ksq, occ)) && !(qR & attacks_bb<ROOK>(ksq, occ)))
            return true;
    return false;
}

// Computes a bitboard of all pieces which attack a given square.
// Slider attacks use the occupied bitboard to indicate occupancy.
Bitboard Position::attackers_to(Square s, Bitboard occupied) const noexcept {

    return (pieces(WHITE, PAWN) & pawn_attacks_bb(BLACK, s))
         | (pieces(BLACK, PAWN) & pawn_attacks_bb(WHITE, s))
         | (pieces(KNIGHT) & attacks_bb<KNIGHT>(s))
         | (pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupied))
         | (pieces(QUEEN, ROOK) & attacks_bb<ROOK>(s, occupied))
         | (pieces(KING) & attacks_bb<KING>(s));
}

template<PieceType PT>
Bitboard Position::attacks_by(Color c, Bitboard target, Bitboard occupied) const noexcept {

    Square ksq = king_square(c);

    Bitboard attacks;
    if constexpr (PT == PAWN)
    {
        attacks = pawn_attacks_bb(
          c, pieces(c, PAWN) & ~(st->blockers[c] & pieces(PAWN) & attacks_bb<ROOK>(ksq, occupied)));
        st->mobility[c] += MobilityWeight[PAWN] * popcount(attacks & pieces(~c));
    }
    else
    {
        attacks = 0;

        Bitboard attackers = pieces(c, PT);
        while (attackers)
        {
            Square   org  = pop_lsb(attackers);
            Bitboard atks = attacks_bb<PT>(org, occupied);
            if (blockers(c) & org)
                atks &= line_bb(ksq, org);
            st->mobility[c] += MobilityWeight[PT] * popcount(atks & target);
            attacks |= atks;
        }
    }
    return attacks;
}

// Tests whether a pseudo-legal move is legal
bool Position::legal(Move m) const noexcept {
    assert(m.is_ok());
    assert(pieces(sideToMove, KING) & king_square(sideToMove));

    const Square org = m.org_sq(), dst = m.dst_sq();

    assert(color_of(piece_on(org)) == sideToMove);

    switch (m.type_of())
    {
    // En-passant captures are a tricky special case. Because they are rather uncommon,
    // Simply by testing whether the king is attacked after the move is made.
    case EN_PASSANT : {
        Square cap = dst - pawn_spush(sideToMove);
        assert(relative_rank(sideToMove, org) == RANK_5);
        assert(relative_rank(sideToMove, dst) == RANK_6);
        assert(type_of(piece_on(org)) == PAWN);
        assert(pieces(~sideToMove, PAWN) & cap);
        assert(!(pieces() & (dst | (dst + pawn_spush(sideToMove)))));
        assert(ep_square() == dst);
        assert(rule50_count() == 0);

        Bitboard occupied = (pieces() ^ org ^ cap) | dst;
        return !(pieces(~sideToMove, QUEEN, BISHOP)
                 & attacks_bb<BISHOP>(king_square(sideToMove), occupied))
            && !(pieces(~sideToMove, QUEEN, ROOK)
                 & attacks_bb<ROOK>(king_square(sideToMove), occupied));
    }
    // Castling moves generation does not check if the castling path is clear of
    // enemy attacks, it is delayed at a later time: now!
    case CASTLING : {
        assert(relative_rank(sideToMove, org) == RANK_1);
        assert(relative_rank(sideToMove, dst) == RANK_1);
        assert(type_of(piece_on(org)) == KING);
        assert(pieces(sideToMove, ROOK) & dst);
        assert(!checkers());
        assert(can_castle(castling_rights(sideToMove, org, dst)));
        assert(!castling_impeded(castling_rights(sideToMove, org, dst)));
        assert(castling_rook_square(castling_rights(sideToMove, org, dst)) == dst);

        // After castling, the rook and king final positions are the same in
        // Chess960 as they would be in standard chess.
        Square    kdst = king_castle_sq(sideToMove, org, dst);
        Direction step = org < kdst ? WEST : EAST;
        for (Square s = kdst; s != org; s += step)
            if (attackers_to(s) & pieces(~sideToMove))
                return false;

        // In case of Chess960, verify if the Rook blocks some checks.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !(blockers(sideToMove) & dst);
    }
    case PROMOTION :
        assert(relative_rank(sideToMove, org) == RANK_7);
        assert(relative_rank(sideToMove, dst) == RANK_8);
        assert(type_of(piece_on(org)) == PAWN);
        return !(blockers(sideToMove) & org);
    default :  // NORMAL
        // If the moving piece is a king, check whether the destination square is
        // attacked by the opponent.
        if (king_square(sideToMove) == org)
            //return !(attackers_to(dst, pieces() ^ org) & pieces(~sideToMove));
            return !(attackers_to(dst) & pieces(~sideToMove));

        // A non-king move is legal if and only if it is not pinned or it
        // is moving along the ray towards or away from the king.
        return !(blockers(sideToMove) & org) || aligned(org, dst, king_square(sideToMove));
    }
}

// Takes a random move and tests whether the move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal(Move m) const noexcept {

    const Square org = m.org_sq(), dst = m.dst_sq();
    const Piece  pc = piece_on(org);

    // If the origin square is not occupied by a piece belonging to
    // the side to move, the move is obviously not legal.
    if (!is_ok(pc) || color_of(pc) != sideToMove)
        return false;

    if (m.type_of() == CASTLING)
    {
        CastlingRights cr = castling_rights(sideToMove, org, dst);
        return relative_rank(sideToMove, org) == RANK_1 && relative_rank(sideToMove, dst) == RANK_1
            && type_of(pc) == KING && (pieces(sideToMove, ROOK) & dst) && !checkers()  //
            && can_castle(cr) && !castling_impeded(cr) && castling_rook_square(cr) == dst;
    }

    // The destination square cannot be occupied by a friendly piece
    if (pieces(sideToMove) & dst)
        return false;

    switch (m.type_of())
    {
    case NORMAL : {
        // Is not a promotion, so the promotion type must be empty
        assert(!is_ok(PieceType(m.promotion_type() - KNIGHT)));

        // Handle the special case of a pawn move
        if (type_of(pc) == PAWN)
        {
            // We have already handled promotion moves, so destination cannot be on the 8th/1st rank
            if (PromotionRankBB & dst)
                return false;
            // clang-format off
            if (!(relative_rank(sideToMove, org) < RANK_7 && relative_rank(sideToMove, dst) < RANK_8
                  && ((org + pawn_spush(sideToMove) == dst && !(pieces() & dst))  // Single push
                      || (pawn_attacks_bb(sideToMove, org) & pieces(~sideToMove) & dst)))  // Capture
             && !(relative_rank(sideToMove, org) == RANK_2 && relative_rank(sideToMove, dst) == RANK_4
                  && org + pawn_dpush(sideToMove) == dst  // Double push
                  && !(pieces() & (dst | (dst - pawn_spush(sideToMove))))))
                return false;
            // clang-format on
        }
        else if (!(attacks_bb(type_of(pc), org, pieces()) & dst))
            return false;
    }
    break;

    case PROMOTION :
        if (!(relative_rank(sideToMove, org) == RANK_7 && relative_rank(sideToMove, dst) == RANK_8
              && type_of(pc) == PAWN  //&& (PromotionRankBB & dst)
              && ((org + pawn_spush(sideToMove) == dst && !(pieces() & dst))
                  || (pawn_attacks_bb(sideToMove, org) & pieces(~sideToMove) & dst))))
            return false;
        break;

    case EN_PASSANT :
        if (!(relative_rank(sideToMove, org) == RANK_5 && relative_rank(sideToMove, dst) == RANK_6
              && type_of(pc) == PAWN && ep_square() == dst && rule50_count() == 0
              && (pieces(~sideToMove, PAWN) & (dst - pawn_spush(sideToMove)))
              && !(pieces() & (dst | (dst + pawn_spush(sideToMove))))
              && (pawn_attacks_bb(sideToMove, org) /*& ~pieces()*/ & dst)))
            return false;
        break;

    default :;  // CASTLING
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // We therefore have to take care that the same kind of moves are filtered out here.
    if (checkers())
    {
        // In case of king moves under check we have to remove the king so as to catch
        // invalid moves like b1a1 when opposite queen is on c1.
        if (king_square(sideToMove) == org)
            return !(attackers_to(dst, pieces() ^ org) & pieces(~sideToMove));

        return
          // Double check? In this case, a king move is required
          !more_than_one(checkers())
          // Pinned piece can never resolve a check
          //&& !(blockers(sideToMove) & org)
          // Our move must be a blocking interposition or a capture of the checking piece
          && ((between_bb(king_square(sideToMove), lsb(checkers())) & dst)
              || (m.type_of() == EN_PASSANT && (checkers() & (dst - pawn_spush(sideToMove)))));
    }

    return true;
}

// Tests whether a pseudo-legal move gives a check
bool Position::gives_check(Move m) const noexcept {
    assert(m.is_ok());

    const Square org = m.org_sq(), dst = m.dst_sq();

    assert(color_of(piece_on(org)) == sideToMove);

    if (
      // Is there a direct check?
      (checks(m.type_of() != PROMOTION ? type_of(piece_on(org)) : m.promotion_type()) & dst)
      // Is there a discovered check?
      || ((blockers(~sideToMove) & org)
          && (!aligned(org, dst, king_square(~sideToMove)) || m.type_of() == CASTLING)))
        return true;

    switch (m.type_of())
    {
    case PROMOTION :
        return attacks_bb(m.promotion_type(), dst, pieces() ^ org) & king_square(~sideToMove);

    // En-passant capture with check? We have already handled the case of direct
    // checks and ordinary discovered check, so the only case we need to handle
    // is the unusual case of a discovered check through the captured pawn.
    case EN_PASSANT : {
        Bitboard occupied = (pieces() ^ org ^ make_square(file_of(dst), rank_of(org))) | dst;
        return (pieces(sideToMove, QUEEN, BISHOP)
                & attacks_bb<BISHOP>(king_square(~sideToMove), occupied))
             | (pieces(sideToMove, QUEEN, ROOK)
                & attacks_bb<ROOK>(king_square(~sideToMove), occupied));
    }
    case CASTLING :
        // Castling is encoded as 'king captures the rook'
        return checks(ROOK) & rook_castle_sq(sideToMove, org, dst);

    default :  // NORMAL
        return false;
    }
}

bool Position::gives_dbl_check(Move m) const noexcept {
    assert(m.is_ok());

    const Square org = m.org_sq(), dst = m.dst_sq();

    assert(color_of(piece_on(org)) == sideToMove);

    switch (m.type_of())
    {
    case NORMAL :
        return
          // Is there a direct check?
          (checks(type_of(piece_on(org))) & dst)
          // Is there a discovered check?
          && ((blockers(~sideToMove) & org) && !aligned(org, dst, king_square(~sideToMove)));

    case PROMOTION :
        return (blockers(~sideToMove) & org)
            && (attacks_bb(m.promotion_type(), dst, pieces() ^ org) & king_square(~sideToMove));

    case EN_PASSANT : {
        Bitboard      occupied = (pieces() ^ org ^ make_square(file_of(dst), rank_of(org))) | dst;
        std::uint16_t checkerCnt =
          popcount((pieces(sideToMove, QUEEN, BISHOP)
                    & attacks_bb<BISHOP>(king_square(~sideToMove), occupied))
                   | (pieces(sideToMove, QUEEN, ROOK)
                      & attacks_bb<ROOK>(king_square(~sideToMove), occupied)));
        return checkerCnt > 1 || (checkerCnt && (checks(PAWN) & dst));
    }
    default :  // CASTLING
        return false;
    }
}

// Makes a move, and saves all information necessary to a StateInfo.
// The move is assumed to be legal. Pseudo-legal moves should
// be filtered out before this function is called.
void Position::do_move(Move m, StateInfo& newSt, bool givesCheck) noexcept {
    assert(m.is_ok());
    assert(&newSt != st);

    Key k = st->key ^ Zobrist::side;

    // Copy some fields of the old state to our new StateInfo object except the
    // ones which are going to be recalculated from scratch anyway and then switch
    // our state pointer to point to the new (ready to be updated) state.
    std::memcpy(&newSt, st, offsetof(StateInfo, key));
    newSt.previous = st;
    st             = &newSt;

    // Used by NNUE
    st->bigAccumulator.computed[WHITE]     = st->bigAccumulator.computed[BLACK] =
      st->smallAccumulator.computed[WHITE] = st->smallAccumulator.computed[BLACK] = false;

    auto& dp    = st->dirtyPiece;
    dp.dirtyNum = 1;

    // Increment ply counters. In particular, rule50 will be reset to zero later on
    // in case of a capture or a pawn move.
    ++gamePly;
    ++st->rule50;
    ++st->nullPly;

    Square org = m.org_sq(), dst = m.dst_sq();
    // clang-format off
    Piece movedPiece    = piece_on(org);
    Piece capturedPiece = m.type_of() != EN_PASSANT ? piece_on(dst) : piece_on(dst - pawn_spush(sideToMove));
    Piece promotedPiece = NO_PIECE;
    // clang-format on
    assert(color_of(movedPiece) == sideToMove);
    assert(!is_ok(capturedPiece)
           || color_of(capturedPiece) == (m.type_of() != CASTLING ? ~sideToMove : sideToMove));
    assert(type_of(capturedPiece) != KING);

    if (m.type_of() == CASTLING)
    {
        assert(type_of(movedPiece) == KING);
        assert(capturedPiece == make_piece(sideToMove, ROOK));

        Square rorg, rdst;
        do_castling<true>(sideToMove, org, dst, rorg, rdst);
        st->kingSquare[sideToMove] = dst;
        st->hasCastled[sideToMove] = true;
        k ^= Zobrist::psq[capturedPiece][rorg] ^ Zobrist::psq[capturedPiece][rdst];
        capturedPiece = NO_PIECE;
    }
    else if (is_ok(capturedPiece))
    {
        Square cap = dst;

        // If the captured piece is a pawn, update pawn hash key, otherwise
        // update non-pawn material.
        if (type_of(capturedPiece) == PAWN)
        {
            if (m.type_of() == EN_PASSANT)
            {
                cap -= pawn_spush(sideToMove);

                assert(relative_rank(sideToMove, org) == RANK_5);
                assert(relative_rank(sideToMove, dst) == RANK_6);
                assert(type_of(movedPiece) == PAWN);
                assert(pieces(~sideToMove, PAWN) & cap);
                assert(!(pieces() & (dst | (dst + pawn_spush(sideToMove)))));
                assert(ep_square() == dst);
                assert(rule50_count() == 1);
            }

            st->pawnKey ^= Zobrist::psq[capturedPiece][cap];
        }
        else
            st->nonPawnMaterial[~sideToMove] -= PieceValue[capturedPiece];

        dp.dirtyNum = 2;  // 1 piece moved, 1 piece captured
        dp.piece[1] = capturedPiece;
        dp.org[1]   = cap;
        dp.dst[1]   = SQ_NONE;

        remove_piece(cap);
        st->capSquare = dst;
        // Update material hash key
        k ^= Zobrist::psq[capturedPiece][cap];
        st->materialKey ^= Zobrist::psq[capturedPiece][pieceCount[capturedPiece]];

        // Reset rule 50 draw counter
        st->rule50 = 0;
    }

    // Update hash key
    k ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

    // Reset en-passant square
    if (is_ok_ep(ep_square()))
    {
        k ^= Zobrist::enpassant[file_of(ep_square())];
        st->epSquare = SQ_NONE;
    }

    // Update castling rights if needed
    if (st->castlingRights && (castlingRightsMask[org] | castlingRightsMask[dst]))
    {
        k ^= Zobrist::castling[st->castlingRights];
        st->castlingRights &= ~(castlingRightsMask[org] | castlingRightsMask[dst]);
        k ^= Zobrist::castling[st->castlingRights];
    }

    // Move the piece. The tricky Chess960 castling is handled earlier
    if (m.type_of() != CASTLING)
    {
        dp.piece[0] = movedPiece;
        dp.org[0]   = org;
        dp.dst[0]   = dst;

        move_piece(org, dst);
        if (type_of(movedPiece) == KING)
            st->kingSquare[sideToMove] = dst;
    }

    // If the moving piece is a pawn do some special extra work
    if (type_of(movedPiece) == PAWN)
    {
        // Set en-passant square if the moved pawn can be captured
        if ((int(dst) ^ int(org)) == NORTH_2
            && can_enpassant(~sideToMove, dst - pawn_spush(sideToMove)))
        {
            assert(relative_rank(sideToMove, org) == RANK_2);
            assert(relative_rank(sideToMove, dst) == RANK_4);

            st->epSquare = dst - pawn_spush(sideToMove);
            k ^= Zobrist::enpassant[file_of(ep_square())];
        }
        else if (m.type_of() == PROMOTION)
        {
            assert(relative_rank(sideToMove, org) == RANK_7);
            assert(relative_rank(sideToMove, dst) == RANK_8);
            assert(KNIGHT <= m.promotion_type() && m.promotion_type() <= QUEEN);

            promotedPiece = make_piece(sideToMove, m.promotion_type());

            // Promoting pawn to SQ_NONE, promoted piece from SQ_NONE
            dp.dst[0]             = SQ_NONE;
            dp.piece[dp.dirtyNum] = promotedPiece;
            dp.org[dp.dirtyNum]   = SQ_NONE;
            dp.dst[dp.dirtyNum]   = dst;
            dp.dirtyNum++;

            remove_piece(dst);
            put_piece(promotedPiece, dst);

            // Update hash keys
            k ^= Zobrist::psq[movedPiece][dst] ^ Zobrist::psq[promotedPiece][dst];
            st->pawnKey ^= Zobrist::psq[movedPiece][dst];
            assert(pieceCount[promotedPiece] != 0);
            st->materialKey ^= Zobrist::psq[movedPiece][pieceCount[movedPiece]]
                             ^ Zobrist::psq[promotedPiece][pieceCount[promotedPiece] - 1];
            // Update material
            st->nonPawnMaterial[sideToMove] += PieceValue[promotedPiece];
        }

        // Update pawn hash key
        st->pawnKey ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

        // Reset rule 50 draw counter
        st->rule50 = 0;
    }

    // Set capture piece
    st->capturedPiece = capturedPiece;
    //st->promotedPiece = promotedPiece;

    // Update the key with the final value
    st->key = k;

    // Calculate checkers bitboard (if move gives check)
    st->checkers = givesCheck ? attackers_to(king_square(~sideToMove)) & pieces(sideToMove) : 0;
    assert(!givesCheck || (checkers() && popcount(checkers()) <= 2));

    sideToMove = ~sideToMove;

    // Update king attacks used for fast check detection
    set_ext_state();

    // Calculate the repetition info. It is the ply distance from the previous
    // occurrence of the same position, negative in the 3-fold case, or zero
    // if the position was not repeated.
    st->repetition   = 0;
    std::uint8_t end = std::min(rule50_count(), null_ply());
    if (end >= 4)
    {
        StateInfo* stp = st->previous->previous;
        for (std::uint8_t i = 4; i <= end; i += 2)
        {
            stp = stp->previous->previous;
            if (stp->key == st->key)
            {
                st->repetition = stp->repetition ? -i : +i;
                break;
            }
        }
    }

    assert(pos_is_ok());
}

// Unmakes a move. When it returns, the position should
// be restored to exactly the same state as before the move was made.
void Position::undo_move(Move m) noexcept {
    assert(m.is_ok());

    sideToMove = ~sideToMove;

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(dst);

    assert(empty_on(org) || m.type_of() == CASTLING);
    assert(type_of(captured_piece()) != KING);

    if (m.type_of() == CASTLING)
    {
        assert(pieces(sideToMove, KING) & king_castle_sq(sideToMove, org, dst));
        assert(pieces(sideToMove, ROOK) & rook_castle_sq(sideToMove, org, dst));

        Square rorg, rdst;
        do_castling<false>(sideToMove, org, dst, rorg, rdst);
    }
    else
    {
        if (m.type_of() == PROMOTION)
        {
            assert(relative_rank(sideToMove, org) == RANK_7);
            assert(relative_rank(sideToMove, dst) == RANK_8);
            assert(KNIGHT <= m.promotion_type() && m.promotion_type() <= QUEEN);
            assert(type_of(pc) == m.promotion_type());
            //assert(promoted_piece() == pc);

            remove_piece(dst);
            pc = make_piece(sideToMove, PAWN);
            put_piece(pc, dst);
        }

        move_piece(dst, org);  // Put the piece back at the source square

        if (is_ok(captured_piece()))
        {
            Square cap = dst;

            if (m.type_of() == EN_PASSANT)
            {
                cap -= pawn_spush(sideToMove);

                assert(type_of(pc) == PAWN);
                assert(relative_rank(sideToMove, org) == RANK_5);
                assert(relative_rank(sideToMove, dst) == RANK_6);
                assert(empty_on(cap));
                assert(captured_piece() == make_piece(~sideToMove, PAWN));
                assert(rule50_count() == 0);
                assert(st->previous->epSquare == dst);
                assert(st->previous->rule50 == 0);
            }

            put_piece(captured_piece(), cap);  // Restore the captured piece
        }
    }

    --gamePly;
    // Finally point our state pointer back to the previous state
    st = st->previous;

    assert(pos_is_ok());
}

// Helper used to do/undo a castling move.
// This is a bit tricky in Chess960 where from/to squares can overlap.
template<bool Do>
void Position::do_castling(Color c, Square org, Square& dst, Square& rorg, Square& rdst) noexcept {

    rorg = dst;  // Castling is encoded as "king captures friendly rook"
    rdst = rook_castle_sq(c, org, dst);
    dst  = king_castle_sq(c, org, dst);

    Piece king = piece_on(Do ? org : dst);
    Piece rook = piece_on(Do ? rorg : rdst);

    if constexpr (Do)
    {
        auto& dp = st->dirtyPiece;

        dp.dirtyNum = 0;
        if (org != dst)
        {
            dp.piece[dp.dirtyNum] = king;
            dp.org[dp.dirtyNum]   = org;
            dp.dst[dp.dirtyNum]   = dst;
            dp.dirtyNum++;
        }
        if (rorg != rdst)
        {
            dp.piece[dp.dirtyNum] = rook;
            dp.org[dp.dirtyNum]   = rorg;
            dp.dst[dp.dirtyNum]   = rdst;
            dp.dirtyNum++;
        }
    }

    // Remove both pieces first since squares could overlap in Chess960
    if (org != dst)
        remove_piece(Do ? org : dst);
    if (rorg != rdst)
        remove_piece(Do ? rorg : rdst);
    if (org != dst)
        put_piece(king, Do ? dst : org);
    if (rorg != rdst)
        put_piece(rook, Do ? rdst : rorg);
}

// Used to do a "null move":
// it flips the side to move without executing any move on the board.
void Position::do_null_move(StateInfo& newSt, const TranspositionTable& tt) noexcept {
    assert(&newSt != st);
    assert(!checkers());

    std::memcpy(&newSt, st, offsetof(StateInfo, bigAccumulator));
    newSt.previous = st;
    st             = &newSt;

    st->bigAccumulator.computed[WHITE]     = st->bigAccumulator.computed[BLACK] =
      st->smallAccumulator.computed[WHITE] = st->smallAccumulator.computed[BLACK] = false;
    st->dirtyPiece.dirtyNum                                                       = 0;
    st->dirtyPiece.piece[0] = NO_PIECE;  // Avoid checks in UpdateAccumulator()
    st->capturedPiece       = NO_PIECE;
    //st->promotedPiece       = NO_PIECE;
    st->capSquare = SQ_NONE;

    st->key ^= Zobrist::side;
    if (is_ok_ep(ep_square()))
    {
        st->key ^= Zobrist::enpassant[file_of(ep_square())];
        st->epSquare = SQ_NONE;
    }

    ++st->rule50;
    prefetch(tt.first_entry(key()));

    st->nullPly = 0;
    //st->checkers = 0;

    sideToMove = ~sideToMove;

    set_ext_state();

    st->repetition = 0;

    assert(pos_is_ok());
}

// Used to undo a "null move"
void Position::undo_null_move() noexcept {
    assert(!checkers());

    sideToMove = ~sideToMove;
    st         = st->previous;
}

// Computes the new hash key after the given move.
// Needed for speculative prefetch.
// It doesn't recognize special moves like castling, en-passant and promotions.
Key Position::move_key(Move m) const noexcept {

    const Square org = m.org_sq(), dst = m.dst_sq();

    Piece movedPiece = piece_on(org);
    Piece capturedPiece =
      m.type_of() != EN_PASSANT ? piece_on(dst) : piece_on(dst - pawn_spush(sideToMove));
    assert(color_of(movedPiece) == sideToMove);
    assert(!is_ok(capturedPiece)
           || color_of(capturedPiece) == (m.type_of() != CASTLING ? ~sideToMove : sideToMove));
    assert(type_of(capturedPiece) != KING);

    Key k = st->key ^ Zobrist::side ^ Zobrist::psq[movedPiece][org]
          ^ Zobrist::psq[m.type_of() != PROMOTION ? movedPiece
                                                  : make_piece(sideToMove, m.promotion_type())]
                        [m.type_of() != CASTLING ? dst : king_castle_sq(sideToMove, org, dst)];

    if (st->castlingRights && (castlingRightsMask[org] | castlingRightsMask[dst]))
        k ^= Zobrist::castling[st->castlingRights]
           ^ Zobrist::castling[st->castlingRights
                               & ~(castlingRightsMask[org] | castlingRightsMask[dst])];

    if (is_ok_ep(ep_square()))
        k ^= Zobrist::enpassant[file_of(ep_square())];

    if (m.type_of() == CASTLING)
    {
        assert(type_of(movedPiece) == KING);
        assert(capturedPiece == make_piece(sideToMove, ROOK));
        // ROOK
        k ^= Zobrist::psq[capturedPiece][dst]
           ^ Zobrist::psq[capturedPiece][rook_castle_sq(sideToMove, org, dst)];
        capturedPiece = NO_PIECE;
    }
    else if (is_ok(capturedPiece))
    {
        Square cap = m.type_of() != EN_PASSANT ? dst : dst - pawn_spush(sideToMove);
        k ^= Zobrist::psq[capturedPiece][cap];
    }
    else if (type_of(movedPiece) == PAWN && (int(dst) ^ int(org)) == NORTH_2
             && can_enpassant(~sideToMove, dst - pawn_spush(sideToMove), true))
        k ^= Zobrist::enpassant[file_of(dst - pawn_spush(sideToMove))];

    return (is_ok(capturedPiece) || type_of(movedPiece) == PAWN) ? k : adjust_key(k, true);
}

// Tests if the SEE (Static Exchange Evaluation)
// value of move is greater or equal to the given threshold.
// An algorithm similar to alpha-beta pruning with a null window.
bool Position::see_ge(Move m, int threshold) const noexcept {
    assert(m.is_ok());

    // Only deal with normal and promotion moves, assume others pass a simple SEE
    // Note that for now don't count promotions as having a higher SEE
    // from the "material gain" of replacing the pawn with a promoted piece.
    if (m.type_of() != NORMAL && m.type_of() != PROMOTION)
        return threshold <= 0;

    Square org = m.org_sq(), dst = m.dst_sq();

    assert(color_of(piece_on(org)) == sideToMove);

    int swap = PieceValue[piece_on(dst)] - threshold;
    if (swap < 0)
        return false;

    swap = PieceValue[piece_on(org)] - swap;
    if (swap <= 0)
        return true;

    int res = 1;

    Color stm = sideToMove;

    bool discovery[COLOR_NB]{true, true};

    Bitboard occupied  = pieces() ^ org ^ dst;  // xoring to is important for pinned piece logic
    Bitboard qB        = pieces(QUEEN, BISHOP) & occupied;
    Bitboard qR        = pieces(QUEEN, ROOK) & occupied;
    Bitboard attackers = attackers_to(dst, occupied) & occupied;
    while (attackers)
    {
        stm = ~stm;

        Bitboard  stmAttackers, b;
        PieceType pt;
        // If stm has no more attackers then give up: stm loses
        if (!(stmAttackers = pieces(stm) & attackers))
            break;

        // Don't allow pinned pieces to attack as long as
        // there are pinners on their original square.
        if ((b = pinners(~stm) & pieces(~stm) & occupied))
        {
            while (b && stmAttackers)
                stmAttackers &= ~between_bb(king_square(stm), pop_lsb(b));

            if (!stmAttackers)
                break;
        }
        if ((blockers(stm) & org)
            && (b = pinners(stm) & pieces(~stm) & line_bb(org, king_square(stm)) & occupied)
            && ((pt = type_of(piece_on(org))) != PAWN || !aligned(org, dst, king_square(stm))))
        {
            stmAttackers &= king_square(stm);

            if (!stmAttackers  //
                && (pt == PAWN || !(attacks_bb(pt, dst, occupied) & king_square(stm))))
            {
                dst  = lsb(b);
                swap = PieceValue[piece_on(org)] - swap;
                if ((swap = PieceValue[piece_on(dst)] - swap) < res)
                    break;

                occupied ^= dst;
                attackers    = attackers_to(dst, occupied) & occupied;
                stmAttackers = pieces(stm) & attackers;
            }

            if (!stmAttackers)
                break;
        }

        res ^= 1;

        if (discovery[stm] && (b = blockers(~stm) & stmAttackers))
        {
            Square sq;
            sq = pop_lsb(b);
            pt = type_of(piece_on(sq));
            if (b && pt == KING)
            {
                sq = pop_lsb(b);
                pt = type_of(piece_on(sq));
            }

            if (!(pinners(~stm) & pieces(stm) & line_bb(sq, king_square(~stm)) & occupied))
            {
                discovery[stm] = false;

                stm = ~stm;
                res ^= 1;
                continue;  // Resume without considering discovery
            }

            if (pt == KING)
            {
                if (!(pieces(~stm) & attackers))
                    break;

                discovery[stm] = false;

                stm = ~stm;
                res ^= 1;
                continue;  // Resume without considering discovery
            }

            if ((swap = PieceValue[piece_on(sq)] - swap) < res)
                break;
            occupied ^= org = sq;

            switch (pt)
            {
            case PAWN :
            case BISHOP :
                qB &= occupied;
                attackers |= qB & attacks_bb<BISHOP>(dst, occupied);
                break;
            case ROOK :
                qR &= occupied;
                attackers |= qR & attacks_bb<ROOK>(dst, occupied);
                break;
            case QUEEN :
                assert(false);
                [[fallthrough]];
            default :;
            }
        }
        // Locate and remove the next least valuable attacker, and add to
        // the bitboard 'attackers' any X-ray attackers behind it.
        else if ((b = pieces(PAWN) & stmAttackers))
        {
            if ((swap = VALUE_PAWN - swap) < res)
                break;
            occupied ^= org = lsb(b);
            //qB &= occupied;
            attackers |= qB & attacks_bb<BISHOP>(dst, occupied);
        }
        else if ((b = pieces(KNIGHT) & stmAttackers))
        {
            if ((swap = VALUE_KNIGHT - swap) < res)
                break;
            occupied ^= org = lsb(b);
        }
        else if ((b = pieces(BISHOP) & stmAttackers))
        {
            if ((swap = VALUE_BISHOP - swap) < res)
                break;
            occupied ^= org = lsb(b);
            qB &= occupied;
            attackers |= qB & attacks_bb<BISHOP>(dst, occupied);
        }
        else if ((b = pieces(ROOK) & stmAttackers))
        {
            if ((swap = VALUE_ROOK - swap) < res)
                break;
            occupied ^= org = lsb(b);
            qR &= occupied;
            attackers |= qR & attacks_bb<ROOK>(dst, occupied);
        }
        else if ((b = pieces(QUEEN) & stmAttackers))
        {
            if ((swap = VALUE_QUEEN - swap) < res)
                break;
            occupied ^= org = lsb(b);
            qB &= occupied;
            qR &= occupied;
            attackers |= (qB & attacks_bb<BISHOP>(dst, occupied))  //
                       | (qR & attacks_bb<ROOK>(dst, occupied));
        }
        else  // KING
              // If we "capture" with the king but the opponent still has attackers,
              // reverse the result.
            return pieces(~stm) & attackers ? !bool(res) : bool(res);

        attackers &= occupied;
    }

    return bool(res);
}

// Tests whether the position is drawn by 50-move rule or by repetition.
// It does not detect stalemates.
bool Position::is_draw(std::int16_t ply) const noexcept {

    return (rule50_count() >= 2 * DrawMoveCount
            && (!checkers() || MoveList<LEGAL>(*this).size() != 0))
        // Return a draw score if a position repeats once earlier but strictly
        // after the root, or repeats twice before or at the root.
        || (st->repetition && st->repetition < ply);
}

// Tests whether there has been at least one repetition
// of positions since the last capture or pawn move.
bool Position::has_repeated() const noexcept {
    std::uint8_t end = std::min(rule50_count(), null_ply());
    if (end < 4)
        return false;

    StateInfo* stc = st;
    while (end-- >= 4)
    {
        if (stc->repetition)
            return true;

        stc = stc->previous;
    }
    return false;
}

// Tests if the position has a move which draws by repetition,
// or an earlier position has a move that directly reaches the current position.
bool Position::has_game_cycle(std::int16_t ply) const noexcept {
    std::uint8_t end = std::min(rule50_count(), null_ply());
    if (end < 3)
        return false;

    Key        key = st->key;
    StateInfo* stp = st->previous;

    for (std::uint8_t i = 3; i <= end; i += 2)
    {
        stp = stp->previous->previous;

        Key moveKey = key ^ stp->key;
        if (std::uint16_t j; (j = H1(moveKey), Cuckoos[j].key == moveKey)
                             || (j = H2(moveKey), Cuckoos[j].key == moveKey))
        {
            Move   move = Cuckoos[j].move;
            Square s1   = move.org_sq();
            Square s2   = move.dst_sq();
            // Path is clear
            if (!(pieces() & (between_bb(s1, s2) ^ s2)))
            {
                if (i < ply)
                    return true;

                // For nodes before or at the root, check that the move is a
                // repetition rather than a move to the current position.
                // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in
                // the same location, so we have to select which square to check.
                if (color_of(piece_on(empty_on(s1) ? s2 : s1)) != sideToMove)
                    continue;

                // For repetitions before or at the root, require one more
                if (stp->repetition)
                    return true;
            }
        }
    }
    return false;
}

// Flips position with the white and black sides reversed.
// This is only useful for debugging e.g. for finding evaluation symmetry bugs.
void Position::flip() noexcept {
    std::istringstream iss(fen());

    std::string f, token;
    for (Rank r = RANK_8; r >= RANK_1; --r)  // Piece placement
    {
        std::getline(iss, token, r > RANK_1 ? '/' : ' ');
        f.insert(0, token + (f.empty() ? " " : "/"));
    }

    iss >> token;                       // Active color
    f += (token == "w" ? "B " : "W ");  // Will be lowercased later
    iss >> token;                       // Castling availability
    f += token + " ";

    std::transform(f.begin(), f.end(), f.begin(), [](char c) noexcept -> char {
        return char(std::islower(c) ? std::toupper(c) : std::tolower(c));
    });

    iss >> token;  // En-passant square
    f += (token == "-" ? token : token.replace(1, 1, token[1] == '3' ? "6" : "3"));

    std::getline(iss, token);  // Half and full moves
    f += token;

    set(f, st);

    assert(pos_is_ok());
}

// Performs some consistency checks for the position object
// and raise an assert if something wrong is detected.
// This is meant to be helpful when debugging.
bool Position::pos_is_ok() const noexcept {

    constexpr bool Fast = true;  // Quick (default) or full check?

    if ((sideToMove != WHITE && sideToMove != BLACK)           //
        || pieceCount[W_KING] != 1 || pieceCount[B_KING] != 1  //
        || piece_on(king_square(WHITE)) != W_KING              //
        || piece_on(king_square(BLACK)) != B_KING              //
        || distance(king_square(WHITE), king_square(BLACK)) <= 1
        || (is_ok_ep(ep_square())  //
            && relative_rank(sideToMove, ep_square()) != RANK_6
            && !can_enpassant(sideToMove, ep_square())))
        assert(false && "Position::pos_is_ok(): Default");

    if (Fast)
        return true;

    if (attackers_to(king_square(~sideToMove)) & pieces(sideToMove))
        assert(false && "Position::pos_is_ok(): King Checker");

    if ((pieces(PAWN) & PromotionRankBB) || pieceCount[W_PAWN] > 8 || pieceCount[B_PAWN] > 8)
        assert(false && "Position::pos_is_ok(): Pawns");

    for (Color c : {WHITE, BLACK})
        if (count<PAWN>(c)                         //
              + std::max(count<KNIGHT>(c) - 2, 0)  //
              + std::max(popcount(pieces(c, BISHOP) & ColorBB[WHITE]) - 1, 0)
              + std::max(popcount(pieces(c, BISHOP) & ColorBB[BLACK]) - 1, 0)
              + std::max(count<ROOK>(c) - 2, 0)  //
              + std::max(count<QUEEN>(c) - 1, 0)
            > 8)
            assert(false && "Position::pos_is_ok(): Piece Count");

    if ((pieces(WHITE) & pieces(BLACK)) || (pieces(WHITE) | pieces(BLACK)) != pieces()
        || popcount(pieces(WHITE)) > 16 || popcount(pieces(BLACK)) > 16)
        assert(false && "Position::pos_is_ok(): Bitboards");

    for (PieceType p1 = PAWN; p1 <= KING; ++p1)
        for (PieceType p2 = PAWN; p2 <= KING; ++p2)
            if (p1 != p2 && (pieces(p1) & pieces(p2)))
                assert(false && "Position::pos_is_ok(): Bitboards");

    for (Piece pc : Pieces)
        if (pieceCount[pc] != popcount(pieces(color_of(pc), type_of(pc)))
            || pieceCount[pc] != std::count(std::begin(board), std::end(board), pc))
            assert(false && "Position::pos_is_ok(): Pieces");

    for (Color c : {WHITE, BLACK})
        for (CastlingRights cr : {c & KING_SIDE, c & QUEEN_SIDE})
        {
            if (!can_castle(cr))
                continue;

            if (!is_ok(castling_rook_square(cr))  //
                || !(pieces(c, ROOK) & castling_rook_square(cr))
                || castlingRightsMask[castling_rook_square(cr)] != cr
                || (castlingRightsMask[king_square(c)] & cr) != cr)
                assert(false && "Position::pos_is_ok(): Castling");
        }

    return true;
}

// Returns an ASCII representation of the position
std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept {
    os << "\n +---+---+---+---+---+---+---+---+\n";
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        for (File f = FILE_A; f <= FILE_H; ++f)
            os << " | " << UCI::piece(pos.piece_on(make_square(f, r)));

        os << " | " << UCI::rank(r) << "\n +---+---+---+---+---+---+---+---+\n";
    }
    for (File f = FILE_A; f <= FILE_H; ++f)
        os << "   " << UCI::file(f);
    os << '\n';

    os << "\nFen: " << pos.fen() << "\nKey: " << std::setw(16) << std::hex << std::uppercase
       << std::setfill('0') << pos.key() << std::setfill(' ') << std::nouppercase << std::dec
       << "\nCheckers: ";
    Bitboard b = pos.checkers();
    if (b)
        while (b)
            os << UCI::square(pop_lsb(b)) << " ";
    else
        os << "(none)";

    if (Tablebases::MaxCardinality >= pos.count<ALL_PIECE>() && !pos.can_castle(ANY_CASTLING))
    {
        StateInfo st;
        ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

        Position p;
        p.set(pos.fen(), &st);

        Tablebases::ProbeState ps1, ps2;

        auto wdl = Tablebases::probe_wdl(p, &ps1);
        auto dtz = Tablebases::probe_dtz(p, &ps2);
        os << "\nTablebases WDL: " << std::setw(4) << wdl << " (" << ps1 << ")"
           << "\nTablebases DTZ: " << std::setw(4) << dtz << " (" << ps2 << ")";
    }

    return os;
}

}  // namespace DON
