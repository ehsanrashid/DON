//#pragma once
#ifndef POSITION_H_
#define POSITION_H_

#include <algorithm>
#include <memory>
#include <stack>

#include "BitBoard.h"
#include "BitScan.h"
#include "Zobrist.h"

class Position;
struct Thread;

#pragma region FEN

// FORSYTH–EDWARDS NOTATION (FEN) is a standard notation for describing a particular board position of a chess game.
// The purpose of FEN is to provide all the necessary information to restart a game from a particular position.

// 88 is the max FEN length - r1n1k1r1/1B1b1q1n/1p1p1p1p/p1p1p1p1/1P1P1P1P/P1P1P1P1/1b1B1Q1N/R1N1K1R1 w KQkq - 12 1000
const uint8_t MAX_FEN     = 88;

// N-FEN (NATURAL-FEN)
// "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
// X-FEN (CHESS960-FEN) (Fischer Random Chess)
// "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1"

//extern const char *const FEN_N;
//extern const char *const FEN_X;
extern const std::string FEN_N;
extern const std::string FEN_X;

// Check the validity of FEN string
#ifdef _DEBUG
extern bool _ok (const        char *fen, bool c960 = false, bool full = true);
#endif
extern bool _ok (const std::string &fen, bool c960 = false, bool full = true);

#pragma endregion

#pragma region State Information

//#pragma pack (push, 4)

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
typedef struct StateInfo
{
public:

    Value non_pawn_matl[CLR_NO];
    Score psq_score;

    // Hash key of materials.
    Key matl_key;
    // Hash key of pawns.
    Key pawn_key;

    // Castling-rights information for both side.
    CRight castle_rights;

    // "In passing" - Target square in algebraic notation.
    // If there's no en-passant target square is "-".
    Square en_passant;
    // Number of halfmoves clock since the last pawn advance or any capture.
    // used to determine if a draw can be claimed under the 50-move rule.
    uint32_t
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

} StateInfo;

//#pragma pack (pop)

#pragma endregion

#pragma region Check Inforamtion

//#pragma pack (push, 4)

// CheckInfo struct is initialized at c'tor time.
// CheckInfo stores critical information used to detect if a move gives check.
//  - checking squares.
//  - pinned pieces.
//  - check discoverer pieces.
//  - enemy king square.
typedef struct CheckInfo
{
public:
    // Checking squares from which the enemy king can be checked
    Bitboard checking_bb[NONE];
    // Pinned pieces
    Bitboard pinneds;
    // Check discoverer pieces
    Bitboard discoverers;
    // Enemy king square
    Square king_sq;

    explicit CheckInfo (const Position &pos);

    void clear ();

} CheckInfo;

//#pragma pack (pop)

#pragma endregion

#pragma region Position

//#pragma pack (push, 4)

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
typedef class Position
{

private:

#pragma region Fields

    // Board for storing pieces.
    Piece    _piece_arr[SQ_NO];
    Bitboard _color_bb[CLR_NO];
    Bitboard _types_bb[ALLS];

    Square   _piece_list [CLR_NO][NONE][16];
    uint8_t  _piece_count[CLR_NO][ALLS];
    int8_t   _piece_index[SQ_NO];

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

    Thread  *_thread;

#pragma endregion

public:

    static uint8_t fifty_move_distance;

    static void initialize ();

#pragma region Constructors

    Position () { clear (); }
#ifdef _DEBUG
    Position (const        char *fen, Thread *thread = NULL, bool c960 = false, bool full = true)
    {
        if (!setup (fen, thread, c960, full)) clear ();
    }
#endif
    Position (const std::string &fen, Thread *thread = NULL, bool c960 = false, bool full = true)
    {
        if (!setup (fen, thread, c960, full)) clear ();
    }
    Position (const Position &pos, Thread *thread = NULL) { *this = pos; _thread = thread; }
    //Position (const Position &pos) { *this = pos; }
    explicit Position (int8_t dummy) {}

#pragma endregion

    Position& operator= (const Position &pos);

#pragma region Basic properties

#pragma region Board properties

    Piece    operator[] (Square s)      const;
    Bitboard operator[] (Color  c)      const;
    Bitboard operator[] (PType pt)      const;
    const Square* operator[] (Piece p)  const;

    bool empty     (Square s)           const;
    Piece piece_on (Square s)           const;

    Square king_sq (Color c)            const;

    Bitboard pieces (Color c)           const;
    Bitboard pieces (PType pt)          const;
    Bitboard pieces (Color c, PType pt) const;
    Bitboard pieces (PType p1, PType p2) const;
    Bitboard pieces (Color c, PType p1, PType p2) const;
    Bitboard pieces () const;
    Bitboard empties () const;
    //Bitboard pieces (Piece p) const;

    template<PType PT>
    int32_t piece_count (Color c)       const;
    template<PType PT>
    int32_t piece_count ()              const;
    int32_t piece_count (Color c)       const;
    int32_t piece_count ()              const;
    int32_t piece_count (Color c, PType pt) const;
    //int32_t piece_count (Piece p) const;
    //int32_t piece_count (PType pt) const;

    template<PType PT>
    const Square* piece_list (Color c)  const;

#pragma endregion

#pragma region StateInfo properties

    // Castling rights for both side
    CRight castle_rights () const;
    // Target square in algebraic notation. If there's no en passant target square is "-"
    Square en_passant () const;
    // Number of halfmoves clock since the last pawn advance or any capture.
    // used to determine if a draw can be claimed under the 50-move rule.
    uint32_t clock50 () const;
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

    Key posi_key_exclusion () const;

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

    Color    active    ()               const;
    uint32_t game_ply  ()               const;
    uint32_t game_move ()               const;
    bool     chess960  ()               const;

    uint64_t game_nodes ()              const;
    void     game_nodes (uint64_t nodes);

    Thread* thread     ()               const;

    bool draw ()                        const;
    bool ok (int8_t *failed_step = NULL) const;

    // Static Exchange Evaluation (SEE)
    int32_t see      (Move m) const;
    int32_t see_sign (Move m) const;

#pragma endregion

#pragma region Attack properties

private:

    Bitboard check_blockers (Color c, Color king_c) const;

public:

    template<PType PT>
    // Attacks of the PAWN (Color) from the square
    Bitboard attacks_from (Color c, Square s) const;
    template<PType PT>
    // Attacks of the PTYPE from the square
    Bitboard attacks_from (Square s) const;

    // Attacks of the piece from the square with occ
    Bitboard attacks_from (Piece p, Square s, Bitboard occ) const;
    // Attacks of the piece from the square
    Bitboard attacks_from (Piece p, Square s) const;

    Bitboard attackers_to (Square s, Bitboard occ) const;
    Bitboard attackers_to (Square s) const;

    Bitboard checkers    (Color c) const;
    Bitboard pinneds     (Color c) const;
    Bitboard discoverers (Color c) const;

#pragma endregion

#pragma region Move properties

    //Piece moved_piece    (Move m)                const;
    //Piece captured_piece (Move m)                const;

    bool pseudo_legal (Move m)                   const;
    bool legal        (Move m, Bitboard pinned)  const;
    bool legal        (Move m)                   const;
    bool capture      (Move m)                   const;
    bool capture_or_promotion (Move m)           const;
    bool check     (Move m, const CheckInfo &ci) const;
    bool checkmate (Move m, const CheckInfo &ci) const;

    //bool   passed_pawn_push (Move m)             const;
    bool advanced_pawn_push (Move m)             const;

#pragma endregion

#pragma region Piece specific properties

    bool passed_pawn (Color c, Square s) const;
    bool pawn_on_7thR (Color c) const;
    bool bishops_pair (Color c) const;
    bool opposite_bishops ()    const;

#pragma endregion

#pragma region Basic methods

private:

    void set_castle (Color c, Square org_rook);

    bool can_en_passant (Square ep_sq) const;
    bool can_en_passant (File   ep_f) const;

public:

    void clear ();

    void   place_piece (Square s, Color c, PType pt);
    void   place_piece (Square s, Piece p);
    void  remove_piece (Square s);
    void    move_piece (Square s1, Square s2);

#ifdef _DEBUG
    bool setup (const        char *fen, Thread *thread = NULL, bool c960 = false, bool full = true);
#endif
    bool setup (const std::string &fen, Thread *thread = NULL, bool c960 = false, bool full = true);

    void flip ();

    Score compute_psq_score () const;
    Value compute_non_pawn_material (Color c) const;

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

#ifdef _DEBUG
    bool        fen (const char *fen, bool c960 = false, bool full = true) const;
#endif
    std::string fen (bool                  c960 = false, bool full = true) const;

    operator std::string () const;

#ifdef _DEBUG
    static bool parse (Position &pos, const        char *fen, Thread *thread = NULL, bool c960 = false, bool full = true);
#endif
    static bool parse (Position &pos, const std::string &fen, Thread *thread = NULL, bool c960 = false, bool full = true);

#pragma endregion

    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const Position &pos)
    {
        os << std::string (pos);
        return os;
    }

    template<class charT, class Traits>
    friend std::basic_istream<charT, Traits>&
        operator>> (std::basic_istream<charT, Traits> &is, Position &pos)
    {
        //is >> std::string (pos);
        return is;
    }

} Position;

//#pragma pack (pop)

#pragma endregion

// -------------------------------

#pragma region Position Def

#pragma region Basic properties

#pragma region Board properties

inline Piece         Position::operator[] (Square s) const { return _piece_arr[s]; }
inline Bitboard      Position::operator[] (Color  c) const { return _color_bb[c];  }
inline Bitboard      Position::operator[] (PType pt) const { return _types_bb[pt]; }
inline const Square* Position::operator[] (Piece  p) const { return _piece_list[_color (p)][_type (p)]; }

inline bool     Position::empty   (Square s) const { return EMPTY == _piece_arr[s]; }
inline Piece    Position::piece_on(Square s) const { return          _piece_arr[s]; }

inline Square   Position::king_sq (Color c)  const { return _piece_list[c][KING][0]; }

inline Bitboard Position::pieces (Color  c)           const { return _color_bb[c];  }
inline Bitboard Position::pieces (PType pt)           const { return _types_bb[pt]; }
inline Bitboard Position::pieces (Color c, PType pt)  const { return _color_bb[c]  & _types_bb[pt]; }
inline Bitboard Position::pieces (PType p1, PType p2) const { return _types_bb[p1] | _types_bb[p2]; }
inline Bitboard Position::pieces (Color c, PType p1, PType p2) const { return _color_bb[c] & (_types_bb[p1] | _types_bb[p2]); }
inline Bitboard Position::pieces ()                   const { return  _types_bb[NONE]; }
inline Bitboard Position::empties ()                  const { return ~_types_bb[NONE]; }
//inline Bitboard Position::pieces (Piece p) const { return pieces (_color (p), _type (p)); }

template<PType PT>
inline int32_t Position::piece_count (Color c) const { return _piece_count[c][PT]; }
template<PType PT>
inline int32_t Position::piece_count ()        const { return _piece_count[WHITE][PT] + _piece_count[BLACK][PT]; }
inline int32_t Position::piece_count (Color c) const { return _piece_count[c][NONE]; }
inline int32_t Position::piece_count ()        const { return _piece_count[WHITE][NONE] + _piece_count[BLACK][NONE]; }
inline int32_t Position::piece_count (Color c, PType pt) const { return _piece_count[c][pt]; }

template<PType PT>
inline const Square* Position::piece_list (Color c) const { return _piece_list[c][PT]; }

#pragma endregion

#pragma region StateInfo properties

// Castling rights for both side
inline CRight   Position::castle_rights () const { return _si->castle_rights; }
// Target square in algebraic notation. If there's no en passant target square is "-"
inline Square   Position::en_passant    () const { return _si->en_passant; }
// Number of halfmoves clock since the last pawn advance or any capture.
// used to determine if a draw can be claimed under the 50-move rule.
inline uint32_t Position::clock50       () const { return _si->clock50; }
//
inline Move     Position::last_move     () const { return _si->last_move; }
//
inline PType    Position::cap_type      () const { return _si->cap_type; }
//
inline Piece    Position::cap_piece     () const { return (NONE == cap_type ()) ? EMPTY : (_active | cap_type ()); }
//
inline Bitboard Position::checkers      () const { return _si->checkers; }
//
inline Key      Position::matl_key      () const { return _si->matl_key; }
//
inline Key      Position::pawn_key      () const { return _si->pawn_key; }
//
inline Key      Position::posi_key      () const { return _si->posi_key; }
//
inline Key      Position::posi_key_exclusion () const { return _si->posi_key ^ Zobrist::exclusion; }

inline Score    Position::psq_score     () const { return _si->psq_score; }

inline Value    Position::non_pawn_material (Color c) const { return _si->non_pawn_matl[c]; }

//inline Value Position::pawn_material (Color c) const { return int32_t (piece_count<PAWN> (c)) * VALUE_EG_PAWN; }

#pragma endregion

#pragma region Castling properties

inline CRight Position::can_castle (CRight cr)           const { return ::can_castle (_si->castle_rights, cr); }
inline CRight Position::can_castle (Color   c)           const { return ::can_castle (_si->castle_rights, c); }
inline CRight Position::can_castle (Color   c, CSide cs) const { return ::can_castle (_si->castle_rights, c, cs); }

inline CRight Position::castle_right (Color c, File   f) const { return _castle_rights[c][f]; }
inline CRight Position::castle_right (Color c, Square s) const { return (R_1 == rel_rank (c, s)) ? _castle_rights[c][_file (s)] : CR_NO; }

inline Square Position::castle_rook  (Color c, CSide cs) const { return _castle_rooks[c][cs]; }

inline bool Position::castle_impeded (Color c, CSide cs) const
{
    return (CS_K == cs || CS_Q == cs)
        ?  (_castle_paths[c][cs]   & pieces ())
        :  (_castle_paths[c][CS_K] & pieces ())
        && (_castle_paths[c][CS_Q] & pieces ());
}

#pragma endregion

// Color of the side on move
inline Color    Position::active    () const { return _active; }
// game_ply starts at 0, and is incremented after every move.
// game_ply  = max (2 * (game_move - 1), 0) + (BLACK == active)
inline uint32_t Position::game_ply  () const { return _game_ply; }
// game_move starts at 1, and is incremented after BLACK's move.
// game_move = max ((game_ply - (BLACK == active)) / 2, 0) + 1
inline uint32_t Position::game_move () const { return std::max ((_game_ply - (BLACK == _active)) / 2, 0) + 1; }
//
inline bool     Position::chess960  () const { return _chess960; }

// Nodes visited
inline uint64_t Position::game_nodes() const { return _game_nodes; }
inline void     Position::game_nodes(uint64_t nodes){ _game_nodes = nodes; }

inline Thread*  Position::thread    () const { return _thread; }

#pragma endregion

#pragma region Attack properties

template<>
// Attacks of the PAWN from the square
inline Bitboard Position::attacks_from<PAWN> (Color c, Square s) const
{
    return BitBoard::attacks_bb<PAWN> (c, s);
}
template<PType PT>
// Attacks of the PTYPE from the square
inline Bitboard Position::attacks_from (Square s) const
{
    return (BSHP == PT || ROOK == PT) ? BitBoard::attacks_bb<PT> (s, pieces ())
        : (QUEN == PT) ? BitBoard::attacks_bb<BSHP> (s, pieces ()) | BitBoard::attacks_bb<ROOK> (s, pieces ())
        : (PAWN == PT) ? BitBoard::attacks_bb<PAWN> (_active, s)
        : BitBoard::attacks_bb<PT> (s);
}
// Attacks of the piece from the square
inline Bitboard Position::attacks_from (Piece p, Square s, Bitboard occ) const
{
    return BitBoard::attacks_bb (p, s, occ);
}
// Attacks of the piece from the square
inline Bitboard Position::attacks_from (Piece p, Square s) const
{
    return attacks_from (p, s, pieces ());
}

// Attackers to the square on given occ
inline Bitboard Position::attackers_to (Square s, Bitboard occ) const
{
    return
        (BitBoard::attacks_bb<PAWN> (WHITE, s) & pieces (BLACK, PAWN)) |
        (BitBoard::attacks_bb<PAWN> (BLACK, s) & pieces (WHITE, PAWN)) |
        (BitBoard::attacks_bb<NIHT> (s)        & pieces (NIHT))        |
        (BitBoard::attacks_bb<BSHP> (s, occ)   & pieces (BSHP, QUEN))  |
        (BitBoard::attacks_bb<ROOK> (s, occ)   & pieces (ROOK, QUEN))  |
        (BitBoard::attacks_bb<KING> (s)        & pieces (KING));
}
// Attackers to the square
inline Bitboard Position::attackers_to (Square s) const
{
    return attackers_to (s, pieces ());
}

// Checkers are enemy pieces that give the direct Check to friend King of color 'c'
inline Bitboard Position::checkers (Color c) const
{
    return attackers_to (king_sq (c)) & pieces (~c);
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

#pragma endregion

#pragma region Piece properties

inline bool Position::passed_pawn (Color c, Square s) const
{
    return !(pieces (~c, PAWN) & BitBoard::passer_pawn_span_bb (c, s));
}

inline bool Position::pawn_on_7thR (Color c) const
{
    return pieces (c, PAWN) & BitBoard::rel_rank_bb (c, R_7);
}
// check the side has pair of opposite color bishops
inline bool Position::bishops_pair (Color c) const
{
    int32_t bishop_count = piece_count<BSHP> (c);
    if (bishop_count > 1)
    {
        for (int32_t pc = 0; pc < bishop_count-1; ++pc)
        {
            if (opposite_colors (piece_list<BSHP> (c)[pc], piece_list<BSHP> (c)[pc+1])) return true;
        }
    }
    return false;
}
// check the opposite sides have opposite bishops
inline bool Position::opposite_bishops () const
{
    return
        (piece_count<BSHP> (WHITE) == 1) &&
        (piece_count<BSHP> (BLACK) == 1) &&
        opposite_colors (piece_list<BSHP> (WHITE)[0], piece_list<BSHP> (BLACK)[0]);
}

#pragma endregion

#pragma region Move properties

//// moved_piece() return piece moved on move
//inline Piece Position::moved_piece (Move m) const { return _piece_arr[org_sq (m)]; }

//// captured_piece() return piece captured by moving piece
//inline Piece Position::captured_piece (Move m) const
//{
//    Square org  = org_sq (m);
//    Square dst  = dst_sq (m);
//
//    Piece p     = piece_on (org);
//    PType pt    = _type (p);
//
//    Square cap = dst;
//
//    switch (m_type (m))
//    {
//    case CASTLE:
//        return EMPTY;
//    case ENPASSANT:
//        if (PAWN == pt)
//        {
//            cap += ((WHITE == _active) ? DEL_S : DEL_N);
//            return (BitBoard::attacks_bb<PAWN> (~_active, _si->en_passant) & pieces (_active, PAWN)) ?
//                piece_on (cap) : EMPTY;
//        }
//        return EMPTY;
//
//    case PROMOTE:
//        if (PAWN != pt
//         || R_7 != rel_rank (_active, org)
//         || R_8 != rel_rank (_active, dst))
//        {
//            return EMPTY;
//        }
//        // NOTE: no break
//    case NORMAL:
//        if (PAWN == pt)
//        {
//            // check not pawn push and can capture
//            return (1 == BitBoard::file_dist (dst, org))
//                && (BitBoard::attacks_bb<PAWN> (~_active, dst) & pieces (_active))
//                ? piece_on (cap) : EMPTY;
//        }
//        return piece_on (cap);
//    }
//    return EMPTY;
//}

inline bool Position::legal        (Move m) const { return legal (m, pinneds (_active)); }

// capture(m) tests move is capture
inline bool Position::capture               (Move m) const
{
    //MType mt = m_type (m);
    //switch (mt)
    //{
    //case CASTLE:
    //    return false;
    //case ENPASSANT:
    //    return _ok (_si->en_passant);
    //case NORMAL:
    //case PROMOTE:
    //    return !empty (dst_sq (m));
    //}
    //return false;

    MType mt = m_type (m);
    //return (!empty (dst_sq (m)) && CASTLE != mt)
    //    || (ENPASSANT == mt && _ok (_si->en_passant));
    return (NORMAL == mt || PROMOTE == mt)
        ?  !empty (dst_sq (m))
        :  (ENPASSANT == mt && _ok (_si->en_passant));
}
// capture_or_promotion(m) tests move is capture or promotion
inline bool Position::capture_or_promotion  (Move m) const
{
    //switch (m_type (m))
    //{
    //case CASTLE:    return false;
    //case PROMOTE:   return true;
    //case ENPASSANT: return _ok (_si->en_passant);
    //case NORMAL:    return !empty (dst_sq (m)); // && (~_active == _color (p)) && (KING != _type (p));
    //}
    //return false;

    MType mt = m_type (m);
    return (NORMAL == mt)
        ?  !empty (dst_sq (m))
        :  (ENPASSANT == mt && _ok (_si->en_passant)) || (CASTLE != mt);
}

//inline bool Position::  passed_pawn_push (Move m) const
//{
//    return (PAWN == _type (moved_piece (m))) && passed_pawn (_active, dst_sq (m));
//}
inline bool Position::advanced_pawn_push    (Move m) const
{
    return (PAWN == _type (piece_on (org_sq (m)))) && (R_4 < rel_rank (_active, org_sq (m)));
}

#pragma endregion

#pragma region Basic methods

inline void  Position:: place_piece (Square s, Color c, PType pt)
{
    ASSERT (empty (s));
    _piece_arr[s]    = (c | pt);
    Bitboard bb       = BitBoard::_square_bb[s];
    _color_bb[c]     |= bb;
    _types_bb[pt]    |= bb;
    _types_bb[NONE] |= bb;
    _piece_count[c][NONE]++;
    // Update piece list, put piece at [s] index
    _piece_index[s] = _piece_count[c][pt]++;
    _piece_list[c][pt][_piece_index[s]] = s;
}
inline void  Position:: place_piece (Square s, Piece p)
{
    place_piece (s, _color (p), _type (p));
}
inline void  Position::remove_piece (Square s)
{
    ASSERT (!empty (s));

    // WARNING: This is not a reversible operation. If we remove a piece in
    // do_move() and then replace it in undo_move() we will put it at the end of
    // the list and not in its original place, it means index[] and pieceList[]
    // are not guaranteed to be invariant to a do_move() + undo_move() sequence.

    Piece p = _piece_arr [s];
    
    Color c  = _color (p);
    PType pt = _type (p);

    _piece_arr [s]     = EMPTY;

    Bitboard bb       = ~BitBoard::_square_bb[s];
    _color_bb[c]     &= bb;
    _types_bb[pt]    &= bb;
    _types_bb[NONE] &= bb;
    _piece_count[c][NONE]--;
    _piece_count[c][pt]--;

    // Update piece list, remove piece at [s] index and shrink the list.
    Square last_sq = _piece_list[c][pt][_piece_count[c][pt]];
    if (s != last_sq)
    {
        _piece_index[last_sq] = _piece_index[s];
        _piece_list[c][pt][_piece_index[last_sq]] = last_sq;
    }
    _piece_index[s] = -1;
    _piece_list[c][pt][_piece_count[c][pt]]   = SQ_NO;
}
inline void  Position::  move_piece (Square s1, Square s2)
{
    ASSERT (!empty (s1));
    ASSERT ( empty (s2));

    Piece p = _piece_arr[s1];

    Color c = _color (p);
    PType pt = _type (p);

    _piece_arr[s1] = EMPTY;
    _piece_arr[s2] = p;

    Bitboard bb = BitBoard::_square_bb[s1] ^ BitBoard::_square_bb[s2];
    _color_bb[c]     ^= bb;
    _types_bb[pt]    ^= bb;
    _types_bb[NONE] ^= bb;

    // _piece_index[s1] is not updated and becomes stale. This works as long
    // as _piece_index[] is accessed just by known occupied squares.
    _piece_index[s2] = _piece_index[s1];
    _piece_index[s1] = -1;
    _piece_list[c][pt][_piece_index[s2]] = s2;
}

// castle_king_rook() exchanges the king and rook
inline void Position::castle_king_rook (Square org_king, Square dst_king, Square org_rook, Square dst_rook)
{
    // Remove both pieces first since squares could overlap in chess960
    remove_piece (org_king);
    remove_piece (org_rook);

    place_piece (dst_king, _active, KING);
    place_piece (dst_rook, _active, ROOK);
}

#pragma endregion

#pragma endregion

#pragma region Check Inforamtion Def

inline CheckInfo::CheckInfo (const Position &pos)
{
    Color active = pos.active ();
    Color pasive = ~active;

    king_sq = pos.king_sq (pasive);
    pinneds = pos.pinneds (active);
    discoverers = pos.discoverers (active);

    checking_bb[PAWN] = BitBoard::attacks_bb<PAWN> (pasive, king_sq);
    checking_bb[NIHT] = BitBoard::attacks_bb<NIHT> (king_sq);
    checking_bb[BSHP] = BitBoard::attacks_bb<BSHP> (king_sq, pos.pieces ());
    checking_bb[ROOK] = BitBoard::attacks_bb<ROOK> (king_sq, pos.pieces ());
    checking_bb[QUEN] = checking_bb[BSHP] | checking_bb[ROOK];
    checking_bb[KING] = U64 (0);
}

//inline void CheckInfo::clear ()
//{
//    //for (PType pt = PAWN; pt <= KING; ++pt) checking_bb[pt] = U64 (0);
//    fill_n (checking_bb, sizeof (checking_bb) / sizeof (*checking_bb), U64 (0));
//
//    king_sq     = SQ_NO;
//    pinneds     = U64 (0);
//    discoverers = U64 (0);
//}

#pragma endregion

typedef std::stack<StateInfo>   StateInfoStack;

#endif
