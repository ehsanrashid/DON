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

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "misc.h"
#include "movegen.h"
#include "prng.h"
#include "syzygy/tbbase.h"
#include "tt.h"

namespace DON {

namespace {

constexpr std::size_t PawnOffset = 8;

constexpr Piece Pieces[]{W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,  //
                         B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING};

// Implements Marcel van Kervinck's cuckoo algorithm to detect repetition of positions for 3-fold repetition draws.
// The algorithm uses hash tables with Zobrist hashes to allow fast detection of recurring positions.
// For details see:
// http://web.archive.org/web/20201107002606/https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// Cuckoo Entry: stores a Zobrist hash key and the associated move
struct Cuckoo final {

    constexpr bool empty() const noexcept { return key == 0; }

    Key  key;
    Move move;
};

// Cuckoo Table: fixed-size hash table with two hash functions and cuckoo eviction,
// contains Zobrist hashes of valid reversible moves, and the moves themselves
template<std::size_t Size>
class CuckooTable final {
    static_assert(exactly_one(Size), "Size has to be a power of 2");

   public:
    constexpr CuckooTable() noexcept                    = default;
    CuckooTable(const CuckooTable&) noexcept            = delete;
    CuckooTable(CuckooTable&&) noexcept                 = delete;
    CuckooTable& operator=(const CuckooTable&) noexcept = delete;
    CuckooTable& operator=(CuckooTable&&) noexcept      = delete;

    [[nodiscard]] constexpr auto begin() noexcept { return cuckoos.begin(); }
    [[nodiscard]] constexpr auto end() noexcept { return cuckoos.end(); }
    [[nodiscard]] constexpr auto begin() const noexcept { return cuckoos.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return cuckoos.end(); }

    [[nodiscard]] constexpr auto size() const noexcept { return cuckoos.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return cuckoos.empty(); }

    [[nodiscard]] constexpr decltype(auto) operator[](std::size_t idx) const noexcept {
        return cuckoos[idx];
    }
    [[nodiscard]] constexpr decltype(auto) operator[](std::size_t idx) noexcept {
        return cuckoos[idx];
    }

    // Hash function for indexing the cuckoo table
    template<std::size_t Part>
    constexpr std::size_t H(Key key) const noexcept {
        return (key >> (16 * Part)) & (size() - 1);
    }

    constexpr void fill(const Cuckoo& cuckoo) noexcept { cuckoos.fill(cuckoo); }

    constexpr void init() noexcept {
        fill({0, Move::None});
        count = 0;
    }

    void insert(Cuckoo cuckoo) noexcept {
        assert(!cuckoo.empty());
        std::size_t index = H<0>(cuckoo.key);
        while (true)
        {
            std::swap(cuckoos[index], cuckoo);
            if (cuckoo.empty())  // Arrived at empty slot?
                break;
            index ^= H<0>(cuckoo.key) ^ H<1>(cuckoo.key);  // Push victim to alternative slot
        }
        assert(cuckoo.empty());
        ++count;
    }

    std::size_t find_key(Key key) const noexcept {
        if (std::size_t index;  //
            (index = H<0>(key), cuckoos[index].key == key)
            || (index = H<1>(key), cuckoos[index].key == key))
            return index;
        return size();
    }

    std::size_t count;

   private:
    std::array<Cuckoo, Size> cuckoos;
};

CuckooTable<0x2000> Cuckoos;

}  // namespace

// Called at startup to initialize the Zobrist arrays used to compute hash keys
void Position::init() noexcept {
    PRNG<XoShiRo256Star> prng(0x105524ULL);

    const auto prng_rand = [&] { return prng.template rand<Key>(); };

    for (Piece pc : Pieces)
        std::generate(std::begin(Zobrist::psq[pc]), std::end(Zobrist::psq[pc]), prng_rand);
    for (Piece pc : {W_PAWN, B_PAWN})
    {
        std::fill_n(&Zobrist::psq[pc][SQ_A1], PawnOffset, Key{});
        std::fill_n(&Zobrist::psq[pc][SQ_A8], PawnOffset, Key{});
    }

    std::generate(std::begin(Zobrist::castling), std::end(Zobrist::castling), prng_rand);

    std::generate(std::begin(Zobrist::enpassant), std::end(Zobrist::enpassant), prng_rand);

    Zobrist::turn = prng_rand();

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
                    Key  key  = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::turn;
                    Move move = Move(s1, s2);
                    Cuckoos.insert({key, move});
                }
    }
    assert(Cuckoos.count == 3668);
}

// Initializes the position object with the given FEN string.
// This function is not very robust - make sure that input FENs are correct,
// this is assumed to be the responsibility of the GUI.
void Position::set(std::string_view fens, State* const newSt) noexcept {
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
    assert(!fens.empty());
    assert(newSt != nullptr);
    std::memset(static_cast<void*>(this), 0, sizeof(*this));
    std::fill(std::begin(castlingRookSq), std::end(castlingRookSq), SQ_NONE);
    std::memset(newSt, 0, sizeof(*newSt));
    newSt->epSq = newSt->capSq = SQ_NONE;
    newSt->kingSq[WHITE] = newSt->kingSq[BLACK] = SQ_NONE;

    st = newSt;

    std::istringstream iss{std::string(fens)};
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
            if (Piece pc = to_piece(token); is_ok(pc))
            {
                assert(file < FILE_NB);
                Square sq = make_square(file, rank);
                put_piece(sq, pc);
                if (type_of(pc) == KING)
                {
                    assert(!is_ok(king_sq(color_of(pc))));
                    st->kingSq[color_of(pc)] = sq;
                }
                ++file;
            }
            else
                assert(false && "Position::set(): Invalid Piece");
        }
    }
    assert(file <= FILE_NB && rank == RANK_1);
    assert(count(W_ALL) <= 16 && count(B_ALL) <= 16);
    assert(count(W_ALL) + count(B_ALL) == count<ALL_PIECE>());
    assert(count(W_PAWN) <= 8 && count(B_PAWN) <= 8);
    assert(count(W_KING) == 1 && count(B_KING) == 1);
    assert(is_ok(king_sq(WHITE)) && is_ok(king_sq(BLACK)));
    assert(!(pieces(PAWN) & PROMOTION_RANK_BB));
    assert(distance(king_sq(WHITE), king_sq(BLACK)) > 1);

    iss >> std::ws;

    // 2. Active color
    iss >> token;
    token = std::tolower(token);

    activeColor = token == 'w' ? WHITE
                : token == 'b' ? BLACK
                               : (assert(false && "Position::set(): Invalid Color"), COLOR_NB);

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
        if (castlingRightsCount > 4)
        {
            assert(false && "Position::set(): Number of Castling Rights");
            continue;
        }

        Color c = std::isupper(token) ? WHITE : BLACK;

        if (relative_rank(c, king_sq(c)) != RANK_1)
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
            rsq = relative_sq(c, SQ_H1);
            while (file_of(rsq) >= FILE_C && !(rooks & rsq) && rsq != king_sq(c))
                --rsq;
        }
        else if (token == 'q')
        {
            rsq = relative_sq(c, SQ_A1);
            while (file_of(rsq) <= FILE_F && !(rooks & rsq) && rsq != king_sq(c))
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

    iss >> std::ws;

    Color ac = active_color();

    // 4. En-passant square.
    // Ignore if square is invalid or not on side to move relative rank 6.
    bool epCheck = false;

    iss >> token;
    if (token != '-')
    {
        std::uint8_t epFile = std::tolower(token);
        std::uint8_t epRank;
        iss >> epRank;
        // clang-format off
        if ('a' <= epFile && epFile <= 'h'
            && epRank == (ac == WHITE ? '6' : ac == BLACK ? '3' : '-'))
        {
            st->epSq = make_square(to_file(epFile), to_rank(epRank));

            // En-passant square will be considered only if
            // a) there is an enemy pawn in front of epSquare
            // b) there is no piece on epSquare or behind epSquare
            // c) there is atleast one friend pawn threatening epSquare
            // d) there is no enemy Bishop, Rook or Queen pinning
            epCheck = (pieces(~ac, PAWN) & (ep_sq() - pawn_spush(ac)))
                   && !(pieces() & make_bitboard(ep_sq(), ep_sq() + pawn_spush(ac)))
                   && (pieces(ac, PAWN) & attacks_bb<PAWN>(ep_sq(), ~ac));
        }
        else
            assert(false && "Position::set(): Invalid En-passant square");
        // clang-format on
    }

    // 5-6. Halfmove clock and fullmove number
    std::int16_t rule50Count = 0;
    std::int16_t moveNum     = 1;
    iss >> std::skipws >> rule50Count >> moveNum;

    st->rule50Count = std::abs(rule50Count);
    // Convert from moveNum starting from 1 to posPly starting from 0,
    // handle also common incorrect FEN with moveNum = 0.
    gamePly = std::max(2 * (std::abs(moveNum) - 1), 0) + (ac == BLACK);

    st->checkers = attackers_to(king_sq(ac)) & pieces(~ac);

    set_ext_state();

    // Reset illegal values
    if (is_ok(ep_sq()))
    {
        reset_rule50_count();
        if (!(epCheck && can_enpassant(ac, ep_sq())))
            reset_ep_sq();
    }
    assert(rule50_count() <= 100);
    gamePly = std::max(ply(), rule50_count());

    set_state();

    assert(pos_is_ok());
}

// Overload to initialize the position object with the given endgame code string like "KBPKN".
// It's mainly a helper to get the material key out of an endgame code.
void Position::set(std::string_view code, Color c, State* const newSt) noexcept {
    assert(!code.empty() && code[0] == 'K' && code.find('K', 1) != std::string_view::npos);
    assert(is_ok(c));

    std::string sides[2]{std::string{code.substr(code.find('K', 1))},                // Weak
                         std::string{code.substr(0, code.find_first_of("vK", 1))}};  // Strong

    assert(0 < sides[0].size() && sides[0].size() < 8);
    assert(0 < sides[1].size() && sides[1].size() < 8);

    sides[c] = lower_case(sides[c]);

    std::string fens;
    fens.reserve(64);
    fens += "8/";
    fens += sides[0];
    fens += digit_to_char(8 - sides[0].size());
    fens += "/8/8/8/8/";
    fens += sides[1];
    fens += digit_to_char(8 - sides[1].size());
    fens += "/8 ";
    fens += (c == WHITE ? 'w' : c == BLACK ? 'b' : '-');
    fens += " - - 0 1";

    set(fens, newSt);
}

void Position::set(const Position& pos, State* const newSt) noexcept {
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
                oss << to_char(piece_on(s));
            }
        }

        // Handle trailing empty squares
        if (emptyCount)
            oss << emptyCount;

        if (r > RANK_1)
            oss << '/';
    }

    oss << ' ' << (active_color() == WHITE ? 'w' : active_color() == BLACK ? 'b' : '-') << ' ';

    if (can_castle(ANY_CASTLING))
    {
        if (can_castle(WHITE_OO))
            oss << (Chess960 ? to_char<true>(file_of(castling_rook_sq(WHITE_OO))) : 'K');
        if (can_castle(WHITE_OOO))
            oss << (Chess960 ? to_char<true>(file_of(castling_rook_sq(WHITE_OOO))) : 'Q');
        if (can_castle(BLACK_OO))
            oss << (Chess960 ? to_char<false>(file_of(castling_rook_sq(BLACK_OO))) : 'k');
        if (can_castle(BLACK_OOO))
            oss << (Chess960 ? to_char<false>(file_of(castling_rook_sq(BLACK_OOO))) : 'q');
    }
    else
        oss << '-';

    oss << ' ' << (is_ok(ep_sq()) ? to_square(ep_sq()) : "-");
    if (full)
        oss << ' ' << rule50_count() << ' ' << move_num();

    return oss.str();
}

// Sets castling rights given the corresponding color and the rook starting square.
void Position::set_castling_rights(Color c, Square rOrg) noexcept {
    assert(relative_rank(c, rOrg) == RANK_1);
    assert((pieces(c, ROOK) & rOrg));
    assert(castlingRightsMask[c * FILE_NB + file_of(rOrg)] == 0);

    Square kOrg = king_sq(c);
    assert(relative_rank(c, kOrg) == RANK_1);
    assert((pieces(c, KING) & kOrg));

    int cr = make_castling_rights(c, kOrg, rOrg);

    std::size_t crLsb = lsb(cr);
    assert(crLsb < std::size(castlingRookSq));
    assert(!is_ok(castlingRookSq[crLsb]));

    st->castlingRights |= cr;
    castlingRightsMask[c * FILE_NB + file_of(kOrg)] |= cr;
    castlingRightsMask[c * FILE_NB + file_of(rOrg)] = cr;

    castlingRookSq[crLsb] = rOrg;

    Square kDst = king_castle_sq(c, kOrg, rOrg);
    Square rDst = rook_castle_sq(c, kOrg, rOrg);

    castlingPath[crLsb] =
      (between_bb(kOrg, kDst) | between_bb(rOrg, rDst)) & ~make_bitboard(kOrg, rOrg);
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
            assert(Zobrist::psq[pc][s]);
            st->pawnKey[color_of(pc)] ^= Zobrist::psq[pc][s];
        }
        else if (pt != KING)
        {
            st->groupKey[color_of(pc)][is_major(pt)] ^= Zobrist::psq[pc][s];
            st->nonPawnMaterial[color_of(pc)] += PIECE_VALUE[pt];
        }
    }

    st->key ^= Zobrist::castling[castling_rights()];

    if (is_ok(ep_sq()))
        st->key ^= Zobrist::enpassant[file_of(ep_sq())];

    if (active_color() == BLACK)
        st->key ^= Zobrist::turn;
}

// Set extra state to detect if a move is check
void Position::set_ext_state() noexcept {

    Bitboard occupied = pieces();

    // clang-format off
    st->checks[NO_PIECE_TYPE] = 0;
    st->checks[PAWN  ] = attacks_bb<PAWN  >(king_sq(~active_color()), ~active_color());
    st->checks[KNIGHT] = attacks_bb<KNIGHT>(king_sq(~active_color()));
    st->checks[BISHOP] = attacks_bb<BISHOP>(king_sq(~active_color()), occupied);
    st->checks[ROOK  ] = attacks_bb<ROOK  >(king_sq(~active_color()), occupied);
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
        Bitboard xsnipers  = xslide_attackers_to(king_sq(c)) & pieces(~c);
        Bitboard xoccupied = occupied ^ xsnipers;

        while (xsnipers)
        {
            Square xsniper = pop_lsb(xsnipers);

            Bitboard blocker = between_bb(king_sq(c), xsniper) & xoccupied;
            if (exactly_one(blocker))
            {
                st->blockers[c] |= blocker;
                st->pinners[blocker & pieces(c) ? ~c : c] |= xsniper;
            }
        }

        st->attacks[c][NO_PIECE_TYPE] = 0;
        st->attacks[c][PAWN  ] = attacks_by<PAWN  >(c);
        st->attacks[c][KNIGHT] = attacks_by<KNIGHT>(c) | attacks<PAWN  >(c);
        st->attacks[c][BISHOP] = attacks_by<BISHOP>(c) | attacks<KNIGHT>(c);
        st->attacks[c][ROOK  ] = attacks_by<ROOK  >(c) | attacks<BISHOP>(c);
        st->attacks[c][QUEEN ] = attacks_by<QUEEN >(c) | attacks<ROOK  >(c);
        st->attacks[c][KING  ] = attacks_by<KING  >(c) | attacks<QUEEN >(c);
    }
    // clang-format on
}

// Check can do en-passant
template<bool After>
bool Position::can_enpassant(Color ac, Square epSq, Bitboard* const epAttackers) const noexcept {
    assert(is_ok(epSq));

    // En-passant attackers
    Bitboard attackers = pieces(ac, PAWN) & attacks_bb<PAWN>(epSq, ~ac);
    if (epAttackers != nullptr)
        *epAttackers = attackers;
    if (!attackers)
        return false;

    Square capSq;
    if constexpr (After)
        capSq = epSq - pawn_spush(ac);
    else
        capSq = epSq + pawn_spush(ac);
    assert(pieces(~ac, PAWN) & capSq);

    Square kingSq = king_sq(ac);

    if constexpr (After)
    {
        // If there are checkers other than the to be captured pawn, ep is never legal
        if (checkers() & ~square_bb(capSq))
            return false;

        // If there are two pawns potentially being abled to capture
        if (more_than_one(attackers))
        {
            // If at least one is not pinned, ep is legal as there are no horizontal exposed checks
            if (!more_than_one(attackers & blockers(ac)))
                return true;

            Bitboard kingFile = file_bb(kingSq);
            // If there is no pawn on our king's file and thus both pawns are pinned by bishops.
            if (!(attackers & kingFile))
                return false;
            // Otherwise remove the pawn on the king file, as an ep capture by it can never be legal
            attackers &= ~kingFile;
            if (epAttackers != nullptr)
                *epAttackers = attackers;
        }
    }

    bool epCheck = false;
    // Check en-passant is legal for the position
    Bitboard occupied = pieces() ^ make_bitboard(capSq, epSq);
    while (attackers)
    {
        Square s = pop_lsb(attackers);
        if (!(slide_attackers_to(kingSq, occupied ^ s) & pieces(~ac)))
        {
            epCheck = true;
            if (epAttackers == nullptr)
                break;
        }
        else if (epAttackers != nullptr)
            *epAttackers ^= s;
    }
    return epCheck;
}

// Helper used to do/undo a castling move.
// This is a bit tricky in Chess960 where org/dst squares can overlap.
template<bool Do>
void Position::do_castling(
  Color ac, Square org, Square& dst, Square& rOrg, Square& rDst, DirtyPiece* const dp) noexcept {
    assert(!Do || dp != nullptr);

    rOrg = dst;  // Castling is encoded as "king captures rook"
    rDst = rook_castle_sq(ac, org, dst);
    dst  = king_castle_sq(ac, org, dst);

    Piece king = piece_on(Do ? org : dst);
    assert(king == make_piece(ac, KING));
    Piece rook = piece_on(Do ? rOrg : rDst);
    assert(rook == make_piece(ac, ROOK));

    bool kingMoved = org != dst;
    bool rookMoved = rOrg != rDst;

    if constexpr (Do)
    {
        dp->dst = dst;

        if (rookMoved)
        {
            dp->removeSq = rOrg;
            dp->addSq    = rDst;
            dp->removePc = dp->addPc = rook;
        }

        st->kingSq[ac]     = dst;
        st->hasCastled[ac] = true;
    }

    // Remove both pieces first since squares could overlap in Chess960
    if (kingMoved)
        remove_piece(Do ? org : dst);
    if (rookMoved)
        remove_piece(Do ? rOrg : rDst);
    if (kingMoved)
        put_piece(Do ? dst : org, king);
    if (rookMoved)
        put_piece(Do ? rDst : rOrg, rook);
}

// Makes a move, and saves all necessary information to new state.
// The move is assumed to be legal.
DirtyPiece
Position::do_move(Move m, State& newSt, bool check, const TranspositionTable* tt) noexcept {
    assert(legal(m));
    assert(&newSt != st);

    Key k = st->key ^ Zobrist::turn;

    // Copy relevant fields from the old state to the new state,
    // excluding those that will recomputed from scratch anyway and
    // then switch the state pointer to point to the new state.
    std::memcpy(&newSt, st, offsetof(State, key));
    newSt.preSt = st;

    st = &newSt;

    // Increment ply counters. rule50Count will be reset to zero later on
    // in case of a capture or a pawn move.
    ++gamePly;
    ++st->rule50Count;
    st->hasRule50High = st->hasRule50High || st->rule50Count >= rule50_threshold();

    ++st->nullPly;

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  movedPiece = piece_on(org);
    Piece  capturedPiece =
      m.type_of() != EN_PASSANT ? piece_on(dst) : piece_on(dst - pawn_spush(ac));
    Piece promotedPiece = NO_PIECE;
    assert(color_of(movedPiece) == ac);
    assert(!is_ok(capturedPiece)
           || (color_of(capturedPiece) == (m.type_of() != CASTLING ? ~ac : ac)
               && type_of(capturedPiece) != KING));

    DirtyPiece dp;
    dp.pc    = movedPiece;
    dp.org   = org;
    dp.dst   = dst;
    dp.addSq = SQ_NONE;

    // Reset en-passant square
    if (is_ok(ep_sq()))
    {
        k ^= Zobrist::enpassant[file_of(ep_sq())];
        reset_ep_sq();
    }

    bool epCheck = false;
    //bool rookMoved = false;

    // If the move is a castling, do some special work
    if (m.type_of() == CASTLING)
    {
        assert(movedPiece == make_piece(ac, KING));
        assert(capturedPiece == make_piece(ac, ROOK));
        assert(can_castle(ac & ANY_CASTLING));
        assert(!has_castled(ac));

        Square rOrg, rDst;
        do_castling<true>(ac, org, dst, rOrg, rDst, &dp);
        assert(rOrg == m.dst_sq());
        //rookMoved = rOrg != rDst;

        // clang-format off
        k                   ^= Zobrist::psq[capturedPiece][rOrg] ^ Zobrist::psq[capturedPiece][rDst];
        st->groupKey[ac][1] ^= Zobrist::psq[capturedPiece][rOrg] ^ Zobrist::psq[capturedPiece][rDst];
        capturedPiece = NO_PIECE;
        // clang-format on

        // Calculate checker only one ROOK possible (if move is check)
        st->checkers = check ? square_bb(rDst) : 0;
        assert(!check || (checkers() && popcount(checkers()) == 1));

        goto DO_MOVE_END;
    }

    if (is_ok(capturedPiece))
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

                assert(movedPiece == make_piece(ac, PAWN));
                assert(relative_rank(ac, org) == RANK_5);
                assert(relative_rank(ac, dst) == RANK_6);
                assert(pieces(~ac, PAWN) & capSq);
                assert(!(pieces() & make_bitboard(dst, dst + pawn_spush(ac))));
                assert(!is_ok(ep_sq()));  // Already reset to SQ_NONE
                assert(rule50_count() == 1);
                assert(st->preSt->epSq == dst);
                assert(st->preSt->rule50Count == 0);
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

        dp.removeSq = capSq;
        dp.removePc = capturedPiece;

        st->capSq = dst;
        // Remove the captured piece
        remove_piece(capSq);
        // Update hash key
        k ^= Zobrist::psq[capturedPiece][capSq];
        // Reset rule 50 draw counter
        reset_rule50_count();
    }

    move_piece(org, dst);

    // If the moving piece is a pawn do some special extra work
    if (type_of(movedPiece) == PAWN)
    {
        if (m.type_of() == PROMOTION)
        {
            assert(relative_rank(ac, org) == RANK_7);
            assert(relative_rank(ac, dst) == RANK_8);

            auto promoted = m.promotion_type();
            assert(KNIGHT <= promoted && promoted <= QUEEN);

            promotedPiece = make_piece(ac, promoted);

            dp.dst   = SQ_NONE;
            dp.addSq = dst;
            dp.addPc = promotedPiece;

            remove_piece(dst);
            put_piece(dst, promotedPiece);
            assert(count(promotedPiece));
            assert(!Zobrist::psq[movedPiece][dst]);
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
        if (type_of(movedPiece) == KING)
            st->kingSq[ac] = dst;
        else
            st->groupKey[ac][is_major(type_of(movedPiece))] ^=
              Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];
    }

    // Calculate checkers (if move is check)
    st->checkers = check ? attackers_to(king_sq(~ac)) & pieces(ac) : 0;
    assert(!check || (checkers() && popcount(checkers()) <= 2));

DO_MOVE_END:

    // Update hash key
    k ^= Zobrist::psq[movedPiece][org] ^ Zobrist::psq[movedPiece][dst];

    // Update castling rights if needed
    if (int cr; castling_rights() && (cr = castling_rights_mask(org, dst)))
    {
        k ^= Zobrist::castling[castling_rights()];
        st->castlingRights &= ~cr;
        k ^= Zobrist::castling[castling_rights()];
    }
    // Speculative prefetch as early as possible
    if (tt != nullptr && !epCheck)
        tt->prefetch_key(adjust_key(k));

    st->capturedPiece = capturedPiece;
    st->promotedPiece = promotedPiece;

    activeColor = ~ac;

    // Update king attacks used for fast check detection
    set_ext_state();

    if (epCheck && can_enpassant(active_color(), dst - pawn_spush(ac)))
    {
        st->epSq = dst - pawn_spush(ac);
        k ^= Zobrist::enpassant[file_of(ep_sq())];
    }

    // Set the key with the updated key
    st->key = k;
    // Speculative prefetch as early as possible
    if (tt != nullptr)
        tt->prefetch_key(key());

    // Calculate the repetition info.
    // It is the ply distance from the previous occurrence of the same position,
    // negative in the 3-fold case, or zero when the position was not repeated.
    st->repetition = 0;

    auto end = std::min(rule50_count(), null_ply());
    if (end >= 4)
    {
        auto* pSt = st->preSt->preSt;
        for (std::int16_t i = 4; i <= end; i += 2)
        {
            pSt = pSt->preSt->preSt;
            if (pSt->key == st->key)
            {
                st->repetition = pSt->repetition ? -i : +i;
                break;
            }
        }
    }

    assert(pos_is_ok());

    assert(is_ok(dp.pc));
    assert(is_ok(dp.org));
    assert(is_ok(dp.dst) ^ (m.type_of() == PROMOTION));
    // The way castling is implemented, this check may fail in Chess960.
    //assert(is_ok(dp.removeSq) ^ (m.type_of() != CASTLING && !is_ok(capturedPiece)));
    //assert(is_ok(dp.addSq) ^ (m.type_of() != PROMOTION && m.type_of() != CASTLING));
    return dp;
}

// Unmakes a move, restoring the position to its exact state before the move was made.
void Position::undo_move(Move m) noexcept {

    Color ac = activeColor = ~active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(dst);

    assert(empty_on(org) || m.type_of() == CASTLING);

    Piece capturedPiece = captured_piece();
    assert(!is_ok(capturedPiece) || type_of(capturedPiece) != KING);

    if (m.type_of() == CASTLING)
    {
        assert(pieces(ac, KING) & king_castle_sq(ac, org, dst));
        assert(pieces(ac, ROOK) & rook_castle_sq(ac, org, dst));
        assert(!is_ok(capturedPiece));
        assert(has_castled(ac));

        Square rOrg, rDst;
        do_castling<false>(ac, org, dst, rOrg, rDst);
        assert(rOrg == m.dst_sq());

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

    if (is_ok(capturedPiece))
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
            assert(st->preSt->epSq == dst);
            assert(st->preSt->rule50Count == 0);
        }
        // Restore the captured piece
        put_piece(capSq, capturedPiece);
    }

UNDO_MOVE_END:

    --gamePly;
    // Finally point state pointer back to the previous state
    st = st->preSt;

    assert(legal(m));
    assert(pos_is_ok());
}

// Makes a null move
// It flips the active color without executing any move on the board.
void Position::do_null_move(State& newSt, const TranspositionTable* tt) noexcept {
    assert(&newSt != st);
    assert(!checkers());

    std::memcpy(&newSt, st, offsetof(State, preSt));
    newSt.preSt = st;

    st = &newSt;

    st->capturedPiece = NO_PIECE;
    st->promotedPiece = NO_PIECE;
    st->capSq         = SQ_NONE;

    // NOTE: no ++st->rule50Count here
    st->nullPly = 0;

    st->key ^= Zobrist::turn;

    if (is_ok(ep_sq()))
    {
        st->key ^= Zobrist::enpassant[file_of(ep_sq())];
        reset_ep_sq();
    }
    // Speculative prefetch as early as possible
    if (tt != nullptr)
        tt->prefetch_key(key());

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
    assert(!is_ok(cap_sq()));

    activeColor = ~active_color();

    st = st->preSt;

    assert(pos_is_ok());
}

// Takes a random move and tests whether the move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal(Move m) const noexcept {
    assert(m.is_ok());

    Color ac = active_color();

    assert(piece_on(king_sq(ac)) == make_piece(ac, KING));

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(org);

    // If the origin square is not occupied by a piece belonging to
    // the side to move, the move is obviously not legal.
    if (!is_ok(pc) || color_of(pc) != ac)
        return false;

    if (m.type_of() == CASTLING)
    {
        CastlingRights cr = make_castling_rights(ac, org, dst);
        return type_of(pc) == KING && (pieces(ac, ROOK) & dst) && !checkers()
            && relative_rank(ac, org) == RANK_1 && relative_rank(ac, dst) == RANK_1
            && can_castle(cr) && !castling_impeded(cr) && castling_rook_sq(cr) == dst;
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
            return !has_attackers_to(pieces(~ac), dst, pieces() ^ org);
        break;

    case PROMOTION :
        if (!(type_of(pc) == PAWN  //&& (PROMOTION_RANK_BB & dst)
              && relative_rank(ac, org) == RANK_7 && relative_rank(ac, dst) == RANK_8
              && ((org + pawn_spush(ac) == dst && !(pieces() & dst))
                  || ((attacks_bb<PAWN>(org, ac) & pieces(~ac)) & dst))))
            return false;
        break;

    case EN_PASSANT :
        if (!(type_of(pc) == PAWN && ep_sq() == dst && rule50_count() == 0
              && relative_rank(ac, org) == RANK_5 && relative_rank(ac, dst) == RANK_6
              && (pieces(~ac, PAWN) & (dst - pawn_spush(ac)))
              && !(pieces() & make_bitboard(dst, dst + pawn_spush(ac)))
              && ((attacks_bb<PAWN>(org, ac) /*& ~pieces()*/) & dst)
              && !(slide_attackers_to(king_sq(ac),
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
             && ((between_bb(king_sq(ac), lsb(checkers())) & dst)
                 || (m.type_of() == EN_PASSANT && (checkers() & (dst - pawn_spush(ac)))))))
        && (type_of(pc) == PAWN || !(blockers(ac) & org) || aligned(king_sq(ac), org, dst));
}

// Tests whether a pseudo-legal move is legal
bool Position::legal(Move m) const noexcept {
    assert(pseudo_legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    switch (m.type_of())
    {
    // En-passant captures are a tricky special case. Because they are rather uncommon,
    // Simply by testing whether the king is attacked after the move is made.
    case EN_PASSANT :
        assert(piece_on(org) == make_piece(ac, PAWN));
        assert(ep_sq() == dst);
        assert(rule50_count() == 0);
        assert(relative_rank(ac, org) == RANK_5);
        assert(relative_rank(ac, dst) == RANK_6);
        assert(pieces(~ac, PAWN) & (dst - pawn_spush(ac)));
        assert(!(pieces() & make_bitboard(dst, dst + pawn_spush(ac))));
        assert((attacks_bb<PAWN>(org, ac) /*& ~pieces()*/) & dst);
        assert(!(
          slide_attackers_to(king_sq(ac), pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac)))
          & pieces(~ac)));

        return true;

    // Castling moves generation does not check if the castling path is clear of
    // enemy attacks, it is delayed at a later time: now!
    case CASTLING : {
        assert(piece_on(org) == make_piece(ac, KING));
        assert(org == king_sq(ac));
        assert(pieces(ac, ROOK) & dst);
        assert(!checkers());
        assert(relative_rank(ac, org) == RANK_1);
        assert(relative_rank(ac, dst) == RANK_1);
        assert(can_castle(make_castling_rights(ac, org, dst)));
        assert(!castling_impeded(make_castling_rights(ac, org, dst)));
        assert(castling_rook_sq(make_castling_rights(ac, org, dst)) == dst);

        // After castling, the rook and king final positions are the same in
        // Chess960 as they would be in standard chess.
        Square    kDst = king_castle_sq(ac, org, dst);
        Direction step = org < kDst ? WEST : EAST;
        for (Square s = kDst; s != org; s += step)
            if (has_attackers_to(pieces(~ac), s))
                return false;

        // In case of Chess960, verify if the Rook blocks some checks.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !(blockers(ac) & dst);
    }
    case NORMAL :
        // For king moves, return true
        if (type_of(piece_on(org)) == KING)
        {
            assert(!has_attackers_to(pieces(~ac), dst, pieces() ^ org));
            return true;
        }
        break;

    case PROMOTION :
        assert(piece_on(org) == make_piece(ac, PAWN));
        assert(relative_rank(ac, org) == RANK_7);
        assert(relative_rank(ac, dst) == RANK_8);
        assert((org + pawn_spush(ac) == dst && !(pieces() & dst))
               || ((attacks_bb<PAWN>(org, ac) & pieces(~ac)) & dst));
        break;
    }

    // For non-king move, check it is not pinned or it is moving along the line from the king.
    return type_of(piece_on(org)) != PAWN || !(blockers(ac) & org)
        || aligned(king_sq(ac), org, dst);
}

// Tests whether a pseudo-legal move is a check
bool Position::check(Move m) const noexcept {
    assert(pseudo_legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    if (
      // Is there a direct check?
      (checks(m.type_of() != PROMOTION ? type_of(piece_on(org)) : m.promotion_type()) & dst)
      // Is there a discovered check?
      || ((blockers(~ac) & org)  //
          && (!aligned(king_sq(~ac), org, dst) || m.type_of() == CASTLING)))
        return true;

    switch (m.type_of())
    {
    case NORMAL :
        return false;

    case PROMOTION :
        return attacks_bb(m.promotion_type(), dst, pieces() ^ org) & king_sq(~ac);

    // En-passant capture with check? Already handled the case of direct check
    // and ordinary discovered check, so the only case need to handle is
    // the unusual case of a discovered check through the captured pawn.
    case EN_PASSANT :
        return slide_attackers_to(king_sq(~ac),
                                  pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac)))
             & pieces(ac);

    case CASTLING :
        // Castling is encoded as "king captures rook"
        return checks(ROOK) & rook_castle_sq(ac, org, dst);
    }
    assert(false);
    return false;
}

bool Position::dbl_check(Move m) const noexcept {
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
          && (blockers(~ac) & org) && !aligned(king_sq(~ac), org, dst);

    case PROMOTION :
        return (blockers(~ac) & org)  //
            && (attacks_bb(m.promotion_type(), dst, pieces() ^ org) & king_sq(~ac));

    case EN_PASSANT : {
        Bitboard checkers =
          slide_attackers_to(king_sq(~ac), pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac)))
          & pieces(ac);
        return more_than_one(checkers) || (checkers && (checks(PAWN) & dst));
    }
    case CASTLING :
        return false;
    }
    assert(false);
    return false;
}

bool Position::fork(Move m) const noexcept {
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
        if (pc != W_KING && pc != B_KING && count(pc))
            materialKey ^= Zobrist::psq[pc][PawnOffset + count(pc) - 1];

    return materialKey;
}

// Computes the new hash key after the given move.
// Needed for speculative prefetch.
// It does recognize special moves like castling, en-passant and promotions.
Key Position::move_key(Move m) const noexcept {
    Key moveKey = st->key ^ Zobrist::turn;

    if (is_ok(ep_sq()))
        moveKey ^= Zobrist::enpassant[file_of(ep_sq())];

    if (m == Move::Null)
        return moveKey;

    assert(legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  movedPiece    = piece_on(org);
    Square capSq         = m.type_of() != EN_PASSANT ? dst : dst - pawn_spush(ac);
    Piece  capturedPiece = piece_on(capSq);
    assert(color_of(movedPiece) == ac);
    assert(!is_ok(capturedPiece)
           || color_of(capturedPiece) == (m.type_of() != CASTLING ? ~ac : ac));
    assert(type_of(capturedPiece) != KING);

    moveKey ^=
      Zobrist::psq[movedPiece][org]
      ^ Zobrist::psq[m.type_of() != PROMOTION ? movedPiece : make_piece(ac, m.promotion_type())]
                    [m.type_of() != CASTLING ? dst : king_castle_sq(ac, org, dst)];
    if (int cr; castling_rights() && (cr = castling_rights_mask(org, dst)))
        moveKey ^= Zobrist::castling[castling_rights()]  //
                 ^ Zobrist::castling[castling_rights() & ~cr];

    if (m.type_of() == CASTLING)
    {
        assert(movedPiece == make_piece(ac, KING));
        assert(capturedPiece == make_piece(ac, ROOK));
        // ROOK
        moveKey ^= Zobrist::psq[capturedPiece][dst]
                 ^ Zobrist::psq[capturedPiece][rook_castle_sq(ac, org, dst)];
        //capturedPiece = NO_PIECE;
        return adjust_key(moveKey, 1);
    }

    if (type_of(movedPiece) == PAWN && (int(dst) ^ int(org)) == NORTH_2
        && can_enpassant<false>(~ac, dst - pawn_spush(ac)))
    {
        assert(relative_rank(ac, org) == RANK_2);
        assert(relative_rank(ac, dst) == RANK_4);

        moveKey ^= Zobrist::enpassant[file_of(dst)];

        return moveKey;
    }

    moveKey ^= Zobrist::psq[capturedPiece][capSq];

    return is_ok(capturedPiece) || type_of(movedPiece) == PAWN ? moveKey : adjust_key(moveKey, 1);
}

// Tests if the SEE (Static Exchange Evaluation) value of the move
// is greater or equal to the given threshold.
// An algorithm similar to alpha-beta pruning with a null window.
bool Position::see_ge(Move m, int threshold) const noexcept {
    assert(pseudo_legal(m));

    // Not deal with castling, can't win any material, nor can lose any.
    if (m.type_of() == CASTLING)
        return threshold <= 0;

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(!empty_on(org) && color_of(piece_on(org)) == ac);

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
        && can_enpassant<false>(~ac, dst - pawn_spush(ac), &epAttackers))
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

    bool discovery[COLOR_NB]{true, true};

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
                acAttackers &= ~between_bb(king_sq(ac), pop_lsb(b));

            if (!acAttackers)
                break;
        }
        if ((blockers(ac) & org)
            && (b = pinners(ac) & pieces(~ac) & line_bb(org, king_sq(ac)) & occupied)
            && ((pt = type_of(piece_on(org))) != PAWN || !aligned(king_sq(ac), org, dst)))
        {
            acAttackers &= king_sq(ac);

            if (!acAttackers  //
                && (pt == PAWN || !(attacks_bb(pt, dst, occupied) & king_sq(ac))))
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

            if (!(pinners(~ac) & pieces(ac) & line_bb(king_sq(~ac), sq) & occupied))
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
            if ((swap = VALUE_QUEEN - swap) < win)
                break;
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

        sq = king_sq(~ac);
        if ((occupied & sq) && has_attackers_to(acAttackers, sq, occupied))
            return true;

        // Even when one of our non-queen pieces attacks opponent queen after exchanges
        Bitboard queen = pieces(~ac, QUEEN) & ~attackers & occupied;

        sq = queen ? lsb(queen) : SQ_NONE;
        if (is_ok(sq) && has_attackers_to(acAttackers & ~pieces(QUEEN), sq, occupied))
            return true;
    }

    return win;
}

// Draw by Repetition: position repeats once earlier but strictly
// after the root, or repeats twice before or at the root.
bool Position::is_repetition(std::int16_t ply) const noexcept {
    return repetition() && repetition() < ply;
}

// Tests whether the current position is drawn by repetition or by 50-move rule.
// It also detect stalemates.
bool Position::is_draw(std::int16_t ply, bool rule50Enabled, bool stalemateEnabled) const noexcept {
    return
      // Draw by Repetition
      is_repetition(ply)
      // Draw by 50-move rule
      || (rule50Enabled && rule50_count() >= 2 * DrawMoveCount
          && (!checkers() || !MoveList<LEGAL, true>(*this).empty()))
      // Draw by Stalemate
      || (stalemateEnabled && !checkers() && MoveList<LEGAL, true>(*this).empty());
}

// Tests whether there has been at least one repetition
// of positions since the last capture or pawn move.
bool Position::has_repeated() const noexcept {
    auto end = std::min(rule50_count(), null_ply());
    if (end < 4)
        return false;

    auto* cSt = st;
    while (end-- >= 4)
    {
        if (cSt->repetition)
            return true;
        cSt = cSt->preSt;
    }
    return false;
}

// Tests if the current position has a move which draws by repetition.
// Accurately matches the outcome of is_draw() over all legal moves.
bool Position::is_upcoming_repetition(std::int16_t ply) const noexcept {
    auto end = std::min(rule50_count(), null_ply());
    // Enough reversible moves played
    if (end < 3)
        return false;

    Key   baseKey = st->key;
    auto* pSt     = st->preSt;
    Key   iterKey = baseKey ^ pSt->key ^ Zobrist::turn;

    for (std::int16_t i = 3; i <= end; i += 2)
    {
        iterKey ^= pSt->preSt->key ^ pSt->preSt->preSt->key ^ Zobrist::turn;
        pSt = pSt->preSt->preSt;

        // Opponent pieces have reverted
        if (iterKey)
            continue;

        Key moveKey = baseKey ^ pSt->key;
        // 'moveKey' is a single move
        auto index = Cuckoos.find_key(moveKey);
        if (index >= Cuckoos.size())
            continue;

        Move m = Cuckoos[index].move;
        assert(m != Move::None);

        // Move path is obstructed
        if (pieces() & between_ex_bb(m.org_sq(), m.dst_sq()))
            continue;

#if !defined(NDEBUG)
        // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in the same location
        if (empty_on(m.org_sq()))
            m = m.reverse();
        assert(pseudo_legal(m) && legal(m) && MoveList<LEGAL>(*this).contains(m));
#endif
        if (i < ply
            // For nodes before or at the root, check that the move is
            // a repetition rather than a move to the current position.
            || pSt->repetition)
            return true;
    }
    return false;
}

// Flips the current position with the white and black sides reversed.
// This is only useful for debugging e.g. for finding evaluation symmetry bugs.
void Position::flip() noexcept {
    std::istringstream iss{fen()};

    std::string fens, token;
    // Piece placement (vertical flip)
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        std::getline(iss, token, r > RANK_1 ? '/' : ' ');
        fens.insert(0, token + (r < RANK_8 ? '/' : ' '));
    }

    // Active color (will be lowercased later)
    iss >> token;
    token[0] = (token[0] == 'w' ? 'B' : token[0] == 'b' ? 'W' : '-');
    fens += token + " ";

    // Castling rights
    iss >> token;
    fens += token + " ";

    fens = toggle_case(fens);

    // En-passant square
    iss >> token;
    if (token != "-")
        token[1] = flip_rank(token[1]);
    fens += token;

    std::getline(iss, token);  // Half and full moves
    fens += token;

    set(fens, st);

    assert(pos_is_ok());
}

void Position::mirror() noexcept {
    std::istringstream iss{fen()};

    std::string fens, token;
    // Piece placement (horizontal flip)
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        std::getline(iss, token, r > RANK_1 ? '/' : ' ');
        std::reverse(token.begin(), token.end());
        fens += token + (r > RANK_1 ? '/' : ' ');
    }

    // Active color (will remain the same)
    iss >> token;
    fens += token + " ";

    // Castling rights
    iss >> token;
    if (token != "-")
        for (auto& ch : token)
        {
            switch (ch)
            {
                // clang-format off
            case 'K' : ch = 'Q'; break;
            case 'Q' : ch = 'K'; break;
            case 'k' : ch = 'q'; break;
            case 'q' : ch = 'k'; break;
            default :  ch = flip_file(ch);
                // clang-format on
            }
        }
    fens += token + " ";

    // En-passant square (flip the file)
    iss >> token;
    if (token != "-")
        token[0] = flip_file(token[0]);
    fens += token;

    std::getline(iss, token);  // Half and full moves
    fens += token;

    set(fens, st);

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

    if (is_ok(ep_sq()))
        key ^= Zobrist::enpassant[file_of(ep_sq())];

    if (active_color() == BLACK)
        key ^= Zobrist::turn;

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
        || piece_on(king_sq(WHITE)) != W_KING                 //
        || piece_on(king_sq(BLACK)) != B_KING                 //
        || distance(king_sq(WHITE), king_sq(BLACK)) <= 1
        || (is_ok(ep_sq()) && !can_enpassant(active_color(), ep_sq())))
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

    if (has_attackers_to(pieces(active_color()), king_sq(~active_color())))
        assert(false && "Position::pos_is_ok(): King Checker");

    if ((pieces(PAWN) & PROMOTION_RANK_BB) || count(W_PAWN) > 8 || count(B_PAWN) > 8)
        assert(false && "Position::pos_is_ok(): Pawns");

    for (Color c : {WHITE, BLACK})
        if (count<PAWN>(c)                                                        //
              + std::max(count<KNIGHT>(c) - 2, 0)                                 //
              + std::max(popcount(pieces(c, BISHOP) & color_bb<WHITE>()) - 1, 0)  //
              + std::max(popcount(pieces(c, BISHOP) & color_bb<BLACK>()) - 1, 0)  //
              + std::max(count<ROOK>(c) - 2, 0)                                   //
              + std::max(count<QUEEN>(c) - 1, 0)                                  //
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

            if (!is_ok(castling_rook_sq(cr))  //
                || !(pieces(c, ROOK) & castling_rook_sq(cr))
                || (castlingRightsMask[c * FILE_NB + file_of(castling_rook_sq(cr))]) != cr
                || (castlingRightsMask[c * FILE_NB + file_of(king_sq(c))] & cr) != cr)
                assert(false && "Position::pos_is_ok(): Castling");
        }

    return true;
}
#endif

std::ostream& operator<<(std::ostream& os, const Position::Board::Cardinal& cardinal) noexcept {
    for (File f = FILE_A; f <= FILE_H; ++f)
        os << " | " << to_char(cardinal.piece_on(f));
    os << " | ";

    return os;
}

std::ostream& operator<<(std::ostream& os, const Position::Board& board) noexcept {
    constexpr std::string_view Sep{"\n  +---+---+---+---+---+---+---+---+\n"};

    os << Sep;
    for (Rank r = RANK_8; r >= RANK_1; --r)
        os << to_char(r) << board.cardinals[r] << Sep;
    os << " ";
    for (File f = FILE_A; f <= FILE_H; ++f)
        os << "   " << to_char<true>(f);
    os << "\n";

    return os;
}

// Returns an ASCII representation of the position
std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept {

    os << pos.board                                           //
       << "\nFen: " << pos.fen()                              //
       << "\nKey: " << u64_to_string(pos.key())               //
       << "\nKing Squares: "                                  //
       << to_square(pos.king_sq(pos.active_color())) << ", "  //
       << to_square(pos.king_sq(~pos.active_color()))         //
       << "\nCheckers: ";
    Bitboard checkers = pos.checkers();
    if (checkers)
        while (checkers)
            os << to_square(pop_lsb(checkers)) << " ";
    else
        os << "(none)";
    os << "\nRepetition: " << pos.repetition();

    if (Tablebases::MaxCardinality >= pos.count<ALL_PIECE>() && !pos.can_castle(ANY_CASTLING))
    {
        State st;

        Position p;
        p.set(pos.fen(), &st);

        Tablebases::ProbeState wdlPs, dtzPs;

        auto wdlScore = Tablebases::probe_wdl(p, &wdlPs);
        auto dtzScore = Tablebases::probe_dtz(p, &dtzPs);
        os << "\nTablebases WDL: " << std::setw(4) << int(wdlScore) << " (" << int(wdlPs) << ")"
           << "\nTablebases DTZ: " << std::setw(4) << int(dtzScore) << " (" << int(dtzPs) << ")";
    }

    return os;
}

}  // namespace DON
