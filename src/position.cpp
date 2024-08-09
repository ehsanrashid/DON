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

constexpr inline std::array<Piece, 12> Pieces{W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
                                              B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING};

constexpr inline std::array<std::uint8_t, PIECE_TYPE_NB> MobilityWeight{0, 3, 26, 29, 35, 32, 1, 0};

// Implements Marcel van Kervinck's cuckoo algorithm to detect repetition of positions
// for 3-fold repetition draws. The algorithm uses two hash tables with Zobrist hashes
// to allow fast detection of recurring positions. For details see:
// http://web.archive.org/web/20201107002606/https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
struct Cuckoo final {

    constexpr bool empty() const noexcept { return key == 0 /*&& move == Move::None()*/; }

    Key  key  = 0;
    Move move = Move::None();
};

template<Key16 Size>
class CuckooHashTable final {
   public:
    CuckooHashTable() noexcept                                  = default;
    CuckooHashTable(const CuckooHashTable&) noexcept            = delete;
    CuckooHashTable(CuckooHashTable&&) noexcept                 = delete;
    CuckooHashTable& operator=(const CuckooHashTable&) noexcept = delete;
    CuckooHashTable& operator=(CuckooHashTable&&) noexcept      = delete;

    constexpr auto size() const noexcept { return Size; }
    constexpr void init() noexcept { fill({0, Move::None()}); }
    constexpr void fill(Cuckoo&& cuckoo) noexcept { cuckoos.fill(std::move(cuckoo)); }

    auto& operator[](std::size_t idx) const noexcept { return cuckoos[idx]; }
    auto& operator[](std::size_t idx) noexcept { return cuckoos[idx]; }

    // Hash functions for indexing the cuckoo table
    static constexpr Key16 H1(Key key) noexcept { return Key16(key >> 00) & (Size - 1); }
    static constexpr Key16 H2(Key key) noexcept { return Key16(key >> 16) & (Size - 1); }

    void insert(Cuckoo& cuckoo) noexcept {
        Key16 index = H1(cuckoo.key);
        while (true)
        {
            std::swap(cuckoos[index], cuckoo);
            if (cuckoo.empty())  // Arrived at empty slot?
                break;
            index ^= H1(cuckoo.key) ^ H2(cuckoo.key);  // Push victim to alternative slot
        }
    }

    Key16 find_index(Key key) const noexcept {
        Key16 index;
        if (index = H1(key); key == cuckoos[index].key)
            return index;
        if (index = H2(key); key == cuckoos[index].key)
            return index;
        return Size;
    }

   private:
    std::array<Cuckoo, Size> cuckoos;
};

CuckooHashTable<0x2000> Cuckoos;

}  // namespace

bool         Position::Chess960      = false;
std::uint8_t Position::DrawMoveCount = 50;

// Initializes at startup the various arrays used to compute hash keys
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
    [[maybe_unused]] std::uint16_t count = 0;
    for (Piece pc : Pieces)
    {
        if (type_of(pc) == PAWN)
            continue;
        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
            for (Square s2 = s1 + 1; s2 <= SQ_H8; ++s2)
                if (attacks_bb(type_of(pc), s1) & s2)
                {
                    Key    key  = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side;
                    Move   move = Move(s1, s2);
                    Cuckoo cuckoo{key, move};
                    Cuckoos.insert(cuckoo);
#if !defined(NDEBUG)
                    ++count;
#endif
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
    assert(si != nullptr);

    std::memset(this, 0, sizeof(Position));
    std::fill(std::begin(castlingRookSquare), std::end(castlingRookSquare), SQ_NONE);
    std::memset(si, 0, sizeof(StateInfo));
    si->epSquare = si->capSquare = SQ_NONE;
    si->kingSquare[WHITE] = si->kingSquare[BLACK] = SQ_NONE;

    st = si;

    std::istringstream iss(fenStr.data());
    iss >> std::noskipws;

    std::uint8_t token;

    File file = FILE_A;
    Rank rank = RANK_8;
    // 1. Piece placement
    while ((iss >> token) && !std::isspace(token))
    {
        if (std::isdigit(token))
        {
            int f = token - '0';
            assert(1 <= f && f <= 8 - file && "Position::set(): Invalid Files");
            file += f;  // Advance the given number of files
        }
        else if (token == '/')
        {
            assert(file <= FILE_NB);
            assert(rank > RANK_1);
            file = FILE_A;
            --rank;
        }
        else if (Piece pc; (pc = UCI::piece(token)) != NO_PIECE)
        {
            assert(file < FILE_NB);
            Square sq = make_square(file, rank);
            put_piece(pc, sq);
            if (type_of(pc) == KING)
                st->kingSquare[color_of(pc)] = sq;
            ++file;
        }
        else
            assert(false && "Position::set(): Invalid Piece");
    }
    assert(file <= FILE_NB && rank == RANK_1);
    assert(count<ALL_PIECE>(WHITE) <= 16 && count<ALL_PIECE>(BLACK) <= 16);
    assert(count<PAWN>(WHITE) <= 8 && count<PAWN>(BLACK) <= 8);
    assert(count<KING>(WHITE) == 1 && count<KING>(BLACK) == 1);
    assert(king_square(WHITE) != SQ_NONE && king_square(BLACK) != SQ_NONE);
    assert(!(pieces(PAWN) & PROMOTION_RANK_BB));
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

    Color stm = side_to_move();

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

        if ('a' <= epFile && epFile <= 'h' && epRank == (stm == WHITE ? '6' : '3'))
        {
            st->epSquare = make_square(File(epFile - 'a'), Rank(epRank - '1'));

            // En-passant square will be considered only if
            // a) there is an enemy pawn in front of epSquare
            // b) there is no piece on epSquare or behind epSquare
            // c) side to move has a pawn threatening epSquare
            // d) there is no enemy Bishop, Rook or Queen pinning
            enpassant = (pieces(~stm, PAWN) & (ep_square() - pawn_spush(stm)))
                     && !(pieces() & (ep_square() | (ep_square() + pawn_spush(stm))))
                     && (pieces(stm, PAWN) & pawn_attacks_bb(~stm, ep_square()))
                     && can_enpassant(stm, ep_square());
        }
        else
            assert(false && "Position::set(): Invalid En-passant square");
    }

    // 5-6. Halfmove clock and fullmove number
    std::int16_t rule50   = 0;
    std::int16_t gameMove = 1;
    iss >> std::skipws >> rule50 >> gameMove;

    st->rule50 = std::max<std::int16_t>(rule50, 0);
    // Convert from gameMove starting from 1 to gamePly starting from 0,
    // handle also common incorrect FEN with gameMove = 0.
    gamePly = std::max(2 * (gameMove - 1), 0) + (stm == BLACK);

    // Reset illegal values
    if (is_ok_ep(ep_square()))
    {
        st->rule50 = 0;
        if (!enpassant)
            st->epSquare = SQ_NONE;
    }
    assert(rule50_count() <= 100);
    gamePly = std::max<std::int16_t>(game_ply(), rule50_count());

    set_state();

    assert(pos_is_ok());

    return *this;
}

// Overload to initialize the position object with the given endgame code string like "KBPKN".
// It's mainly a helper to get the material key out of an endgame code.
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
            if (emptyCount != 0)
                oss << int(emptyCount);

            if (f <= FILE_H)
                oss << UCI::piece(piece_on(make_square(f, r)));
        }

        if (r > RANK_1)
            oss << '/';
    }

    oss << (side_to_move() == WHITE ? " w " : side_to_move() == BLACK ? " b " : " - ");

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
        oss << '-';

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

    int cr    = make_castling_rights(c, korg, rorg);
    int crLsb = lsb(cr);
    assert(0 <= crLsb && crLsb < 4);
    assert(!is_ok(castlingRookSquare[crLsb]));

    st->castlingRights |= cr;
    castlingRightsMask[korg] |= cr;
    castlingRightsMask[rorg]  = cr;
    castlingRookSquare[crLsb] = rorg;

    Square kdst = king_castle_sq(c, korg, rorg);
    Square rdst = rook_castle_sq(c, korg, rorg);

    castlingPath[crLsb] = (between_bb(korg, kdst) | between_bb(rorg, rdst)) & ~(korg | rorg);
}

// Computes the hash keys of the position, and other data
// that once computed is updated incrementally as moves are made.
// The function is only used when a new position is set up.
void Position::set_state() noexcept {
    assert(st->pawnKey == 0 && st->materialKey == 0 && st->key == 0);
    assert(st->nonPawnMaterial[WHITE] == VALUE_ZERO);
    assert(st->nonPawnMaterial[BLACK] == VALUE_ZERO);

    Color stm = side_to_move();

    Bitboard occupied = pieces();
    while (occupied)
    {
        Square s  = pop_lsb(occupied);
        Piece  pc = piece_on(s);
        st->key ^= Zobrist::psq[pc][s];

        if (type_of(pc) == PAWN)
            st->pawnKey ^= Zobrist::psq[pc][s];

        else if (type_of(pc) != KING)
            st->nonPawnMaterial[color_of(pc)] += PIECE_VALUE[pc];
    }

    st->key ^= Zobrist::castling[castling_rights()];

    if (is_ok_ep(ep_square()))
        st->key ^= Zobrist::enpassant[file_of(ep_square())];

    if (stm == BLACK)
        st->key ^= Zobrist::side;

    for (Piece pc : Pieces)
        for (std::uint8_t cnt = 0; cnt < pieceCount[pc]; ++cnt)
            st->materialKey ^= Zobrist::psq[pc][cnt];

    st->checkers = attackers_to(king_square(stm)) & pieces(~stm);

    set_ext_state();
}

// Set extra state to detect if a move gives check
void Position::set_ext_state() noexcept {

    Color stm = side_to_move();

    Bitboard occupied = pieces();

    Square ksq = king_square(~stm);
    // clang-format off
    st->checks[ALL_PIECE]                   = 0;
    st->checks[PAWN]                        = pawn_attacks_bb(~stm, ksq);
    st->checks[KNIGHT]                      = attacks_bb<KNIGHT>(ksq);
    st->checks[QUEEN] = st->checks[BISHOP]  = attacks_bb<BISHOP>(ksq, occupied);
    st->checks[QUEEN] |= st->checks[ROOK]   = attacks_bb<ROOK>  (ksq, occupied);
    st->checks[KING] = st->checks[EX_PIECE] = 0;
    // clang-format on

    Bitboard pinners = st->pinners[WHITE] = st->pinners[BLACK] = 0;
    for (Color c : {WHITE, BLACK})
    {
        ksq = king_square(c);

        Bitboard friends = pieces(c);

        // Calculates st->blockers[c] and st->pinners[],
        // which store respectively the pieces preventing king of color c from being in check
        // and the slider pieces of color ~c pinning pieces of color c to the king.
        st->blockers[c] = 0;

        // Snipers are sliders that attack 'ksq' when a piece and other snipers are removed
        Bitboard snipers = pieces(~c)
                         & ((pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(ksq))
                            | (pieces(QUEEN, ROOK) & attacks_bb<ROOK>(ksq)));
        Bitboard fixOccupied = occupied ^ snipers;

        while (snipers)
        {
            Square sniper = pop_lsb(snipers);

            Bitboard b = between_bb(ksq, sniper) & fixOccupied;
            if (b && !more_than_one(b))
            {
                st->blockers[c] |= b;
                pinners |= st->pinners[b & friends ? ~c : c] |= sniper;
            }
        }
        // clang-format off
        st->mobility[c] = MobilityWeight[PAWN] * popcount(pawn_push_bb(c, pieces(c, PAWN) & ~(st->blockers[c] & pieces(PAWN) & attacks_bb<QUEEN>(ksq, occupied))) & ~occupied);
        st->attacks[c][PAWN] = attacks_by<PAWN>(c, pieces(~c), occupied);
        // clang-format on
    }
    for (Color c : {WHITE, BLACK})
    {
        // clang-format off
        Bitboard target = ~(st->attacks[~c][PAWN]
                          | (pieces(~c) & pinners)
                          | (pieces( c) & (st->blockers[c]
                                         | pieces(QUEEN, KING)
                                         | (pieces(PAWN) & (LOW_RANK_BB[c] | (pawn_push_bb(~c, occupied) & ~pawn_attacks_bb(~c, pieces(~c) & ~pieces(KING))))))));

        st->attacks[c][KNIGHT]    = st->attacks[c][PAWN]   | attacks_by<KNIGHT>(c, target);
        st->attacks[c][BISHOP]    = st->attacks[c][KNIGHT] | attacks_by<BISHOP>(c, target, occupied ^ ((pieces(c, QUEEN, BISHOP) & ~st->blockers[c]) | (pieces(~c, KING, QUEEN, ROOK) & ~pinners)));
        st->attacks[c][ROOK]      = st->attacks[c][BISHOP] | attacks_by<ROOK>  (c, target, occupied ^ ((pieces(c, QUEEN, ROOK)   & ~st->blockers[c]) | (pieces(~c, KING, QUEEN)       & ~pinners)));
        st->attacks[c][QUEEN]     = st->attacks[c][ROOK]   | attacks_by<QUEEN> (c, target, occupied ^ ((pieces(c, QUEEN)         & ~st->blockers[c]) | (pieces(~c, KING)                        )));
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
    if (!(qB | qR))
        return true;
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
    // clang-format off
    return (pieces(WHITE, PAWN)   & pawn_attacks_bb(BLACK, s))
         | (pieces(BLACK, PAWN)   & pawn_attacks_bb(WHITE, s))
         | (pieces(KNIGHT)        & attacks_bb<KNIGHT>(s))
         | (pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupied))
         | (pieces(QUEEN, ROOK)   & attacks_bb<ROOK>(s, occupied))
         | (pieces(KING)          & attacks_bb<KING>(s));
    // clang-format on
}

template<PieceType PT>
Bitboard Position::attacks_by(Color c, Bitboard target, Bitboard occupied) noexcept {

    Square ksq = king_square(c);

    Bitboard attacks;
    if constexpr (PT == PAWN)
    {
        attacks = pawn_attacks_bb(
          c, pieces(c, PAWN) & ~(st->blockers[c] & pieces(PAWN) & attacks_bb<ROOK>(ksq, occupied)));
        st->mobility[c] += MobilityWeight[PAWN] * popcount(attacks & target);
    }
    else
    {
        attacks = 0;

        Bitboard pc = pieces<PT>(c, ksq);
        while (pc)
        {
            Square   org  = pop_lsb(pc);
            Bitboard atks = attacks_bb<PT>(org, occupied);
            if constexpr (PT != KNIGHT)
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
    assert(m.is_ok() && pseudo_legal(m));

    Color stm = side_to_move();

    Square ksq = king_square(stm);
    assert(pieces(stm, KING) & ksq);

    const Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == stm);

    Bitboard occupied = pieces();
    Bitboard blockers = this->blockers(stm);

    switch (m.type_of())
    {
    // En-passant captures are a tricky special case. Because they are rather uncommon,
    // Simply by testing whether the king is attacked after the move is made.
    case EN_PASSANT : {
        Square cap = dst - pawn_spush(stm);
        assert(relative_rank(stm, org) == RANK_5);
        assert(relative_rank(stm, dst) == RANK_6);
        assert(type_of(piece_on(org)) == PAWN);
        assert(pieces(~stm, PAWN) & cap);
        assert(!(occupied & (dst | (dst + pawn_spush(stm)))));
        assert(ep_square() == dst);
        assert(rule50_count() == 0);

        occupied = (occupied ^ org ^ cap) | dst;
        return !(pieces(~stm, QUEEN, BISHOP) & attacks_bb<BISHOP>(ksq, occupied))
            && !(pieces(~stm, QUEEN, ROOK) & attacks_bb<ROOK>(ksq, occupied));
    }
    // Castling moves generation does not check if the castling path is clear of
    // enemy attacks, it is delayed at a later time: now!
    case CASTLING : {
        assert(relative_rank(stm, org) == RANK_1);
        assert(relative_rank(stm, dst) == RANK_1);
        assert(org == ksq);
        assert(type_of(piece_on(org)) == KING);
        assert(pieces(stm, ROOK) & dst);
        assert(!checkers());
        assert(can_castle(make_castling_rights(stm, org, dst)));
        assert(!castling_impeded(make_castling_rights(stm, org, dst)));
        assert(dst == castling_rook_square(make_castling_rights(stm, org, dst)));

        occupied = occupied ^ ksq;
        // After castling, the rook and king final positions are the same in
        // Chess960 as they would be in standard chess.
        Square    kdst = king_castle_sq(stm, ksq, dst);
        Direction step = ksq < kdst ? WEST : EAST;
        for (Square s = kdst; s != ksq; s += step)
            if (attackers_to(s, occupied) & pieces(~stm))
                return false;

        // In case of Chess960, verify if the Rook blocks some checks.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !(blockers & dst);
    }
    case PROMOTION :
        assert(relative_rank(stm, org) == RANK_7);
        assert(relative_rank(stm, dst) == RANK_8);
        assert(type_of(piece_on(org)) == PAWN);
        assert((org + pawn_spush(stm) == dst && !(occupied & dst))
               || (pawn_attacks_bb(stm, org) & pieces(~stm) & dst));
        break;
    default :  // NORMAL
        // If the moving piece is a king then
        // check whether the destination square is attacked by the opponent.
        if (ksq == org)
            return !(attackers_to(dst, occupied ^ ksq) & pieces(~stm));
    }
    // A non-king move is legal if and only if it is not pinned or
    // it is moving along the line from the king.
    return !(blockers & org) || aligned(org, dst, ksq);
}

// Takes a random move and tests whether the move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal(Move m) const noexcept {

    Color stm = side_to_move();

    const Square org = m.org_sq(), dst = m.dst_sq();
    const Piece  pc = piece_on(org);

    // If the origin square is not occupied by a piece belonging to
    // the side to move, the move is obviously not legal.
    if (!is_ok(pc) || color_of(pc) != stm)
        return false;

    Bitboard occupied = pieces();
    Bitboard checkers = this->checkers();

    if (m.type_of() == CASTLING)
    {
        CastlingRights cr = make_castling_rights(stm, org, dst);
        return relative_rank(stm, org) == RANK_1 && relative_rank(stm, dst) == RANK_1
            && type_of(pc) == KING && (pieces(stm, ROOK) & dst) && !checkers  //
            && can_castle(cr) && !castling_impeded(cr) && castling_rook_square(cr) == dst;
    }

    // The destination square cannot be occupied by a friendly piece
    if (pieces(stm) & dst)
        return false;

    switch (m.type_of())
    {
    case NORMAL : {
        // Is not a promotion, so the promotion type must be empty
        assert(!is_ok(PieceType(m.promotion_type() - KNIGHT)));

        // Handle the special case of a pawn move
        if (type_of(pc) == PAWN)
        {
            // Already handled promotion moves, so origin & destination cannot be on the 8th/1st rank
            if (PROMOTION_RANK_BB & (org | dst))
                return false;
            // clang-format off
            if (!(relative_rank(stm, org) < RANK_7 && relative_rank(stm, dst) < RANK_8
                  && ((org + pawn_spush(stm) == dst && !(occupied & dst))  // Single push
                      || (pawn_attacks_bb(stm, org) & pieces(~stm) & dst)))  // Capture
             && !(relative_rank(stm, org) == RANK_2 && relative_rank(stm, dst) == RANK_4
                  && org + pawn_dpush(stm) == dst  // Double push
                  && !(occupied & (dst | (dst - pawn_spush(stm))))))
                return false;
            // clang-format on
        }
        else if (!(attacks_bb(type_of(pc), org, occupied) & dst))
            return false;
    }
    break;

    case PROMOTION :
        if (!(relative_rank(stm, org) == RANK_7 && relative_rank(stm, dst) == RANK_8
              && type_of(pc) == PAWN  //&& (PROMOTION_RANK_BB & dst)
              && ((org + pawn_spush(stm) == dst && !(occupied & dst))
                  || (pawn_attacks_bb(stm, org) & pieces(~stm) & dst))))
            return false;
        break;

    case EN_PASSANT :
        if (!(relative_rank(stm, org) == RANK_5 && relative_rank(stm, dst) == RANK_6
              && type_of(pc) == PAWN && ep_square() == dst && rule50_count() == 0
              && (pieces(~stm, PAWN) & (dst - pawn_spush(stm)))
              && !(occupied & (dst | (dst + pawn_spush(stm))))
              && (pawn_attacks_bb(stm, org) /*& ~occupied*/ & dst)))
            return false;
        break;

    default :;  // CASTLING
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // Therefore have to take care that the some kind of moves are filtered out here.
    if (checkers)
    {
        Square ksq = king_square(stm);
        // For king moves under check, remove the king so as to catch
        // invalid moves like b1a1 when opponent's queen is on c1.
        if (ksq == org)
            return !(attackers_to(dst, occupied ^ ksq) & checkers);

        return
          // Double check? In this case, a king move is required
          !more_than_one(checkers)
          // Pinned piece can never resolve a check
          // NOTE: there is some issue with this condition
          //&& !(blockers(stm) & org)
          // Our move must be a blocking interposition or a capture of the checking piece
          && ((between_bb(ksq, lsb(checkers)) & dst)
              || (m.type_of() == EN_PASSANT && (checkers & (dst - pawn_spush(stm)))));
    }
    return true;
}

// Tests whether a pseudo-legal move gives a check
bool Position::gives_check(Move m) const noexcept {
    assert(m.is_ok());

    Color stm = side_to_move();

    Square ksq = king_square(~stm);
    assert(pieces(~stm, KING) & ksq);

    const Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == stm);

    if (
      // Is there a direct check?
      (checks(m.type_of() != PROMOTION ? type_of(piece_on(org)) : m.promotion_type()) & dst)
      // Is there a discovered check?
      || ((blockers(~stm) & org) && (!aligned(org, dst, ksq) || m.type_of() == CASTLING)))
        return true;

    switch (m.type_of())
    {
    case PROMOTION :
        return attacks_bb(m.promotion_type(), dst, pieces() ^ org) & ksq;

    // En-passant capture with check? Already handled the case of direct check
    // and ordinary discovered check, so the only case need to handle is
    // the unusual case of a discovered check through the captured pawn.
    case EN_PASSANT : {
        Bitboard occupied = (pieces() ^ org ^ make_square(file_of(dst), rank_of(org))) | dst;
        return (pieces(stm, QUEEN, BISHOP) & attacks_bb<BISHOP>(ksq, occupied))
             | (pieces(stm, QUEEN, ROOK) & attacks_bb<ROOK>(ksq, occupied));
    }
    case CASTLING :
        // Castling is encoded as 'king captures the rook'
        return checks(ROOK) & rook_castle_sq(stm, org, dst);

    default :;  // NORMAL
    }
    return false;
}

bool Position::gives_dbl_check(Move m) const noexcept {
    assert(m.is_ok());

    Color stm = side_to_move();

    Square ksq = king_square(~stm);
    assert(pieces(~stm, KING) & ksq);

    const Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == stm);

    switch (m.type_of())
    {
    case NORMAL :
        return
          // Is there a direct check?
          (checks(type_of(piece_on(org))) & dst)
          // Is there a discovered check?
          && ((blockers(~stm) & org) && !aligned(org, dst, ksq));

    case PROMOTION :
        return (blockers(~stm) & org)
            && (attacks_bb(m.promotion_type(), dst, pieces() ^ org) & ksq);

    case EN_PASSANT : {
        Bitboard occupied = (pieces() ^ org ^ make_square(file_of(dst), rank_of(org))) | dst;
        auto checkerCnt = popcount((pieces(stm, QUEEN, BISHOP) & attacks_bb<BISHOP>(ksq, occupied))
                                   | (pieces(stm, QUEEN, ROOK) & attacks_bb<ROOK>(ksq, occupied)));
        return checkerCnt > 1 || (checkerCnt && (checks(PAWN) & dst));
    }
    default :;  // CASTLING
    }
    return false;
}

// Makes a move, and saves all information necessary to a StateInfo.
// The move is assumed to be legal.
void Position::do_move(Move m, StateInfo& newSt, bool givesCheck) noexcept {
    assert(m.is_ok() && pseudo_legal(m) && legal(m));
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

    Color stm = side_to_move();

    Square org = m.org_sq(), dst = m.dst_sq();
    // clang-format off
    Piece movedPiece    = piece_on(org);
    Piece capturedPiece = m.type_of() != EN_PASSANT ? piece_on(dst) : piece_on(dst - pawn_spush(stm));
    Piece promotedPiece = NO_PIECE;
    // clang-format on
    assert(color_of(movedPiece) == stm);
    assert(!is_ok(capturedPiece)
           || color_of(capturedPiece) == (m.type_of() != CASTLING ? ~stm : stm));
    assert(type_of(capturedPiece) != KING);

    if (m.type_of() == CASTLING)
    {
        assert(type_of(movedPiece) == KING);
        assert(capturedPiece == make_piece(stm, ROOK));

        Square rorg, rdst;
        do_castling<true>(stm, org, dst, rorg, rdst);
        st->hasCastled[stm] = true;
        k ^= Zobrist::psq[capturedPiece][rorg] ^ Zobrist::psq[capturedPiece][rdst];
        capturedPiece = NO_PIECE;
    }
    else if (is_ok(capturedPiece))
    {
        Square cap = dst;

        // If the captured piece is a pawn, update pawn hash key,
        // otherwise update non-pawn material.
        if (type_of(capturedPiece) == PAWN)
        {
            if (m.type_of() == EN_PASSANT)
            {
                cap -= pawn_spush(stm);

                assert(relative_rank(stm, org) == RANK_5);
                assert(relative_rank(stm, dst) == RANK_6);
                assert(type_of(movedPiece) == PAWN);
                assert(pieces(~stm, PAWN) & cap);
                assert(!(pieces() & (dst | (dst + pawn_spush(stm)))));
                assert(ep_square() == dst);
                assert(rule50_count() == 1);
            }

            st->pawnKey ^= Zobrist::psq[capturedPiece][cap];
        }
        else
        {
            st->nonPawnMaterial[~stm] -= PIECE_VALUE[capturedPiece];
        }
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

    // Update castling rights if needed
    if (std::uint8_t cr; castling_rights() && (cr = castling_rights_mask(org, dst)))
    {
        k ^= Zobrist::castling[castling_rights() & cr];
        st->castlingRights &= ~cr;
    }

    // Reset en-passant square
    if (is_ok_ep(ep_square()))
    {
        k ^= Zobrist::enpassant[file_of(ep_square())];
        st->epSquare = SQ_NONE;
    }

    // Move the piece. The tricky Chess960 castling is handled earlier
    if (m.type_of() != CASTLING)
    {
        dp.piece[0] = movedPiece;
        dp.org[0]   = org;
        dp.dst[0]   = dst;

        move_piece(org, dst);
    }

    // Update hash key
    k ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

    if (type_of(movedPiece) == KING)
        st->kingSquare[stm] = dst;

    // If the moving piece is a pawn do some special extra work
    if (type_of(movedPiece) == PAWN)
    {
        // Set en-passant square if the moved pawn can be captured
        if ((int(dst) ^ int(org)) == NORTH_2 && can_enpassant(~stm, dst - pawn_spush(stm)))
        {
            assert(relative_rank(stm, org) == RANK_2);
            assert(relative_rank(stm, dst) == RANK_4);

            st->epSquare = dst - pawn_spush(stm);
            k ^= Zobrist::enpassant[file_of(ep_square())];
        }
        else if (m.type_of() == PROMOTION)
        {
            assert(relative_rank(stm, org) == RANK_7);
            assert(relative_rank(stm, dst) == RANK_8);
            assert(KNIGHT <= m.promotion_type() && m.promotion_type() <= QUEEN);

            promotedPiece = make_piece(stm, m.promotion_type());

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
            st->nonPawnMaterial[stm] += PIECE_VALUE[promotedPiece];
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
    st->checkers = givesCheck ? attackers_to(king_square(~stm)) & pieces(stm) : 0;
    assert(!givesCheck || (checkers() && popcount(checkers()) <= 2));

    sideToMove = ~stm;

    // Update king attacks used for fast check detection
    set_ext_state();

    // Calculate the repetition info.
    // It is the ply distance from the previous occurrence of the same position,
    // negative in the 3-fold case, or zero when the position was not repeated.
    st->repetition = 0;

    auto end = std::min(rule50_count(), null_ply());
    if (end >= 4)
    {
        StateInfo* stp = st->previous->previous;
        for (auto i = 4; i <= end; i += 2)
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

    Color stm = sideToMove = ~side_to_move();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(dst);

    assert(empty_on(org) || m.type_of() == CASTLING);

    Piece capturedPiece = captured_piece();
    assert(type_of(capturedPiece) != KING);

    if (m.type_of() == CASTLING)
    {
        assert(pieces(stm, KING) & king_castle_sq(stm, org, dst));
        assert(pieces(stm, ROOK) & rook_castle_sq(stm, org, dst));
        assert(capturedPiece == NO_PIECE);

        Square rorg, rdst;
        do_castling<false>(stm, org, dst, rorg, rdst);
    }
    else
    {
        if (m.type_of() == PROMOTION)
        {
            assert(relative_rank(stm, org) == RANK_7);
            assert(relative_rank(stm, dst) == RANK_8);
            assert(KNIGHT <= m.promotion_type() && m.promotion_type() <= QUEEN);
            assert(type_of(pc) == m.promotion_type());
            //assert(promoted_piece() == pc);

            remove_piece(dst);
            pc = make_piece(stm, PAWN);
            put_piece(pc, dst);
        }

        move_piece(dst, org);  // Put the piece back at the source square

        if (is_ok(capturedPiece))
        {
            Square cap = dst;

            if (m.type_of() == EN_PASSANT)
            {
                cap -= pawn_spush(stm);

                assert(type_of(pc) == PAWN);
                assert(relative_rank(stm, org) == RANK_5);
                assert(relative_rank(stm, dst) == RANK_6);
                assert(empty_on(cap));
                assert(capturedPiece == make_piece(~stm, PAWN));
                assert(rule50_count() == 0);
                assert(st->previous->epSquare == dst);
                assert(st->previous->rule50 == 0);
            }

            put_piece(capturedPiece, cap);  // Restore the captured piece
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
void Position::do_null_move(StateInfo& newSt) noexcept {
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
    st->nullPly = 0;
    //st->checkers = 0;

    sideToMove = ~side_to_move();

    set_ext_state();

    st->repetition = 0;

    assert(pos_is_ok());
}

// Used to undo a "null move"
void Position::undo_null_move() noexcept {
    assert(!checkers());

    sideToMove = ~side_to_move();

    st = st->previous;

    assert(pos_is_ok());
}

// Computes the new hash key after the given move.
// Needed for speculative prefetch.
// It does recognize special moves like castling, en-passant and promotions.
Key Position::move_key(Move m) const noexcept {

    Color stm = side_to_move();

    const Square org = m.org_sq(), dst = m.dst_sq();

    Piece  movedPiece    = piece_on(org);
    Square cap           = m.type_of() != EN_PASSANT ? dst : dst - pawn_spush(stm);
    Piece  capturedPiece = piece_on(cap);
    assert(color_of(movedPiece) == stm);
    assert(!is_ok(capturedPiece)
           || color_of(capturedPiece) == (m.type_of() != CASTLING ? ~stm : stm));
    assert(type_of(capturedPiece) != KING);

    Key k =
      st->key ^ Zobrist::side ^ Zobrist::psq[movedPiece][org]
      ^ Zobrist::psq[m.type_of() != PROMOTION ? movedPiece : make_piece(stm, m.promotion_type())]
                    [m.type_of() != CASTLING ? dst : king_castle_sq(stm, org, dst)]
      ^ Zobrist::castling[castling_rights() & castling_rights_mask(org, dst)];

    if (is_ok_ep(ep_square()))
        k ^= Zobrist::enpassant[file_of(ep_square())];

    if (m.type_of() == CASTLING)
    {
        assert(type_of(movedPiece) == KING);
        assert(capturedPiece == make_piece(stm, ROOK));
        // ROOK
        k ^= Zobrist::psq[capturedPiece][dst]
           ^ Zobrist::psq[capturedPiece][rook_castle_sq(stm, org, dst)];
        capturedPiece = NO_PIECE;
    }
    else if (is_ok(capturedPiece))
    {
        k ^= Zobrist::psq[capturedPiece][cap];
    }
    else if (type_of(movedPiece) == PAWN && (int(dst) ^ int(org)) == NORTH_2
             && can_enpassant(~stm, dst - pawn_spush(stm), true))
    {
        assert(relative_rank(stm, org) == RANK_2);
        assert(relative_rank(stm, dst) == RANK_4);
        k ^= Zobrist::enpassant[file_of(dst - pawn_spush(stm))];
    }

    return is_ok(capturedPiece) || type_of(movedPiece) == PAWN ? k : adjust_key(k, 1);
}

Key Position::null_move_key() const noexcept {

    Key k = st->key ^ Zobrist::side;
    if (is_ok_ep(ep_square()))
        k ^= Zobrist::enpassant[file_of(ep_square())];

    return adjust_key(k, 1);
}

// Tests if the SEE (Static Exchange Evaluation) value of the move
// is greater or equal to the given threshold.
// An algorithm similar to alpha-beta pruning with a null window.
bool Position::see_ge(Move m, int threshold) const noexcept {
    assert(m.is_ok());

    // Not deal with castling, can't win any material, nor can lose any.
    if (m.type_of() == CASTLING)
        return threshold <= 0;

    Color stm = side_to_move();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == stm);

    Bitboard occupied = pieces();

    Square cap = dst;
    if (m.type_of() == EN_PASSANT)
    {
        cap -= pawn_spush(stm);
        occupied ^= cap;
    }

    int swap;

    swap = PIECE_VALUE[piece_on(cap)] - threshold;
    // If promotion, get the promoted piece and lose the pawn
    if (m.type_of() == PROMOTION)
        swap += PIECE_VALUE[m.promotion_type()] - PIECE_VALUE[PAWN];
    if (swap < 0)
        return false;

    Piece movedPiece =
      m.type_of() == PROMOTION ? make_piece(stm, m.promotion_type()) : piece_on(org);
    swap = PIECE_VALUE[movedPiece] - swap;
    if (swap <= 0)
        return true;

    bool win = true;

    bool enpassant = false;
    if (type_of(movedPiece) == PAWN && (int(dst) ^ int(org)) == NORTH_2
        && can_enpassant(~stm, dst - pawn_spush(stm), true))
    {
        assert(relative_rank(stm, org) == RANK_2);
        assert(relative_rank(stm, dst) == RANK_4);
        dst -= pawn_spush(stm);
        enpassant = true;
    }
    // It doesn't matter if the destination square is occupied or not
    // xoring to is important for pinned piece logic
    occupied = occupied ^ org ^ dst;

    Bitboard qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
    Bitboard qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

    bool discovery[COLOR_NB]{true, true};

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
                swap = PIECE_VALUE[piece_on(org)] - swap;
                if ((swap = PIECE_VALUE[piece_on(dst)] - swap) < win)
                    break;

                occupied ^= dst;

                qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
                qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

                attackers    = attackers_to(dst, occupied) & occupied;
                stmAttackers = pieces(stm) & attackers;
            }

            if (!stmAttackers)
                break;
        }

        win = !win;

        if (!enpassant && discovery[stm] && (b = blockers(~stm) & stmAttackers))
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
                win = !win;
                continue;  // Resume without considering discovery
            }

            if (pt == KING)
            {
                if (!(attackers & pieces(~stm)))
                    break;

                discovery[stm] = false;

                stm = ~stm;
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
                    attackers |= qB & attacks_bb<BISHOP>(dst, occupied);
                break;
            case ROOK :
                qR &= occupied;
                if (qR)
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
            if ((swap = VALUE_PAWN - swap) < win)
                break;
            occupied ^= org = lsb(b);
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(dst, occupied);
            enpassant = false;
        }
        else if ((b = pieces(KNIGHT) & stmAttackers))
        {
            if ((swap = VALUE_KNIGHT - swap) < win)
                break;
            occupied ^= org = lsb(b);
        }
        else if ((b = pieces(BISHOP) & stmAttackers))
        {
            if ((swap = VALUE_BISHOP - swap) < win)
                break;
            occupied ^= org = lsb(b);
            qB &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(dst, occupied);
        }
        else if ((b = pieces(ROOK) & stmAttackers))
        {
            if ((swap = VALUE_ROOK - swap) < win)
                break;
            occupied ^= org = lsb(b);
            qR &= occupied;
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(dst, occupied);
        }
        else if ((b = pieces(QUEEN) & stmAttackers))
        {
            if ((swap = VALUE_QUEEN - swap) < win)
                break;
            occupied ^= org = lsb(b);
            qB &= occupied;
            qR &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(dst, occupied);
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(dst, occupied);
        }
        else  // KING
        {
            // If "capture" with the king but the opponent still has attackers, reverse the result.
            win ^= bool(pieces(~stm) & attackers);
            break;
        }

        attackers &= occupied;
    }

    return win;
}

// Tests whether the current position is drawn by repetition or by 50-move rule.
// It also detect stalemates.
bool Position::is_draw(std::int16_t ply, bool checkStaleMate) const noexcept {

    return  // Draw by Repetition: position repeats once earlier but strictly
            // after the root, or repeats twice before or at the root.
      /**/ (repetition() && repetition() < ply)
      // Draw by 50-move rule
      || (rule50_count() >= 2 * DrawMoveCount
          && (!checkers() || MoveList<LEGAL>(*this).size() != 0))
      // Draw by Stalemate
      || (checkStaleMate && !checkers() && MoveList<LEGAL>(*this).size() == 0);
}

// Tests whether there has been at least one repetition
// of positions since the last capture or pawn move.
bool Position::has_repeated() const noexcept {
    auto end = std::min(rule50_count(), null_ply());
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

// Tests if the current position has a move which draws by repetition.
// Accurately matches the outcome of is_draw() over all legal moves.
Move Position::upcoming_repetition(std::int16_t ply) const noexcept {
    auto end = std::min(rule50_count(), null_ply());
    // Enough reversible moves played
    if (end < 3)
        return Move::None();

    StateInfo* stp = st->previous;

    Key baseKey = st->key;
    Key iterKey = baseKey ^ stp->key ^ Zobrist::side;

    Bitboard occupied = pieces();

    for (auto i = 3; i <= end; i += 2)
    {
        iterKey ^= stp->previous->key ^ stp->previous->previous->key ^ Zobrist::side;
        stp = stp->previous->previous;

        // Opponent pieces have reverted
        if (iterKey != 0)
            continue;

        Key moveKey = baseKey ^ stp->key;
        // ‘moveKey’ is a single move
        Key16 index = Cuckoos.find_index(moveKey);
        if (index >= Cuckoos.size())
            continue;

        Move m = Cuckoos[index].move;
        assert(m != Move::None());
        Square s1 = m.org_sq();
        Square s2 = m.dst_sq();

        // Move path is obstructed
        if (occupied & ex_between_bb(s1, s2))
            continue;

        // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in the same location
        if (empty_on(s1))
            m = Move(s2, s1);
        assert(MoveList<LEGAL>(*this).contains(m));

        if (i < ply
            // For nodes before or at the root, check that the move is
            // a repetition rather than a move to the current position.
            || stp->repetition)
            return m;
    }
    return Move::None();
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
        key ^= Zobrist::psq[piece_on(s)][s];
    }

    key ^= Zobrist::castling[castling_rights()];

    if (is_ok_ep(ep_square()))
        key ^= Zobrist::enpassant[file_of(ep_square())];

    if (side_to_move() == BLACK)
        key ^= Zobrist::side;

    return key;
}

// Performs some consistency checks for the position object
// and raise an assert if something wrong is detected.
// This is meant to be helpful when debugging.
bool Position::pos_is_ok() const noexcept {

    constexpr bool Fast = true;  // Quick (default) or full check?

    if ((side_to_move() != WHITE && side_to_move() != BLACK)   //
        || pieceCount[W_KING] != 1 || pieceCount[B_KING] != 1  //
        || piece_on(king_square(WHITE)) != W_KING              //
        || piece_on(king_square(BLACK)) != B_KING              //
        || distance(king_square(WHITE), king_square(BLACK)) <= 1
        || (is_ok_ep(ep_square())  //
            && relative_rank(side_to_move(), ep_square()) != RANK_6
            && !can_enpassant(side_to_move(), ep_square())))
        assert(false && "Position::pos_is_ok(): Default");

    if (Fast)
        return true;

    if (st->key != compute_key())
        assert(false && "Position::pos_is_ok(): Key");

    if (attackers_to(king_square(~side_to_move())) & pieces(side_to_move()))
        assert(false && "Position::pos_is_ok(): King Checker");

    if ((pieces(PAWN) & PROMOTION_RANK_BB) || pieceCount[W_PAWN] > 8 || pieceCount[B_PAWN] > 8)
        assert(false && "Position::pos_is_ok(): Pawns");

    for (Color c : {WHITE, BLACK})
        if (count<PAWN>(c)                         //
              + std::max(count<KNIGHT>(c) - 2, 0)  //
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
#endif

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
        ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

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
