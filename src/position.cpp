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

#include <array>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "misc.h"
#include "movegen.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace DON {

namespace Zobrist {

// clang-format off
std::array2d<Key, PIECE_NB, SQUARE_NB> psq;
std::array  <Key, CASTLING_RIGHTS_NB>  castling;
std::array  <Key, FILE_NB>             enpassant;
             Key                       side;
// clang-format on
}  // namespace Zobrist

namespace {
// clang-format off
constexpr inline std::array<Piece, COLOR_NB * KING> Pieces{
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING};
// clang-format on
constexpr inline std::array2d<std::int8_t, PIECE_TYPE_NB - 1, 28> MobilityBonus{
  {{0},
   {-2, 0, 2, 4, 6, 8, 10, 12, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 27},
   {-75, -61, -14, -2, 9, 20, 31, 40, 47},                             // 8
   {-50, -24, 15, 30, 42, 55, 58, 63, 67, 73, 83, 88, 97, 102},        // 14
   {-68, -25, 2, 23, 38, 57, 61, 76, 87, 90, 97, 102, 111, 114, 117},  // 15
   {-40, -23, -9, 3,  19, 26, 32,  39,  45,  57,  69,  72,  73,  74,
    76,  77,  78, 80, 83, 86, 100, 109, 113, 116, 120, 123, 125, 127},  // 28
   {-8, -4, 0, 4, 7, 9, 11, 13, 15}}};

// Implements Marcel van Kervinck's cuckoo algorithm to detect repetition of positions
// for 3-fold repetition draws. The algorithm uses hash tables with Zobrist hashes
// to allow fast detection of recurring positions. For details see:
// http://web.archive.org/web/20201107002606/https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// Cuckoo table with Zobrist hashes of valid reversible moves, and the moves themselves
struct Cuckoo final {

    constexpr bool empty() const noexcept { return key == 0 /*&& move == Move::None()*/; }

    Key  key  = 0;
    Move move = Move::None();
};

template<Key16 Size>
class CuckooTable final {
   public:
    CuckooTable() noexcept                              = default;
    CuckooTable(const CuckooTable&) noexcept            = delete;
    CuckooTable(CuckooTable&&) noexcept                 = delete;
    CuckooTable& operator=(const CuckooTable&) noexcept = delete;
    CuckooTable& operator=(CuckooTable&&) noexcept      = delete;

    constexpr void init() noexcept {
        fill({0, Move::None()});
        count = 0;
    }

    constexpr void fill(Cuckoo&& cuckoo) noexcept { cuckoos.fill(std::move(cuckoo)); }

    constexpr auto size() const noexcept { return cuckoos.size(); }

    auto& operator[](std::size_t idx) const noexcept { return cuckoos[idx]; }
    auto& operator[](std::size_t idx) noexcept { return cuckoos[idx]; }

    // Hash functions for indexing the cuckoo table
    constexpr Key16 H1(Key key) const noexcept { return Key16(key >> 00) & (size() - 1); }
    constexpr Key16 H2(Key key) const noexcept { return Key16(key >> 16) & (size() - 1); }

    void insert(Cuckoo& cuckoo) noexcept {
        Key16 index = H1(cuckoo.key);
        while (true)
        {
            std::swap(cuckoos[index], cuckoo);
            if (cuckoo.empty())  // Arrived at empty slot?
                break;
            index ^= H1(cuckoo.key) ^ H2(cuckoo.key);  // Push victim to alternative slot
        }
        ++count;
    }

    Key16 find_key(Key key) const noexcept {
        Key16 index;
        if (index = H1(key); cuckoos[index].key == key)
            return index;
        if (index = H2(key); cuckoos[index].key == key)
            return index;
        return size();
    }

   private:
    std::array<Cuckoo, Size> cuckoos;

   public:
    std::uint16_t count = 0;
};

CuckooTable<0x2000> Cuckoos;

}  // namespace

bool         Position::Chess960      = false;
std::uint8_t Position::DrawMoveCount = 50;

// Called at startup to initialize the Zobrist arrays used to compute hash keys
void Position::init() noexcept {
    PRNG1024 rng(0x105524ull);

    // for (std::uint8_t pc : {0, 7, 8, 15})
    //     for (std::uint8_t s = 0; s < Zobrist::psq[pc].size(); ++s)
    //         Zobrist::psq[pc][s] = 0;
    for (Piece pc : Pieces)
        for (std::uint8_t s = 0; s < Zobrist::psq[pc].size(); ++s)
            Zobrist::psq[pc][s] = rng.rand<Key>();

    for (std::uint8_t cr = 0; cr < Zobrist::castling.size(); ++cr)
    {
        Zobrist::castling[cr] = 0;

        std::uint8_t mask = 1;
        while (mask <= cr)
        {
            if (std::uint8_t index = cr & mask; index)
            {
                if (Key k = Zobrist::castling[index]; k)
                    Zobrist::castling[cr] ^= k;
                else
                {
                    Zobrist::castling[index] = rng.rand<Key>();
                    break;
                }
            }
            mask <<= 1;
        }
    }

    for (std::uint8_t f = 0; f < Zobrist::enpassant.size(); ++f)
        Zobrist::enpassant[f] = rng.rand<Key>();

    Zobrist::side = rng.rand<Key>();

    // Prepare the cuckoo tables
    Cuckoos.init();
    for (Piece pc : Pieces)
    {
        if (type_of(pc) == PAWN)
            continue;
        for (Square s1 = SQ_A1; s1 < SQ_H8; ++s1)
            for (Square s2 = s1 + 1; s2 <= SQ_H8; ++s2)
                if (attacks_bb(type_of(pc), s1) & s2)
                {
                    Key    key  = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side;
                    Move   move = Move(s1, s2);
                    Cuckoo cuckoo{key, move};
                    Cuckoos.insert(cuckoo);
                }
    }
    assert(Cuckoos.count == 3668);
}

// Initializes the position object with the given FEN string.
// This function is not very robust - make sure that input FENs are correct,
// this is assumed to be the responsibility of the GUI.
void Position::set(std::string_view fenStr, State* newSt) noexcept {
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
    assert(newSt != nullptr);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
    std::memset(this, 0, sizeof(Position));
#pragma GCC diagnostic pop
    std::fill(std::begin(castlingRookSquare), std::end(castlingRookSquare), SQ_NONE);
    std::memset(newSt, 0, sizeof(State));
    newSt->epSquare = newSt->capSquare = SQ_NONE;
    newSt->kingSquare[WHITE] = newSt->kingSquare[BLACK] = SQ_NONE;

    st = newSt;

    std::istringstream iss(fenStr.data());
    iss >> std::noskipws;

    std::uint8_t token;

    File file = FILE_A;
    Rank rank = RANK_8;
    // 1. Piece placement
    while ((iss >> token) && !std::isspace(token))
    {
        if (token == '/')
        {
            assert(file <= FILE_NB);
            assert(rank > RANK_1);
            file = FILE_A;
            --rank;
            continue;
        }
        if (std::isdigit(token))
        {
            int f = token - '0';
            assert(1 <= f && f <= 8 - file && "Position::set(): Invalid Files");
            file += f;  // Advance the given number of files
            continue;
        }
        if (Piece pc; (pc = UCI::piece(token)) != NO_PIECE)
        {
            assert(file < FILE_NB);
            Square sq = make_square(file, rank);
            put_piece(sq, pc);
            if (type_of(pc) == KING)
                st->kingSquare[color_of(pc)] = sq;
            ++file;
            continue;
        }
        assert(false && "Position::set(): Invalid Piece");
    }
    assert(file <= FILE_NB && rank == RANK_1);
    assert(count(W_ALL) <= 16 && count(B_ALL) <= 16);
    assert(count(W_ALL) + count(B_ALL) == count<ALL_PIECE>());
    assert(count(W_PAWN) <= 8 && count(B_PAWN) <= 8);
    assert(count(W_KING) == 1 && count(B_KING) == 1);
    assert(king_square(WHITE) != SQ_NONE && king_square(BLACK) != SQ_NONE);
    assert(!(pieces(PAWN) & PROMOTION_RANK_BB));
    assert(distance(king_square(WHITE), king_square(BLACK)) > 1);

    iss >> std::ws;

    // 2. Active color
    iss >> token;
    token = std::tolower(token);
    assert((token == 'w' || token == 'b') && "Position::set(): Invalid Color");
    activeColor = token == 'w' ? WHITE : token == 'b' ? BLACK : COLOR_NB;

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
        ++castlingRightsCount;
        assert(castlingRightsCount <= 4 && "Position::set(): Number of Castling Rights");

        Color c = std::isupper(token) ? WHITE : BLACK;

        if (relative_rank(c, king_square(c)) != RANK_1)
        {
            assert(false && "Position::set(): Missing King on RANK_1");
            continue;
        }

        Bitboard rooks = pieces(c, ROOK);

        if (!(rooks & relative_rank(c, RANK_1)))
        {
            assert(false && "Position::set(): Missing Rook on RANK_1");
            continue;
        }

        token = std::tolower(token);

        Square rsq = SQ_NONE;
        if (token == 'k')
        {
            rsq = relative_square(c, SQ_H1);
            while (file_of(rsq) >= FILE_C && !(rooks & rsq) && rsq != king_square(c))
                --rsq;
            goto CASTLING_RIGHTS_SET;
        }
        if (token == 'q')
        {
            rsq = relative_square(c, SQ_A1);
            while (file_of(rsq) <= FILE_F && !(rooks & rsq) && rsq != king_square(c))
                ++rsq;
            goto CASTLING_RIGHTS_SET;
        }
        if ('a' <= token && token <= 'h')
        {
            rsq = make_square(File(token - 'a'), relative_rank(c, RANK_1));
            goto CASTLING_RIGHTS_SET;
        }

        assert(false && "Position::set(): Invalid Castling Rights");
        continue;

CASTLING_RIGHTS_SET:
        if (!(rooks & rsq))
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
    //    st->castled[WHITE] = true;
    // if (!can_castle(BLACK_CASTLING)
    //    && ((king_square(BLACK) == SQ_C8
    //         && !(pieces(BLACK, ROOK) & SQ_A8))
    //        || (king_square(BLACK) == SQ_G8
    //            && !(pieces(BLACK, ROOK) & SQ_H8))))
    //    st->castled[BLACK] = true;

    iss >> std::ws;

    Color ac = active_color();

    // 4. En-passant square.
    // Ignore if square is invalid or not on side to move relative rank 6.
    bool enpassant = false;

    std::uint8_t epFile;
    iss >> epFile;

    if (epFile != '-')
    {
        epFile = std::tolower(epFile);
        std::uint8_t epRank;
        iss >> epRank;

        if ('a' <= epFile && epFile <= 'h' && epRank == (ac == WHITE ? '6' : '3'))
        {
            st->epSquare = make_square(File(epFile - 'a'), Rank(epRank - '1'));

            // En-passant square will be considered only if
            // a) there is an enemy pawn in front of epSquare
            // b) there is no piece on epSquare or behind epSquare
            // c) the side must have atleast one pawn threatening epSquare
            // d) there is no enemy Bishop, Rook or Queen pinning
            enpassant = (pieces(~ac, PAWN) & (ep_square() - pawn_spush(ac)))
                     && !(pieces() & (ep_square() | (ep_square() + pawn_spush(ac))))
                     && (pieces(ac, PAWN) & pawn_attacks_bb(~ac, ep_square()))
                     && can_enpassant(ac, ep_square());
        }
        else
        {
            assert(false && "Position::set(): Invalid En-passant square");
        }
    }

    // 5-6. Halfmove clock and fullmove number
    std::int16_t rule50  = 0;
    std::int16_t moveNum = 1;
    iss >> std::skipws >> rule50 >> moveNum;

    st->rule50 = std::max<std::int16_t>(rule50, 0);
    // Convert from moveNum starting from 1 to posPly starting from 0,
    // handle also common incorrect FEN with moveNum = 0.
    posPly = std::max(2 * (moveNum - 1), 0) + (ac == BLACK);

    // Reset illegal values
    if (ep_is_ok(ep_square()))
    {
        reset_rule50_count();
        if (!enpassant)
            reset_ep_square();
    }
    assert(rule50_count() <= 100);
    posPly = std::max<std::int16_t>(ply(), rule50_count());

    set_state();

    assert(pos_is_ok());
}

// Overload to initialize the position object with the given endgame code string like "KBPKN".
// It's mainly a helper to get the material key out of an endgame code.
void Position::set(std::string_view code, Color c, State* newSt) noexcept {
    assert(!code.empty() && code[0] == 'K');

    std::string sides[COLOR_NB]{
      std::string(code.substr(code.find('K', 1))),                                // Weak
      std::string(code.substr(0, std::min(code.find('v'), code.find('K', 1))))};  // Strong

    assert(0 < sides[WHITE].length() && sides[WHITE].length() < 8);
    assert(0 < sides[BLACK].length() && sides[BLACK].length() < 8);

    sides[c] = lower_case(sides[c]);

    std::string fenStr = "8/" + sides[WHITE] + char('0' + 8 - sides[WHITE].length()) + "/8/8/8/8/"
                       + sides[BLACK] + char('0' + 8 - sides[BLACK].length()) + "/8 w - - 0 1";

    set(fenStr, newSt);
}

void Position::set(const Position& pos, State* newSt) noexcept {
    assert(newSt != nullptr);

    *this = pos;

    st = newSt;
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
            int emptyCount = 0;
            while (f <= FILE_H && empty_on(make_square(f, r)))
            {
                ++emptyCount;
                ++f;
            }
            if (emptyCount)
                oss << emptyCount;

            if (f <= FILE_H)
                oss << UCI::piece(piece_on(make_square(f, r)));
        }

        if (r > RANK_1)
            oss << '/';
    }

    oss << (active_color() == WHITE ? " w " : active_color() == BLACK ? " b " : " - ");

    if (can_castle(ANY_CASTLING))
    {
        if (can_castle(WHITE_OO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(WHITE_OO)), true) : 'K');
        if (can_castle(WHITE_OOO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(WHITE_OOO)), true) : 'Q');
        if (can_castle(BLACK_OO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(BLACK_OO)), false) : 'k');
        if (can_castle(BLACK_OOO))
            oss << (Chess960 ? UCI::file(file_of(castling_rook_square(BLACK_OOO)), false) : 'q');
    }
    else
        oss << '-';

    oss << ' ' << (ep_is_ok(ep_square()) ? UCI::square(ep_square()) : "-");
    if (full)
        oss << ' ' << rule50_count() << ' ' << move_num();

    return oss.str();
}

// Sets castling rights given the corresponding color and the rook starting square.
void Position::set_castling_rights(Color c, Square rorg) noexcept {
    assert(relative_rank(c, rorg) == RANK_1 && (pieces(c, ROOK) & rorg)
           && !castlingRightsMask[c * FILE_NB + file_of(rorg)]);
    Square korg = king_square(c);
    assert(relative_rank(c, korg) == RANK_1 && (pieces(c, KING) & korg));

    int cr    = make_castling_rights(c, korg, rorg);
    int crLsb = lsb(cr);
    assert(0 <= crLsb && crLsb < 4);
    assert(castlingRookSquare[crLsb] == SQ_NONE);

    st->castlingRights |= cr;
    castlingRightsMask[c * FILE_NB + file_of(korg)] |= cr;
    castlingRightsMask[c * FILE_NB + file_of(rorg)] = cr;

    castlingRookSquare[crLsb] = rorg;

    Square kdst = king_castle_sq(c, korg, rorg);
    Square rdst = rook_castle_sq(c, korg, rorg);

    castlingPath[crLsb] = (between_bb(korg, kdst) | between_bb(rorg, rdst)) & ~(korg | rorg);
}

// Computes the hash keys of the position, and other data
// that once computed is updated incrementally as moves are made.
// The function is only used when a new position is set up.
void Position::set_state() noexcept {
    assert(st->pawnKey[WHITE] == 0 && st->pawnKey[BLACK] == 0);
    assert(st->nonPawnKey[WHITE] == 0 && st->nonPawnKey[BLACK] == 0);
    assert(st->groupKey[0] == 0 && st->groupKey[1] == 0);
    assert(st->key == 0);

    Color ac = active_color();

    Bitboard occupied = pieces();
    while (occupied)
    {
        Square s  = pop_lsb(occupied);
        Piece  pc = piece_on(s);
        auto   pt = type_of(pc);
        assert(is_ok(pc));

        st->key ^= Zobrist::psq[pc][s];

        if (pt == PAWN)
        {
            st->pawnKey[color_of(pc)] ^= Zobrist::psq[pc][s];
            continue;
        }

        st->nonPawnKey[color_of(pc)] ^= Zobrist::psq[pc][s];

        if (pt == KING)
        {
            st->groupKey[0] ^= Zobrist::psq[pc][s];
            st->groupKey[1] ^= Zobrist::psq[pc][s];
        }
        else
        {
            st->groupKey[is_major(pt)] ^= Zobrist::psq[pc][s];
        }
    }

    st->key ^= Zobrist::castling[castling_rights()];

    if (ep_is_ok(ep_square()))
        st->key ^= Zobrist::enpassant[file_of(ep_square())];

    if (ac == BLACK)
        st->key ^= Zobrist::side;

    st->checkers = pieces(~ac) & attackers_to(king_square(ac));

    set_ext_state();
}

// Set extra state to detect if a move is check
void Position::set_ext_state() noexcept {

    Color ac = active_color();

    Bitboard occupied = pieces();

    Square ksq = king_square(~ac);
    // clang-format off
    st->checks[PAWN - 1]   = pawn_attacks_bb(~ac, ksq);
    st->checks[KNIGHT - 1] = attacks_bb<KNIGHT>(ksq);
    st->checks[BISHOP - 1] = attacks_bb<BISHOP>(ksq, occupied);
    st->checks[ROOK - 1]   = attacks_bb<ROOK>  (ksq, occupied);
    st->checks[QUEEN - 1]  = st->checks[BISHOP - 1] | st->checks[ROOK - 1];
    st->checks[KING - 1]   = 0;
    // clang-format on

    st->pinners[WHITE] = st->pinners[BLACK] = 0;
    for (Color c : {WHITE, BLACK})
    {
        // Calculates st->blockers[c] and st->pinners[],
        // which store respectively the pieces preventing king of color c from being in check
        // and the slider pieces of color ~c pinning pieces of color c to the king.
        st->blockers[c] = 0;

        if (!pieces(~c, QUEEN, ROOK, BISHOP))
            continue;

        ksq = king_square(c);

        // Snipers are xsliders that attack 'ksq' when other snipers are removed
        Bitboard xsnipers    = pieces(~c) & xslide_attackers_to(ksq);
        Bitboard fixOccupied = occupied ^ xsnipers;
        Bitboard friends     = pieces(c);

        while (xsnipers)
        {
            Square xsniper = pop_lsb(xsnipers);

            Bitboard blocker = between_bb(ksq, xsniper) & fixOccupied;
            if (exactly_one(blocker))
            {
                st->blockers[c] |= blocker;
                st->pinners[(blocker & friends) ? ~c : c] |= xsniper;
            }
        }
        // clang-format off
        st->mobility[c] = MobilityBonus[PAWN][popcount(pawn_push_bb(c, pieces(c, PAWN) & ~(st->blockers[c] & pieces(PAWN) & attacks_bb<QUEEN>(ksq, occupied))) & ~occupied)];
        st->attacks[c][PAWN] = attacks_by<PAWN>(c, pieces(~c), occupied);
        // clang-format on
    }

    for (Color c : {WHITE, BLACK})
    {
        // clang-format off
        Bitboard target = ~(st->attacks[~c][PAWN]
                          | (pieces(~c) & pinners())
                          | (pieces( c) & (st->blockers[c]
                                         | pieces(QUEEN, KING)
                                         | (pieces(PAWN) & (LOW_RANK_BB[c] | (pawn_push_bb(~c, occupied) & ~pawn_attacks_bb(~c, pieces(~c) & ~pieces(KING))))))));
        //st->attacks[c][0]    = 0;
        st->attacks[c][KNIGHT] = st->attacks[c][PAWN]   | attacks_by<KNIGHT>(c, target);
        st->attacks[c][BISHOP] = st->attacks[c][KNIGHT] | attacks_by<BISHOP>(c, target, occupied ^ ((pieces(c, QUEEN, BISHOP) & ~st->blockers[c]) | (pieces(~c, KING, QUEEN, ROOK) & ~pinners())));
        st->attacks[c][ROOK]   = st->attacks[c][BISHOP] | attacks_by<ROOK>  (c, target, occupied ^ ((pieces(c, QUEEN, ROOK)   & ~st->blockers[c]) | (pieces(~c, KING, QUEEN)       & ~pinners())));
        st->attacks[c][QUEEN]  = st->attacks[c][ROOK]   | attacks_by<QUEEN> (c, target, occupied ^ ((pieces(c, QUEEN)         & ~st->blockers[c]) | (pieces(~c, KING)                        )));
        st->attacks[c][KING]   = st->attacks[c][QUEEN]  | attacks_by<KING>  (c, target);

        st->attacks[~c][EX_PIECE] = (pieces(~c, KNIGHT, BISHOP) & st->attacks[c][PAWN])
                                  | (pieces(~c, ROOK)           & st->attacks[c][MINOR])
                                  | (pieces(~c, QUEEN)          & st->attacks[c][ROOK]);
        // clang-format on
    }
}

// Check can do en-passant
bool Position::can_enpassant(Color ac, Square epSq, bool before) const noexcept {
    assert(ep_is_ok(epSq));

    // En-passant attackers
    Bitboard attackers = pieces(ac, PAWN) & pawn_attacks_bb(~ac, epSq);
    if (!attackers)
        return false;

    Square cap = before ? epSq + pawn_spush(ac) : epSq - pawn_spush(ac);
    assert(pieces(~ac, PAWN) & cap);

    Square ksq = king_square(ac);

    Bitboard qB = pieces(~ac, QUEEN, BISHOP) & attacks_bb<BISHOP>(ksq);
    Bitboard qR = pieces(~ac, QUEEN, ROOK) & attacks_bb<ROOK>(ksq);
    if (!(qB | qR))
        return true;

    const Magic(*magic)[2] = &Magics[ksq];
    // Check en-passant is legal for the position
    Bitboard occupied = (pieces() ^ cap) | epSq;
    while (attackers)
        if (Bitboard occ = occupied ^ pop_lsb(attackers);
            !(qB & attacks_bb<BISHOP>(magic, occ)) && !(qR & attacks_bb<ROOK>(magic, occ)))
            return true;
    return false;
}

template<PieceType PT>
Bitboard Position::attacks_by(Color c, Bitboard target, Bitboard occupied) noexcept {

    Square ksq = king_square(c);

    Bitboard attacks;
    if constexpr (PT == PAWN)
    {
        attacks = pawn_attacks_bb(
          c, pieces(c, PAWN) & ~(st->blockers[c] & pieces(PAWN) & attacks_bb<ROOK>(ksq, occupied)));
        st->mobility[c] += MobilityBonus[PAWN][popcount(attacks & target)];
    }
    else
    {
        attacks = 0;

        Bitboard pc = pieces<PT>(c, ksq);
        while (pc)
        {
            Square   org  = pop_lsb(pc);
            Bitboard atks = attacks_bb<PT>(org, occupied);
            if (PT != KNIGHT && (blockers(c) & org))
                atks &= line_bb(ksq, org);
            st->mobility[c] += MobilityBonus[PT][popcount(atks & target)];
            attacks |= atks;
        }
    }
    return attacks;
}

// Helper used to do/undo a castling move.
// This is a bit tricky in Chess960 where from/to squares can overlap.
template<bool Do>
void Position::do_castling(Color ac, Square org, Square& dst, Square& rorg, Square& rdst) noexcept {

    rorg = dst;  // Castling is encoded as "king captures rook"
    rdst = rook_castle_sq(ac, org, dst);
    dst  = king_castle_sq(ac, org, dst);

    Piece king = make_piece(ac, KING);  //piece_on(Do ? org : dst);
    Piece rook = make_piece(ac, ROOK);  //piece_on(Do ? rorg : rdst);

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
        put_piece(Do ? dst : org, king);
    if (rorg != rdst)
        put_piece(Do ? rdst : rorg, rook);
}

// Makes a move, and saves all necessary information to new state.
// The move is assumed to be legal.
void Position::do_move(Move m, State& newSt, bool check) noexcept {
    assert(m.is_ok() && pseudo_legal(m) && legal(m));
    assert(&newSt != st);

    Key k = st->key ^ Zobrist::side;

    // Copy relevant fields from the old state to the new state,
    // excluding those that will recomputed from scratch anyway and
    // then switch the state pointer to point to the new state.
    std::memcpy(&newSt, st, offsetof(State, key));
    newSt.preState = st;

    st->nxtState = &newSt;
    st           = &newSt;

    // Used by NNUE
    st->bigAccumulator.computed[WHITE]     = st->bigAccumulator.computed[BLACK] =
      st->smallAccumulator.computed[WHITE] = st->smallAccumulator.computed[BLACK] = false;

    auto& dp = st->dirtyPiece;

    dp.dirtyNum = 1;

    // Increment ply counters. In particular, rule50 will be reset to zero later on
    // in case of a capture or a pawn move.
    ++posPly;
    ++st->rule50;
    ++st->nullPly;

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    // clang-format off
    Piece movedPiece    = piece_on(org);
    Piece capturedPiece = m.type_of() != EN_PASSANT ? piece_on(dst) : piece_on(dst - pawn_spush(ac));
    Piece promotedPiece = NO_PIECE;
    assert(color_of(movedPiece) == ac);
    assert(capturedPiece == NO_PIECE || (color_of(capturedPiece) == (m.type_of() != CASTLING ? ~ac : ac) && type_of(capturedPiece) != KING));
    auto pt = type_of(movedPiece);
    // clang-format on

    // Reset en-passant square
    if (ep_is_ok(ep_square()))
    {
        k ^= Zobrist::enpassant[file_of(ep_square())];
        reset_ep_square();
    }

    if (m.type_of() == CASTLING)
    {
        assert(pt == KING);
        assert(capturedPiece == make_piece(ac, ROOK));
        assert(!st->castled[ac]);

        Square rorg, rdst;
        do_castling<true>(ac, org, dst, rorg, rdst);
        assert(rorg == m.dst_sq());

        st->kingSquare[ac] = dst;
        st->castled[ac]    = true;
        // Update castling rights
        std::uint8_t cr = ac & ANY_CASTLING;
        assert(castling_rights() & cr);
        k ^= Zobrist::castling[castling_rights() & cr];
        st->castlingRights &= ~cr;

        // clang-format off
        k                  ^= Zobrist::psq[capturedPiece][rorg] ^ Zobrist::psq[capturedPiece][rdst];
        st->nonPawnKey[ac] ^= Zobrist::psq[capturedPiece][rorg] ^ Zobrist::psq[capturedPiece][rdst];
        st->groupKey[1]    ^= Zobrist::psq[capturedPiece][rorg] ^ Zobrist::psq[capturedPiece][rdst];
        capturedPiece = NO_PIECE;

        st->nonPawnKey[ac] ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
        st->groupKey[0]    ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
        st->groupKey[1]    ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
        // clang-format on

        // Calculate checker only one ROOK possible (if move is check)
        st->checkers = check ? attacks_bb<ROOK>(king_square(~ac), pieces()) & rdst : 0;
        assert(!check || (checkers() & rdst));

        goto DO_MOVE_END;
    }

    if (capturedPiece != NO_PIECE)
    {
        auto captured = type_of(capturedPiece);

        Square cap = dst;
        // If the captured piece is a pawn, update pawn hash key,
        // otherwise update non-pawn material.
        if (captured == PAWN)
        {
            if (m.type_of() == EN_PASSANT)
            {
                cap -= pawn_spush(ac);

                assert(relative_rank(ac, org) == RANK_5);
                assert(relative_rank(ac, dst) == RANK_6);
                assert(pt == PAWN);
                assert(pieces(~ac, PAWN) & cap);
                assert(!(pieces() & (dst | (dst + pawn_spush(ac)))));
                assert(ep_square() == SQ_NONE);  // Already reset to SQ_NONE
                assert(rule50_count() == 1);
                assert(st->preState->epSquare == dst);
                assert(st->preState->rule50 == 0);
            }

            st->pawnKey[~ac] ^= Zobrist::psq[capturedPiece][cap];
        }
        else
        {
            // clang-format off
            st->nonPawnKey[~ac]              ^= Zobrist::psq[capturedPiece][cap];
            st->groupKey[is_major(captured)] ^= Zobrist::psq[capturedPiece][cap];
            // clang-format on
        }
        dp.dirtyNum = 2;  // 1 piece moved, 1 piece captured
        dp.piece[1] = capturedPiece;
        dp.org[1]   = cap;
        dp.dst[1]   = SQ_NONE;
        // Remove the captured piece
        remove_piece(cap);
        st->capSquare = dst;
        // Update hash key
        k ^= Zobrist::psq[capturedPiece][cap];
        // Reset rule 50 draw counter
        reset_rule50_count();
    }

    // Move the piece. The tricky Chess960 castling is handled earlier
    dp.piece[0] = movedPiece;
    dp.org[0]   = org;
    dp.dst[0]   = dst;

    move_piece(org, dst);

    // Update castling rights if needed
    if (std::uint8_t cr; castling_rights() && (cr = castling_rights_mask(org, dst)))
    {
        k ^= Zobrist::castling[castling_rights() & cr];
        st->castlingRights &= ~cr;
    }

    // If the moving piece is a pawn do some special extra work
    if (pt == PAWN)
    {
        if (m.type_of() == PROMOTION)
        {
            assert(relative_rank(ac, org) == RANK_7);
            assert(relative_rank(ac, dst) == RANK_8);

            auto promoted = m.promotion_type();
            assert(KNIGHT <= promoted && promoted <= QUEEN);

            promotedPiece = make_piece(ac, promoted);

            // Promoting pawn to SQ_NONE, promoted piece from SQ_NONE
            dp.dst[0]             = SQ_NONE;
            dp.piece[dp.dirtyNum] = promotedPiece;
            dp.org[dp.dirtyNum]   = SQ_NONE;
            dp.dst[dp.dirtyNum]   = dst;
            dp.dirtyNum++;

            remove_piece(dst);
            put_piece(dst, promotedPiece);
            assert(count(promotedPiece) != 0);
            // Update hash keys
            // clang-format off
            k                                ^= Zobrist::psq[movedPiece][dst] ^ Zobrist::psq[promotedPiece][dst];
            st->pawnKey   [ac]               ^= Zobrist::psq[movedPiece][dst];
            st->nonPawnKey[ac]               ^= Zobrist::psq[promotedPiece][dst];
            st->groupKey[is_major(promoted)] ^= Zobrist::psq[promotedPiece][dst];
            // clang-format on
        }
        // Set en-passant square if the moved pawn can be captured
        else if ((int(dst) ^ int(org)) == NORTH_2 && can_enpassant(~ac, dst - pawn_spush(ac)))
        {
            assert(relative_rank(ac, org) == RANK_2);
            assert(relative_rank(ac, dst) == RANK_4);

            st->epSquare = dst - pawn_spush(ac);
            k ^= Zobrist::enpassant[file_of(dst)];
        }

        // Update pawn hash key
        st->pawnKey[ac] ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

        // Reset rule 50 draw counter
        reset_rule50_count();
    }
    else
    {
        st->nonPawnKey[ac] ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

        if (pt == KING)
        {
            st->kingSquare[ac] = dst;
            st->groupKey[0] ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
            st->groupKey[1] ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
        }
        else
        {
            st->groupKey[is_major(pt)] ^=
              Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
        }
    }
    // Calculate checkers (if move is check)
    st->checkers = check ? pieces(ac) & attackers_to(king_square(~ac)) : 0;
    assert(!check || (checkers() && popcount(checkers()) <= 2));

DO_MOVE_END:

    // Update hash key
    k ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

    // Set the key with the updated key
    st->key = k;

    st->capturedPiece = capturedPiece;
    st->promotedPiece = promotedPiece;

    activeColor = ~ac;

    // Update king attacks used for fast check detection
    set_ext_state();

    // Calculate the repetition info.
    // It is the ply distance from the previous occurrence of the same position,
    // negative in the 3-fold case, or zero when the position was not repeated.
    st->repetition = 0;

    auto end = std::min(rule50_count(), null_ply());
    if (end >= 4)
    {
        State* pst = st->preState->preState;
        for (auto i = 4; i <= end; i += 2)
        {
            pst = pst->preState->preState;
            if (pst->key == st->key)
            {
                st->repetition = pst->repetition ? -i : +i;
                break;
            }
        }
    }

    assert(pos_is_ok());
}

// Unmakes a move, restoring the position to its exact state before the move was made.
void Position::undo_move(Move m) noexcept {
    assert(m.is_ok());

    Color ac = activeColor = ~active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(dst);

    assert(empty_on(org) || m.type_of() == CASTLING);

    Piece capturedPiece = captured_piece();
    assert(capturedPiece == NO_PIECE || type_of(capturedPiece) != KING);

    if (m.type_of() == CASTLING)
    {
        assert(pieces(ac, KING) & king_castle_sq(ac, org, dst));
        assert(pieces(ac, ROOK) & rook_castle_sq(ac, org, dst));
        assert(capturedPiece == NO_PIECE);
        assert(st->castled[ac]);

        Square rorg, rdst;
        do_castling<false>(ac, org, dst, rorg, rdst);
        assert(rorg == m.dst_sq());

        goto UNDO_MOVE_END;
    }

    if (m.type_of() == PROMOTION)
    {
        assert(relative_rank(ac, org) == RANK_7);
        assert(relative_rank(ac, dst) == RANK_8);
        assert(KNIGHT <= m.promotion_type() && m.promotion_type() <= QUEEN);
        assert(type_of(pc) == m.promotion_type());
        assert(promoted_piece() == pc);

        remove_piece(dst);
        pc = make_piece(ac, PAWN);
        put_piece(dst, pc);
    }

    // Move back the piece
    move_piece(dst, org);

    if (capturedPiece != NO_PIECE)
    {
        Square cap = dst;

        if (m.type_of() == EN_PASSANT)
        {
            cap -= pawn_spush(ac);

            assert(type_of(pc) == PAWN);
            assert(relative_rank(ac, org) == RANK_5);
            assert(relative_rank(ac, dst) == RANK_6);
            assert(empty_on(cap));
            assert(capturedPiece == make_piece(~ac, PAWN));
            assert(rule50_count() == 0);
            assert(st->preState->epSquare == dst);
            assert(st->preState->rule50 == 0);
        }
        // Restore the captured piece
        put_piece(cap, capturedPiece);
    }

UNDO_MOVE_END:

    --posPly;
    // Finally point our state pointer back to the previous state
    st = st->preState;

    assert(pos_is_ok());
}

// Used to do a "null move":
// it flips the side to move without executing any move on the board.
void Position::do_null_move(State& newSt) noexcept {
    assert(&newSt != st);
    assert(!checkers());

    std::memcpy(&newSt, st, offsetof(State, bigAccumulator));
    newSt.preState = st;

    st->nxtState = &newSt;
    st           = &newSt;

    st->bigAccumulator.computed[WHITE]     = st->bigAccumulator.computed[BLACK] =
      st->smallAccumulator.computed[WHITE] = st->smallAccumulator.computed[BLACK] = false;

    st->dirtyPiece.dirtyNum = 0;
    st->dirtyPiece.piece[0] = NO_PIECE;  // Avoid checks in UpdateAccumulator()
    st->capturedPiece       = NO_PIECE;
    st->promotedPiece       = NO_PIECE;
    st->capSquare           = SQ_NONE;

    ++st->rule50;
    st->nullPly = 0;
    //st->checkers = 0;

    activeColor = ~active_color();

    st->key ^= Zobrist::side;
    if (ep_is_ok(ep_square()))
    {
        st->key ^= Zobrist::enpassant[file_of(ep_square())];
        reset_ep_square();
    }

    set_ext_state();

    st->repetition = 0;

    assert(pos_is_ok());
}

// Used to undo a "null move"
void Position::undo_null_move() noexcept {
    assert(!checkers());
    assert(captured_piece() == NO_PIECE);
    assert(promoted_piece() == NO_PIECE);
    assert(cap_square() == SQ_NONE);

    activeColor = ~active_color();

    st = st->preState;

    assert(pos_is_ok());
}

// Tests whether a pseudo-legal move is legal
bool Position::legal(Move m) const noexcept {
    assert(m.is_ok() && pseudo_legal(m));

    Color ac = active_color();

    Square ksq = king_square(ac);
    assert(pieces(ac, KING) & ksq);

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    Bitboard occupied = pieces();
    Bitboard blockers = this->blockers(ac);

    switch (m.type_of())
    {
    // En-passant captures are a tricky special case. Because they are rather uncommon,
    // Simply by testing whether the king is attacked after the move is made.
    case EN_PASSANT : {
        Square cap = dst - pawn_spush(ac);
        assert(relative_rank(ac, org) == RANK_5);
        assert(relative_rank(ac, dst) == RANK_6);
        assert(type_of(piece_on(org)) == PAWN);
        assert(pieces(~ac, PAWN) & cap);
        assert(!(occupied & (dst | (dst + pawn_spush(ac)))));
        assert(ep_square() == dst);
        assert(rule50_count() == 0);

        occupied = (occupied ^ org ^ cap) | dst;
        return !(pieces(~ac) & slide_attackers_to(ksq, occupied));
    }
    // Castling moves generation does not check if the castling path is clear of
    // enemy attacks, it is delayed at a later time: now!
    case CASTLING : {
        assert(relative_rank(ac, org) == RANK_1);
        assert(relative_rank(ac, dst) == RANK_1);
        assert(org == ksq);
        assert(type_of(piece_on(org)) == KING);
        assert(pieces(ac, ROOK) & dst);
        assert(!checkers());
        assert(can_castle(make_castling_rights(ac, org, dst)));
        assert(!castling_impeded(make_castling_rights(ac, org, dst)));
        assert(dst == castling_rook_square(make_castling_rights(ac, org, dst)));

        occupied = occupied ^ ksq;
        // After castling, the rook and king final positions are the same in
        // Chess960 as they would be in standard chess.
        Square    kdst = king_castle_sq(ac, ksq, dst);
        Direction step = ksq < kdst ? WEST : EAST;
        for (Square s = kdst; s != ksq; s += step)
            if (pieces(~ac) & attackers_to(s, occupied))
                return false;

        // In case of Chess960, verify if the Rook blocks some checks.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !(blockers & dst);
    }
    case PROMOTION :
        assert(relative_rank(ac, org) == RANK_7);
        assert(relative_rank(ac, dst) == RANK_8);
        assert(type_of(piece_on(org)) == PAWN);
        assert((org + pawn_spush(ac) == dst && !(occupied & dst))
               || (pawn_attacks_bb(ac, org) & pieces(~ac) & dst));
        break;
    default :  // NORMAL
        // If the moving piece is a king then
        // check whether the destination square is attacked by the opponent.
        if (ksq == org)
            return !(pieces(~ac) & attackers_to(dst, occupied ^ ksq));
    }
    // A non-king move is legal if and only if it is not pinned or
    // it is moving along the line from the king.
    return !(blockers & org) || aligned(org, dst, ksq);
}

// Takes a random move and tests whether the move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal(Move m) const noexcept {

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(org);

    // If the origin square is not occupied by a piece belonging to
    // the side to move, the move is obviously not legal.
    if (!is_ok(pc) || color_of(pc) != ac)
        return false;

    Bitboard occupied = pieces();
    Bitboard checkers = this->checkers();

    if (m.type_of() == CASTLING)
    {
        CastlingRights cr = make_castling_rights(ac, org, dst);
        return relative_rank(ac, org) == RANK_1 && relative_rank(ac, dst) == RANK_1
            && type_of(pc) == KING && (pieces(ac, ROOK) & dst) && !checkers  //
            && can_castle(cr) && !castling_impeded(cr) && castling_rook_square(cr) == dst;
    }

    // The destination square cannot be occupied by a friendly piece
    if (pieces(ac) & dst)
        return false;

    switch (m.type_of())
    {
    case NORMAL : {
        // Is not a promotion, so the promotion type must be empty
        assert((m.promotion_type() - KNIGHT) == NO_PIECE_TYPE);

        // Handle the special case of a pawn move
        if (type_of(pc) == PAWN)
        {
            // Already handled promotion moves, so origin & destination cannot be on the 8th/1st rank
            if (PROMOTION_RANK_BB & (org | dst))
                return false;
            // clang-format off
            if (!(relative_rank(ac, org) < RANK_7 && relative_rank(ac, dst) < RANK_8
                  && ((org + pawn_spush(ac) == dst && !(occupied & dst))  // Single push
                      || (pawn_attacks_bb(ac, org) & pieces(~ac) & dst)))  // Capture
             && !(relative_rank(ac, org) == RANK_2 && relative_rank(ac, dst) == RANK_4
                  && org + pawn_dpush(ac) == dst  // Double push
                  && !(occupied & (dst | (dst - pawn_spush(ac))))))
                return false;
            // clang-format on
        }
        else if (!(attacks_bb(type_of(pc), org, occupied) & dst))
            return false;
    }
    break;

    case PROMOTION :
        if (!(relative_rank(ac, org) == RANK_7 && relative_rank(ac, dst) == RANK_8
              && type_of(pc) == PAWN  //&& (PROMOTION_RANK_BB & dst)
              && ((org + pawn_spush(ac) == dst && !(occupied & dst))
                  || (pawn_attacks_bb(ac, org) & pieces(~ac) & dst))))
            return false;
        break;

    case EN_PASSANT :
        if (!(relative_rank(ac, org) == RANK_5 && relative_rank(ac, dst) == RANK_6
              && type_of(pc) == PAWN && ep_square() == dst && rule50_count() == 0
              && (pieces(~ac, PAWN) & (dst - pawn_spush(ac)))
              && !(occupied & (dst | (dst + pawn_spush(ac))))
              && (pawn_attacks_bb(ac, org) /*& ~occupied*/ & dst)))
            return false;
        break;

    default :;  // CASTLING
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // Therefore have to take care that the some kind of moves are filtered out here.
    if (checkers)
    {
        Square ksq = king_square(ac);
        // For king moves under check, remove the king so as to catch
        // invalid moves like b1a1 when opponent's queen is on c1.
        if (ksq == org)
            return !(checkers & attackers_to(dst, occupied ^ ksq));

        return
          // Double check? In this case, a king move is required
          !more_than_one(checkers)
          // Pinned piece can never resolve a check
          // NOTE: there is some issue with this condition
          //&& !(blockers(ac) & org)
          // Our move must be a blocking interposition or a capture of the checking piece
          && ((between_bb(ksq, lsb(checkers)) & dst)
              || (m.type_of() == EN_PASSANT && (checkers & (dst - pawn_spush(ac)))));
    }
    return true;
}

// Tests whether a pseudo-legal move is a check
bool Position::check(Move m) const noexcept {
    assert(m.is_ok());

    Color ac = active_color();

    Square ksq = king_square(~ac);
    assert(pieces(~ac, KING) & ksq);

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    if (
      // Is there a direct check?
      (checks(m.type_of() != PROMOTION ? type_of(piece_on(org)) : m.promotion_type()) & dst)
      // Is there a discovered check?
      || ((blockers(~ac) & org) && (!aligned(org, dst, ksq) || m.type_of() == CASTLING)))
        return true;

    Bitboard occupied = pieces();

    switch (m.type_of())
    {
    case PROMOTION :
        return attacks_bb(m.promotion_type(), dst, occupied ^ org) & ksq;

    // En-passant capture with check? Already handled the case of direct check
    // and ordinary discovered check, so the only case need to handle is
    // the unusual case of a discovered check through the captured pawn.
    case EN_PASSANT :
        occupied = (occupied ^ org ^ (dst - pawn_spush(ac))) | dst;
        return pieces(ac) & slide_attackers_to(ksq, occupied);

    case CASTLING :
        // Castling is encoded as "king captures rook"
        return checks(ROOK) & rook_castle_sq(ac, org, dst);

    default :;  // NORMAL
    }
    return false;
}

bool Position::dbl_check(Move m) const noexcept {
    assert(m.is_ok());

    Color ac = active_color();

    Square ksq = king_square(~ac);
    assert(pieces(~ac, KING) & ksq);

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    Bitboard occupied = pieces();

    switch (m.type_of())
    {
    case NORMAL :
        return
          // Is there a direct check?
          (checks(type_of(piece_on(org))) & dst)
          // Is there a discovered check?
          && ((blockers(~ac) & org) && !aligned(org, dst, ksq));

    case PROMOTION :
        return (blockers(~ac) & org)  //
            && (attacks_bb(m.promotion_type(), dst, occupied ^ org) & ksq);

    case EN_PASSANT : {
        occupied = (occupied ^ org ^ (dst - pawn_spush(ac))) | dst;

        Bitboard checker = pieces(ac) & slide_attackers_to(ksq, occupied);
        return more_than_one(checker) || (checker && (checks(PAWN) & dst));
    }
    default :;  // CASTLING
    }
    return false;
}

bool Position::fork(Move m) const noexcept {
    assert(m.is_ok());

    Color ac = active_color();

    switch (type_of(piece_on(m.org_sq())))
    {
    case PAWN :
        return more_than_one(pieces(~ac) & ~pieces(PAWN) & pawn_attacks_bb(ac, m.dst_sq()));
    case KNIGHT :
        return more_than_one(pieces(~ac) & attacks_bb<KNIGHT>(m.dst_sq()));
    case BISHOP :
        return more_than_one(pieces(~ac) & attacks_bb<BISHOP>(m.dst_sq()));
    case ROOK :
        return more_than_one(pieces(~ac) & attacks_bb<ROOK>(m.dst_sq()));
    case QUEEN :
        return more_than_one(pieces(~ac) & attacks_bb<QUEEN>(m.dst_sq()));
    case KING :
        return more_than_one(pieces(~ac) & ~pieces(KING, QUEEN) & attacks_bb<KING>(m.dst_sq()));
    default :;
    }
    return false;
}

Key Position::material_key() const noexcept {

    Key materialKey = 0;
    for (Piece pc : Pieces)
    {
        //if (type_of(pc) == KING)
        //    continue;
        for (auto cnt = 0; cnt < count(pc); ++cnt)
            materialKey ^= Zobrist::psq[pc][cnt];
    }
    return materialKey;
}

// Computes the new hash key after the given move.
// Needed for speculative prefetch.
// It does recognize special moves like castling, en-passant and promotions.
Key Position::move_key(Move m) const noexcept {
    assert(m.is_ok());

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  movedPiece    = piece_on(org);
    auto   moved         = type_of(movedPiece);
    Square cap           = m.type_of() != EN_PASSANT ? dst : dst - pawn_spush(ac);
    Piece  capturedPiece = piece_on(cap);
    assert(color_of(movedPiece) == ac);
    assert(capturedPiece == NO_PIECE
           || color_of(capturedPiece) == (m.type_of() != CASTLING ? ~ac : ac));
    assert(type_of(capturedPiece) != KING);

    Key moveKey =
      st->key ^ Zobrist::side  //
      ^ Zobrist::psq[movedPiece][org]
      ^ Zobrist::psq[m.type_of() != PROMOTION ? movedPiece : make_piece(ac, m.promotion_type())]
                    [m.type_of() != CASTLING ? dst : king_castle_sq(ac, org, dst)]
      ^ Zobrist::castling[castling_rights() & castling_rights_mask(org, dst)];

    if (ep_is_ok(ep_square()))
        moveKey ^= Zobrist::enpassant[file_of(ep_square())];

    if (m.type_of() == CASTLING)
    {
        assert(moved == KING);
        assert(capturedPiece == make_piece(ac, ROOK));
        // ROOK
        moveKey ^= Zobrist::psq[capturedPiece][dst]
                 ^ Zobrist::psq[capturedPiece][rook_castle_sq(ac, org, dst)];
        //capturedPiece = NO_PIECE;
        return adjust_key(moveKey, 1);
    }

    if (capturedPiece != NO_PIECE)
    {
        moveKey ^= Zobrist::psq[capturedPiece][cap];
        return moveKey;
    }

    if (moved == PAWN && (int(dst) ^ int(org)) == NORTH_2
        && can_enpassant(~ac, dst - pawn_spush(ac), true))
    {
        assert(relative_rank(ac, org) == RANK_2);
        assert(relative_rank(ac, dst) == RANK_4);
        moveKey ^= Zobrist::enpassant[file_of(dst)];
        return moveKey;
    }

    return moved == PAWN /*|| capturedPiece != NO_PIECE*/ ? moveKey : adjust_key(moveKey, 1);
}

Key Position::move_key() const noexcept {

    Key moveKey = st->key ^ Zobrist::side;
    if (ep_is_ok(ep_square()))
        moveKey ^= Zobrist::enpassant[file_of(ep_square())];

    return adjust_key(moveKey, 1);
}

// Tests if the SEE (Static Exchange Evaluation) value of the move
// is greater or equal to the given threshold.
// An algorithm similar to alpha-beta pruning with a null window.
bool Position::see_ge(Move m, Value threshold) const noexcept {
    assert(m.is_ok());

    // Not deal with castling, can't win any material, nor can lose any.
    if (m.type_of() == CASTLING)
        return threshold <= VALUE_ZERO;

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    Bitboard occupied = pieces();

    Square cap = dst;
    if (m.type_of() == EN_PASSANT)
    {
        cap -= pawn_spush(ac);
        occupied ^= cap;
    }

    Value swap;

    swap = PIECE_VALUE[type_of(piece_on(cap))] - threshold;
    // If promotion, get the promoted piece and lose the pawn
    if (m.type_of() == PROMOTION)
        swap += PIECE_VALUE[m.promotion_type()] - VALUE_PAWN;
    // If can't beat the threshold despite capturing the piece,
    // it is impossible to beat the threshold.
    if (swap < VALUE_ZERO)
        return false;

    auto moved = m.type_of() != PROMOTION ? type_of(piece_on(org)) : m.promotion_type();

    swap = PIECE_VALUE[moved] - swap;
    // If still beat the threshold after losing the piece,
    // it is guaranteed to beat the threshold.
    if (swap <= VALUE_ZERO)
        return true;

    // It doesn't matter if the destination square is occupied or not
    // xoring to is important for pinned piece logic
    occupied = occupied ^ org ^ dst;

    Bitboard attackers = attackers_to(dst, occupied) & occupied;

    Square epSq = SQ_NONE;
    if (moved == PAWN && (int(dst) ^ int(org)) == NORTH_2)
    //&& can_enpassant(~ac, dst - pawn_spush(ac), true))
    {
        assert(relative_rank(ac, org) == RANK_2);
        assert(relative_rank(ac, dst) == RANK_4);

        epSq = dst - pawn_spush(ac);

        Bitboard epAttackers = pieces(~ac, PAWN) & pawn_attacks_bb(ac, epSq);
        if (epAttackers)
            attackers |= epAttackers;
        else
            epSq = SQ_NONE;
    }

    if (!attackers)
        return true;

    bool win = true;

    Bitboard qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
    Bitboard qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

    const Magic(*magic)[2] = &Magics[dst];

    bool discovery[COLOR_NB]{true, true};

    while (attackers)
    {
        ac = ~ac;

        Bitboard acAttackers = pieces(ac) & attackers;
        // If ac has no more attackers then give up: ac loses
        if (!acAttackers)
            break;

        Bitboard  b;
        PieceType pt;
        // Don't allow pinned pieces to attack as long as
        // there are pinners on their original square.
        if ((b = pinners(~ac) & pieces(~ac) & occupied))
        {
            while (b && acAttackers)
                acAttackers &= ~between_bb(king_square(ac), pop_lsb(b));

            if (!acAttackers)
                break;
        }
        if ((blockers(ac) & org)
            && (b = pinners(ac) & pieces(~ac) & line_bb(org, king_square(ac)) & occupied)
            && ((pt = type_of(piece_on(org))) != PAWN || !aligned(org, dst, king_square(ac))))
        {
            acAttackers &= king_square(ac);

            if (!acAttackers  //
                && (pt == PAWN || !(attacks_bb(pt, dst, occupied) & king_square(ac))))
            {
                dst  = lsb(b);
                swap = PIECE_VALUE[type_of(piece_on(org))] - swap;
                if ((swap = PIECE_VALUE[type_of(piece_on(dst))] - swap) < win)
                    break;

                occupied ^= dst;

                attackers = attackers_to(dst, occupied) & occupied;
                if (!attackers)
                    break;

                qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
                qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

                magic = &Magics[dst];

                acAttackers = pieces(ac) & attackers;
            }

            if (!acAttackers)
                break;
        }

        win = !win;

        if (epSq == SQ_NONE && discovery[ac] && (b = blockers(~ac) & acAttackers))
        {
            Square sq;
            sq = pop_lsb(b);
            pt = type_of(piece_on(sq));
            if (b && pt == KING)
            {
                sq = pop_lsb(b);
                pt = type_of(piece_on(sq));
            }

            if (pt == KING)
            {
                if (!(pieces(~ac) & attackers))
                    break;

                discovery[ac] = false;

                ac  = ~ac;
                win = !win;
                continue;  // Resume without considering discovery
            }

            if (!(pinners(~ac) & pieces(ac) & line_bb(sq, king_square(~ac)) & occupied))
            {
                discovery[ac] = false;

                ac  = ~ac;
                win = !win;
                continue;  // Resume without considering discovery
            }

            if ((swap = PIECE_VALUE[pt] - swap) < win)
                break;
            occupied ^= org = sq;
            switch (pt)
            {
            case PAWN :
            case BISHOP :
                qB &= occupied;
                if (qB)
                    attackers |= qB & attacks_bb<BISHOP>(magic, occupied);
                break;
            case ROOK :
                qR &= occupied;
                if (qR)
                    attackers |= qR & attacks_bb<ROOK>(magic, occupied);
                break;
            case QUEEN :
                assert(false);
                [[fallthrough]];
            default :;
            }
        }
        // Locate and remove the next least valuable attacker, and add to
        // the bitboard 'attackers' any X-ray attackers behind it.
        else if ((b = pieces(PAWN) & acAttackers))
        {
            if ((swap = VALUE_PAWN - swap) < win)
                break;
            occupied ^= org = lsb(b);
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(magic, occupied);

            if (epSq != SQ_NONE && rank_of(org) == rank_of(dst))
            {
                occupied ^= (dst | epSq);

                dst       = epSq;
                attackers = attackers_to(dst, occupied) & occupied;
                if (!attackers)
                    break;

                qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
                qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

                magic = &Magics[dst];

                epSq = SQ_NONE;
            }
        }
        else if ((b = pieces(KNIGHT) & acAttackers))
        {
            if ((swap = VALUE_KNIGHT - swap) < win)
                break;
            occupied ^= org = lsb(b);
        }
        else if ((b = pieces(BISHOP) & acAttackers))
        {
            if ((swap = VALUE_BISHOP - swap) < win)
                break;
            occupied ^= org = lsb(b);
            qB &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(magic, occupied);
        }
        else if ((b = pieces(ROOK) & acAttackers))
        {
            if ((swap = VALUE_ROOK - swap) < win)
                break;
            occupied ^= org = lsb(b);
            qR &= occupied;
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(magic, occupied);
        }
        else if ((b = pieces(QUEEN) & acAttackers))
        {
            if ((swap = VALUE_QUEEN - swap) < win)
                break;
            occupied ^= org = lsb(b);
            qB &= occupied;
            qR &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(magic, occupied);
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(magic, occupied);
        }
        else  // KING
        {
            // If "capture" with the king but the opponent still has attackers, reverse the result.
            win ^= bool(pieces(~ac) & attackers);
            break;
        }

        attackers &= occupied;
    }

    return win;
}

// Tests whether the current position is drawn by repetition or by 50-move rule.
// It also detect stalemates.
bool Position::is_draw(std::int16_t ply, bool checkStalemate) const noexcept {

    return
      // Draw by Repetition: position repeats once earlier but strictly
      // after the root, or repeats twice before or at the root.
      /**/ (repetition() && repetition() < ply)
      // Draw by 50-move rule
      || (rule50_count() >= 2 * DrawMoveCount && (!checkers() || !LegalMoveList(*this).empty()))
      // Draw by Stalemate
      || (checkStalemate && !checkers() && LegalMoveList(*this).empty());
}

// Tests whether there has been at least one repetition
// of positions since the last capture or pawn move.
bool Position::has_repeated() const noexcept {
    auto end = std::min(rule50_count(), null_ply());
    if (end < 4)
        return false;

    State* cst = st;
    while (end-- >= 4)
    {
        if (cst->repetition)
            return true;
        cst = cst->preState;
    }
    return false;
}

// Tests if the current position has a move which draws by repetition.
// Accurately matches the outcome of is_draw() over all legal moves.
bool Position::upcoming_repetition(std::int16_t ply) const noexcept {
    auto end = std::min(rule50_count(), null_ply());
    // Enough reversible moves played
    if (end < 3)
        return false;

    State* pst = st->preState;

    Key baseKey = st->key;
    Key iterKey = baseKey ^ pst->key ^ Zobrist::side;

    Bitboard occupied = pieces();

    for (auto i = 3; i <= end; i += 2)
    {
        iterKey ^= pst->preState->key ^ pst->preState->preState->key ^ Zobrist::side;
        pst = pst->preState->preState;

        // Opponent pieces have reverted
        if (iterKey != 0)
            continue;

        Key moveKey = baseKey ^ pst->key;
        // 'moveKey' is a single move
        Key16 index = Cuckoos.find_key(moveKey);
        if (index >= Cuckoos.size())
            continue;

        Move m = Cuckoos[index].move;
        assert(m != Move::None());
        Square s1 = m.org_sq();
        Square s2 = m.dst_sq();

        // Move path is obstructed
        if (occupied & ex_between_bb(s1, s2))
            continue;

#if !defined(NDEBUG)
        // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in the same location
        if (empty_on(s1))
            m = Move(s2, s1);
        assert(pseudo_legal(m) && legal(m));
        //assert(LegalMoveList(*this).contains(m));
#endif
        if (i < ply
            // For nodes before or at the root, check that the move is
            // a repetition rather than a move to the current position.
            || pst->repetition)
            return true;
    }
    return false;
}

void Position::reset_ep_square() noexcept { st->epSquare = SQ_NONE; }

void Position::reset_rule50_count() noexcept {
    st->rule50High |= st->rule50 >= rule50_threshold();
    st->rule50 = 0;
}

void Position::reset_repetitions() noexcept {

    State* cst = st;
    while (cst != nullptr)
    {
        cst->repetition = 0;

        cst = cst->preState;
    }
}

// Flips the current position with the white and black sides reversed.
// This is only useful for debugging e.g. for finding evaluation symmetry bugs.
void Position::flip() noexcept {
    std::istringstream iss(fen());

    std::string f, token;
    for (Rank r = RANK_8; r >= RANK_1; --r)  // Piece placement
    {
        std::getline(iss, token, r > RANK_1 ? '/' : ' ');
        f.insert(0, token + (f.empty() ? " " : "/"));
    }

    iss >> token;  // Active color (will be lowercased later)
    f += (token[0] == 'w' ? "B " : "W ");
    iss >> token;  // Castling availability
    f += token + ' ';

    f = toggle_case(f);

    iss >> token;  // En-passant square
    f += (token[0] == '-' ? "-" : token.replace(1, 1, token[1] == '3' ? "6" : "3"));

    std::getline(iss, token);  // Half and full moves
    f += token;

    set(f, st);

    assert(pos_is_ok());
}

#if !defined(NDEBUG)
// Computes the hash key of the current position.
Key Position::compute_key() const noexcept {
    Key key = 0;

    Bitboard occupied = pieces();
    while (occupied)
    {
        Square s = pop_lsb(occupied);
        assert(is_ok(piece_on(s)));
        key ^= Zobrist::psq[piece_on(s)][s];
    }

    key ^= Zobrist::castling[castling_rights()];

    if (ep_is_ok(ep_square()))
        key ^= Zobrist::enpassant[file_of(ep_square())];

    if (active_color() == BLACK)
        key ^= Zobrist::side;

    return key;
}

// Performs some consistency checks for the position object
// and raise an assert if something wrong is detected.
// This is meant to be helpful when debugging.
bool Position::pos_is_ok() const noexcept {

    constexpr bool Fast = true;  // Quick (default) or full check?

    if ((active_color() != WHITE && active_color() != BLACK)  //
        || count(W_KING) != 1 || count(B_KING) != 1           //
        || piece_on(king_square(WHITE)) != W_KING             //
        || piece_on(king_square(BLACK)) != B_KING             //
        || distance(king_square(WHITE), king_square(BLACK)) <= 1
        || (ep_is_ok(ep_square())  //
            && !can_enpassant(active_color(), ep_square())))
        assert(false && "Position::pos_is_ok(): Default");

    if (Fast)
        return true;

    if (st->key != compute_key())
        assert(false && "Position::pos_is_ok(): Key");

    if (pieces(active_color()) & attackers_to(king_square(~active_color())))
        assert(false && "Position::pos_is_ok(): King Checker");

    if ((pieces(PAWN) & PROMOTION_RANK_BB) || count(W_PAWN) > 8 || count(B_PAWN) > 8)
        assert(false && "Position::pos_is_ok(): Pawns");

    for (Color c : {WHITE, BLACK})
        if (count<PAWN>(c)  //
              + std::max(count<KNIGHT>(c) - 2, 0)
              + std::max(popcount(pieces(c, BISHOP) & COLOR_BB[WHITE]) - 1, 0)
              + std::max(popcount(pieces(c, BISHOP) & COLOR_BB[BLACK]) - 1, 0)
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
        if (count(pc) != popcount(pieces(color_of(pc), type_of(pc)))
            || count(pc) != board.count(pc))
            assert(false && "Position::pos_is_ok(): Pieces");

    for (Color c : {WHITE, BLACK})
        for (CastlingRights cr : {c & KING_SIDE, c & QUEEN_SIDE})
        {
            if (!can_castle(cr))
                continue;

            if (castling_rook_square(cr) == SQ_NONE  //
                || !(pieces(c, ROOK) & castling_rook_square(cr))
                || (castlingRightsMask[c * FILE_NB + file_of(castling_rook_square(cr))]) != cr
                || (castlingRightsMask[c * FILE_NB + file_of(king_square(c))] & cr) != cr)
                assert(false && "Position::pos_is_ok(): Castling");
        }

    return true;
}
#endif

std::ostream& operator<<(std::ostream& os, const Position::Board& board) noexcept {
    os << "\n +---+---+---+---+---+---+---+---+\n";
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        for (File f = FILE_A; f <= FILE_H; ++f)
            os << " | " << UCI::piece_figure(board.piece_on(make_square(f, r)));
        os << " | " << UCI::rank(r)  //
           << "\n +---+---+---+---+---+---+---+---+\n";
    }
    for (File f = FILE_A; f <= FILE_H; ++f)
        os << "   " << UCI::file(f, true);

    return os;
}

// Returns an ASCII representation of the position
std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept {

    os << pos.board << '\n'                                         //
       << "\nFen: " << pos.fen()                                    //
       << "\nKey: " << u64_to_string(pos.key())                     //
       << "\nKing Squares: "                                        //
       << UCI::square(pos.king_square(pos.active_color())) << ", "  //
       << UCI::square(pos.king_square(~pos.active_color()))         //
       << "\nCheckers: ";
    Bitboard checkers = pos.checkers();
    if (checkers)
        while (checkers)
            os << UCI::square(pop_lsb(checkers)) << " ";
    else
        os << "(none)";
    os << "\nRepetition: " << pos.repetition();

    if (Tablebases::MaxCardinality >= pos.count<ALL_PIECE>() && !pos.can_castle(ANY_CASTLING))
    {
        State st;
        ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

        Position p;
        p.set(pos, &st);
        st = *pos.state();

        Tablebases::ProbeState wdlPs, dtzPs;

        auto wdl = Tablebases::probe_wdl(p, &wdlPs);
        auto dtz = Tablebases::probe_dtz(p, &dtzPs);
        os << "\nTablebases WDL: " << std::setw(4) << wdl << " (" << wdlPs << ")"
           << "\nTablebases DTZ: " << std::setw(4) << dtz << " (" << dtzPs << ")";
    }

    return os;
}

}  // namespace DON
