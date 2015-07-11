#ifndef _POSITION_H_INC_
#define _POSITION_H_INC_

#include <memory>
#include <stack>

#include "BitBoard.h"
#include "Zobrist.h"

class Position;

// FORSYTH-EDWARDS NOTATION (FEN) is a standard notation for describing a particular board position of a chess game.
// The purpose of FEN is to provide all the necessary information to restart a game from a particular position.
extern const std::string STARTUP_FEN;

extern Score PSQ[CLR_NO][NONE][SQ_NO];

// Check the validity of FEN string
extern bool _ok (const std::string &fen, bool c960 = false, bool full = true);

// StateInfo stores information needed to restore a Position object to its previous state
// when we retract a move. Whenever a move is made on the board (by calling do_move),
// a StateInfo object must be passed as a parameter.
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
    Move   last_move;      // Move played on the previous position.
    PieceT capture_type;   // Piece type captured.
    Key    posi_key;       // Hash key of position.
    Bitboard checkers;     // Checkers bitboard.

    StateInfo *ptr = nullptr;
};

typedef std::stack<StateInfo>   StateStack;

// CheckInfo stores critical information used to detect if a move gives check.
//
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

    CheckInfo () = default;
    explicit CheckInfo (const Position &pos);
};

namespace Threading {
    class Thread;
}

using namespace Threading;

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
//  - Initial files of the kings and both pairs of rooks, also kings path
//    This is used to implement the Chess960 castling rules.
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
    i08      _piece_index[SQ_NO];

    StateInfo  _sb; // Object for base status information
    StateInfo *_si; // Pointer for current status information

    CRight   _castle_mask[SQ_NO];
    Square   _castle_rook[CR_ALL];
    Bitboard _castle_path[CR_ALL];
    Bitboard _king_path  [CR_ALL];

    // Side on move
    // "w" - WHITE
    // "b" - BLACK
    Color    _active;
    // Ply of the game, incremented after every move.
    i32      _game_ply;
    u64      _game_nodes;

    bool     _chess960;

    Thread  *_thread;

    // ------------------------

    void set_castle (Color c, Square rook_org);

    bool can_en_passant (Square ep_sq) const;
    bool can_en_passant (File   ep_f ) const { return can_en_passant (ep_f | rel_rank (_active, R_6)); }

    Bitboard check_blockers (Color piece_c, Color king_c) const;

    template<bool Do>
    void do_castling (Square king_org, Square &king_dst, Square &rook_org, Square &rook_dst);

    template<PieceT PT>
    PieceT least_valuable_attacker (Square dst, Bitboard stm_attackers, Bitboard &mocc, Bitboard &attackers) const;

public:

    static u08  FiftyMoveDist;

    static void initialize ();

    Position () = default; // To define the global object RootPos
    Position (const Position&) = delete;
    Position (const std::string &f, Thread *const th = nullptr, bool c960 = false, bool full = true)
    {
        if (!setup (f, th, c960, full)) clear ();
    }
    Position (const Position &pos, Thread *const th)
    {
        *this = pos;
        _thread = th;
    }

    Position& operator= (const Position &pos); // To assign RootPos from UCI

    Piece    operator[] (Square s)      const;
    //Bitboard operator[] (Color  c)      const;
    //Bitboard operator[] (PieceT pt)     const;
    const Square* operator[] (Piece p)  const;

    bool   empty   (Square s)   const;

    Square king_sq (Color c)    const;

    Bitboard pieces ()          const;
    Bitboard pieces (Color c)   const;
    Bitboard pieces (PieceT pt) const;
    Bitboard pieces (Color c, PieceT pt) const;
    Bitboard pieces (PieceT p1, PieceT p2) const;
    Bitboard pieces (Color c, PieceT p1, PieceT p2) const;

    template<PieceT PT>
    i32      count  ()          const;
    template<PieceT PT>
    i32      count  (Color c)   const;
    i32      count  (Color c, PieceT pt) const;

    template<PieceT PT>
    const Square* list (Color c) const;

    // Castling rights for both side
    CRight castle_rights () const;
    // Target square in algebraic notation. If there's no en passant target square is "-"
    Square en_passant_sq () const;
    // Number of halfmoves clock since the last pawn advance or any capture.
    // used to determine if a draw can be claimed under the 50-move rule.
    u08    clock50       () const;
    Move   last_move     () const;  // Last move played
    PieceT capture_type  () const;  // Last ptype captured
    //Piece  capture_piece () const;  // Last piece captured
    Bitboard checkers    () const;

    Key matl_key      () const;
    Key pawn_key      () const;
    Key posi_key      () const;
    Key posi_move_key (Move m) const;

    Value non_pawn_material (Color c) const;    // Incremental piece-square evaluation

    Score   psq_score () const;

    CRight   can_castle  (CRight cr) const;
    CRight   can_castle  (Color   c) const;

    Square   castle_rook (CRight cr) const;
    Bitboard castle_path (CRight cr) const;
    Bitboard king_path   (CRight cr) const;
    bool  castle_impeded (CRight cr) const;

    Color   active    () const;
    i32     game_ply  () const;
    i32     game_move () const;
    bool    chess960  () const;
    bool    draw      () const;
    bool    repeated  () const;

    u64   game_nodes ()  const;
    void  game_nodes (u64 nodes);
    Phase game_phase ()  const;

    Thread* thread   ()  const;

    bool ok (i08 *step = nullptr) const;

    // Static Exchange Evaluation (SEE)
    Value see      (Move m) const;
    Value see_sign (Move m) const;
    

    Bitboard attackers_to (Square s, Color c, Bitboard occ) const;
    Bitboard attackers_to (Square s, Color c) const;
    Bitboard attackers_to (Square s, Bitboard occ) const;
    Bitboard attackers_to (Square s) const;

    Bitboard checkers    (Color c) const;
    Bitboard pinneds     (Color c) const;
    Bitboard discoverers (Color c) const;

    bool pseudo_legal  (Move m) const;
    bool legal         (Move m, Bitboard pinned) const;
    bool legal         (Move m) const;
    bool capture       (Move m) const;
    bool capture_or_promotion (Move m)  const;
    bool gives_check   (Move m, const CheckInfo &ci) const;
    //bool gives_checkmate (Move m, const CheckInfo &ci)
    bool advanced_pawn_push (Move m)    const;
    //Piece moving_piece (Move m) const;

    bool passed_pawn  (Color c, Square s) const;
    bool bishops_pair (Color c) const;
    bool opposite_bishops ()    const;

    void clear ();

    void  place_piece (Square s, Color c, PieceT pt);
    void  place_piece (Square s, Piece p);
    void remove_piece (Square s);
    void   move_piece (Square s1, Square s2);

    bool setup (const std::string &f, Thread *const th = nullptr, bool c960 = false, bool full = true);

    Score compute_psq_score () const;
    Value compute_non_pawn_material (Color c) const;

    // Do/Undo move
    void   do_move (Move m, StateInfo &si, bool check);
    void   do_move (std::string &can, StateInfo &si);
    void undo_move ();
    void   do_null_move (StateInfo &si);
    void undo_null_move ();

    void flip ();

    std::string fen (bool c960 = false, bool full = true) const;
    
    operator std::string () const;

    template<class CharT, class Traits>
    friend std::basic_ostream<CharT, Traits>&
        operator<< (std::basic_ostream<CharT, Traits> &os, const Position &pos)
    {
        os << std::string(pos);
        return os;
    }

};

// -------------------------------

inline Piece         Position::operator[] (Square s)  const { return _board[s]; }
//inline Bitboard      Position::operator[] (Color  c)  const { return _color_bb[c];  }
//inline Bitboard      Position::operator[] (PieceT pt) const { return _types_bb[pt]; }
inline const Square* Position::operator[] (Piece  p)  const { return _piece_list[color (p)][ptype (p)]; }

inline bool     Position::empty   (Square s) const { return EMPTY == _board[s]; }
inline Square   Position::king_sq (Color c)  const { return _piece_list[c][KING][0]; }

inline Bitboard Position::pieces ()          const { return _types_bb[NONE]; }
inline Bitboard Position::pieces (Color c)   const { return _color_bb[c];  }
inline Bitboard Position::pieces (PieceT pt) const { return _types_bb[pt]; }
inline Bitboard Position::pieces (Color c,   PieceT pt) const { return _color_bb[c]&_types_bb[pt]; }
inline Bitboard Position::pieces (PieceT p1, PieceT p2) const { return _types_bb[p1]|_types_bb[p2]; }
inline Bitboard Position::pieces (Color c, PieceT p1, PieceT p2) const { return _color_bb[c]&(_types_bb[p1]|_types_bb[p2]); }

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
inline i32 Position::count (Color c, PieceT pt) const { return _piece_count[c][pt]; }

template<PieceT PT>
inline const Square* Position::list (Color c) const { return _piece_list[c][PT]; }
// Castling rights for both side
inline CRight Position::castle_rights () const { return _si->castle_rights; }
// Target square in algebraic notation. If there's no en passant target square is "-"
inline Square Position::en_passant_sq () const { return _si->en_passant_sq; }
// Number of halfmoves clock since the last pawn advance or any capture.
// used to determine if a draw can be claimed under the 50-move rule.
inline u08    Position::clock50       () const { return _si->clock50; }
inline Move   Position::last_move     () const { return _si->last_move; }
inline PieceT Position::capture_type  () const { return _si->capture_type; }
//inline Piece  Position::capture_piece () const { return NONE != _si->capture_type ? (_active|_si->capture_type) : EMPTY; }
inline Bitboard Position::checkers    () const { return _si->checkers; }

inline Key    Position::matl_key      () const { return _si->matl_key; }
inline Key    Position::pawn_key      () const { return _si->pawn_key; }
inline Key    Position::posi_key      () const { return _si->posi_key; }
// posi_move_key() computes the new hash key after the given moven. Needed for speculative prefetch.
// It doesn't recognize special moves like castling, en-passant and promotions.
inline Key    Position::posi_move_key (Move m) const
{
    auto org = org_sq (m);
    auto dst = dst_sq (m);
    auto mpt = ptype (_board[org]);
    auto ppt = mpt == PAWN && mtype (m) == PROMOTE ? promote (m) : mpt;
    auto cpt = mpt == PAWN && mtype (m) == ENPASSANT && _si->en_passant_sq == dst_sq (m) ? PAWN : ptype (_board[dst]);
    return _si->posi_key
        ^  Zob._.act_side
        ^  Zob._.piece_square[_active][mpt][org]
        ^  Zob._.piece_square[_active][ppt][dst]
        ^  (cpt != NONE ? Zob._.piece_square[~_active][cpt][dst] : U64(0));
}

inline Score  Position::psq_score     () const { return _si->psq_score; }
inline Value  Position::non_pawn_material (Color c) const { return _si->non_pawn_matl[c]; }

inline CRight Position::can_castle   (CRight cr) const { return _si->castle_rights & cr; }
inline CRight Position::can_castle   (Color   c) const { return _si->castle_rights & mk_castle_right (c); }

inline Square   Position::castle_rook (CRight cr) const { return _castle_rook[cr]; }
inline Bitboard Position::castle_path (CRight cr) const { return _castle_path[cr]; }
inline Bitboard Position::king_path   (CRight cr) const { return _king_path[cr]; }

inline bool  Position::castle_impeded (CRight cr) const { return (_castle_path[cr] & _types_bb[NONE]) != U64(0); }
// Color of the side on move
inline Color Position::active   () const { return _active; }
// game_ply starts at 0, and is incremented after every move.
// game_ply  = max (2 * (game_move - 1), 0) + (BLACK == active)
inline i32  Position::game_ply  () const { return _game_ply; }
// game_move starts at 1, and is incremented after BLACK's move.
// game_move = max ((game_ply - (BLACK == active)) / 2, 0) + 1
inline i32  Position::game_move () const { return std::max ((_game_ply - (BLACK == _active)) / 2, 0) + 1; }
// Nodes visited
inline u64  Position::game_nodes() const { return _game_nodes; }
inline void Position::game_nodes(u64 nodes){ _game_nodes = nodes; }

inline bool Position::chess960  () const { return _chess960; }
inline Thread* Position::thread () const { return _thread; }

// Attackers to the square 's' by color 'c' on occupancy 'occ'
inline Bitboard Position::attackers_to (Square s, Color c, Bitboard occ) const
{
    return((BitBoard::PAWN_ATTACKS[~c][s]    & _types_bb[PAWN])
        |  (BitBoard::PIECE_ATTACKS[NIHT][s] & _types_bb[NIHT])
        |  ((_types_bb[BSHP]|_types_bb[QUEN]) & _color_bb[c] ? BitBoard::attacks_bb<BSHP> (s, occ)&(_types_bb[BSHP]|_types_bb[QUEN]) : U64(0))
        |  ((_types_bb[ROOK]|_types_bb[QUEN]) & _color_bb[c] ? BitBoard::attacks_bb<ROOK> (s, occ)&(_types_bb[ROOK]|_types_bb[QUEN]) : U64(0))
        |  (BitBoard::PIECE_ATTACKS[KING][s] & _types_bb[KING])) & _color_bb[c];
}
// Attackers to the square 's' by color 'c'
inline Bitboard Position::attackers_to (Square s, Color c) const
{
    return attackers_to (s, c, _types_bb[NONE]);
}

// Attackers to the square 's' on occupancy 'occ'
inline Bitboard Position::attackers_to (Square s, Bitboard occ) const
{
    return (BitBoard::PAWN_ATTACKS[WHITE][s] & _types_bb[PAWN]&_color_bb[BLACK])
        |  (BitBoard::PAWN_ATTACKS[BLACK][s] & _types_bb[PAWN]&_color_bb[WHITE])
        |  (BitBoard::PIECE_ATTACKS[NIHT][s] & _types_bb[NIHT])
        |  ((_types_bb[BSHP]|_types_bb[QUEN]) ? BitBoard::attacks_bb<BSHP> (s, occ)&(_types_bb[BSHP]|_types_bb[QUEN]) : U64(0))
        |  ((_types_bb[ROOK]|_types_bb[QUEN]) ? BitBoard::attacks_bb<ROOK> (s, occ)&(_types_bb[ROOK]|_types_bb[QUEN]) : U64(0))
        |  (BitBoard::PIECE_ATTACKS[KING][s] & _types_bb[KING]);
}
// Attackers to the square 's'
inline Bitboard Position::attackers_to (Square s) const
{
    return attackers_to (s, _types_bb[NONE]);
}

// Checkers are enemy pieces that give the direct Check to friend King of color 'c'
inline Bitboard Position::checkers (Color c) const
{
    return attackers_to (_piece_list[c][KING][0], ~c);
}
// Pinners => Only bishops, rooks, queens...  kings, knights, and pawns cannot pin.
// Pinneds => All except king, king must be immediately removed from check under all circumstances.
// Pinneds are friend pieces, that save the friend king from enemy pinners.
inline Bitboard Position::pinneds (Color c) const
{
    return check_blockers (c,  c); // blockers for own king
}
// Check discovers are candidate friend anti-sliders w.r.t piece behind it,
// that give the discover check to enemy king when moved.
inline Bitboard Position::discoverers (Color c) const
{
    return check_blockers (c, ~c); // blockers for opp king
}
inline bool Position::passed_pawn (Color c, Square s) const
{
    return ((_types_bb[PAWN]&_color_bb[~c]) & BitBoard::PAWN_PASS_SPAN[c][s]) == U64(0);
}
// check the side has pair of opposite color bishops
inline bool Position::bishops_pair (Color c) const
{
    auto bishop_count = _piece_count[c][BSHP];
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
    // Castling is encoded as "king captures the rook"
    return ((mtype (m) == NORMAL || mtype (m) == PROMOTE) && !empty (dst_sq (m)))
        || ((mtype (m) == ENPASSANT                     ) &&  empty (dst_sq (m)) && _si->en_passant_sq == dst_sq (m));
}
// capture_or_promotion(m) tests move is capture or promotion
inline bool Position::capture_or_promotion  (Move m) const
{
    return (mtype (m) == NORMAL    && !empty (dst_sq (m)))
        || (mtype (m) == ENPASSANT &&  empty (dst_sq (m)) && _si->en_passant_sq == dst_sq (m))
        || (mtype (m) == PROMOTE);
}
inline bool Position::advanced_pawn_push    (Move m) const
{
    return PAWN == ptype (_board[org_sq (m)]) && R_4 < rel_rank (_active, org_sq (m));
}
//inline Piece Position::moving_piece (Move m) const { return _board[org_sq (m)]; }

inline void  Position:: place_piece (Square s, Color c, PieceT pt)
{
    assert (empty (s));

    _board[s] = (c | pt);

    auto bb = BitBoard::SQUARE_bb[s];
    _color_bb[c]    |= bb;
    _types_bb[pt]   |= bb;
    _types_bb[NONE] |= bb;

    // Update piece list, put piece at [s] index
    _piece_index[s]  = _piece_count[c][pt]++;
    _piece_list[c][pt][_piece_index[s]] = s;
}
inline void  Position:: place_piece (Square s, Piece p)
{
    assert (_ok (p));
    place_piece (s, color (p), ptype (p));
}
inline void  Position::remove_piece (Square s)
{
    assert (!empty (s));

    // WARNING: This is not a reversible operation. If remove a piece in
    // do_move() and then replace it in undo_move() will put it at the end of
    // the list and not in its original place, it means index[] and pieceList[]
    // are not guaranteed to be invariant to a do_move() + undo_move() sequence.

    auto c  = color (_board[s]);
    auto pt = ptype (_board[s]);
    _board[s] = EMPTY;

    auto bb = ~BitBoard::SQUARE_bb[s];
    _color_bb[c]    &= bb;
    _types_bb[pt]   &= bb;
    _types_bb[NONE] &= bb;

    _piece_count[c][pt]--;

    // Update piece list, remove piece at [s] index and shrink the list.
    auto last_sq = _piece_list[c][pt][_piece_count[c][pt]];
    if (s != last_sq)
    {
        _piece_index[last_sq] = _piece_index[s];
        _piece_list[c][pt][_piece_index[last_sq]] = last_sq;
    }
    _piece_index[s] = -1;
    _piece_list[c][pt][_piece_count[c][pt]] = SQ_NO;
}
inline void  Position::  move_piece (Square s1, Square s2)
{
    assert (!empty (s1));
    assert ( empty (s2));
    assert (_piece_index[s1] != -1);

    auto c  = color (_board[s1]);
    auto pt = ptype (_board[s1]);

    _board[s2] = _board[s1];
    _board[s1] = EMPTY;

    auto bb = BitBoard::SQUARE_bb[s1] ^ BitBoard::SQUARE_bb[s2];
    _color_bb[c]    ^= bb;
    _types_bb[pt]   ^= bb;
    _types_bb[NONE] ^= bb;

    // _piece_index[s1] is not updated and becomes stale. This works as long
    // as _piece_index[] is accessed just by known occupied squares.
    _piece_index[s2] = _piece_index[s1];
    _piece_index[s1] = -1;
    _piece_list[c][pt][_piece_index[s2]] = s2;
}
// do_castling() is a helper used to do/undo a castling move.
// This is a bit tricky, especially in Chess960.
template<bool Do>
inline void Position::do_castling (Square king_org, Square &king_dst, Square &rook_org, Square &rook_dst)
{
    // Move the piece. The tricky Chess960 castle is handled earlier
    rook_org = king_dst; // castle is always encoded as "King captures friendly Rook"
    king_dst = rel_sq (_active, king_dst > king_org ? SQ_G1 : SQ_C1);
    rook_dst = rel_sq (_active, king_dst > king_org ? SQ_F1 : SQ_D1);
    // Remove both pieces first since squares could overlap in chess960
    remove_piece (Do ? king_org : king_dst);
    remove_piece (Do ? rook_org : rook_dst);
    place_piece (Do ? king_dst : king_org, _active, KING);
    place_piece (Do ? rook_dst : rook_org, _active, ROOK);
}

// ----------------------------------------------

inline CheckInfo::CheckInfo (const Position &pos)
{
    king_sq = pos.king_sq (~pos.active ());
    pinneds = pos.pinneds ( pos.active ());
    discoverers = pos.discoverers (pos.active ());

    checking_bb[PAWN] = BitBoard::PAWN_ATTACKS[~pos.active ()][king_sq];
    checking_bb[NIHT] = BitBoard::PIECE_ATTACKS[NIHT][king_sq];
    checking_bb[BSHP] = BitBoard::attacks_bb<BSHP> (king_sq, pos.pieces ());
    checking_bb[ROOK] = BitBoard::attacks_bb<ROOK> (king_sq, pos.pieces ());
    checking_bb[QUEN] = checking_bb[BSHP] | checking_bb[ROOK];
    checking_bb[KING] = U64(0);
}

#endif // _POSITION_H_INC_
