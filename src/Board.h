//#pragma once
#ifndef BOARD_H_
#define BOARD_H_

#include "Square.h"
#include "Piece.h"

// Board consits of data about piece placement
//  - 64-entry array of pieces, indexed by the square.
//  - Bitboards of each piece type.
//  - Bitboards of each color
//  - Bitboard of all occupied squares.
//  - List of squares for the pieces.
//  - Count of the pieces.
typedef class Board sealed
{

private:

    Piece
        _piece_arr[SQ_NO];          // [Square]

    Bitboard
        _bb_types[1 + PT_NO],       // [PType] + 1 -> (ALL)
        _bb_color[CLR_NO];          // [Color]

    SquareList
        _piece_list[CLR_NO][PT_NO]; // [Color][PType]<vector>

public:

#pragma region Properties

    bool is_empty (Square s) const;

    const Piece operator[] (Square s) const;
    const Bitboard operator[] (Color c) const;
    const Bitboard operator[] (PType t) const;
    const SquareList operator[] (Piece p) const;

    Square king_sq (Color c) const;

    Bitboard pieces (Color c) const;
    Bitboard pieces (PType t) const;
    Bitboard pieces (Color c, PType t) const;
    Bitboard pieces (Piece p) const;
    Bitboard pieces (PType t1, PType t2) const;
    Bitboard pieces (Color c, PType t1, PType t2) const;
    Bitboard pieces () const;
    Bitboard empties () const;

    size_t piece_count (Color c, PType t) const;
    size_t piece_count (Piece p) const;
    size_t piece_count (Color c) const;
    size_t piece_count (PType t) const;
    size_t piece_count () const;

    bool ok (int8_t *step_failed = NULL) const;

#pragma endregion

    void clear ();
    //void reset ();

    void   place_piece (Square s, Color c, PType t);
    void   place_piece (Square s, Piece p);
    Piece remove_piece (Square s);
    Piece   move_piece (Square s1, Square s2);

    operator ::std::string () const;

} Board;

#pragma region Properties

inline bool Board::is_empty (Square s) const
{
    return (PS_NO == _piece_arr[s]);
}

inline const Piece Board::operator[] (Square s) const
{
    return _piece_arr[s];
}
inline const Bitboard Board::operator[] (Color c) const
{
    return _bb_color[c];
}
inline const Bitboard Board::operator[] (PType t) const
{
    return _bb_types[t];
}
inline const SquareList Board::operator[] (Piece p) const
{
    return _piece_list[_color (p)][_ptype (p)];
}

inline Square Board::king_sq (Color c) const
{
    return _piece_list[c][KING][0];
}

inline Bitboard Board::pieces (Color c) const
{
    return _bb_color[c];
}
inline Bitboard Board::pieces (PType t) const
{
    return _bb_types[t];
}
inline Bitboard Board::pieces (Color c, PType t) const
{
    return pieces (c) & pieces (t);
}
inline Bitboard Board::pieces (Piece p) const
{
    return pieces (_color (p), _ptype (p));
}
inline Bitboard Board::pieces (PType t1, PType t2) const
{
    return pieces (t1) | pieces (t2);
}
inline Bitboard Board::pieces (Color c, PType t1, PType t2) const
{
    return pieces (c) & pieces (t1, t2);
}
inline Bitboard Board::pieces () const
{
    return _bb_types[PT_NO];
}
inline Bitboard Board::empties () const
{
    return ~_bb_types[PT_NO];
}

inline size_t Board::piece_count (Color c, PType t) const
{
    return _piece_list[c][t].size ();
}
inline size_t Board::piece_count (Piece p) const
{
    return _piece_list[_color (p)][_ptype (p)].size ();
}
inline size_t Board::piece_count (Color c) const
{
    return
        piece_count (c, PAWN) + piece_count (c, NIHT) + piece_count (c, BSHP) +
        piece_count (c, ROOK) + piece_count (c, QUEN) + piece_count (c, KING);
}
inline size_t Board::piece_count (PType t) const
{
    return piece_count (WHITE, t) + piece_count (BLACK, t);
}
inline size_t Board::piece_count () const
{
    return piece_count (WHITE) + piece_count (BLACK);
}

#pragma endregion


template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const Board &board)
{
    os << ::std::string (board);
    return os;
}

#endif
