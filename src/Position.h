//#pragma once
#ifndef POSITION_H_
#define POSITION_H_

#include <algorithm>
#include <memory>
#include <stack>

#include "Board.h"
#include "Castle.h"
#include "Move.h"
#include "BitScan.h"
#include "Zobrist.h"

class Position;

#pragma region FEN

// FORSYTH–EDWARDS NOTATION (FEN) is a standard notation for describing a particular board position of a chess game.
// The purpose of FEN is to provide all the necessary information to restart a game from a particular position.

// 88 is the max FEN length - r1n1k1r1/1B1b1q1n/1p1p1p1p/p1p1p1p1/1P1P1P1P/P1P1P1P1/1b1B1Q1N/R1N1K1R1 w KQkq - 12 1000
const uint8_t MAX_FEN     = 88;

// N-FEN (NATURAL-FEN)
// "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
// X-FEN (CHESS960-FEN) (Fischer Random Chess)
// "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1"

extern const char *const FEN_N;
extern const char *const FEN_X;
//extern const std::string FEN_N;
//extern const std::string FEN_X;

// Check the validity of FEN string
extern bool _ok (const        char *fen, bool c960 = false, bool full = true);
extern bool _ok (const std::string &fen, bool c960 = false, bool full = true);

#pragma endregion

#pragma region State Information

#pragma pack (push, 4)

// StateInfo stores information to restore Position object to its previous state when retracting a move.
// Whenever a move is made on the board (do_move), a StateInfo object must be passed as a parameter.
//
// StateInfo consists of the following data:
//
//  - Castling-rights information for both sides.
//  - En-passant square (SQ_NO if no en passant capture is possible).
//  - Counter (clock) for detecting 50 move rule draws.
//  - Hash key of the material situation.
//  - Hash key of the pawn structure.
//  - Hash key of the position.
//  - Move played on the last position.
//  - Piece type captured on last position.
//  - Bitboard of all checking pieces.
//  - Pointer to previous StateInfo. 
//  - Hash keys for all previous positions in the game for detecting repetition draws.
typedef struct StateInfo sealed
{
public:
    // Hash key of materials.
    Key matl_key;
    // Hash key of pawns.
    Key pawn_key;

    Value non_pawn_matl[CLR_NO];
    Score psq_score;

    // Castling-rights information for both side.
    CRight castle_rights;

    // "In passing" - Target square in algebraic notation.
    // If there's no en-passant target square is "-".
    Square en_passant;
    // Number of halfmoves clock since the last pawn advance or any capture.
    // used to determine if a draw can be claimed under the 50-move rule.
    uint8_t
        clock50,
        null_ply;

    // -------------------------------------

    // Hash key of position.
    Key posi_key;
    // Move played on the previous position.
    Move last_move;
    // Piece type captured.
    PType cap_type;

    Bitboard checkers;

    StateInfo *p_si;

    void clear ();

    operator std::string () const;

} StateInfo;

#pragma pack (pop)

#pragma endregion

#pragma region Check Inforamtion

#pragma pack (push, 4)

// CheckInfo struct is initialized at c'tor time.
// CheckInfo stores critical information used to detect if a move gives check.
//  - checking squares.
//  - pinned pieces.
//  - check discoverer pieces.
//  - enemy king square.
typedef struct CheckInfo sealed
{
public:
    // Checking squares from which the enemy king can be checked
    Bitboard checking_bb[PT_NO];
    // Pinned pieces
    Bitboard pinneds;
    // Check discoverer pieces
    Bitboard check_discovers;
    // Enemy king square
    Square king_sq;

    explicit CheckInfo (const Position &pos);

    void clear ();

} CheckInfo;

#pragma pack (pop)

#pragma endregion

#pragma region Position

#pragma pack (push, 4)

// The position data structure. A position consists of the following data:
//
//  - Board for storing pieces.
//  - Color of side on move.
//  - Ply of the game.
//  - StateInfo object for the base status.
//  - StateInfo pointer for the current status.
//  - Information about the castling rights for both sides.
//  - The initial files of the kings and both pairs of rooks. This is
//    used to implement the Chess960 castling rules.
//  - Nodes visited during search.
//  - Chess 960 info
typedef class Position sealed
{
private:

#pragma region Fields

    // Board for storing pieces.
    Board _board;
    // Object for base status information
    StateInfo  _sb;
    // Pointer for current status information
    StateInfo *_si;

    Square   _castle_rooks[CLR_NO][CS_NO];
    Bitboard _castle_paths[CLR_NO][CS_NO];

    CRight   _castle_rights[CLR_NO][F_NO];

    // Color of the side on move
    // "w" - WHITE
    // "b" - BLACK
    Color    _active;
    // Ply of the game, incremented after every move.
    uint16_t _game_ply;
    bool     _chess960;

    uint64_t _game_nodes;

#pragma endregion

    void _link_ptr () { _si = &_sb; }

public:

#pragma region Constructors

    Position ()
    {
        clear ();
    }
    Position (const        char *fen, bool c960 = false, bool full = true)
    {
        if (!setup (fen, c960, full)) clear ();
    }
    Position (const std::string &fen, bool c960 = false, bool full = true)
    {
        if (!setup (fen, c960, full)) clear ();
    }
    Position (const Position &pos)
    {
        *this = pos;
    }
    explicit Position (int8_t dummy) {}

    //~Position ()
    //{
    //    while (_si->p_si)
    //    {
    //        _si = _si->p_si;
    //    }
    //}

#pragma endregion

    Position& operator= (const Position &pos);
    //static void initialize ();

#pragma region Basic properties

#pragma region Board properties

    bool empty (Square s) const;
    const Piece operator[] (Square s) const;
    const Bitboard operator[] (Color c) const;
    const Bitboard operator[] (PType t) const;
    const SquareList operator[] (Piece p) const;

    Square king_sq (Color c) const;

    Bitboard pieces (Color c) const;
    Bitboard pieces (PType t) const;
    Bitboard pieces (Color c, PType t) const;
    Bitboard pieces (PType t1, PType t2) const;
    Bitboard pieces (Color c, PType t1, PType t2) const;
    Bitboard pieces () const;
    Bitboard empties () const;
    //Bitboard pieces (Piece p) const;

    template<PType T>
    uint32_t piece_count (Color c) const;
    template<PType T>
    uint32_t piece_count () const;
    uint32_t piece_count (Color c) const;
    uint32_t piece_count () const;
    uint32_t piece_count (Color c, PType t) const;
    //uint32_t piece_count (Piece p) const;
    //uint32_t piece_count (PType t) const;

    template<PType T>
    const SquareList list (Color c) const;

#pragma endregion

#pragma region StateInfo properties

    // Castling rights for both side
    CRight castle_rights () const;
    // Target square in algebraic notation. If there's no en passant target square is "-"
    Square en_passant () const;
    // Number of halfmoves clock since the last pawn advance or any capture.
    // used to determine if a draw can be claimed under the 50-move rule.
    uint16_t clock50 () const;
    //
    Move last_move () const;
    //
    PType cap_type () const;
    //
    Piece cap_piece () const;
    //
    Bitboard checkers () const;
    //
    Key matl_key () const;
    //
    Key pawn_key () const;
    //
    Key posi_key () const;

    Key key_posi_exclusion () const;

    // Incremental piece-square evaluation
    Value non_pawn_material (Color c) const;
    //Value pawn_material (Color c) const;
    

    Score psq_score () const;

#pragma endregion

#pragma region Castling properties

    CRight can_castle (CRight cr) const;
    CRight can_castle (Color   c) const;
    CRight can_castle (Color   c, CSide cs) const;
    // ---
    CRight castle_right (Color c, File   f) const;
    CRight castle_right (Color c, Square s) const;

    Square castle_rook (Color c, CSide cs) const;
    bool castle_impeded (Color c, CSide cs = CS_NO) const;

#pragma endregion

    Color active () const;
    uint16_t game_ply () const;
    uint16_t game_move () const;
    bool chess960 () const;

    uint64_t& game_nodes ();

    bool draw () const;
    bool ok (int8_t *failed_step = NULL) const;

#pragma endregion

#pragma region Attack properties

private:
    inline Bitboard blockers (Square s, Bitboard attackers) const;
    inline Bitboard hidden_checkers (Square sq_king, Color c) const;

public:

    template<PType T>
    // Attacks of the PAWN (Color) from the square
    Bitboard attacks_from (Color c, Square s) const;
    template<PType T>
    // Attacks of the PTYPE from the square
    Bitboard attacks_from (Square s) const;

    // Attacks of the piece from the square with occ
    Bitboard attacks_from (Piece p, Square s, Bitboard occ) const;
    // Attacks of the piece from the square
    Bitboard attacks_from (Piece p, Square s) const;

    Bitboard attackers_to (Square s, Bitboard occ) const;
    Bitboard attackers_to (Square s) const;

    Bitboard checkers (Color c) const;

    Bitboard pinneds () const;
    Bitboard check_discovers () const;

#pragma endregion

#pragma region Move properties

    Piece moved_piece (Move m) const;
    Piece captured_piece (Move m) const;

    bool pseudo_legal (Move m) const;
    bool legal (Move m, Bitboard pinned) const;
    bool legal (Move m) const;
    bool capture (Move m) const;
    bool capture_or_promotion(Move m) const;
    bool check (Move m, const CheckInfo &ci) const;
    bool checkmate (Move m, const CheckInfo &ci) const;

#pragma endregion

#pragma region Piece specific properties

    bool passed_pawn (Color c, Square s) const;
    bool has_pawn_on_7thR (Color c) const;
    bool has_opposite_bishops () const;
    bool has_pair_bishops (Color c) const;

#pragma endregion

#pragma region Basic methods

    void clear ();

    void   place_piece (Square s, Color c, PType t);
    void   place_piece (Square s, Piece p);
    Piece remove_piece (Square s);
    Piece   move_piece (Square s1, Square s2);

    bool setup (const        char *fen, bool c960 = false, bool full = true);
    bool setup (const std::string &fen, bool c960 = false, bool full = true);

private:
    void clr_castles ();
    void set_castle (Color c, Square org_rook);

    bool can_en_passant (Square ep_sq) const;
    bool can_en_passant (File   ep_f) const;

public:
    void flip ();

#pragma region Do/Undo Move

private:
    void castle_king_rook (Square org_king, Square dst_king, Square org_rook, Square dst_rook);

public:
    // do/undo move
    void do_move (Move m, StateInfo &si_n, const CheckInfo *ci);
    void do_move (Move m, StateInfo &si_n);
    void do_move (std::string &can, StateInfo &si_n);
    void undo_move ();

    void do_null_move (StateInfo &si_n);
    void undo_null_move ();

#pragma endregion

#pragma endregion

#pragma region Conversions

    bool        fen (const char *fen, bool c960 = false, bool full = true) const;
    std::string fen (bool                  c960 = false, bool full = true) const;

    operator std::string () const;

    static bool parse (Position &pos, const        char *fen, bool c960 = false, bool full = true);
    static bool parse (Position &pos, const std::string &fen, bool c960 = false, bool full = true);

#pragma endregion

} Position;

#pragma pack (pop)

#pragma region Basic properties

#pragma region Board properties

inline bool Position::empty (Square s) const { return _board.empty (s); }

inline const Piece    Position::operator[] (Square s) const { return _board[s]; }
inline const Bitboard Position::operator[] (Color  c) const { return _board[c]; }
inline const Bitboard Position::operator[] (PType  t) const { return _board[t]; }
inline const SquareList Position::operator[] (Piece p) const { return _board[p]; }

inline Square Position::king_sq (Color c) const { return _board.king_sq (c); }

inline Bitboard Position::pieces (Color c) const { return _board.pieces (c); }
inline Bitboard Position::pieces (PType t) const { return _board.pieces (t); }
inline Bitboard Position::pieces (Color c, PType t) const { return _board.pieces (c, t); }
inline Bitboard Position::pieces (PType t1, PType t2) const { return _board.pieces (t1, t2); }
inline Bitboard Position::pieces (Color c, PType t1, PType t2) const { return _board.pieces (c, t1, t2); }
inline Bitboard Position::pieces ()  const { return _board.pieces (); }
inline Bitboard Position::empties () const { return _board.empties (); }
//inline Bitboard Position::pieces (Piece p) const { return _board.pieces (p); }

template<PType T>
inline uint32_t Position::piece_count (Color c) const { return _board.piece_count<T> (c); }
template<PType T>
inline uint32_t Position::piece_count ()        const { return _board.piece_count<T> (); }
inline uint32_t Position::piece_count (Color c) const { return _board.piece_count (c); }
inline uint32_t Position::piece_count ()        const { return _board.piece_count (); }
inline uint32_t Position::piece_count (Color c, PType t) const { return _board.piece_count (c, t); }
//inline uint32_t Position::piece_count (Piece p) const { return _board.piece_count (p); }
//inline uint32_t Position::piece_count (PType t) const { return _board.piece_count (t); }

template<PType T>
inline const SquareList Position::list (Color c) const
{
    return _board[c | T];
}

#pragma endregion

#pragma region StateInfo properties

// Castling rights for both side
inline CRight Position::castle_rights () const { return _si->castle_rights; }
// Target square in algebraic notation. If there's no en passant target square is "-"
inline Square Position::en_passant () const { return _si->en_passant; }
// Number of halfmoves clock since the last pawn advance or any capture.
// used to determine if a draw can be claimed under the 50-move rule.
inline uint16_t Position::clock50 () const { return _si->clock50; }
//
inline Move Position::last_move () const { return _si->last_move; }
//
inline PType Position::cap_type () const { return _si->cap_type; }
//
inline Piece Position::cap_piece () const { return (PT_NO == cap_type ()) ? PS_NO : (_active | cap_type ()); }
//
inline Bitboard Position::checkers () const { return _si->checkers; }
//
inline Key Position::matl_key () const { return _si->matl_key; }
//
inline Key Position::pawn_key () const { return _si->pawn_key; }
//
inline Key Position::posi_key () const { return _si->posi_key; }
//
inline Key Position::key_posi_exclusion () const { return _si->posi_key ^ Zobrist::exclusion;}

inline Score Position::psq_score () const { return _si->psq_score; }

inline Value Position::non_pawn_material (Color c) const { return _si->non_pawn_matl[c]; }

//inline Value Position::pawn_material (Color c) const { return int32_t (piece_count<PAWN>(c)) * VALUE_EG_PAWN; }

#pragma endregion

#pragma region Castling properties

inline CRight Position::can_castle (CRight cr) const { return ::can_castle (castle_rights (), cr); }
inline CRight Position::can_castle (Color   c) const { return ::can_castle (castle_rights (), c); }
inline CRight Position::can_castle (Color   c, CSide cs) const { return ::can_castle (castle_rights (), c, cs); }

inline CRight Position::castle_right (Color c, File   f) const { return _castle_rights[c][f]; }
inline CRight Position::castle_right (Color c, Square s) const { return (R_1 == rel_rank (c, s)) ? castle_right (c, _file (s)) : CR_NO; }

inline Square Position::castle_rook  (Color c, CSide cs) const { return _castle_rooks[c][cs]; }

inline bool Position::castle_impeded (Color c, CSide cs) const
{
    Bitboard occ = _board.pieces ();
    switch (cs)
    {
    case CS_K:
    case CS_Q:
        return (_castle_paths[c][cs] & occ);
        break;
    default:
        return (_castle_paths[c][CS_K] & occ) && (_castle_paths[c][CS_Q] & occ);
        break;
    }
}

#pragma endregion

// Color of the side on move
inline Color    Position::active () const { return _active; }
// game_ply starts at 0, and is incremented after every move.
// game_ply  = std::max (2 * (game_move - 1), 0) + (BLACK == Active)
inline uint16_t Position::game_ply () const { return _game_ply; }
// game_move starts at 1, and is incremented after BLACK's move.
// game_move = std::max ((game_ply - (BLACK == Active)) / 2, 0) + 1
inline uint16_t Position::game_move () const { return std::max<uint32_t> ((_game_ply - (BLACK == _active)) / 2, 0) + 1; }
//
inline bool     Position::chess960 () const { return _chess960; }

// Nodes visited
inline uint64_t& Position::game_nodes () { return _game_nodes; }

#pragma endregion

#pragma region Attack properties

template<>
// Attacks of the PAWN from the square
inline Bitboard Position::attacks_from<PAWN> (Color c, Square s) const
{
    return BitBoard::attacks_bb<PAWN> (c, s);
}
template<PType T>
// Attacks of the PTYPE from the square
inline Bitboard Position::attacks_from (Square s) const
{
    switch (T)
    {
    case PAWN:
        return BitBoard::attacks_bb<PAWN> (_active, s);
        break;
    case NIHT:
    case BSHP:
    case ROOK:
    case QUEN:
    case KING:
        return BitBoard::attacks_bb<T> (s);
        break;
    }
    return 0;
}
// Attacks of the piece from the square
inline Bitboard Position::attacks_from (Piece p, Square s, Bitboard occ) const
{
    return BitBoard::attacks_bb (p, s, occ);
}
// Attacks of the piece from the square
inline Bitboard Position::attacks_from (Piece p, Square s) const
{
    return attacks_from (p, s, _board.pieces ());
}

// Attackers to the square on given occ
inline Bitboard Position::attackers_to (Square s, Bitboard occ) const
{
    return
        (BitBoard::attacks_bb<PAWN> (WHITE, s) & _board.pieces (BLACK, PAWN)) |
        (BitBoard::attacks_bb<PAWN> (BLACK, s) & _board.pieces (WHITE, PAWN)) |
        (BitBoard::attacks_bb<NIHT> (s)      & _board.pieces (NIHT)) |
        (BitBoard::attacks_bb<BSHP> (s, occ) & _board.pieces (BSHP, QUEN)) |
        (BitBoard::attacks_bb<ROOK> (s, occ) & _board.pieces (ROOK, QUEN)) |
        (BitBoard::attacks_bb<KING> (s)      & _board.pieces (KING));
}
// Attackers to the square
inline Bitboard Position::attackers_to (Square s) const
{
    return attackers_to (s, _board.pieces ());
}

// Checkers are enemy pieces that give the direct Check to friend King of color 'c'
inline Bitboard Position::checkers (Color c) const
{
    return attackers_to (king_sq (c)) & _board.pieces (~c);
}

// Blockers are lonely defenders of the attacks on square by the attackers
inline Bitboard Position::blockers (Square s, Bitboard pinners) const
{
    Bitboard occ = _board.pieces ();
    Bitboard defenders = _board.pieces (_active);
    Bitboard blockers = 0;
    while (pinners)
    {
        Bitboard blocker = BitBoard::betwen_sq_bb (s, pop_lsb (pinners)) & occ;
        //if (blocker && !BitBoard::more_than_one (blocker) && (blocker & defenders))
        //{
        //    blockers |= blocker;
        //}
        if (!BitBoard::more_than_one (blocker))
        {
            blockers |= (blocker & defenders);
        }
    }
    return blockers;
}

inline Bitboard Position::hidden_checkers (Square sq_king, Color c) const
{
    Bitboard hdn_chkrs =
        (BitBoard::attacks_bb<ROOK> (sq_king) & _board.pieces (c, QUEN, ROOK)) |
        (BitBoard::attacks_bb<BSHP> (sq_king) & _board.pieces (c, QUEN, BSHP));
    return (hdn_chkrs) ? blockers (sq_king, hdn_chkrs) : 0;
}

// Pinners => Only bishops, rooks, queens...  kings, knights, and pawns cannot pin.
// Pinneds => All except king, king must be immediately removed from check under all circumstances.
// Pinneds are friend pieces, that save the friend king from enemy pinners.
inline Bitboard Position::pinneds () const
{
    return hidden_checkers (_board.king_sq (_active), ~_active);
}

// Check discovers are candidate friend anti-sliders w.r.t piece behind it,
// that give the discover check to enemy king when moved.
inline Bitboard Position::check_discovers () const
{
    return hidden_checkers (_board.king_sq (~_active), _active);
}

#pragma endregion

#pragma region Piece properties

inline bool Position::passed_pawn (Color c, Square s) const
{
    return !(_board.pieces (~c, PAWN) & BitBoard::passer_span_pawn_bb (c, s));
}

inline bool Position::has_pawn_on_7thR (Color c) const
{
    return _board.pieces (c, PAWN) & BitBoard::rel_rank_bb (c, R_7);
}
// check the opposite sides have opposite bishops
inline bool Position::has_opposite_bishops () const
{
    return
        (_board.piece_count (WHITE, BSHP) == 1) &&
        (_board.piece_count (BLACK, BSHP) == 1) &&
        opposite_colors(_board[W_BSHP][0], _board[B_BSHP][0]);
}
// check the side has pair of opposite color bishops
inline bool Position::has_pair_bishops (Color c) const
{
    uint32_t bishop_count = _board.piece_count(c, BSHP);
    if (bishop_count >= 2)
    {
        for (uint32_t cnt = 0; cnt < bishop_count-1; ++cnt)
        {
            if (opposite_colors(_board[c|BSHP][cnt], _board[c|BSHP][cnt+1])) return true;
        }
    }
    return false;
}

#pragma endregion

#pragma region Basic methods

inline void Position::place_piece (Square s, Color c, PType t) { _board.place_piece (s, c, t); }
inline void Position::place_piece (Square s, Piece p) { _board.place_piece (s, p); }
inline Piece Position::remove_piece (Square s) { return _board.remove_piece (s); }
inline Piece Position::move_piece (Square s1, Square s2) { return _board.move_piece (s1, s2); }

#pragma endregion

#pragma endregion


template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits>& os, const StateInfo &si)
{
    os << std::string (si);
    return os;
}

template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits>& os, const Position &pos)
{
    os << std::string (pos);
    return os;
}

template<class charT, class Traits>
inline std::basic_istream<charT, Traits>&
    operator>> (std::basic_istream<charT, Traits>& is, Position &pos)
{
    //is >> std::string (pos);
    return is;
}


typedef std::stack<StateInfo>             StateInfoStack;
typedef std::unique_ptr<StateInfoStack>   StateInfoStackPtr;


#endif
