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
#include <iomanip>
#include <sstream>
#include <utility>

#include "movegen.h"
#include "prng.h"
#include "syzygy/tbbase.h"
#include "tt.h"

namespace DON {

namespace {

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
    static_assert((Size & (Size - 1)) == 0, "Size has to be a power of 2");

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
        ++count;
    }

    void clear() noexcept {
        cuckoos.fill({0, Move::None});
        count = 0;
    }

    // Prepare the cuckoo tables
    void init() noexcept {
        clear();

        for (Color c : {WHITE, BLACK})
            for (PieceType pt : PIECE_TYPES)
            {
                if (pt == PAWN)
                    continue;
                for (Square s1 = SQ_A1; s1 < SQ_H8; ++s1)
                    for (Square s2 = s1 + 1; s2 <= SQ_H8; ++s2)
                        if (attacks_bb(s1, pt) & s2)
                        {
                            Key key = Zobrist::piece_square(c, pt, s1)
                                    ^ Zobrist::piece_square(c, pt, s2)  //
                                    ^ Zobrist::turn();

                            Move move = Move(s1, s2);

                            insert({key, move});
                        }
            }
        assert(count == 3668);
    }

    std::size_t find_key(Key key) const noexcept {
        if (std::size_t index;  //
            (index = H<0>(key), cuckoos[index].key == key)
            || (index = H<1>(key), cuckoos[index].key == key))
            return index;
        return size();
    }

   private:
    StdArray<Cuckoo, Size> cuckoos;
    std::size_t            count;
};

CuckooTable<0x2000> Cuckoos;

}  // namespace

void Zobrist::init() noexcept {
    PRNG<XoShiRo256Star> prng(0x105524ULL);

    const auto prng_rand = [&] { return prng.template rand<Key>(); };

    for (Color c : {WHITE, BLACK})
    {
        for (PieceType pt : PIECE_TYPES)
            std::generate(PieceSquare[c][pt].begin(), PieceSquare[c][pt].end(), prng_rand);

        auto itr1 = PieceSquare[c][PAWN].begin() + SQ_A1;
        std::fill(itr1, itr1 + PAWN_OFFSET, 0);

        auto itr2 = PieceSquare[c][PAWN].begin() + SQ_A8;
        std::fill(itr2, itr2 + PAWN_OFFSET, 0);
    }

    std::generate(Castling.begin(), Castling.end(), prng_rand);

    std::generate(Enpassant.begin(), Enpassant.end(), prng_rand);

    Turn = prng_rand();

    std::generate(MR50.begin(), MR50.end(), prng_rand);
}

void State::clear() noexcept {
    std::memset(this, 0, sizeof(*this));

    enPassantSq = capturedSq = SQ_NONE;
}

// Called at startup to initialize the Zobrist and Cuckoo tables.
void Position::init() noexcept {

    Zobrist::init();

    Cuckoos.init();
}

// Default constructor
Position::Position() noexcept { construct(); }

void Position::construct() noexcept {
    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
            pieceList[c][pt].set(OFFSET[pt - 1], CAPACITY[pt - 1]);
}

void Position::clear() noexcept {
    std::memset(squaresTable.data(), SQ_NONE, sizeof(squaresTable));
    // No need to clear indexMap as it is always overwritten when putting/removing pieces
    std::memset(indexMap.data(), INDEX_NONE, sizeof(indexMap));
    std::memset(pieceMap.data(), NO_PIECE, sizeof(pieceMap));
    std::memset(typeBB.data(), 0, sizeof(typeBB));
    std::memset(colorBB.data(), 0, sizeof(colorBB));
    std::memset(pieceCount.data(), 0, sizeof(pieceCount));
    std::memset(castlingRightsMask.data(), 0, sizeof(castlingRightsMask));

    for (std::size_t i = 0; i < castlings.size(); ++i)
        castlings[i].clear();
    // Don't memset pieceList, as they point to the above lists
    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
            pieceList[c][pt].clear();

    st          = nullptr;
    gamePly     = 0;
    activeColor = COLOR_NB;
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

    clear();

    newSt->clear();

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
        else if ('1' <= token && token <= '8')
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
                ++file;
            }
            else
                assert(false && "Position::set(): Invalid Piece");
        }
    }

    assert(file <= FILE_NB && rank == RANK_1);
    assert(count<PAWN>(WHITE) + count<KNIGHT>(WHITE) + count<BISHOP>(WHITE)  //
               + count<ROOK>(WHITE) + count<QUEEN>(WHITE) + count<KING>(WHITE)
             <= 16
           && count<PAWN>(BLACK) + count<KNIGHT>(BLACK) + count<BISHOP>(BLACK)  //
                  + count<ROOK>(BLACK) + count<QUEEN>(BLACK) + count<KING>(BLACK)
                <= 16);
    assert(count<PAWN>() + count<KNIGHT>() + count<BISHOP>()  //
             + count<ROOK>() + count<QUEEN>() + count<KING>()
           == count());
    assert(count(W_PAWN) <= 8 && count(B_PAWN) <= 8);
    assert(count(W_KING) == 1 && count(B_KING) == 1);
    assert(is_ok(square<KING>(WHITE)) && is_ok(square<KING>(BLACK)));
    assert(!(pieces(PAWN) & PROMOTION_RANKS_BB));
    assert(distance(square<KING>(WHITE), square<KING>(BLACK)) > 1);

    iss >> std::ws;

    // 2. Active color
    iss >> token;

    switch (std::tolower(token))
    {
    case 'w' :
        activeColor = WHITE;
        break;
    case 'b' :
        activeColor = BLACK;
        break;
    default :
        assert(false && "Position::set(): Invalid Active Color");
        break;
    }

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
        token   = std::tolower(token);

        if (relative_rank(c, square<KING>(c)) != RANK_1)
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

        if (token == 'k')
        {
            rsq = relative_sq(c, SQ_H1);
            while (file_of(rsq) >= FILE_C && !(rooks & rsq) && rsq != square<KING>(c))
                --rsq;
        }
        else if (token == 'q')
        {
            rsq = relative_sq(c, SQ_A1);
            while (file_of(rsq) <= FILE_F && !(rooks & rsq) && rsq != square<KING>(c))
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
    Square enPassantSq = SQ_NONE;
    bool   epCheck     = false;

    iss >> token;
    if (token != '-')
    {
        std::uint8_t epFile = std::tolower(token);
        std::uint8_t epRank;
        iss >> epRank;

        if ('a' <= epFile && epFile <= 'h' && epRank == (ac == WHITE ? '6' : '3'))
        {
            st->enPassantSq = enPassantSq = make_square(to_file(epFile), to_rank(epRank));

            // En-passant square will be considered only if
            // a) there is an enemy pawn in front of epSquare
            // b) there is no piece on epSquare or behind epSquare
            // c) there is atleast one friend pawn threatening epSquare
            // d) there is no enemy Bishop, Rook or Queen pinning
            epCheck = (pieces(~ac, PAWN) & (enPassantSq - pawn_spush(ac)))
                   && !(pieces() & make_bb(enPassantSq, enPassantSq + pawn_spush(ac)))
                   && (pieces(ac, PAWN) & attacks_bb<PAWN>(enPassantSq, ~ac));
        }
        else
            assert(false && "Position::set(): Invalid En-passant square");
    }

    // 5-6. Halfmove clock and fullmove number
    std::int16_t rule50Count = 0;
    std::int16_t moveNum     = 1;
    iss >> std::skipws >> rule50Count >> moveNum;

    st->rule50Count = std::abs(rule50Count);
    // Convert from moveNum starting from 1 to posPly starting from 0,
    // handle also common incorrect FEN with moveNum = 0.
    gamePly = std::max(2 * (std::abs(moveNum) - 1), 0) + (ac == BLACK);

    st->checkers = attackers_to(square<KING>(ac)) & pieces(~ac);

    set_ext_state();

    // Reset illegal fields
    if (is_ok(enPassantSq))
    {
        reset_rule50_count();
        if (epCheck && !can_enpassant(ac, enPassantSq))
            reset_en_passant_sq();
    }
    assert(rule50_count() <= 100);
    gamePly = std::max(ply(), rule50_count());

    set_state();

    assert(_is_ok());
}

// Overload to initialize the position object with the given endgame code string like "KBPKN".
// It's mainly a helper to get the material key out of an endgame code.
void Position::set(std::string_view code, Color c, State* const newSt) noexcept {
    assert(!code.empty() && code[0] == 'K' && code.find('K', 1) != std::string_view::npos);
    assert(is_ok(c));

    StdArray<std::string, COLOR_NB> sides{
      std::string{code.substr(code.find('K', 1))},                // Weak
      std::string{code.substr(0, code.find_first_of("vK", 1))}};  // Strong

    assert(0 < sides[WHITE].size() && sides[WHITE].size() < 8);
    assert(0 < sides[BLACK].size() && sides[BLACK].size() < 8);

    sides[c] = lower_case(sides[c]);

    std::string fens;
    fens.reserve(64);
    fens += "8/";
    fens += sides[WHITE];
    fens += digit_to_char(8 - sides[WHITE].size());
    fens += "/8/8/8/8/";
    fens += sides[BLACK];
    fens += digit_to_char(8 - sides[BLACK].size());
    fens += "/8 ";
    fens += c == WHITE ? 'w' : 'b';
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
    std::string fens;
    fens.reserve(64);

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
                    fens += digit_to_char(emptyCount);

                fens += to_char(piece_on(s));
                emptyCount = 0;
            }
        }

        // Handle trailing empty squares
        if (emptyCount)
            fens += digit_to_char(emptyCount);

        if (r > RANK_1)
            fens += '/';
    }

    fens += active_color() == WHITE ? " w " : " b ";

    if (has_castling_rights(ANY_CASTLING))
    {
        if (has_castling_rights(WHITE_OO))
            fens += Chess960 ? to_char<true>(file_of(castling_rook_sq(WHITE_OO))) : 'K';
        if (has_castling_rights(WHITE_OOO))
            fens += Chess960 ? to_char<true>(file_of(castling_rook_sq(WHITE_OOO))) : 'Q';
        if (has_castling_rights(BLACK_OO))
            fens += Chess960 ? to_char<false>(file_of(castling_rook_sq(BLACK_OO))) : 'k';
        if (has_castling_rights(BLACK_OOO))
            fens += Chess960 ? to_char<false>(file_of(castling_rook_sq(BLACK_OOO))) : 'q';
    }
    else
        fens += '-';

    fens += ' ';
    if (is_ok(en_passant_sq()))
        fens += to_square(en_passant_sq());
    else
        fens += '-';

    if (full)
    {
        fens += ' ';
        fens += std::to_string(rule50_count());
        fens += ' ';
        fens += std::to_string(move_num());
    }

    return fens;
}

// Sets castling rights given the corresponding color and the rook starting square.
void Position::set_castling_rights(Color c, Square rOrg) noexcept {
    assert(relative_rank(c, rOrg) == RANK_1);
    assert((pieces(c, ROOK) & rOrg));
    assert(castlingRightsMask[c * FILE_NB + file_of(rOrg)] == 0);

    Square kOrg = square<KING>(c);
    assert(relative_rank(c, kOrg) == RANK_1);
    assert((pieces(c, KING) & kOrg));

    CastlingRights cs = castling_side(kOrg, rOrg);

    CastlingRights cr = c & cs;

    assert(!is_ok(castling_rook_sq(cr)));

    st->castlingRights |= cr;
    castlingRightsMask[c * FILE_NB + file_of(kOrg)] |= cr;
    castlingRightsMask[c * FILE_NB + file_of(rOrg)] = cr;

    auto& castling = castlings[BIT[cr]];

    Square kDst = king_castle_sq(c, kOrg, rOrg);
    Square rDst = rook_castle_sq(c, kOrg, rOrg);

    Bitboard castlingPath =
      (between_bb(kOrg, kDst) | between_bb(rOrg, rDst)) & ~make_bb(kOrg, rOrg);
    while (castlingPath)
        castling.fullPathSqs[castling.fullPathLen++] = cs == KING_SIDE  //
                                                       ? pop_lsb(castlingPath)
                                                       : pop_msb(castlingPath);

    Bitboard castlingKingPath = between_bb(kOrg, kDst);
    while (castlingKingPath)
        castling.kingPathSqs[castling.kingPathLen++] = cs == KING_SIDE  //
                                                       ? pop_lsb(castlingKingPath)
                                                       : pop_msb(castlingKingPath);

    castling.rookSq = rOrg;
}

// Computes the hash keys of the position, and other data
// that once computed is updated incrementally as moves are made.
// The function is only used when a new position is set up.
void Position::set_state() noexcept {
    assert(st->pawnKey[WHITE] == 0 && st->pawnKey[BLACK] == 0);
    assert(st->nonPawnKey[WHITE][0] == 0 && st->nonPawnKey[BLACK][0] == 0);
    assert(st->nonPawnKey[WHITE][1] == 0 && st->nonPawnKey[BLACK][1] == 0);
    assert(st->key == 0);

    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            const auto& pL = squares(c, pt);
            const auto* pB = base(c);
            for (const Square* s = pL.begin(pB); s != pL.end(pB); ++s)
            {
                Key key = Zobrist::piece_square(c, pt, *s);
                assert(key != 0);

                st->key ^= key;

                if (pt == PAWN)
                    st->pawnKey[c] ^= key;

                else if (pt != KING)
                    st->nonPawnKey[c][is_major(pt)] ^= key;
            }
        }

    st->key ^= Zobrist::castling(castling_rights());

    if (is_ok(en_passant_sq()))
        st->key ^= Zobrist::enpassant(en_passant_sq());

    if (active_color() == BLACK)
        st->key ^= Zobrist::turn();
}

// Set extra state to detect if a move is check
void Position::set_ext_state() noexcept {

    Square kingSq = square<KING>(~active_color());

    // clang-format off
    st->checks[NO_PIECE_TYPE] = 0;
    st->checks[PAWN  ] = attacks_bb<PAWN  >(kingSq, ~active_color());
    st->checks[KNIGHT] = attacks_bb<KNIGHT>(kingSq);
    st->checks[BISHOP] = attacks_bb<BISHOP>(kingSq, pieces());
    st->checks[ROOK  ] = attacks_bb<ROOK  >(kingSq, pieces());
    st->checks[QUEEN ] = checks(BISHOP) | checks(ROOK);
    st->checks[KING  ] = 0;

    st->pinners[WHITE] = st->pinners[BLACK] = 0;

    for (Color c : {WHITE, BLACK})
    {
        st->blockers[c] = blockers_to(square<KING>(c), pieces(~c), st->pinners[c], st->pinners[~c]);

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

    Square kingSq = square<KING>(ac);

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
    Bitboard occupied = pieces() ^ make_bb(capSq, epSq);
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
  Color ac, Square org, Square& dst, Square& rOrg, Square& rDst, DirtyBoard* const db) noexcept {
    assert(!Do || db != nullptr);

    rOrg = dst;  // Castling is encoded as "king captures rook"
    rDst = rook_castle_sq(ac, org, dst);
    dst  = king_castle_sq(ac, org, dst);

    Piece king = piece_on(Do ? org : dst);
    assert(king == make_piece(ac, KING));
    Piece rook = piece_on(Do ? rOrg : rDst);
    assert(rook == make_piece(ac, ROOK));

    // Remove both pieces first since squares could overlap in Chess960
    if constexpr (Do)
    {
        db->dp.dst      = dst;
        db->dp.removeSq = rOrg;
        db->dp.addSq    = rDst;
        db->dp.removePc = db->dp.addPc = rook;

        remove_piece(org, &db->dts);
        remove_piece(rOrg, &db->dts);
        put_piece(dst, king, &db->dts);
        put_piece(rDst, rook, &db->dts);

        st->hasCastled[ac] = true;
    }
    else
    {
        remove_piece(dst);
        remove_piece(rDst);
        put_piece(org, king);
        put_piece(rOrg, rook);
    }
}

// Makes a move, and saves all necessary information to new state.
// The move is assumed to be legal.
DirtyBoard
Position::do_move(Move m, State& newSt, bool isCheck, const TranspositionTable* const tt) noexcept {
    assert(legal(m));
    assert(&newSt != st);

    Key k = st->key ^ Zobrist::turn();

    newSt.switch_to_prefix(st);

    st = &newSt;

    // Increment ply counters. rule50Count will be reset to zero later on
    // in case of a capture or a pawn move.
    ++gamePly;
    ++st->rule50Count;
    st->hasRule50High |= rule50_count() >= rule50_threshold();

    ++st->nullPly;

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  movedPiece    = piece_on(org);
    Piece  capturedPiece = piece_on(m.type_of() != EN_PASSANT ? dst : dst - pawn_spush(ac));
    Piece  promotedPiece = NO_PIECE;
    assert(color_of(movedPiece) == ac);
    assert(!is_ok(capturedPiece)
           || (color_of(capturedPiece) == (m.type_of() != CASTLING ? ~ac : ac)
               && type_of(capturedPiece) != KING));

    auto movedPt = type_of(movedPiece);

    Key movedKey;

    DirtyBoard db;

    db.dp.pc             = movedPiece;
    db.dp.org            = org;
    db.dp.dst            = dst;
    db.dp.addSq          = SQ_NONE;
    db.dts.ac            = ac;
    db.dts.preKingSq     = square<KING>(ac);
    db.dts.threateningBB = 0;
    db.dts.threatenedBB  = 0;
    assert(is_ok(db.dts.preKingSq));
    assert(db.dts.list.empty());

    // Reset en-passant square
    if (is_ok(en_passant_sq()))
    {
        k ^= Zobrist::enpassant(en_passant_sq());
        reset_en_passant_sq();
    }

    bool capture;
    bool epCheck = false;

    // If the move is a castling, do some special work
    if (m.type_of() == CASTLING)
    {
        assert(movedPiece == make_piece(ac, KING));
        assert(capturedPiece == make_piece(ac, ROOK));
        assert(has_castling_rights(ac & ANY_CASTLING));
        assert(!has_castled(ac));

        Square rOrg, rDst;
        do_castling<true>(ac, org, dst, rOrg, rDst, &db);
        assert(rOrg == m.dst_sq());

        movedKey = Zobrist::piece_square(ac, movedPt, org)  //
                 ^ Zobrist::piece_square(ac, movedPt, dst);
        Key rookKey = Zobrist::piece_square(ac, ROOK, rOrg)  //
                    ^ Zobrist::piece_square(ac, ROOK, rDst);

        k ^= rookKey;
        st->nonPawnKey[ac][1] ^= rookKey;

        capturedPiece = NO_PIECE;
        capture       = false;

        // Calculate checker only one ROOK possible
        st->checkers = isCheck ? square_bb(rDst) : 0;
        assert(!isCheck || (checkers() && popcount(checkers()) == 1));

        goto DO_MOVE_END;
    }

    movedKey = Zobrist::piece_square(ac, movedPt, org)  //
             ^ Zobrist::piece_square(ac, movedPt, dst);

    capture = is_ok(capturedPiece);

    if (capture)
    {
        auto capturedPt = type_of(capturedPiece);

        Square capturedSq = dst;

        Key capturedKey = Zobrist::piece_square(~ac, capturedPt, capturedSq);

        // If the captured piece is a pawn, update pawn hash key,
        // otherwise update non-pawn material.
        if (capturedPt == PAWN)
        {
            if (m.type_of() == EN_PASSANT)
            {
                capturedSq -= pawn_spush(ac);

                assert(movedPiece == make_piece(ac, PAWN));
                assert(relative_rank(ac, org) == RANK_5);
                assert(relative_rank(ac, dst) == RANK_6);
                assert(pieces(~ac, PAWN) & capturedSq);
                assert(!(pieces() & make_bb(dst, dst + pawn_spush(ac))));
                assert(!is_ok(en_passant_sq()));  // Already reset to SQ_NONE
                assert(rule50_count() == 1);
                assert(st->preSt->enPassantSq == dst);
                assert(st->preSt->rule50Count == 0);

                capturedKey = Zobrist::piece_square(~ac, capturedPt, capturedSq);

                // Remove the captured pawn
                remove_piece(capturedSq);
            }

            st->pawnKey[~ac] ^= capturedKey;
        }
        else
        {
            st->nonPawnKey[~ac][is_major(capturedPt)] ^= capturedKey;
        }

        db.dp.removeSq = capturedSq;
        db.dp.removePc = capturedPiece;

        st->capturedSq = dst;

        // Update hash key
        k ^= capturedKey;
        // Reset rule 50 draw counter
        reset_rule50_count();
    }

    if (capture && m.type_of() != EN_PASSANT)
    {
        // Remove the captured piece
        remove_piece(org, &db.dts);
        swap_piece(dst, movedPiece, &db.dts);
    }
    else
    {
        move_piece(org, dst, &db.dts);
    }

    // If the moving piece is a pawn do some special extra work
    if (movedPt == PAWN)
    {
        if (m.type_of() == PROMOTION)
        {
            assert(relative_rank(ac, org) == RANK_7);
            assert(relative_rank(ac, dst) == RANK_8);

            auto promoted = m.promotion_type();
            assert(KNIGHT <= promoted && promoted <= QUEEN);

            promotedPiece = make_piece(ac, promoted);

            Key promoKey = Zobrist::piece_square(ac, promoted, dst);

            db.dp.dst   = SQ_NONE;
            db.dp.addSq = dst;
            db.dp.addPc = promotedPiece;

            swap_piece(dst, promotedPiece, &db.dts);
            assert(count(promotedPiece));
            assert(Zobrist::piece_square(ac, PAWN, dst) == 0);
            // Update hash keys
            k ^= promoKey;
            st->nonPawnKey[ac][is_major(promoted)] ^= promoKey;
        }
        // Set en-passant square if the moved pawn can be captured
        else if ((int(dst) ^ int(org)) == NORTH_2)
        {
            assert(relative_rank(ac, org) == RANK_2);
            assert(relative_rank(ac, dst) == RANK_4);

            epCheck = true;
        }

        // Update pawn hash key
        st->pawnKey[ac] ^= movedKey;

        // Reset rule 50 draw counter
        reset_rule50_count();
    }
    else if (movedPt != KING)
    {
        st->nonPawnKey[ac][is_major(movedPt)] ^= movedKey;
    }

    // Calculate checkers
    st->checkers = isCheck ? attackers_to(square<KING>(~ac)) & pieces(ac) : 0;
    assert(!isCheck || (checkers() && popcount(checkers()) <= 2));

DO_MOVE_END:

    db.dts.kingSq = square<KING>(ac);

    // Update hash key
    k ^= movedKey;

    // Update castling rights if needed
    if (int cr; castling_rights() && (cr = castling_rights_mask(org, dst)))
    {
        k ^= Zobrist::castling(castling_rights());
        st->castlingRights &= ~cr;
        k ^= Zobrist::castling(castling_rights());
    }
    // Speculative prefetch as early as possible
    if (tt != nullptr && !epCheck)
        tt->prefetch_key(k ^ Zobrist::mr50(rule50_count()));

    activeColor = ~ac;

    st->capturedPiece = capturedPiece;
    st->promotedPiece = promotedPiece;

    // Update king attacks used for fast check detection
    set_ext_state();

    if (epCheck && can_enpassant(active_color(), dst - pawn_spush(ac)))
    {
        st->enPassantSq = dst - pawn_spush(ac);
        k ^= Zobrist::enpassant(en_passant_sq());
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
        const State* preSt = st->preSt->preSt;

        for (std::int16_t i = 4; i <= end; i += 2)
        {
            preSt = preSt->preSt->preSt;

            if (preSt->key == st->key)
            {
                st->repetition = preSt->repetition ? -i : +i;
                break;
            }
        }
    }

    assert(_is_ok());

    assert(is_ok(db.dp.pc));
    assert(is_ok(db.dp.org));
    assert(is_ok(db.dp.dst) ^ !(m.type_of() != PROMOTION));
    assert(is_ok(db.dp.removeSq) ^ !(capture || m.type_of() == CASTLING));
    assert(is_ok(db.dp.addSq) ^ !(m.type_of() == PROMOTION || m.type_of() == CASTLING));
    return db;
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
            assert(st->preSt->enPassantSq == dst);
            assert(st->preSt->rule50Count == 0);
        }
        // Restore the captured piece
        put_piece(capSq, capturedPiece);
    }

UNDO_MOVE_END:

    --gamePly;
    // Finally point state pointer back to the previous state
    st = const_cast<State*>(st->preSt);

    assert(legal(m));
    assert(_is_ok());
}

// Makes a null move
// It flips the active color without executing any move on the board.
void Position::do_null_move(State& newSt, const TranspositionTable* const tt) noexcept {
    assert(&newSt != st);
    assert(!checkers());

    Key k = st->key ^ Zobrist::turn();

    newSt.switch_to_prefix(st);

    st = &newSt;

    st->nullPly = 0;

    if (is_ok(en_passant_sq()))
    {
        k ^= Zobrist::enpassant(en_passant_sq());
        reset_en_passant_sq();
    }

    st->key = k;
    // Speculative prefetch as early as possible
    if (tt != nullptr)
        tt->prefetch_key(key());

    activeColor = ~active_color();

    st->capturedSq    = SQ_NONE;
    st->checkers      = 0;
    st->capturedPiece = NO_PIECE;
    st->promotedPiece = NO_PIECE;

    set_ext_state();

    st->repetition = 0;

    assert(_is_ok());
}

// Unmakes a null move
void Position::undo_null_move() noexcept {
    assert(!is_ok(captured_sq()));
    assert(!checkers());
    assert(!is_ok(captured_piece()));
    assert(!is_ok(promoted_piece()));

    activeColor = ~active_color();

    st = const_cast<State*>(st->preSt);

    assert(_is_ok());
}

// Tests whether a move is legal
bool Position::legal(Move m) const noexcept {
    assert(m.is_ok());

    Color ac = active_color();

    assert(piece_on(square<KING>(ac)) == make_piece(ac, KING));

    Square org = m.org_sq(), dst = m.dst_sq();
    Piece  pc = piece_on(org);

    // If the origin square is not occupied by a piece belonging to
    // the side to move, the move is obviously not legal.
    if (!is_ok(pc) || color_of(pc) != ac)
        return false;

    if (m.type_of() == CASTLING)
    {
        if (type_of(pc) == KING && (pieces(ac, ROOK) & dst) && !checkers()
            && relative_rank(ac, org) == RANK_1 && relative_rank(ac, dst) == RANK_1)
        {
            CastlingRights cs = castling_side(org, dst);
            CastlingRights cr = ac & cs;

            return castling_rook_sq(cr) == dst  //
                && castling_possible(cr, blockers(ac), pieces(~ac));
        }
        return false;
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
            if (PROMOTION_RANKS_BB & make_bb(org, dst))
                return false;
            if (!(relative_rank(ac, org) < RANK_7 && relative_rank(ac, dst) < RANK_8
                  && ((org + pawn_spush(ac) == dst && !(pieces() & dst))    // Single push
                      || (attacks_bb<PAWN>(org, ac) & pieces(~ac) & dst)))  // Capture
                && !(relative_rank(ac, org) == RANK_2 && relative_rank(ac, dst) == RANK_4
                     && org + pawn_dpush(ac) == dst  // Double push
                     && !(pieces() & make_bb(dst, dst - pawn_spush(ac)))))
                return false;
        }
        else if (!(attacks_bb(org, type_of(pc), pieces()) & dst))
            return false;

        // For king moves, check whether the destination square is attacked by the enemies.
        if (type_of(pc) == KING)
            return !has_attackers_to(dst, pieces(~ac), pieces() ^ org);
        break;

    case PROMOTION :
        if (!(type_of(pc) == PAWN  //&& (PROMOTION_RANKS_BB & dst)
              && relative_rank(ac, org) == RANK_7 && relative_rank(ac, dst) == RANK_8
              && ((org + pawn_spush(ac) == dst && !(pieces() & dst))
                  || ((attacks_bb<PAWN>(org, ac) & pieces(~ac)) & dst))))
            return false;
        break;

    case EN_PASSANT :
        if (!(type_of(pc) == PAWN && en_passant_sq() == dst && rule50_count() == 0
              && relative_rank(ac, org) == RANK_5 && relative_rank(ac, dst) == RANK_6
              && (pieces(~ac, PAWN) & (dst - pawn_spush(ac)))
              && !(pieces() & make_bb(dst, dst + pawn_spush(ac)))
              && ((attacks_bb<PAWN>(org, ac) /*& ~pieces()*/) & dst)
              && !(slide_attackers_to(square<KING>(ac),
                                      pieces() ^ make_bb(org, dst, dst - pawn_spush(ac)))
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
             && ((between_bb(square<KING>(ac), lsb(checkers())) & dst)
                 || (m.type_of() == EN_PASSANT && (checkers() & (dst - pawn_spush(ac)))))))
        && (!(blockers(ac) & org) || aligned(square<KING>(ac), org, dst));
}

// Tests whether a pseudo-legal move is a check
bool Position::check(Move m) const noexcept {
    assert(legal(m));

    Color ac = active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(piece_on(org)) == ac);

    if (
      // Is there a direct check?
      (checks(m.type_of() != PROMOTION ? type_of(piece_on(org)) : m.promotion_type()) & dst)
      // Is there a discovered check?
      || ((blockers(~ac) & org)  //
          && (!aligned(square<KING>(~ac), org, dst) || m.type_of() == CASTLING)))
        return true;

    switch (m.type_of())
    {
    case NORMAL :
        return false;

    case PROMOTION :
        return attacks_bb(dst, m.promotion_type(), pieces() ^ org) & square<KING>(~ac);

    // En-passant capture with check? Already handled the case of direct check
    // and ordinary discovered check, so the only case need to handle is
    // the unusual case of a discovered check through the captured pawn.
    case EN_PASSANT :
        return slide_attackers_to(square<KING>(~ac),
                                  pieces() ^ make_bb(org, dst, dst - pawn_spush(ac)))
             & pieces(ac);

    case CASTLING :
        // Castling is encoded as "king captures rook"
        return checks(ROOK) & rook_castle_sq(ac, org, dst);
    }
    assert(false);
    return false;
}

bool Position::dbl_check(Move m) const noexcept {
    assert(legal(m));

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
          && (blockers(~ac) & org) && !aligned(square<KING>(~ac), org, dst);

    case PROMOTION :
        return (blockers(~ac) & org)  //
            && (attacks_bb(dst, m.promotion_type(), pieces() ^ org) & square<KING>(~ac));

    case EN_PASSANT : {
        Bitboard checkers =
          slide_attackers_to(square<KING>(~ac), pieces() ^ make_bb(org, dst, dst - pawn_spush(ac)))
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
    assert(legal(m));

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

// Computes the new hash key after the given move.
// Needed for speculative prefetch.
// It does recognize special moves like castling, en-passant and promotions.
Key Position::move_key(Move m) const noexcept {
    Key moveKey = st->key ^ Zobrist::turn();

    if (is_ok(en_passant_sq()))
        moveKey ^= Zobrist::enpassant(en_passant_sq());

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

    auto movedPt = type_of(movedPiece);

    moveKey ^= Zobrist::piece_square(ac, movedPt, org)
             ^ Zobrist::piece_square(ac, m.type_of() != PROMOTION ? movedPt : m.promotion_type(),
                                     m.type_of() != CASTLING ? dst : king_castle_sq(ac, org, dst));
    if (int cr; castling_rights() && (cr = castling_rights_mask(org, dst)))
        moveKey ^= Zobrist::castling(castling_rights())  //
                 ^ Zobrist::castling(castling_rights() & ~cr);

    if (m.type_of() == CASTLING)
    {
        assert(movedPiece == make_piece(ac, KING));
        assert(capturedPiece == make_piece(ac, ROOK));
        // ROOK
        return moveKey  //
             ^ Zobrist::piece_square(ac, ROOK, dst)
             ^ Zobrist::piece_square(ac, ROOK, rook_castle_sq(ac, org, dst))
             ^ Zobrist::mr50(rule50_count() + 1);
    }

    if (movedPt == PAWN && (int(dst) ^ int(org)) == NORTH_2
        && can_enpassant<false>(~ac, dst - pawn_spush(ac)))
    {
        assert(relative_rank(ac, org) == RANK_2);
        assert(relative_rank(ac, dst) == RANK_4);

        return moveKey ^ Zobrist::enpassant(dst);
    }

    return moveKey  //
         ^ Zobrist::piece_square(~ac, type_of(capturedPiece), capSq)
         ^ Zobrist::mr50(!is_ok(capturedPiece) && movedPt != PAWN ? rule50_count() + 1 : 0);
}

// Tests if the SEE (Static Exchange Evaluation) value of the move
// is greater or equal to the given threshold.
// An algorithm similar to alpha-beta pruning with a null window.
bool Position::see_ge(Move m, int threshold) const noexcept {
    assert(legal(m));

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

    int swap = piece_value(type_of(piece_on(cap))) + m.promotion_value() - threshold;

    // If can't beat the threshold despite capturing the piece,
    // it is impossible to beat the threshold.
    if (swap < 0)
        return false;

    auto moved = type_of(piece_on(org));

    swap = piece_value(m.type_of() != PROMOTION ? moved : m.promotion_type()) - swap;

    // If still beat the threshold after losing the piece,
    // it is guaranteed to beat the threshold.
    if (swap <= 0)
        return true;

    // It doesn't matter if the destination square is occupied or not
    // xoring dst is important for pinned piece logic
    occupied ^= make_bb(org, dst);

    Bitboard attackers = attackers_to(dst, occupied);

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

    bool ge = true;

    Bitboard acAttackers, b;
    Square   sq;

    Bitboard qB = pieces(QUEEN, BISHOP) & attacks_bb<BISHOP>(dst) & occupied;
    Bitboard qR = pieces(QUEEN, ROOK) & attacks_bb<ROOK>(dst) & occupied;

    const auto* magic = &Magics[dst];

    StdArray<bool, COLOR_NB> discovery{true, true};

    while (true)
    {
        ac = ~ac;
        attackers &= occupied;

        acAttackers = pieces(ac) & attackers;
        // If ac has no more attackers then give up: ac loses
        if (!acAttackers)
            break;

        PieceType pt;
        // Don't allow pinned pieces to attack as long as
        // there are pinners on their original square.
        if (pinners(~ac) & pieces(~ac) & occupied)
        {
            acAttackers &= ~blockers(ac);

            if (!acAttackers)
                break;
        }
        if ((blockers(ac) & org)
            && (b = pinners(ac) & pieces(~ac) & line_bb(org, square<KING>(ac)) & occupied)
            && ((pt = type_of(piece_on(org))) != PAWN || !aligned(square<KING>(ac), org, dst)))
        {
            acAttackers &= square<KING>(ac);

            if (!acAttackers  //
                && (pt == PAWN || !(attacks_bb(dst, pt, occupied) & square<KING>(ac))))
            {
                dst  = lsb(b);
                swap = piece_value(type_of(piece_on(org))) - swap;
                if ((swap = piece_value(type_of(piece_on(dst))) - swap) < ge)
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

        ge = !ge;

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

                ac = ~ac;
                ge = !ge;
                continue;  // Resume without considering discovery
            }

            if (!(pinners(~ac) & pieces(ac) & line_bb(square<KING>(~ac), sq) & occupied))
            {
                discovery[ac] = false;

                ac = ~ac;
                ge = !ge;
                continue;  // Resume without considering discovery
            }

            occupied ^= org = sq;
            if ((swap = piece_value(pt) - swap) < ge)
                break;

            switch (pt)
            {
            case PAWN :
            case BISHOP :
                qB &= occupied;
                if (qB)
                    attackers |= qB & attacks_bb<BISHOP>(*magic, occupied);
                break;
            case ROOK :
                qR &= occupied;
                if (qR)
                    attackers |= qR & attacks_bb<ROOK>(*magic, occupied);
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
            if ((swap = VALUE_PAWN - swap) < ge)
                break;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(*magic, occupied);

            if (is_ok(epSq) && rank_of(org) == rank_of(dst))
            {
                occupied ^= make_bb(dst, epSq);

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
            if ((swap = VALUE_KNIGHT - swap) < ge)
                break;
        }
        else if ((b = pieces(BISHOP) & acAttackers))
        {
            occupied ^= org = lsb(b);
            if ((swap = VALUE_BISHOP - swap) < ge)
                break;
            qB &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(*magic, occupied);
        }
        else if ((b = pieces(ROOK) & acAttackers))
        {
            occupied ^= org = lsb(b);
            if ((swap = VALUE_ROOK - swap) < ge)
                break;
            qR &= occupied;
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(*magic, occupied);
        }
        else if ((b = pieces(QUEEN) & acAttackers))
        {
            occupied ^= org = lsb(b);
            if ((swap = VALUE_QUEEN - swap) < ge)
                break;
            qB &= occupied;
            qR &= occupied;
            if (qB)
                attackers |= qB & attacks_bb<BISHOP>(*magic, occupied);
            if (qR)
                attackers |= qR & attacks_bb<ROOK>(*magic, occupied);
        }
        else  // KING
        {
            occupied ^= acAttackers;
            // If "capture" with the king but the opponent still has attackers, reverse the result.
            ge ^= bool(pieces(~ac) & attackers);
            break;
        }
    }

    return ge;
}

// Draw by Repetition: position repeats once earlier but strictly
// after the root, or repeats twice before or at the root.
bool Position::is_repetition(std::int16_t ply) const noexcept {
    return repetition() && repetition() < ply;
}

// Tests whether the current position is drawn by repetition or by 50-move rule.
// It also detect stalemates.
bool Position::is_draw(std::int16_t ply, bool rule50Active, bool chkStalemate) const noexcept {
    return
      // Draw by Repetition
      is_repetition(ply)
      // Draw by 50-move rule
      || (rule50Active && rule50_count() >= 2 * DrawMoveCount
          && (!checkers() || !MoveList<LEGAL, true>(*this).empty()))
      // Draw by Stalemate
      || (chkStalemate && !checkers() && MoveList<LEGAL, true>(*this).empty());
}

// Tests whether there has been at least one repetition
// of positions since the last capture or pawn move.
bool Position::has_repeated() const noexcept {
    auto end = std::min(rule50_count(), null_ply());

    if (end < 4)
        return false;

    const State* cSt = st;

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

    Key          baseKey = st->key;
    const State* preSt   = st->preSt;
    Key          iterKey = baseKey ^ preSt->key ^ Zobrist::turn();

    for (std::int16_t i = 3; i <= end; i += 2)
    {
        iterKey ^= preSt->preSt->key ^ preSt->preSt->preSt->key ^ Zobrist::turn();

        preSt = preSt->preSt->preSt;

        // Opponent pieces have reverted
        if (iterKey)
            continue;

        Key moveKey = baseKey ^ preSt->key;
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
        assert(legal(m) && MoveList<LEGAL>(*this).contains(m));
#endif
        if (i < ply
            // For nodes before or at the root, check that the move is
            // a repetition rather than a move to the current position.
            || preSt->repetition)
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
    token[0] = token[0] == 'w' ? 'B' : 'W';
    fens += token;
    fens += ' ';

    // Castling rights
    iss >> token;
    fens += token;
    fens += ' ';

    fens = toggle_case(fens);

    // En-passant square
    iss >> token;
    if (token != "-")
        token[1] = flip_rank(token[1]);
    fens += token;

    std::getline(iss, token);  // Half and full moves
    fens += token;

    set(fens, st);

    assert(_is_ok());
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
    fens += token;
    fens += ' ';

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
    fens += token;
    fens += ' ';

    // En-passant square (flip the file)
    iss >> token;
    if (token != "-")
        token[0] = flip_file(token[0]);
    fens += token;

    std::getline(iss, token);  // Half and full moves
    fens += token;

    set(fens, st);

    assert(_is_ok());
}

#if !defined(NDEBUG)
// Computes the hash key of the current position.
Key Position::compute_key() const noexcept {
    Key key = 0;

    std::size_t n;
    auto        sqs = squares(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        Square s  = sqs[i];
        Piece  pc = piece_on(s);

        key ^= Zobrist::piece_square(pc, s);
    }

    key ^= Zobrist::castling(castling_rights());

    if (is_ok(en_passant_sq()))
        key ^= Zobrist::enpassant(en_passant_sq());

    if (active_color() == BLACK)
        key ^= Zobrist::turn();

    return key;
}

Key Position::compute_minor_key() const noexcept {
    Key minorKey = 0;

    std::size_t n;
    auto        sqs = squares(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        Square s  = sqs[i];
        Piece  pc = piece_on(s);
        auto   pt = type_of(pc);

        if (pt != PAWN && pt != KING && !is_major(pt))
            minorKey ^= Zobrist::piece_square(color_of(pc), pt, s);
    }

    return minorKey;
}

Key Position::compute_major_key() const noexcept {
    Key majorKey = 0;

    std::size_t n;
    auto        sqs = squares(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        Square s  = sqs[i];
        Piece  pc = piece_on(s);
        auto   pt = type_of(pc);

        if (pt != PAWN && pt != KING && is_major(pt))
            majorKey ^= Zobrist::piece_square(color_of(pc), pt, s);
    }

    return majorKey;
}

Key Position::compute_non_pawn_key() const noexcept {
    Key nonPawnKey = 0;

    std::size_t n;
    auto        sqs = squares(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        Square s  = sqs[i];
        Piece  pc = piece_on(s);
        auto   pt = type_of(pc);

        if (pt != PAWN)
            nonPawnKey ^= Zobrist::piece_square(color_of(pc), pt, s);
    }

    return nonPawnKey;
}

// Performs some consistency checks for the position object
// and raise an assert if something wrong is detected.
// This is meant to be helpful when debugging.
bool Position::_is_ok() const noexcept {

    constexpr bool Fast = true;  // Quick (default) or full check?

    if ((active_color() != WHITE && active_color() != BLACK)  //
        || count(W_KING) != 1 || count(B_KING) != 1           //
        || piece_on(square<KING>(WHITE)) != W_KING            //
        || piece_on(square<KING>(BLACK)) != B_KING            //
        || distance(square<KING>(WHITE), square<KING>(BLACK)) <= 1
        || (is_ok(en_passant_sq()) && !can_enpassant(active_color(), en_passant_sq())))
        assert(false && "Position::_is_ok(): Default");

    if (st->key != compute_key())
        assert(false && "Position::_is_ok(): Key");

    if (Fast)
        return true;

    if (minor_key() != compute_minor_key())
        assert(false && "Position::_is_ok(): Minor Key");

    if (major_key() != compute_major_key())
        assert(false && "Position::_is_ok(): Major Key");

    if (non_pawn_key() != compute_non_pawn_key())
        assert(false && "Position::_is_ok(): NonPawn Key");

    if (has_attackers_to(square<KING>(~active_color()), pieces(active_color())))
        assert(false && "Position::_is_ok(): King Checker");

    if ((pieces(PAWN) & PROMOTION_RANKS_BB) || count<PAWN>(WHITE) > 8 || count<PAWN>(BLACK) > 8)
        assert(false && "Position::_is_ok(): Pawns");

    if ((pieces(WHITE) & pieces(BLACK)) || (pieces(WHITE) | pieces(BLACK)) != pieces()
        || popcount(pieces(WHITE)) > 16 || popcount(pieces(BLACK)) > 16)
        assert(false && "Position::_is_ok(): Bitboards");

    for (PieceType p1 : PIECE_TYPES)
        for (PieceType p2 : PIECE_TYPES)
            if (p1 != p2 && (pieces(p1) & pieces(p2)))
                assert(false && "Position::_is_ok(): Bitboards");

    for (Color c : {WHITE, BLACK})
    {
        const auto* pB = base(c);
        for (PieceType pt : PIECE_TYPES)
        {
            Piece       pc = make_piece(c, pt);
            const auto& pL = squares(c, pt);
            for (std::uint8_t i = 0; i < pL.count(); ++i)
            {
                Square s = pL.at(i, pB);
                if (piece_on(s) != pc || indexMap[s] != i)
                    assert(0 && "_is_ok: Piece List");
            }
        }
    }

    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            Piece pc    = make_piece(c, pt);
            auto  count = this->count(c, pt);
            if (count != popcount(pieces(c, pt))
                || count != std::count(piece_map().begin(), piece_map().end(), pc))
                assert(false && "Position::_is_ok(): Piece List Count");
        }

    for (Color c : {WHITE, BLACK})
        if (count<PAWN>(c)                                                         //
              + std::max(count<KNIGHT>(c) - 2, 0)                                  //
              + std::max(popcount(pieces(c, BISHOP) & colors_bb<WHITE>()) - 1, 0)  //
              + std::max(popcount(pieces(c, BISHOP) & colors_bb<BLACK>()) - 1, 0)  //
              + std::max(count<ROOK>(c) - 2, 0)                                    //
              + std::max(count<QUEEN>(c) - 1, 0)                                   //
            > 8)
            assert(false && "Position::_is_ok(): Piece Count");

    for (Color c : {WHITE, BLACK})
        for (CastlingRights cr : {c & KING_SIDE, c & QUEEN_SIDE})
        {
            if (!has_castling_rights(cr))
                continue;

            if (!is_ok(castling_rook_sq(cr))  //
                || !(pieces(c, ROOK) & castling_rook_sq(cr))
                || (castlingRightsMask[c * FILE_NB + file_of(castling_rook_sq(cr))]) != cr
                || (castlingRightsMask[c * FILE_NB + file_of(square<KING>(c))] & cr) != cr)
                assert(false && "Position::_is_ok(): Castling");
        }

    return true;
}

#endif

// Returns ASCII representation of the position as string
Position::operator std::string() const noexcept {
    constexpr std::string_view Sep{"\n  +---+---+---+---+---+---+---+---+\n"};

    std::string str;
    str.reserve(672);

    str += Sep;

    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        str += to_char(r);

        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            str += " | ";
            str += to_char(piece_on(make_square(f, r)));
        }

        str += " |";
        str += Sep;
    }

    str += " ";

    for (File f = FILE_A; f <= FILE_H; ++f)
    {
        str += "   ";
        str += to_char<true>(f);
    }

    str += "\n";

    return str;
}

// Prints to the output stream the position in ASCII + detailed info
std::ostream& operator<<(std::ostream& os, const Position& pos) noexcept {

    os << std::string(pos);

    os << "\nFen: " << pos.fen();

    os << "\nKey: " << u64_to_string(pos.key());

    os << "\nKing (s): " << to_square(pos.square<KING>(pos.active_color()))  //
       << ", " << to_square(pos.square<KING>(~pos.active_color()));

    os << "\nCheckers: ";

    for (Bitboard checkers = pos.checkers(); checkers;)
        os << to_square(pop_lsb(checkers)) << " ";

    os << "\nRepetition: " << pos.repetition();

    if (Tablebases::MaxCardinality >= pos.count() && !pos.has_castling_rights(ANY_CASTLING))
    {
        State    st;
        Position p;
        p.set(pos, &st);
        st = *pos.state();

        Tablebases::ProbeState wdlPs, dtzPs;

        auto wdlScore = Tablebases::probe_wdl(p, &wdlPs);
        auto dtzScore = Tablebases::probe_dtz(p, &dtzPs);

        os << "\nTablebases WDL: " << std::setw(4) << int(wdlScore) << " (" << int(wdlPs) << ")"
           << "\nTablebases DTZ: " << std::setw(4) << int(dtzScore) << " (" << int(dtzPs) << ")";
    }

    return os;
}

void Position::dump(std::ostream& os) const noexcept {
    constexpr std::string_view Sep{"\n  +-----+-----+-----+-----+-----+-----+-----+-----+\n"};

    os << *this << "\n";

    os << "Color Bitboards:\n";
    for (Color c : {WHITE, BLACK})
    {
        os << (c == WHITE ? "W" : "B") << ": ";
        os << u64_to_string(pieces(c)) << "\n";
    }

    os << "Piece Bitboards:\n";
    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            os << to_char(make_piece(c, pt)) << ": ";
            os << u64_to_string(pieces(c, pt)) << "\n";
        }

    os << "Piece Lists:\n";
    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            os << to_char(make_piece(c, pt)) << ": ";
            const auto& pL = squares(c, pt);
            const auto* pB = base(c);
            for (const Square* s = pL.begin(pB); s != pL.end(pB); ++s)
                os << to_square(*s) << " ";
            os << "\n";
        }

    os << "Piece List Map:\n";
    os << Sep;
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        os << to_char(r);
        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            Square s = make_square(f, r);

            os << " | ";

            if (indexMap[s] == INDEX_NONE)
                os << "  ";
            else
                os << std::setw(2) << int(indexMap[s]);

            os << " ";
        }
        os << " |";
        os << Sep;
    }
    for (File f = FILE_A; f <= FILE_H; ++f)
    {
        os << "     ";
        os << to_char<true>(f);
    }
    os << "\n";

    os << "Square Table:\n";
    for (Color c : {WHITE, BLACK})
    {
        for (Square s : squaresTable[c])
        {
            if (is_ok(s))
                os << to_square(s);
            else
                os << "-";
            os << " ";
        }
        os << "\n";
    }

    os << "Castlings:\n";
    for (std::size_t i = 0; i < castlings.size(); ++i)
    {
        const auto& castling = castlings[i];

        os << i << ":\n";
        for (std::size_t len = 0; len < castling.fullPathLen; ++len)
        {
            Square s = castling.fullPathSqs[len];

            os << to_square(s) << " ";
        }
        os << "\n";
        for (std::size_t len = 0; len < castling.kingPathLen; ++len)
        {
            Square s = castling.kingPathSqs[len];

            os << to_square(s) << " ";
        }
        os << "\n";
        if (is_ok(castling.rookSq))
            os << to_square(castling.rookSq);
        os << "\n";
    }

    flush(os);
}

}  // namespace DON
