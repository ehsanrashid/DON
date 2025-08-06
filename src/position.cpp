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
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "memory.h"
#include "misc.h"
#include "movegen.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace DON {

namespace {

constexpr std::size_t PawnOffset = 8;

// clang-format off
constexpr std::array<Piece, COLOR_NB * KING> Pieces{
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING
};

const inline std::array<std::unique_ptr<const std::int8_t[]>, PIECE_TYPE_NB - 1> MobilityBonus{
  nullptr,
  make_array<20>({ -2,  0,  2,  4,  6,  8, 10, 12, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}),                                 // PAWN
  make_array< 9>({-75,-61,-14, -2,  9, 20, 31, 41, 48}),                                                                             // KNIGHT
  make_array<14>({-50,-24, 15, 30, 42, 55, 58, 63, 67, 73, 83, 88, 97,102}),                                                         // BISHOP
  make_array<15>({-68,-25,  2, 23, 38, 57, 63, 76, 87, 90, 97,102,111,114,117}),                                                     // ROOK
  make_array<28>({-40,-23, -9,  3, 19, 26, 32, 39, 45, 57, 69, 72, 73, 74, 76, 77, 78, 80, 83, 86,100,109,113,116,120,123,125,127}), // QUEEN
  make_array< 9>({ -8, -4,  0,  4,  7,  9, 11, 13, 15})                                                                              // KING
};
// clang-format on

// Implements Marcel van Kervinck's cuckoo algorithm to detect repetition of positions for 3-fold repetition draws.
// The algorithm uses hash tables with Zobrist hashes to allow fast detection of recurring positions.
// For details see:
// http://web.archive.org/web/20201107002606/https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// Cuckoo table with Zobrist hashes of valid reversible moves, and the moves themselves
struct Cuckoo final {

    constexpr bool empty() const noexcept { return key == 0; }

    Key  key;
    Move move;
};

// Cuckoo table with Zobrist hashes of valid reversible moves, and the moves themselves
template<std::size_t Size>
class CuckooTable final {
    static_assert(exactly_one(Size), "Size has to be a power of 2");

   public:
    CuckooTable() noexcept                              = default;
    CuckooTable(const CuckooTable&) noexcept            = delete;
    CuckooTable(CuckooTable&&) noexcept                 = delete;
    CuckooTable& operator=(const CuckooTable&) noexcept = delete;
    CuckooTable& operator=(CuckooTable&&) noexcept      = delete;

    constexpr auto begin() const noexcept { return cuckoos.begin(); }
    constexpr auto end() const noexcept { return cuckoos.end(); }
    constexpr auto begin() noexcept { return cuckoos.begin(); }
    constexpr auto end() noexcept { return cuckoos.end(); }

    constexpr auto size() const noexcept { return cuckoos.size(); }
    constexpr auto empty() const noexcept { return cuckoos.empty(); }

    constexpr auto& operator[](std::size_t idx) const noexcept { return cuckoos[idx]; }
    constexpr auto& operator[](std::size_t idx) noexcept { return cuckoos[idx]; }

    // Hash function for indexing the cuckoo table
    template<unsigned Index>
    constexpr std::size_t H(Key key) const noexcept {
        return (key >> (16 * Index)) & (size() - 1);
    }

    constexpr void fill(Cuckoo&& cuckoo) noexcept { cuckoos.fill(std::move(cuckoo)); }

    constexpr void init() noexcept {
        fill({0, Move::None});
        count = 0;
    }

    void insert(Cuckoo cuckoo) noexcept {
        std::size_t index = H<0>(cuckoo.key);
        while (true)
        {
            std::swap(cuckoos[index], cuckoo);
            if (cuckoo.empty())  // Arrived at empty slot?
                break;
            index ^= H<0>(cuckoo.key) ^ H<1>(cuckoo.key);  // Push victim to alternative slot
        }
        ++count;
    }

    std::size_t find_key(Key key) const noexcept {
        std::size_t index;
        if (index = H<0>(key); cuckoos[index].key == key)
            return index;
        if (index = H<1>(key); cuckoos[index].key == key)
            return index;
        return size();
    }

   private:
    std::array<Cuckoo, Size> cuckoos;

   public:
    std::size_t count = 0;
};

CuckooTable<0x2000> Cuckoos;

}  // namespace

namespace Zobrist {

Key psq[PIECE_NB][SQUARE_NB];
Key castling[CASTLING_RIGHTS_NB];
Key enpassant[FILE_NB];
Key side;

void init() noexcept {
    PRNG1024 rng(0x105524ull);

    std::memset(psq, 0, sizeof(psq));
    for (Piece pc : Pieces)
    {
        std::size_t offset = PawnOffset * (type_of(pc) == PAWN);
        for (std::size_t s = 0 + offset; s < std::size(psq[pc]) - offset; ++s)
            psq[pc][s] = rng.rand<Key>();
    }

    for (std::size_t cr = 0; cr < std::size(castling); ++cr)
    {
        castling[cr] = 0;

        Bitboard b = cr;
        while (b)
        {
            Key k = castling[square_bb(pop_lsb(b))];
            castling[cr] ^= (k != 0) ? k : rng.rand<Key>();
        }
    }

    for (std::size_t f = 0; f < std::size(enpassant); ++f)
        enpassant[f] = rng.rand<Key>();

    side = rng.rand<Key>();
}
}  // namespace Zobrist

std::uint8_t DrawMoveCount = 50;

bool Chess960 = false;

// Called at startup to initialize the Zobrist arrays used to compute hash keys
void Position::init() noexcept {

    Zobrist::init();

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
                    Key  key  = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side;
                    Move move = Move(s1, s2);
                    Cuckoos.insert({key, move});
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
    std::memset(static_cast<void*>(this), 0, sizeof(Position));
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
        }
        else if (std::isdigit(token))
        {
            int f = char_to_digit(token);
            assert(1 <= f && f <= 8 - file && "Position::set(): Invalid File");
            file += f;  // Advance the given number of file
        }
        else
        {
            Piece pc = UCI::piece(token);
            if (pc != NO_PIECE)
            {
                assert(file < FILE_NB);
                Square sq = make_square(file, rank);
                put_piece(sq, pc);
                if (type_of(pc) == KING)
                {
                    assert(!is_ok(st->kingSquare[color_of(pc)]));
                    st->kingSquare[color_of(pc)] = sq;
                }
                ++file;
            }
            else
            {
                assert(false && "Position::set(): Invalid Piece");
            }
        }
    }
    assert(file <= FILE_NB && rank == RANK_1);
    assert(count(W_ALL) <= 16 && count(B_ALL) <= 16);
    assert(count(W_ALL) + count(B_ALL) == count<ALL_PIECE>());
    assert(count(W_PAWN) <= 8 && count(B_PAWN) <= 8);
    assert(count(W_KING) == 1 && count(B_KING) == 1);
    assert(is_ok(king_square(WHITE)) && is_ok(king_square(BLACK)));
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
        if (castlingRightsCount > 4)
            continue;

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

        Square rsq = SQ_NONE;

        token = std::tolower(token);

        if (token == 'k')
        {
            rsq = relative_square(c, SQ_H1);
            while (file_of(rsq) >= FILE_C && !(rooks & rsq) && rsq != king_square(c))
                --rsq;
        }
        else if (token == 'q')
        {
            rsq = relative_square(c, SQ_A1);
            while (file_of(rsq) <= FILE_F && !(rooks & rsq) && rsq != king_square(c))
                ++rsq;
        }
        else if ('a' <= token && token <= 'h')
        {
            rsq = make_square(File(token - 'a'), relative_rank(c, RANK_1));
        }
        else
        {
            assert(false && "Position::set(): Invalid Castling Rights");
            continue;
        }

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

    iss >> token;
    if (token != '-')
    {
        std::uint8_t epFile = std::tolower(token);
        std::uint8_t epRank;
        iss >> epRank;

        if ('a' <= epFile && epFile <= 'h' && epRank == (ac == WHITE ? '6' : '3'))
        {
            st->epSquare = make_square(File(epFile - 'a'), Rank(epRank - '1'));

            // En-passant square will be considered only if
            // a) there is an enemy pawn in front of epSquare
            // b) there is no piece on epSquare or behind epSquare
            // c) there is atleast one friend pawn threatening epSquare
            // d) there is no enemy Bishop, Rook or Queen pinning
            enpassant = (pieces(~ac, PAWN) & (ep_square() - pawn_spush(ac)))
                     && !(pieces() & make_bitboard(ep_square(), ep_square() + pawn_spush(ac)))
                     && (pieces(ac, PAWN) & attacks_bb<PAWN>(ep_square(), ~ac))
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

    st->rule50 = std::abs(rule50);
    // Convert from moveNum starting from 1 to posPly starting from 0,
    // handle also common incorrect FEN with moveNum = 0.
    gamePly = std::max(2 * (std::abs(moveNum) - 1), 0) + (ac == BLACK);

    // Reset illegal values
    if (is_ok(ep_square()))
    {
        reset_rule50_count();
        if (!enpassant)
            reset_ep_square();
    }
    assert(rule50_count() <= 100);
    gamePly = std::max(ply(), rule50_count());

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

    std::string fenStr = "8/" + sides[WHITE] + digit_to_char(8 - sides[WHITE].length())
                       + "/8/8/8/8/" + sides[BLACK] + digit_to_char(8 - sides[BLACK].length())
                       + "/8 w - - 0 1";

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
        unsigned emptyCount = 0;

        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            Square s = make_square(f, r);

            if (empty_on(s))
                ++emptyCount;
            else
            {
                if (emptyCount)
                {
                    oss << emptyCount;
                    emptyCount = 0;
                }
                oss << UCI::piece(piece_on(s));
            }
        }

        // Handle trailing empty squares
        if (emptyCount)
            oss << emptyCount;

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
        oss << "-";

    oss << ' ' << (is_ok(ep_square()) ? UCI::square(ep_square()) : "-");
    if (full)
        oss << ' ' << rule50_count() << ' ' << move_num();

    return oss.str();
}

// Sets castling rights given the corresponding color and the rook starting square.
void Position::set_castling_rights(Color c, Square rorg) noexcept {
    assert(relative_rank(c, rorg) == RANK_1);
    assert((pieces(c, ROOK) & rorg));
    assert(castlingRightsMask[c * FILE_NB + file_of(rorg)] == 0);

    Square korg = king_square(c);
    assert(relative_rank(c, korg) == RANK_1);
    assert((pieces(c, KING) & korg));

    int cr = make_castling_rights(c, korg, rorg);

    std::size_t crLsb = lsb(cr);
    assert(crLsb < std::size(castlingRookSquare));
    assert(!is_ok(castlingRookSquare[crLsb]));

    st->castlingRights |= cr;
    castlingRightsMask[c * FILE_NB + file_of(korg)] |= cr;
    castlingRightsMask[c * FILE_NB + file_of(rorg)] = cr;

    castlingRookSquare[crLsb] = rorg;

    Square kdst = king_castle_sq(c, korg, rorg);
    Square rdst = rook_castle_sq(c, korg, rorg);

    castlingPath[crLsb] =
      (between_bb(korg, kdst) | between_bb(rorg, rdst)) & ~make_bitboard(korg, rorg);
}

// Computes the hash keys of the position, and other data
// that once computed is updated incrementally as moves are made.
// The function is only used when a new position is set up.
void Position::set_state() noexcept {
    assert(st->pawnKey[WHITE] == 0 && st->pawnKey[BLACK] == 0);
    assert(st->groupKey[WHITE][0] == 0 && st->groupKey[BLACK][0] == 0);
    assert(st->groupKey[WHITE][1] == 0 && st->groupKey[BLACK][1] == 0);
    assert(st->nonPawnMaterial[WHITE] == VALUE_ZERO && st->nonPawnMaterial[BLACK] == VALUE_ZERO);
    assert(st->key == 0);

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
            assert(Zobrist::psq[pc][s] != 0);
            st->pawnKey[color_of(pc)] ^= Zobrist::psq[pc][s];
        }
        else if (pt != KING)
        {
            st->groupKey[color_of(pc)][is_major(pt)] ^= Zobrist::psq[pc][s];
            st->nonPawnMaterial[color_of(pc)] += PIECE_VALUE[pt];
        }
    }

    st->key ^= Zobrist::castling[castling_rights()];

    if (is_ok(ep_square()))
        st->key ^= Zobrist::enpassant[file_of(ep_square())];

    Color ac = active_color();

    if (ac == BLACK)
        st->key ^= Zobrist::side;

    st->checkers = attackers_to(king_square(ac)) & pieces(~ac);

    set_ext_state();
}

// Set extra state to detect if a move is check
void Position::set_ext_state() noexcept {

    Bitboard occupied = pieces();

    // clang-format off
    st->checks[PAWN  ] = attacks_bb<PAWN>(king_square(~active_color()), ~active_color());
    st->checks[KNIGHT] = attacks_bb<KNIGHT>(king_square(~active_color()));
    st->checks[BISHOP] = attacks_bb<BISHOP>(king_square(~active_color()), occupied);
    st->checks[ROOK  ] = attacks_bb<ROOK  >(king_square(~active_color()), occupied);
    st->checks[QUEEN ] = checks(BISHOP) | checks(ROOK);
    st->checks[KING  ] = 0;

    st->pinners[WHITE] = st->pinners[BLACK] = 0;

    for (Color c : {WHITE, BLACK})
    {
        // Calculates st->blockers[c] and st->pinners[],
        // which store respectively the pieces preventing king of color c from being in check
        // and the slider pieces of color ~c pinning pieces of color c to the king.
        st->blockers[c] = 0;

        // Snipers are xsliders enemies that attack 'king' when other snipers are removed
        Bitboard xsnipers  = xslide_attackers_to(king_square(c)) & pieces(~c);
        Bitboard xoccupied = occupied ^ xsnipers;

        while (xsnipers)
        {
            Square xsniper = pop_lsb(xsnipers);

            Bitboard blocker = between_bb(king_square(c), xsniper) & xoccupied;
            if (exactly_one(blocker))
            {
                st->blockers[c] |= blocker;
                st->pinners[blocker & pieces(c) ? ~c : c] |= xsniper;
            }
        }

        st->mobility[c] = MobilityBonus[PAWN][popcount(push_pawn_bb(pieces(c, PAWN), c) & ~occupied)];
        st->attacks[c][PAWN] = attacks_mob_by<PAWN>(c, 0, pieces(~c), occupied);
    }

    for (Color c : {WHITE, BLACK})
    {
        Bitboard target = ~((attacks<PAWN>(~c))
                          | (pieces(~c) & (pinners()))
                          | (pieces( c) & ((blockers(c))
                                         | (pieces(QUEEN, KING))
                                         | (pieces(PAWN) & (LOW_RANK_BB[c] | (push_pawn_bb(occupied, ~c) & ~attacks_pawn_bb(pieces(~c) & ~pieces(KING), ~c)))))));

        st->attacks[c][KNIGHT] = st->attacks[c][PAWN  ] | attacks_mob_by<KNIGHT>(c, blockers(c), target, occupied                                                                                             );
        st->attacks[c][BISHOP] = st->attacks[c][KNIGHT] | attacks_mob_by<BISHOP>(c, blockers(c), target, occupied ^ ((pieces(c, QUEEN, BISHOP) & ~blockers(c)) | (pieces(~c, KING, QUEEN, ROOK) & ~pinners())));
        st->attacks[c][ROOK  ] = st->attacks[c][BISHOP] | attacks_mob_by<ROOK  >(c, blockers(c), target, occupied ^ ((pieces(c, QUEEN, ROOK  ) & ~blockers(c)) | (pieces(~c, KING, QUEEN      ) & ~pinners())));
        st->attacks[c][QUEEN ] = st->attacks[c][ROOK  ] | attacks_mob_by<QUEEN >(c, blockers(c), target, occupied ^ ((pieces(c, QUEEN        ) & ~blockers(c)) | (pieces(~c, KING             )             )));
        st->attacks[c][KING  ] =                          attacks_mob_by<KING  >(c, blockers(c), target, occupied                                                                                             );

        st->attacks[~c][EXT_PIECE] = (attacks<PAWN >(c) & pieces(~c, KNIGHT, BISHOP))
                                   | (attacks<MINOR>(c) & pieces(~c, ROOK          ))
                                   | (attacks<ROOK >(c) & pieces(~c, QUEEN         ));
    }
    // clang-format on
}

template<PieceType PT>
Bitboard
Position::attacks_mob_by(Color c, Bitboard blockers, Bitboard target, Bitboard occupied) noexcept {

    Bitboard attacks;
    if constexpr (PT == PAWN)
    {
        attacks = attacks_pawn_bb(pieces(c, PAWN), c);
        st->mobility[c] += MobilityBonus[PAWN][popcount(attacks & target)];
    }
    else
    {
        attacks = 0;

        Square ksq = king_square(c);

        Bitboard pc = pieces<PT>(c, ksq, blockers);
        while (pc)
        {
            Square s = pop_lsb(pc);

            Bitboard atks = attacks_bb<PT>(s, occupied);
            if (PT != KNIGHT && PT != KING && (blockers & s))
                atks &= line_bb(ksq, s);
            st->mobility[c] += MobilityBonus[PT][popcount(atks & target)];
            attacks |= atks;
        }
    }
    return attacks;
}

// Check can do en-passant
bool Position::can_enpassant(Color           ac,
                             Square          epSq,
                             bool            before,
                             Bitboard* const epAttackers) const noexcept {
    assert(is_ok(epSq));

    //if (epAttackers != nullptr)
    //    *epAttackers = 0;

    // En-passant attackers
    Bitboard attackers = pieces(ac, PAWN) & attacks_bb<PAWN>(epSq, ~ac);
    if (!attackers)
        return false;

    Square capSq = epSq + (before ? +1 : -1) * pawn_spush(ac);
    assert(pieces(~ac, PAWN) & capSq);

    if (!before)
    {
        // If there are checkers other than the to be captured pawn, ep is never legal
        if (checkers() & ~square_bb(capSq))
            return false;

        // If there are two pawns potentially being abled to capture and both are pinned.
        if (more_than_one(blockers(ac) & attackers))
        {
            Bitboard kingFile = file_bb(king_square(ac));
            // If there is no pawn on our king's file and thus both pawns are pinned by bishops.
            if (!(attackers & kingFile))
                return false;

            attackers &= ~kingFile;
        }
    }

    if (epAttackers != nullptr)
        *epAttackers = attackers;

    bool enpassant = false;
    // Check en-passant is legal for the position
    Bitboard occupied = pieces() ^ make_bitboard(capSq, epSq);
    while (attackers)
    {
        Square s = pop_lsb(attackers);
        if (!(slide_attackers_to(king_square(ac), occupied ^ s) & pieces(~ac)))
        {
            enpassant = true;
            if (epAttackers == nullptr)
                break;
        }
        else if (epAttackers != nullptr)
            *epAttackers ^= s;
    }
    return enpassant;
}

// Helper used to do/undo a castling move.
// This is a bit tricky in Chess960 where org/dst squares can overlap.
template<bool Do>
void Position::do_castling(
  Color ac, Square org, Square& dst, Square& rorg, Square& rdst, DirtyPiece* const dp) noexcept {

    rorg = dst;  // Castling is encoded as "king captures rook"
    rdst = rook_castle_sq(ac, org, dst);
    dst  = king_castle_sq(ac, org, dst);

    Piece king = piece_on(Do ? org : dst);
    assert(king == make_piece(ac, KING));
    Piece rook = piece_on(Do ? rorg : rdst);
    assert(rook == make_piece(ac, ROOK));

    bool kingMoved = org != dst;
    bool rookMoved = rorg != rdst;

    assert(!Do || dp != nullptr);

    if constexpr (Do)
    {
        dp->dst = dst;

        if (rookMoved)
        {
            dp->removePc = dp->addPc = rook;
            dp->removeSq             = rorg;
            dp->addSq                = rdst;
        }

        st->kingSquare[ac] = dst;
        st->castled[ac]    = true;
    }

    // Remove both pieces first since squares could overlap in Chess960
    if (kingMoved)
        remove_piece(Do ? org : dst);
    if (rookMoved)
        remove_piece(Do ? rorg : rdst);
    if (kingMoved)
        put_piece(Do ? dst : org, king);
    if (rookMoved)
        put_piece(Do ? rdst : rorg, rook);
}

// Makes a move, and saves all necessary information to new state.
// The move is assumed to be legal.
DirtyPiece Position::do_move(const Move& m, State& newSt, bool check) noexcept {
    assert(legal(m));
    assert(&newSt != st);

    Key k = st->key ^ Zobrist::side;

    // Copy relevant fields from the old state to the new state,
    // excluding those that will recomputed from scratch anyway and
    // then switch the state pointer to point to the new state.
    std::memcpy(&newSt, st, offsetof(State, key));
    newSt.preState = st;

    st = &newSt;

    // Increment ply counters. In particular, rule50 will be reset to zero later on
    // in case of a capture or a pawn move.
    ++gamePly;
    ++st->rule50;
    st->rule50High = st->rule50High || st->rule50 >= rule50_threshold();

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

    DirtyPiece dp;
    dp.pc    = movedPiece;
    dp.org   = org;
    dp.dst   = dst;
    dp.addSq = SQ_NONE;

    // Reset en-passant square
    if (is_ok(ep_square()))
    {
        k ^= Zobrist::enpassant[file_of(ep_square())];
        reset_ep_square();
    }

    bool epCheck = false;

    // If the move is a castling, do some special work
    if (m.type_of() == CASTLING)
    {
        assert(pt == KING);
        assert(capturedPiece == make_piece(ac, ROOK));
        assert(!st->castled[ac]);

        Square rorg, rdst;
        do_castling<true>(ac, org, dst, rorg, rdst, &dp);
        assert(rorg == m.dst_sq());

        // Update castling rights
        int cr = ac & ANY_CASTLING;
        assert(castling_rights() & cr);
        k ^= Zobrist::castling[castling_rights() & cr];
        st->castlingRights &= ~cr;

        // clang-format off
        k                   ^= Zobrist::psq[capturedPiece][rorg] ^ Zobrist::psq[capturedPiece][rdst];
        st->groupKey[ac][1] ^= Zobrist::psq[capturedPiece][rorg] ^ Zobrist::psq[capturedPiece][rdst];
        capturedPiece = NO_PIECE;
        // clang-format on

        // Calculate checker only one ROOK possible (if move is check)
        st->checkers = check ? square_bb(rdst) : 0;
        assert(!check || (checkers() & rdst));

        goto DO_MOVE_END;
    }

    if (capturedPiece != NO_PIECE)
    {
        auto captured = type_of(capturedPiece);

        Square capSq = dst;
        // If the captured piece is a pawn, update pawn hash key,
        // otherwise update non-pawn material.
        if (captured == PAWN)
        {
            if (m.type_of() == EN_PASSANT)
            {
                capSq -= pawn_spush(ac);

                assert(relative_rank(ac, org) == RANK_5);
                assert(relative_rank(ac, dst) == RANK_6);
                assert(pt == PAWN);
                assert(pieces(~ac, PAWN) & capSq);
                assert(!(pieces() & make_bitboard(dst, dst + pawn_spush(ac))));
                assert(!is_ok(ep_square()));  // Already reset to SQ_NONE
                assert(rule50_count() == 1);
                assert(st->preState->epSquare == dst);
                assert(st->preState->rule50 == 0);
            }

            st->pawnKey[~ac] ^= Zobrist::psq[capturedPiece][capSq];
        }
        else
        {
            // clang-format off
            st->groupKey[~ac][is_major(captured)] ^= Zobrist::psq[capturedPiece][capSq];
            st->nonPawnMaterial[~ac]              -= PIECE_VALUE[captured];
            // clang-format on
        }

        dp.removePc = capturedPiece;
        dp.removeSq = capSq;

        // Remove the captured piece
        remove_piece(capSq);
        st->capSquare = dst;
        // Update hash key
        k ^= Zobrist::psq[capturedPiece][capSq];
        // Reset rule 50 draw counter
        reset_rule50_count();
    }
    else
        dp.removeSq = SQ_NONE;

    move_piece(org, dst);

    // Update castling rights if needed
    if (int cr; castling_rights() && (cr = castling_rights_mask(org, dst)))
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

            dp.addPc = promotedPiece;
            dp.addSq = dst;
            dp.dst   = SQ_NONE;

            remove_piece(dst);
            put_piece(dst, promotedPiece);
            assert(count(promotedPiece) != 0);
            assert(Zobrist::psq[movedPiece][dst] == 0);
            // Update hash keys
            // clang-format off
            k                                    ^= Zobrist::psq[promotedPiece][dst];
            st->groupKey[ac][is_major(promoted)] ^= Zobrist::psq[promotedPiece][dst];
            st->nonPawnMaterial[ac]              += PIECE_VALUE[promoted];
            // clang-format on
        }
        // Set en-passant square if the moved pawn can be captured
        else if ((int(dst) ^ int(org)) == NORTH_2)
        {
            assert(relative_rank(ac, org) == RANK_2);
            assert(relative_rank(ac, dst) == RANK_4);
            epCheck = true;
        }

        // Update pawn hash key
        st->pawnKey[ac] ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

        // Reset rule 50 draw counter
        reset_rule50_count();
    }
    else
    {
        if (pt == KING)
            st->kingSquare[ac] = dst;
        else
            st->groupKey[ac][is_major(pt)] ^=
              Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
    }

    // Calculate checkers (if move is check)
    st->checkers = check ? attackers_to(king_square(~ac)) & pieces(ac) : 0;
    assert(!check || (checkers() && popcount(checkers()) <= 2));

DO_MOVE_END:

    // Update hash key
    k ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

    st->capturedPiece = capturedPiece;
    st->promotedPiece = promotedPiece;

    activeColor = ~ac;

    // Update king attacks used for fast check detection
    set_ext_state();

    if (epCheck && can_enpassant(active_color(), dst - pawn_spush(ac)))
    {
        st->epSquare = dst - pawn_spush(ac);
        k ^= Zobrist::enpassant[file_of(ep_square())];
    }

    // Set the key with the updated key
    st->key = k;

    // Calculate the repetition info.
    // It is the ply distance from the previous occurrence of the same position,
    // negative in the 3-fold case, or zero when the position was not repeated.
    st->repetition = 0;

    auto end = std::min(rule50_count(), null_ply());
    if (end >= 4)
    {
        auto* pst = st->preState->preState;
        for (auto i = 4; i <= end; i += 2)
        {
            pst = pst->preState->preState;
            if (pst->key == st->key)
            {
                st->repetition = pst->repetition != 0 ? -i : +i;
                break;
            }
        }
    }

    assert(pos_is_ok());

    assert(is_ok(dp.pc));
    assert(is_ok(dp.org));
    //assert(!(capturedPiece != NO_PIECE || m.type_of() == CASTLING) ^ is_ok(dp.removeSq));
    //assert(!is_ok(dp.addSq) ^ (m.type_of() == PROMOTION || m.type_of() == CASTLING));
    return dp;
}

// Unmakes a move, restoring the position to its exact state before the move was made.
void Position::undo_move(const Move& m) noexcept {

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
        Square capSq = dst;

        if (m.type_of() == EN_PASSANT)
        {
            capSq -= pawn_spush(ac);

            assert(type_of(pc) == PAWN);
            assert(relative_rank(ac, org) == RANK_5);
            assert(relative_rank(ac, dst) == RANK_6);
            assert(empty_on(capSq));
            assert(capturedPiece == make_piece(~ac, PAWN));
            assert(rule50_count() == 0);
            assert(st->preState->epSquare == dst);
            assert(st->preState->rule50 == 0);
        }
        // Restore the captured piece
        put_piece(capSq, capturedPiece);
    }

UNDO_MOVE_END:

    --gamePly;
    // Finally point our state pointer back to the previous state
    st = st->preState;

    assert(legal(m));
    assert(pos_is_ok());
}

// Makes a null move
// It flips the active color without executing any move on the board.
void Position::do_null_move(State& newSt) noexcept {
    assert(&newSt != st);
    assert(!checkers());

    std::memcpy(&newSt, st, offsetof(State, preState));
    newSt.preState = st;

    st = &newSt;

    st->capturedPiece = NO_PIECE;
    st->promotedPiece = NO_PIECE;
    st->capSquare     = SQ_NONE;

    // NOTE: no ++st->rule50 here
    st->nullPly = 0;

    st->key ^= Zobrist::side;

    if (is_ok(ep_square()))
    {
        st->key ^= Zobrist::enpassant[file_of(ep_square())];
        reset_ep_square();
    }

    activeColor = ~active_color();

    set_ext_state();

    st->repetition = 0;

    assert(pos_is_ok());
}

// Unmakes a null move
void Position::undo_null_move() noexcept {
    assert(!checkers());
    assert(!is_ok(captured_piece()));
    assert(!is_ok(promoted_piece()));
    assert(!is_ok(cap_square()));

    activeColor = ~active_color();

    st = st->preState;

    assert(pos_is_ok());
}

// Takes a random move and tests whether the move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal(const Move& m) const noexcept {
    assert(m.is_ok());

    Color ac = active_color();

    assert(piece_on(king_square(ac)) == make_piece(ac, KING));

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(org);

    // If the origin square is not occupied by a piece belonging to
    // the side to move, the move is obviously not legal.
    if (!is_ok(pc) || color_of(pc) != ac)
        return false;

    if (m.type_of() == CASTLING)
    {
        CastlingRights cr = make_castling_rights(ac, org, dst);
        return relative_rank(ac, org) == RANK_1 && relative_rank(ac, dst) == RANK_1
            && type_of(pc) == KING && (pieces(ac, ROOK) & dst) && !checkers()  //
            && can_castle(cr) && !castling_impeded(cr) && castling_rook_square(cr) == dst;
    }

    // The destination square cannot be occupied by a friendly piece
    if (pieces(ac) & dst)
        return false;

    switch (m.type_of())
    {
    case NORMAL :
        assert(m.promotion_type() - KNIGHT == NO_PIECE_TYPE);

        // Handle the special case of a pawn move
        if (type_of(pc) == PAWN)
        {
            // Already handled promotion moves, so origin & destination cannot be on the 8th/1st rank
            if (PROMOTION_RANK_BB & make_bitboard(org, dst))
                return false;
            if (!(relative_rank(ac, org) < RANK_7 && relative_rank(ac, dst) < RANK_8
                  && ((org + pawn_spush(ac) == dst && !(pieces() & dst))    // Single push
                      || (attacks_bb<PAWN>(org, ac) & pieces(~ac) & dst)))  // Capture
                && !(relative_rank(ac, org) == RANK_2 && relative_rank(ac, dst) == RANK_4
                     && org + pawn_dpush(ac) == dst  // Double push
                     && !(pieces() & make_bitboard(dst, dst - pawn_spush(ac)))))
                return false;
        }
        else if (!(attacks_bb(type_of(pc), org, pieces()) & dst))
            return false;

        // For king moves, check whether the destination square is attacked by the enemies.
        if (type_of(pc) == KING)
            return !exist_attackers_to(dst, pieces(~ac), pieces() ^ org);
        break;

    case PROMOTION :
        if (!(relative_rank(ac, org) == RANK_7 && relative_rank(ac, dst) == RANK_8
              && type_of(pc) == PAWN  //&& (PROMOTION_RANK_BB & dst)
              && ((org + pawn_spush(ac) == dst && !(pieces() & dst))
                  || ((attacks_bb<PAWN>(org, ac) & pieces(~ac)) & dst))))
            return false;
        break;

    case EN_PASSANT :
        if (!(relative_rank(ac, org) == RANK_5 && relative_rank(ac, dst) == RANK_6
              && type_of(pc) == PAWN && ep_square() == dst && rule50_count() == 0
              && (pieces(~ac, PAWN) & (dst - pawn_spush(ac)))
              && !(pieces() & make_bitboard(dst, dst + pawn_spush(ac)))
              && ((attacks_bb<PAWN>(org, ac) /*& ~pieces()*/) & dst)
              && !(slide_attackers_to(king_square(ac),
                                      pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac)))
                   & pieces(~ac))))
            return false;
        break;

    default :  // NONE
        assert(false);
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // Therefore have to take care that the some kind of moves are filtered out here.
    return (!checkers() ||
            // Double check? In this case, a king move is required
            (!more_than_one(checkers())
             // Pinned piece can never resolve a check
             // NOTE: there is some issue with this condition
             //&& !(blockers(ac) & org)
             // Our move must be a blocking interposition or a capture of the checking piece
             && ((between_bb(king_square(ac), lsb(checkers())) & dst)
                 || (m.type_of() == EN_PASSANT && (checkers() & (dst - pawn_spush(ac)))))))
        && (type_of(pc) == PAWN || !(blockers(ac) & org) || aligned(king_square(ac), org, dst));
}

// Tests whether a pseudo-legal move is legal
bool Position::legal(const Move& m) const noexcept {
    assert(pseudo_legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    switch (m.type_of())
    {
    // En-passant captures are a tricky special case. Because they are rather uncommon,
    // Simply by testing whether the king is attacked after the move is made.
    case EN_PASSANT :
        assert(relative_rank(ac, org) == RANK_5);
        assert(relative_rank(ac, dst) == RANK_6);
        assert(type_of(piece_on(org)) == PAWN);
        assert(ep_square() == dst);
        assert(rule50_count() == 0);
        assert(pieces(~ac, PAWN) & (dst - pawn_spush(ac)));
        assert(!(pieces() & make_bitboard(dst, dst + pawn_spush(ac))));
        assert((attacks_bb<PAWN>(org, ac) /*& ~pieces()*/) & dst);
        assert(!(slide_attackers_to(king_square(ac),
                                    pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac)))
                 & pieces(~ac)));

        return true;

    // Castling moves generation does not check if the castling path is clear of
    // enemy attacks, it is delayed at a later time: now!
    case CASTLING : {
        assert(relative_rank(ac, org) == RANK_1);
        assert(relative_rank(ac, dst) == RANK_1);
        assert(type_of(piece_on(org)) == KING);
        assert(org == king_square(ac));
        assert(pieces(ac, ROOK) & dst);
        assert(!checkers());
        assert(can_castle(make_castling_rights(ac, org, dst)));
        assert(!castling_impeded(make_castling_rights(ac, org, dst)));
        assert(castling_rook_square(make_castling_rights(ac, org, dst)) == dst);

        // After castling, the rook and king final positions are the same in
        // Chess960 as they would be in standard chess.
        Square    kdst = king_castle_sq(ac, org, dst);
        Direction step = org < kdst ? WEST : EAST;
        for (Square s = kdst; s != org; s += step)
            if (exist_attackers_to(s, pieces(~ac)))
                return false;

        // In case of Chess960, verify if the Rook blocks some checks.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !(blockers(ac) & dst);
    }
    case NORMAL :
        // For king moves, return true
        if (type_of(piece_on(org)) == KING)
        {
            assert(!exist_attackers_to(dst, pieces(~ac), pieces() ^ org));
            return true;
        }
        break;

    case PROMOTION :
        assert(relative_rank(ac, org) == RANK_7);
        assert(relative_rank(ac, dst) == RANK_8);
        assert(type_of(piece_on(org)) == PAWN);
        assert((org + pawn_spush(ac) == dst && !(pieces() & dst))
               || ((attacks_bb<PAWN>(org, ac) & pieces(~ac)) & dst));
        break;
    }

    // For non-king move, check it is not pinned or it is moving along the line from the king.
    return type_of(piece_on(org)) != PAWN || !(blockers(ac) & org)
        || aligned(king_square(ac), org, dst);
}

// Tests whether a pseudo-legal move is a check
bool Position::check(const Move& m) const noexcept {
    assert(pseudo_legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    if (
      // Is there a direct check?
      (checks(m.type_of() != PROMOTION ? type_of(piece_on(org)) : m.promotion_type()) & dst)
      // Is there a discovered check?
      || ((blockers(~ac) & org)  //
          && (!aligned(king_square(~ac), org, dst) || m.type_of() == CASTLING)))
        return true;

    switch (m.type_of())
    {
    case NORMAL :
        return false;

    case PROMOTION :
        return attacks_bb(m.promotion_type(), dst, pieces() ^ org) & pieces(~ac, KING);

    // En-passant capture with check? Already handled the case of direct check
    // and ordinary discovered check, so the only case need to handle is
    // the unusual case of a discovered check through the captured pawn.
    case EN_PASSANT :
        return slide_attackers_to(king_square(~ac),
                                  pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac)))
             & pieces(ac);

    case CASTLING :
        // Castling is encoded as "king captures rook"
        return checks(ROOK) & rook_castle_sq(ac, org, dst);
    }
    assert(false);
    return false;
}

bool Position::dbl_check(const Move& m) const noexcept {
    assert(pseudo_legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    switch (m.type_of())
    {
    case NORMAL :
        return
          // Is there a direct check?
          (checks(type_of(piece_on(org))) & dst)
          // Is there a discovered check?
          && (blockers(~ac) & org) && !aligned(king_square(~ac), org, dst);

    case PROMOTION :
        return (blockers(~ac) & org)  //
            && (attacks_bb(m.promotion_type(), dst, pieces() ^ org) & pieces(~ac, KING));

    case EN_PASSANT : {
        Bitboard checkers =
          slide_attackers_to(king_square(~ac),
                             pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac)))
          & pieces(ac);
        return more_than_one(checkers) || (checkers && (checks(PAWN) & dst));
    }
    case CASTLING :
        return false;
    }
    assert(false);
    return false;
}

bool Position::fork(const Move& m) const noexcept {
    assert(pseudo_legal(m));

    Color ac = active_color();

    switch (type_of(piece_on(m.org_sq())))
    {
    case PAWN :
        return more_than_one(pieces(~ac) & ~pieces(PAWN) & attacks_bb<PAWN>(m.dst_sq(), ac));
    case KNIGHT :
        return more_than_one(pieces(~ac) & ~pieces(KNIGHT) & attacks_bb<KNIGHT>(m.dst_sq()));
    case BISHOP :
        return more_than_one(pieces(~ac) & ~pieces(BISHOP)
                             & attacks_bb<BISHOP>(m.dst_sq(), pieces(ac) ^ m.org_sq()));
    case ROOK :
        return more_than_one(pieces(~ac) & ~pieces(ROOK)
                             & attacks_bb<ROOK>(m.dst_sq(), pieces(ac) ^ m.org_sq()));
    case QUEEN :
        return more_than_one(pieces(~ac) & ~pieces(QUEEN)
                             & attacks_bb<QUEEN>(m.dst_sq(), pieces(ac) ^ m.org_sq()));
    case KING :
        return more_than_one(pieces(~ac) & ~pieces(KING, QUEEN) & attacks_bb<KING>(m.dst_sq()));
    default :;
    }
    return false;
}

Key Position::material_key() const noexcept {
    Key materialKey = 0;

    for (Piece pc : Pieces)
        if (pc != W_KING && pc != B_KING && count(pc) != 0)
            materialKey ^= Zobrist::psq[pc][PawnOffset + count(pc) - 1];

    return materialKey;
}

// Computes the new hash key after the given move.
// Needed for speculative prefetch.
// It does recognize special moves like castling, en-passant and promotions.
Key Position::move_key(const Move& m) const noexcept {
    Key moveKey = st->key ^ Zobrist::side;

    if (is_ok(ep_square()))
        moveKey ^= Zobrist::enpassant[file_of(ep_square())];

    if (m == Move::Null)
        return moveKey;

    assert(legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  movedPiece    = piece_on(org);
    Square capSq         = m.type_of() != EN_PASSANT ? dst : dst - pawn_spush(ac);
    Piece  capturedPiece = piece_on(capSq);
    assert(color_of(movedPiece) == ac);
    assert(capturedPiece == NO_PIECE
           || color_of(capturedPiece) == (m.type_of() != CASTLING ? ~ac : ac));
    assert(type_of(capturedPiece) != KING);

    moveKey ^=
      Zobrist::psq[movedPiece][org]
      ^ Zobrist::psq[m.type_of() != PROMOTION ? movedPiece : make_piece(ac, m.promotion_type())]
                    [m.type_of() != CASTLING ? dst : king_castle_sq(ac, org, dst)]
      ^ Zobrist::castling[castling_rights() & castling_rights_mask(org, dst)];

    if (m.type_of() == CASTLING)
    {
        assert(type_of(movedPiece) == KING);
        assert(capturedPiece == make_piece(ac, ROOK));
        // ROOK
        moveKey ^= Zobrist::psq[capturedPiece][dst]
                 ^ Zobrist::psq[capturedPiece][rook_castle_sq(ac, org, dst)];
        //capturedPiece = NO_PIECE;
        return adjust_key(moveKey, 1);
    }

    if (type_of(movedPiece) == PAWN && (int(dst) ^ int(org)) == NORTH_2
        && can_enpassant(~ac, dst - pawn_spush(ac), true))
    {
        assert(relative_rank(ac, org) == RANK_2);
        assert(relative_rank(ac, dst) == RANK_4);

        moveKey ^= Zobrist::enpassant[file_of(dst)];

        return moveKey;
    }

    moveKey ^= Zobrist::psq[capturedPiece][capSq];

    return capturedPiece || type_of(movedPiece) == PAWN ? moveKey : adjust_key(moveKey, 1);
}

// Tests if the SEE (Static Exchange Evaluation) value of the move
// is greater or equal to the given threshold.
// An algorithm similar to alpha-beta pruning with a null window.
bool Position::see_ge(const Move& m, int threshold) const noexcept {
    assert(pseudo_legal(m));

    // Not deal with castling, can't win any material, nor can lose any.
    if (m.type_of() == CASTLING)
        return threshold <= 0;

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

    int swap;

    swap = PIECE_VALUE[type_of(piece_on(cap))] - threshold;
    // If promotion, get the promoted piece and lose the pawn
    if (m.type_of() == PROMOTION)
        swap += PIECE_VALUE[m.promotion_type()] - VALUE_PAWN;
    // If can't beat the threshold despite capturing the piece,
    // it is impossible to beat the threshold.
    if (swap < 0)
        return false;

    auto moved = m.type_of() == PROMOTION ? m.promotion_type() : type_of(piece_on(org));
    assert(is_ok(moved));

    swap = PIECE_VALUE[moved] - swap;
    // If still beat the threshold after losing the piece,
    // it is guaranteed to beat the threshold.
    if (swap <= 0)
        return true;

    // It doesn't matter if the destination square is occupied or not
    // xoring to is important for pinned piece logic
    occupied ^= make_bitboard(org, dst);

    Bitboard attackers = attackers_to(dst, occupied) & occupied;

    Square   epSq = SQ_NONE;
    Bitboard epAttackers;
    if (moved == PAWN && (int(dst) ^ int(org)) == NORTH_2
        && can_enpassant(~ac, dst - pawn_spush(ac), true, &epAttackers))
    {
        assert(relative_rank(ac, org) == RANK_2);
        assert(relative_rank(ac, dst) == RANK_4);

        epSq = dst - pawn_spush(ac);
        assert(epAttackers);
        attackers |= epAttackers;
    }

    if (!attackers)
        return true;

    bool win = true;

    Bitboard acAttackers, b = 0;
    Square   sq;

    Bitboard qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
    Bitboard qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

    Magic(*magic)[2] = &Magics[dst];

    std::array<bool, COLOR_NB> discovery{true, true};

    while (attackers)
    {
        ac = ~ac;

        acAttackers = pieces(ac) & attackers;
        // If ac has no more attackers then give up: ac loses
        if (!acAttackers)
            break;

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
            && ((pt = type_of(piece_on(org))) != PAWN || !aligned(king_square(ac), org, dst)))
        {
            acAttackers &= king_square(ac);

            if (!acAttackers  //
                && (pt == PAWN || !(attacks_bb(pt, dst, occupied) & pieces(ac, KING))))
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

        if (!is_ok(epSq) && discovery[ac] && (b = blockers(~ac) & acAttackers))
        {
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

            if (!(pinners(~ac) & pieces(ac) & line_bb(king_square(~ac), sq) & occupied))
            {
                discovery[ac] = false;

                ac  = ~ac;
                win = !win;
                continue;  // Resume without considering discovery
            }

            occupied ^= org = sq;
            if ((swap = PIECE_VALUE[pt] - swap) < win)
                break;
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
            occupied ^= org = lsb(b);
            if ((swap = VALUE_PAWN - swap) < win)
                break;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(magic, occupied);

            if (is_ok(epSq) && rank_of(org) == rank_of(dst))
            {
                occupied ^= make_bitboard(dst, epSq);

                dst       = epSq;
                epSq      = SQ_NONE;
                attackers = attackers_to(dst, occupied) & occupied;
                if (!attackers)
                    break;

                qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
                qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

                magic = &Magics[dst];
            }
        }
        else if ((b = pieces(KNIGHT) & acAttackers))
        {
            occupied ^= org = lsb(b);
            if ((swap = VALUE_KNIGHT - swap) < win)
                break;
        }
        else if ((b = pieces(BISHOP) & acAttackers))
        {
            occupied ^= org = lsb(b);
            if ((swap = VALUE_BISHOP - swap) < win)
                break;
            qB &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(magic, occupied);
        }
        else if ((b = pieces(ROOK) & acAttackers))
        {
            occupied ^= org = lsb(b);
            if ((swap = VALUE_ROOK - swap) < win)
                break;
            qR &= occupied;
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(magic, occupied);
        }
        else if ((b = pieces(QUEEN) & acAttackers))
        {
            occupied ^= org = lsb(b);

            swap = VALUE_QUEEN - swap;
            // Implies that the previous recapture was done by a higher rated piece than a Queen (King is excluded)
            assert(swap >= win);

            qB &= occupied;
            qR &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(magic, occupied);
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(magic, occupied);
        }
        else  // KING
        {
            occupied ^= acAttackers;
            // If "capture" with the king but the opponent still has attackers, reverse the result.
            win ^= bool(pieces(~ac) & attackers);
            break;
        }

        attackers &= occupied;
    }

    if (!win)
    {
        ac = active_color();
        occupied |= dst;

        //b &= occupied;
        acAttackers = pieces(ac) & occupied;

        sq = king_square(~ac);
        if ((occupied & sq) && exist_attackers_to(sq, acAttackers, occupied))
            return true;

        // Even when one of our non-queen pieces attacks opponent queen after exchanges
        Bitboard queen = pieces(~ac, QUEEN) & ~attackers & occupied;

        sq = queen ? lsb(queen) : SQ_NONE;
        if (is_ok(sq) && exist_attackers_to(sq, acAttackers & ~pieces(QUEEN), occupied))
            return true;
    }

    return win;
}

// Draw by Repetition: position repeats once earlier but strictly
// after the root, or repeats twice before or at the root.
bool Position::is_repetition(std::int16_t ply) const noexcept {
    return repetition() != 0 && repetition() < ply;
}

// Tests whether the current position is drawn by repetition or by 50-move rule.
// It also detect stalemates.
bool Position::is_draw(std::int16_t ply, bool rule50Use, bool stalemateUse) const noexcept {
    return
      // Draw by Repetition
      is_repetition(ply)
      // Draw by 50-move rule
      || (rule50Use && rule50_count() >= 2 * DrawMoveCount
          && (!checkers() || !MoveList<LEGAL, true>(*this).empty()))
      // Draw by Stalemate
      || (stalemateUse && !checkers() && MoveList<LEGAL, true>(*this).empty());
}

// Tests whether there has been at least one repetition
// of positions since the last capture or pawn move.
bool Position::has_repetition() const noexcept {
    auto end = std::min(rule50_count(), null_ply());
    if (end < 4)
        return false;

    auto* cst = st;
    while (end-- >= 4)
    {
        if (cst->repetition != 0)
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

    auto* pst = st->preState;

    Key baseKey = st->key;
    Key iterKey = baseKey ^ pst->key ^ Zobrist::side;

    for (std::int16_t i = 3; i <= end; i += 2)
    {
        iterKey ^= pst->preState->key ^ pst->preState->preState->key ^ Zobrist::side;
        pst = pst->preState->preState;

        // Opponent pieces have reverted
        if (iterKey != 0)
            continue;

        Key moveKey = baseKey ^ pst->key;
        // 'moveKey' is a single move
        std::size_t index = Cuckoos.find_key(moveKey);
        if (index >= Cuckoos.size())
            continue;

        Move m = Cuckoos[index].move;
        assert(m != Move::None);
        Square s1 = m.org_sq();
        Square s2 = m.dst_sq();

        // Move path is obstructed
        if (pieces() & between_ex_bb(s1, s2))
            continue;

#if !defined(NDEBUG)
        // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in the same location
        if (empty_on(s1))
            m = Move(s2, s1);
        assert(pseudo_legal(m) && legal(m) && MoveList<LEGAL>(*this).contains(m));
#endif
        if (i < ply
            // For nodes before or at the root, check that the move is
            // a repetition rather than a move to the current position.
            || pst->repetition != 0)
            return true;
    }
    return false;
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
    f += token + " ";

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
        Square s  = pop_lsb(occupied);
        Piece  pc = piece_on(s);
        assert(is_ok(pc));
        key ^= Zobrist::psq[pc][s];
    }

    key ^= Zobrist::castling[castling_rights()];

    if (is_ok(ep_square()))
        key ^= Zobrist::enpassant[file_of(ep_square())];

    if (active_color() == BLACK)
        key ^= Zobrist::side;

    return key;
}

Key Position::compute_minor_key() const noexcept {
    Key minorKey = 0;

    Bitboard occupied = pieces();
    while (occupied)
    {
        Square s  = pop_lsb(occupied);
        Piece  pc = piece_on(s);
        auto   pt = type_of(pc);
        assert(is_ok(pc));

        if (pt != PAWN && pt != KING && !is_major(pt))
            minorKey ^= Zobrist::psq[pc][s];
    }
    return minorKey;
}

Key Position::compute_major_key() const noexcept {
    Key majorKey = 0;

    Bitboard occupied = pieces();
    while (occupied)
    {
        Square s  = pop_lsb(occupied);
        Piece  pc = piece_on(s);
        auto   pt = type_of(pc);
        assert(is_ok(pc));

        if (pt != PAWN && pt != KING && is_major(pt))
            majorKey ^= Zobrist::psq[pc][s];
    }
    return majorKey;
}

Key Position::compute_non_pawn_key() const noexcept {
    Key nonPawnKey = 0;

    Bitboard occupied = pieces();
    while (occupied)
    {
        Square s  = pop_lsb(occupied);
        Piece  pc = piece_on(s);
        auto   pt = type_of(pc);
        assert(is_ok(pc));

        if (pt != PAWN)
            nonPawnKey ^= Zobrist::psq[pc][s];
    }
    return nonPawnKey;
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
        || (is_ok(ep_square())  //
            && !can_enpassant(active_color(), ep_square())))
        assert(false && "Position::pos_is_ok(): Default");

    if (Fast)
        return true;

    if (st->key != compute_key())
        assert(false && "Position::pos_is_ok(): Key");

    if (minor_key() != compute_minor_key())
        assert(false && "Position::pos_is_ok(): Minor Key");

    if (major_key() != compute_major_key())
        assert(false && "Position::pos_is_ok(): Major Key");

    if (non_pawn_key() != compute_non_pawn_key())
        assert(false && "Position::pos_is_ok(): NonPawn Key");

    if (exist_attackers_to(king_square(~active_color()), pieces(active_color())))
        assert(false && "Position::pos_is_ok(): King Checker");

    if ((pieces(PAWN) & PROMOTION_RANK_BB) || count(W_PAWN) > 8 || count(B_PAWN) > 8)
        assert(false && "Position::pos_is_ok(): Pawns");

    for (Color c : {WHITE, BLACK})
        if (count<PAWN>(c)                                                      //
              + std::max(count<KNIGHT>(c) - 2, 0)                               //
              + std::max(popcount(pieces(c, BISHOP) & COLOR_BB[WHITE]) - 1, 0)  //
              + std::max(popcount(pieces(c, BISHOP) & COLOR_BB[BLACK]) - 1, 0)  //
              + std::max(count<ROOK>(c) - 2, 0)                                 //
              + std::max(count<QUEEN>(c) - 1, 0)                                //
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

            if (!is_ok(castling_rook_square(cr))  //
                || !(pieces(c, ROOK) & castling_rook_square(cr))
                || (castlingRightsMask[c * FILE_NB + file_of(castling_rook_square(cr))]) != cr
                || (castlingRightsMask[c * FILE_NB + file_of(king_square(c))] & cr) != cr)
                assert(false && "Position::pos_is_ok(): Castling");
        }

    return true;
}
#endif

std::ostream& operator<<(std::ostream& os, const Position::Board::Cardinal& cardinal) noexcept {
    for (File f = FILE_A; f <= FILE_H; ++f)
        os << " | " << UCI::piece(cardinal.piece_on(f));
    os << " | ";

    return os;
}

std::ostream& operator<<(std::ostream& os, const Position::Board& board) noexcept {
    static constexpr std::string_view Sep = "\n  +---+---+---+---+---+---+---+---+\n";

    os << Sep;
    for (Rank r = RANK_8; r >= RANK_1; --r)
        os << UCI::rank(r) << board.cardinals[r] << Sep;
    os << " ";
    for (File f = FILE_A; f <= FILE_H; ++f)
        os << "   " << UCI::file(f, true);
    os << "\n";

    return os;
}

// Returns an ASCII representation of the position
std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept {

    os << pos.board                                                 //
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
        Position p;
        State    st;
        p.set(pos.fen(), &st);

        Tablebases::ProbeState wdlPs, dtzPs;

        auto wdl = Tablebases::probe_wdl(p, &wdlPs);
        auto dtz = Tablebases::probe_dtz(p, &dtzPs);
        os << "\nTablebases WDL: " << std::setw(4) << wdl << " (" << wdlPs << ")"
           << "\nTablebases DTZ: " << std::setw(4) << dtz << " (" << dtzPs << ")";
    }

    return os;
}

}  // namespace DON
