#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _POSITION_H_INC_
#define _POSITION_H_INC_

#include <algorithm>
#include <memory>
#include <stack>

#include "BitBoard.h"
#include "BitScan.h"
#include "Zobrist.h"

class Position;

namespace Threads {
    class Thread;
}

// FORSYTH-EDWARDS NOTATION (FEN) is a standard notation for describing a particular board position of a chess game.
// The purpose of FEN is to provide all the necessary information to restart a game from a particular position.

extern const std::string StartFEN;

// Check the validity of FEN string
extern bool _ok (const std::string &fen, bool c960 = false, bool full = true);

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
struct StateInfo
{
public:
    Value  non_pawn_matl[CLR_NO];
    Score  psq_score;
    Key    matl_key;       // Hash key of materials.
    Key    pawn_key;       // Hash key of pawns.
    CRight castle_rights;  // Castling-rights information for both side.
    // "In passing" - Target square in algebraic notation.
    // If there's no en-passant target square is "-".
    Square en_passant_sq;
    // Number of halfmoves clock since the last pawn advance or any capture.
    // used to determine if a draw can be claimed under the 50-move rule.
    u08    clock50;
    u08    null_ply;
    // -------------------------------------
    Key    posi_key;       // Hash key of position.
    Move   last_move;      // Move played on the previous position.
    PieceT capture_type;   // Piece type captured.
    
    Bitboard checkers;     // Checkers bitboard.

    StateInfo *p_si;
};

typedef std::stack<StateInfo>   StateInfoStack;

// CheckInfo struct is initialized at c'tor time.
// CheckInfo stores critical information used to detect if a move gives check.
//  - Checking squares from which the enemy king can be checked
//  - Pinned pieces.
//  - Check discoverer pieces.
//  - Enemy king square.
struct CheckInfo
{
public:
    Bitboard checking_bb[NONE]; // Checking squares from which the enemy king can be checked
    Bitboard pinneds;           // Pinned pieces
    Bitboard discoverers;       // Check discoverer pieces
    Square   king_sq;           // Enemy king square

    CheckInfo () {}
    explicit CheckInfo (const Position &pos);
};

// The position data structure. A position consists of the following data:
//
// Board consits of data about piece placement
//  - 64-entry array of pieces, indexed by the square.
//  - Bitboards of each piece type.
//  - Bitboards of each color
//  - Bitboard of all occupied squares.
//  - List of squares for the pieces.
//  - Count of the pieces.
//  - ----------x-----------
//  - Color of side on move.
//  - Ply of the game.
//  - StateInfo object for the base status.
//  - StateInfo pointer for the current status.
//  - Information about the castling rights for both sides.
//  - The initial files of the kings and both pairs of rooks. This is
//    used to implement the Chess960 castling rules.
//  - Nodes visited during search.
//  - Chess 960 info
class Position
{

private:
    
    Piece    _board   [SQ_NO];  // Board for storing pieces.

    Bitboard _color_bb[CLR_NO];
    Bitboard _types_bb[TOTL];

    Square   _piece_list [CLR_NO][NONE][16];
    u08      _piece_count[CLR_NO][NONE];
    i08      _index   [SQ_NO];

    StateInfo  _sb; // Object for base status information
    StateInfo *_si; // Pointer for current status information

    CRight   _castle_mask[SQ_NO];
    Square   _castle_rook[CR_ALL];
    Bitboard _castle_path[CR_ALL];

    // Side on move
    // "w" - WHITE
    // "b" - BLACK
    Color    _active;
    // Ply of the game, incremented after every move.
    u16      _game_ply;
    bool     _chess960;

    u64      _game_nodes;

    Threads::Thread *_thread;

    // ------------------------

    void set_castle (Color c, Square org_rook);

    bool can_en_passant (Square ep_sq) const;
    bool can_en_passant (File   ep_f ) const { return can_en_passant (ep_f | rel_rank (_active, R_6)); }

    Bitboard check_blockers (Color piece_c, Color king_c) const;

    template<bool DO>
    void do_castling (Square org_king, Square &dst_king, Square &org_rook, Square &dst_rook);

    template<PieceT PT>
    PieceT least_valuable_attacker (Square dst, Bitboard stm_attackers, Bitboard &occupied, Bitboard &attackers) const;

public:

    static u08 _50_move_dist;

    static void initialize ();

    Position () { clear (); }

    Position (const std::string &f, Threads::Thread *th = NULL, bool c960 = false, bool full = true)
    {
        if (!setup (f, th, c960, full)) clear ();
    }
    Position (const Position  &pos, Threads::Thread *th = NULL) { *this = pos; _thread = th; }
    
    explicit Position (i32) {}

   ~Position() { _thread = NULL; }

    Position& operator= (const Position &pos);

    Piece    operator[] (Square s)      const;
    Bitboard operator[] (Color  c)      const;
    Bitboard operator[] (PieceT pt)     const;
    const Square* operator[] (Piece p)  const;

    bool   empty   (Square s)   const;

    Square king_sq (Color c)    const;

    Bitboard pieces (Color c)   const;

    Bitboard pieces (PieceT pt) const;
    template<PieceT PT>
    Bitboard pieces ()          const;

    Bitboard pieces (Color c, PieceT pt)   const;
    template<PieceT PT>
    Bitboard pieces (Color c)   const;

    Bitboard pieces (PieceT p1, PieceT p2) const;
    Bitboard pieces (Color c, PieceT p1, PieceT p2) const;

    Bitboard pieces ()          const;

    i32      count  (Color c, PieceT pt)   const;
    template<PieceT PT>
    i32      count  (Color c)   const;
    template<PieceT PT>
    i32      count  ()          const;

    template<PieceT PT>
    const Square* list (Color c)const;

    // Castling rights for both side
    CRight castle_rights () const;
    // Target square in algebraic notation. If there's no en passant target square is "-"
    Square en_passant_sq () const;
    // Number of halfmoves clock since the last pawn advance or any capture.
    // used to determine if a draw can be claimed under the 50-move rule.
    u08    clock50       () const;
    Move   last_move     () const;  // Last move played
    PieceT capture_type  () const;  // Last ptype captured
    Piece  capture_piece () const;  // Last piece captured
    Bitboard checkers    () const;

    Key matl_key      () const;
    Key pawn_key      () const;
    Key posi_key      () const;
    Key posi_key_excl () const;

    Value non_pawn_material (Color c) const;    // Incremental piece-square evaluation

    Score psq_score ()      const;

    CRight can_castle   (CRight cr) const;
    CRight can_castle   (Color   c) const;

    Square   castle_rook (CRight cr) const;
    Bitboard castle_path (CRight cr) const;
    bool  castle_impeded (CRight cr) const;

    Color   active    () const;
    u16     game_ply  () const;
    u16     game_move () const;
    bool    chess960  () const;
    bool    draw      () const;
    bool    repeated  () const;

    u64  game_nodes ()   const;
    void game_nodes (u64 nodes);

    Threads::Thread* thread () const;

    bool ok (i08 *step = NULL) const;

    // Static Exchange Evaluation (SEE)
    Value see      (Move m) const;
    Value see_sign (Move m) const;

    Bitboard attackers_to (Square s, Bitboard occ) const;
    Bitboard attackers_to (Square s) const;

    Bitboard checkers    (Color c)   const;
    Bitboard pinneds     (Color c)   const;
    Bitboard discoverers (Color c)   const;

    bool pseudo_legal (Move m)       const;
    bool legal        (Move m, Bitboard pinned)  const;
    bool legal        (Move m)       const;
    bool capture      (Move m)       const;
    bool capture_or_promotion (Move m)    const;
    bool gives_check     (Move m, const CheckInfo &ci) const;
    //bool gives_checkmate (Move m, const CheckInfo &ci) const;
    bool advanced_pawn_push (Move m)      const;
    Piece moved_piece (Move m)  const;

    bool passed_pawn  (Color c, Square s) const;
    bool pawn_on_7thR (Color c) const;
    bool bishops_pair (Color c) const;
    bool opposite_bishops ()    const;

    void clear ();

    void  place_piece (Square s, Color c, PieceT pt);
    void  place_piece (Square s, Piece p);
    void remove_piece (Square s);
    void   move_piece (Square s1, Square s2);

    bool setup (const std::string &f, Threads::Thread *th = NULL, bool c960 = false, bool full = true);

    void flip ();

    Score compute_psq_score () const;
    Value compute_non_pawn_material (Color c) const;

    // Do/Undo move
    void   do_move (Move m, StateInfo &si, const CheckInfo *ci);
    void   do_move (Move m, StateInfo &si);
    void   do_move (std::string &can, StateInfo &si);
    void undo_move ();
    void   do_null_move (StateInfo &si);
    void undo_null_move ();

    std::string fen (bool c960, bool full = true) const;
    std::string fen () const { return fen (false); }
    
    operator std::string () const;

    static bool parse (Position &pos, const std::string &fen, Threads::Thread *thread = NULL, bool c960 = false, bool full = true);

    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const Position &pos)
    {
        os << std::string (pos);
        return os;
    }

};

// -------------------------------

INLINE Piece         Position::operator[] (Square s)  const { return _board[s]; }
inline Bitboard      Position::operator[] (Color  c)  const { return _color_bb[c];  }
inline Bitboard      Position::operator[] (PieceT pt) const { return _types_bb[pt]; }
inline const Square* Position::operator[] (Piece  p)  const { return _piece_list[color (p)][ptype (p)]; }

INLINE bool     Position::empty   (Square s) const { return EMPTY == _board[s]; }
inline Square   Position::king_sq (Color c)  const { return _piece_list[c][KING][0]; }

inline Bitboard Position::pieces (Color c)   const { return _color_bb[c];  }

inline Bitboard Position::pieces (PieceT pt) const { return _types_bb[pt]; }
template<PieceT PT>
inline Bitboard Position::pieces ()                   const { return _types_bb[PT]; }

inline Bitboard Position::pieces (Color c, PieceT pt) const { return _color_bb[c]&_types_bb[pt]; }
template<PieceT PT>
inline Bitboard Position::pieces (Color c)            const { return _color_bb[c]&_types_bb[PT]; }

inline Bitboard Position::pieces (PieceT p1, PieceT p2)const { return _types_bb[p1]|_types_bb[p2]; }
inline Bitboard Position::pieces (Color c, PieceT p1, PieceT p2) const { return _color_bb[c]&(_types_bb[p1]|_types_bb[p2]); }

inline Bitboard Position::pieces ()                   const { return _types_bb[NONE]; }

inline i32 Position::count (Color c, PieceT pt)   const { return _piece_count[c][pt]; }
template<PieceT PT>
// Count specific piece of color
inline i32 Position::count (Color c) const { return _piece_count[c][PT]; }
template<>
// Count total pieces of color
inline i32 Position::count<NONE>    (Color c) const
{
    return _piece_count[c][PAWN]
         + _piece_count[c][NIHT]
         + _piece_count[c][BSHP]
         + _piece_count[c][ROOK]
         + _piece_count[c][QUEN]
         + _piece_count[c][KING];
}
template<>
// Count non-pawn pieces of color
inline i32 Position::count<NONPAWN> (Color c) const
{
    return _piece_count[c][NIHT]
         + _piece_count[c][BSHP]
         + _piece_count[c][ROOK]
         + _piece_count[c][QUEN];
}

template<PieceT PT>
// Count specific piece
inline i32 Position::count ()          const
{
    return _piece_count[WHITE][PT] + _piece_count[BLACK][PT];
}
template<>
// Count total pieces
inline i32 Position::count<NONE>    () const
{
    return _piece_count[WHITE][PAWN] + _piece_count[BLACK][PAWN]
         + _piece_count[WHITE][NIHT] + _piece_count[BLACK][NIHT]
         + _piece_count[WHITE][BSHP] + _piece_count[BLACK][BSHP]
         + _piece_count[WHITE][ROOK] + _piece_count[BLACK][ROOK]
         + _piece_count[WHITE][QUEN] + _piece_count[BLACK][QUEN]
         + _piece_count[WHITE][KING] + _piece_count[BLACK][KING];
}
template<>
// Count non-pawn pieces
inline i32 Position::count<NONPAWN> () const
{
    return _piece_count[WHITE][NIHT] + _piece_count[BLACK][NIHT]
         + _piece_count[WHITE][BSHP] + _piece_count[BLACK][BSHP]
         + _piece_count[WHITE][ROOK] + _piece_count[BLACK][ROOK]
         + _piece_count[WHITE][QUEN] + _piece_count[BLACK][QUEN];
}


template<PieceT PT>
inline const Square* Position::list (Color c) const { return _piece_list[c][PT]; }
// Castling rights for both side
inline CRight   Position::castle_rights () const { return _si->castle_rights; }
// Target square in algebraic notation. If there's no en passant target square is "-"
inline Square   Position::en_passant_sq () const { return _si->en_passant_sq; }
// Number of halfmoves clock since the last pawn advance or any capture.
// used to determine if a draw can be claimed under the 50-move rule.
inline u08    Position::clock50       () const { return _si->clock50; }
inline Move   Position::last_move     () const { return _si->last_move; }
inline PieceT Position::capture_type  () const { return _si->capture_type; }
inline Piece  Position::capture_piece () const { return (NONE == _si->capture_type) ? EMPTY : (_active | _si->capture_type); }
inline Bitboard Position::checkers    () const { return _si->checkers; }

inline Key    Position::matl_key      () const { return _si->matl_key; }
inline Key    Position::pawn_key      () const { return _si->pawn_key; }
inline Key    Position::posi_key      () const { return _si->posi_key; }
inline Key    Position::posi_key_excl () const { return _si->posi_key ^ Zobrist::Exclusion; }

inline Score  Position::psq_score     () const { return _si->psq_score; }
inline Value  Position::non_pawn_material (Color c) const { return _si->non_pawn_matl[c]; }

inline CRight Position::can_castle   (CRight cr) const { return _si->castle_rights & cr; }
inline CRight Position::can_castle   (Color   c) const { return _si->castle_rights & mk_castle_right (c); }

inline Square   Position::castle_rook (CRight cr) const { return _castle_rook[cr]; }
inline Bitboard Position::castle_path (CRight cr) const { return _castle_path[cr]; }
inline bool  Position::castle_impeded (CRight cr) const { return _castle_path[cr] & _types_bb[NONE]; }
// Color of the side on move
inline Color Position::active   () const { return _active; }
// game_ply starts at 0, and is incremented after every move.
// game_ply  = max (2 * (game_move - 1), 0) + (BLACK == active)
inline u16  Position::game_ply  () const { return _game_ply; }
// game_move starts at 1, and is incremented after BLACK's move.
// game_move = max ((game_ply - (BLACK == active)) / 2, 0) + 1
inline u16  Position::game_move () const { return std::max ((_game_ply - (BLACK == _active)) / 2, 0) + 1; }
inline bool Position::chess960  () const { return _chess960; }
// Nodes visited
inline u64  Position::game_nodes() const { return _game_nodes; }
inline void Position::game_nodes(u64 nodes){ _game_nodes = nodes; }
inline Threads::Thread* Position::thread () const { return _thread; }

// Attackers to the square on given occ
inline Bitboard Position::attackers_to (Square s, Bitboard occ) const
{
    return (BitBoard::PawnAttacks[WHITE][s]    & _types_bb[PAWN]&_color_bb[BLACK])
        |  (BitBoard::PawnAttacks[BLACK][s]    & _types_bb[PAWN]&_color_bb[WHITE])
        |  (BitBoard::PieceAttacks[NIHT][s]    & _types_bb[NIHT])
        |  (BitBoard::attacks_bb<BSHP> (s, occ)&(_types_bb[BSHP]|_types_bb[QUEN]))
        |  (BitBoard::attacks_bb<ROOK> (s, occ)&(_types_bb[ROOK]|_types_bb[QUEN]))
        |  (BitBoard::PieceAttacks[KING][s]    & _types_bb[KING]);
}
// Attackers to the square
inline Bitboard Position::attackers_to (Square s) const
{
    return attackers_to (s, _types_bb[NONE]);
}
// Checkers are enemy pieces that give the direct Check to friend King of color 'c'
inline Bitboard Position::checkers (Color c) const
{
    return attackers_to (_piece_list[c][KING][0], _types_bb[NONE]) & _color_bb[~c];
}
// Pinners => Only bishops, rooks, queens...  kings, knights, and pawns cannot pin.
// Pinneds => All except king, king must be immediately removed from check under all circumstances.
// Pinneds are friend pieces, that save the friend king from enemy pinners.
inline Bitboard Position::pinneds (Color c) const
{
    return check_blockers (c,  c); // blockers for self king
}
// Check discovers are candidate friend anti-sliders w.r.t piece behind it,
// that give the discover check to enemy king when moved.
inline Bitboard Position::discoverers (Color c) const
{
    return check_blockers (c, ~c); // blockers for opp king
}
inline bool Position::passed_pawn (Color c, Square s) const
{
    return !(pieces<PAWN> (~c) & BitBoard::PasserPawnSpan[c][s]);
}
inline bool Position::pawn_on_7thR (Color c) const
{
    return pieces<PAWN> (c) & BitBoard::Rank_bb[rel_rank (c, R_7)];
}
// check the side has pair of opposite color bishops
inline bool Position::bishops_pair (Color c) const
{
    u08 bishop_count = _piece_count[c][BSHP];
    if (bishop_count > 1)
    {
        for (u08 pc = 0; pc < bishop_count-1; ++pc)
        {
            if (opposite_colors (_piece_list[c][BSHP][pc], _piece_list[c][BSHP][pc+1])) return true;
        }
    }
    return false;
}
// check the opposite sides have opposite bishops
inline bool Position::opposite_bishops () const
{
    return _piece_count[WHITE][BSHP] == 1
        && _piece_count[BLACK][BSHP] == 1
        && opposite_colors (_piece_list[WHITE][BSHP][0], _piece_list[BLACK][BSHP][0]);
}
inline bool Position::legal         (Move m) const { return legal (m, pinneds (_active)); }
// capture(m) tests move is capture
inline bool Position::capture       (Move m) const
{
    MoveT mt = mtype (m);
    return (mt == NORMAL || mt == PROMOTE) ? (EMPTY != _board[dst_sq (m)])
         : (mt == ENPASSANT) ? _ok (_si->en_passant_sq)
         : false;
}
// capture_or_promotion(m) tests move is capture or promotion
inline bool Position::capture_or_promotion  (Move m) const
{
    MoveT mt = mtype (m);
    return (mt == NORMAL) ? (EMPTY != _board[dst_sq (m)])
         : (mt == ENPASSANT) ? _ok (_si->en_passant_sq)
         : (mt != CASTLE);
}
inline bool Position::advanced_pawn_push    (Move m) const
{
    return (PAWN == ptype (_board[org_sq (m)])) && (R_4 < rel_rank (_active, org_sq (m)));
}
inline Piece Position:: moved_piece (Move m) const { return _board[org_sq (m)]; }

inline void  Position:: place_piece (Square s, Color c, PieceT pt)
{
    ASSERT (EMPTY == _board[s]);
    _board[s] = (c | pt);

    Bitboard bb      = BitBoard::Square_bb[s];
    _color_bb[c]    |= bb;
    _types_bb[pt]   |= bb;
    _types_bb[NONE] |= bb;

    // Update piece list, put piece at [s] index
    _index[s]  = _piece_count[c][pt]++;
    _piece_list[c][pt][_index[s]] = s;
}
inline void  Position:: place_piece (Square s, Piece p)
{
    place_piece (s, color (p), ptype (p));
}
inline void  Position::remove_piece (Square s)
{
    ASSERT (EMPTY != _board[s]);

    // WARNING: This is not a reversible operation. If we remove a piece in
    // do_move() and then replace it in undo_move() we will put it at the end of
    // the list and not in its original place, it means index[] and pieceList[]
    // are not guaranteed to be invariant to a do_move() + undo_move() sequence.

    Piece  p  = _board[s];
    Color  c  = color (p);
    PieceT pt = ptype (p);
    _board[s] = EMPTY;

    Bitboard bb      = ~BitBoard::Square_bb[s];
    _color_bb[c]    &= bb;
    _types_bb[pt]   &= bb;
    _types_bb[NONE] &= bb;

    _piece_count[c][pt]--;

    // Update piece list, remove piece at [s] index and shrink the list.
    Square last_sq = _piece_list[c][pt][_piece_count[c][pt]];
    if (s != last_sq)
    {
        _index[last_sq] = _index[s];
        _piece_list[c][pt][_index[last_sq]] = last_sq;
    }
    _index[s] = -1;
    _piece_list[c][pt][_piece_count[c][pt]]   = SQ_NO;
}
inline void  Position::  move_piece (Square s1, Square s2)
{
    ASSERT (EMPTY != _board[s1]);
    ASSERT (EMPTY == _board[s2]);
    ASSERT (_index[s1] != -1);

    Piece  p  = _board[s1];
    Color  c  = color (p);
    PieceT pt = ptype (p);

    _board[s1] = EMPTY;
    _board[s2] = p;

    Bitboard bb = BitBoard::Square_bb[s1] ^ BitBoard::Square_bb[s2];
    _color_bb[c]    ^= bb;
    _types_bb[pt]   ^= bb;
    _types_bb[NONE] ^= bb;

    // _index[s1] is not updated and becomes stale. This works as long
    // as _index[] is accessed just by known occupied squares.
    _index[s2] = _index[s1];
    _index[s1] = -1;
    _piece_list[c][pt][_index[s2]] = s2;
}
// do_castling() is a helper used to do/undo a castling move.
// This is a bit tricky, especially in Chess960.
template<bool DO>
inline void Position::do_castling (Square org_king, Square &dst_king, Square &org_rook, Square &dst_rook)
{
    // Move the piece. The tricky Chess960 castle is handled earlier
    bool king_side = (dst_king > org_king);
    org_rook = dst_king; // castle is always encoded as "King captures friendly Rook"
    dst_king = rel_sq (_active, king_side ? SQ_G1 : SQ_C1);
    dst_rook = rel_sq (_active, king_side ? SQ_F1 : SQ_D1);
    // Remove both pieces first since squares could overlap in chess960
    remove_piece (DO ? org_king : dst_king);
    remove_piece (DO ? org_rook : dst_rook);
    place_piece (DO ? dst_king : org_king, _active, KING);
    place_piece (DO ? dst_rook : org_rook, _active, ROOK);
}

// ----------------------------------------------

inline CheckInfo::CheckInfo (const Position &pos)
{
    Color active = pos.active ();
    Color pasive = ~active;

    king_sq = pos.king_sq (pasive);
    pinneds = pos.pinneds (active);
    discoverers = pos.discoverers (active);

    checking_bb[PAWN] = BitBoard::PawnAttacks[pasive][king_sq];
    checking_bb[NIHT] = BitBoard::PieceAttacks[NIHT][king_sq];
    checking_bb[BSHP] = BitBoard::attacks_bb<BSHP> (king_sq, pos.pieces ());
    checking_bb[ROOK] = BitBoard::attacks_bb<ROOK> (king_sq, pos.pieces ());
    checking_bb[QUEN] = checking_bb[BSHP] | checking_bb[ROOK];
    checking_bb[KING] = U64 (0);
}

#endif // _POSITION_H_INC_
