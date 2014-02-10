#include "Notation.h"

#include <sstream>
#include <iomanip>
#include "xstring.h"

#include "Position.h"
#include "MoveGenerator.h"
#include "Time.h"

using namespace std;
using namespace BitBoard;
using namespace MoveGenerator;
using namespace Time;

// Ambiguity if more then one piece of same type can reach 'dst' with a legal move.
// NOTE: for pawns it is not needed because 'org' file is explicit.
AmbT ambiguity (Move m, const Position &pos)
{
    ASSERT (pos.legal (m));

    Square org = org_sq (m);
    Square dst = dst_sq (m);
    Piece p   = pos[org];
    PieceT pt  = _ptype (p);


    //uint8_t n = 0;
    //uint8_t f = 0;
    //uint8_t r = 0;
    //for (MoveList<LEGAL> itr (pos); *itr; ++itr)
    //{
    //    Move mm = *itr;
    //    if (org_sq (mm) != org)
    //    {
    //        if (pos[org_sq (mm)] == p && dst_sq (mm) == dst)
    //        {
    //            ++n;
    //            if (_file (org_sq (mm)) == _file (org))
    //            {
    //                ++f;
    //            }
    //            if (_rank (org_sq (mm)) == _rank (org))
    //            {
    //                ++r;
    //            }
    //        }
    //    }
    //}
    //if (!n) return AMB_NONE;
    //if (!f) return AMB_RANK;
    //if (!r) return AMB_FILE;
    //return AMB_SQR;

    // Disambiguation if we have more then one piece with destination 'dst'
    // note that for pawns is not needed because starting file is explicit.

    Bitboard pinneds = pos.pinneds (pos.active ());

    // Disambiguation if we have more then one piece with destination 'dst'
    // note that for pawns is not needed because starting file is explicit.
    //bool
    //    amb_move = false,
    //    amb_file = false,
    //    amb_rank = false;
    //Bitboard b = (pos.attacks_from (p, dst) & pos.pieces (pos.active (), pt)) - org;
    //while (b)
    //{
    //    Square sq = pop_lsq (b);
    //    // Pinned pieces are not included in the possible sub-set
    //    if (!pos.legal (mk_move (sq, dst), pinneds))
    //    {
    //        continue;
    //    }
    //    amb_file |= (_file (sq) == _file (org));
    //    amb_rank |= (_rank (sq) == _rank (org));
    //    amb_move = true;
    //}
    //if (!amb_move) return AMB_NONE;
    //if (!amb_file) return AMB_RANK;
    //if (!amb_rank) return AMB_FILE;
    //return AMB_SQR;

    Bitboard others, b;
    others = b = (pos.attacks_from (p, dst) & pos.pieces (pos.active (), pt)) - org;
    while (b)
    {
        org = pop_lsq (b);
        Move move = mk_move (org, dst);
        if (!pos.legal (move, pinneds))
        {
            others -= org;
        }
    }

    //if (!(others)) return AMB_NONE;
    //if (!(others & file_bb (org))) return AMB_RANK;
    //if (!(others & rank_bb (org))) return AMB_FILE;
    //return AMB_SQR;

    if (others)
    {
        if (!(others & file_bb (org))) return AMB_RANK;
        if (!(others & rank_bb (org))) return AMB_FILE;
        return AMB_SQR;
    }
    return AMB_NONE;
}

// move_from_can(can, pos) takes a position and a string representing a move in
// simple coordinate algebraic notation and returns an equivalent legal move if any.
Move move_from_can (string &can, const Position &pos)
{
    if (5 == can.length ())
    {
        // promotion piece in lowercase
        if (isupper (uint8_t (can[4])))
        {
            can[4] = uint8_t (tolower (can[4]));
        }
    }

    for (MoveList<LEGAL> itr (pos); *itr; ++itr)
    {
        Move m = *itr;
        if (iequals (can, move_to_can (m, pos.chess960 ())))
        {
            return m;
        }
    }
    return MOVE_NONE;
}

//Move move_from_san (string &san, const Position &pos)
//{
//    return MOVE_NONE;
//}
//
//Move move_from_lan (string &lan, const Position &pos)
//{
//    return MOVE_NONE;
//}

// move_to_can(m, c960) converts a move to a string in coordinate algebraic notation (g1f3, a7a8q, etc.).
// The only special case is castling moves,
//  - e1g1 notation in normal chess mode,
//  - e1h1 notation in chess960 mode.
// Internally castle moves are always coded as "king captures rook".
const string move_to_can (Move m, bool c960)
{
    if (MOVE_NONE == m) return "(none)";
    if (MOVE_NULL == m) return "(null)";
    //if (!_ok (m))       return "(xxxx)";

    Square org = org_sq (m);
    Square dst = dst_sq (m);
    MoveT mt   = mtype (m);
    if (!c960 && (CASTLE == mt)) dst = ((dst > org) ? F_G : F_C) | _rank (org);
    string can = to_string (org) + to_string (dst);
    if (PROMOTE == mt) can += CharPiece[(BLACK | prom_type (m))]; // lower case
    return can;
}

// move_to_san(m, pos) takes a position and a legal move as input
// and returns its short algebraic notation representation.
const string move_to_san (Move m, Position &pos)
{
    if (MOVE_NONE == m) return "(none)";
    if (MOVE_NULL == m) return "(null)";
    ASSERT (pos.legal (m));
    string san;
    Square org = org_sq (m);
    Square dst = dst_sq (m);
    Piece p   = pos[org];
    PieceT pt  = _ptype (p);

    //    switch (pt)
    //    {
    //    case PAWN:
    //        san = "";
    //        if (pos.capture (m))
    //        {
    //            san += to_char (_file (org));
    //            san += 'x';
    //        }
    //
    //        san += to_string (dst);
    //
    //        if (PROMOTE == mtype (m))
    //        {
    //            switch (prom_type (m))
    //            {
    //            case QUEN: san += "Q"; break;
    //            case ROOK: san += "R"; break;
    //            case BSHP: san += "B"; break;
    //            case NIHT: san += "N"; break;
    //            default: ASSERT (false); // "Wrong Promotion Piece"
    //            }
    //        }
    //        goto move_marker;
    //
    //    case KING:
    //        if (CASTLE == mtype (m))
    //        {
    //            CSide cs = ((WHITE == pos.active ()) ?
    //                (dst == SQ_C1) ? CS_Q : (dst == SQ_G1) ? CS_K : CS_NO :
    //                (dst == SQ_C8) ? CS_Q : (dst == SQ_G8) ? CS_K : CS_NO);
    //
    //            switch (cs)
    //            {
    //            case CS_Q: san  = "O-";
    //            case CS_K: san += "O-O"; break;
    //            }
    //            goto move_marker;
    //        }
    //        // NOTE: no break
    //    default:
    //        // piece notation
    //        san = to_char (pt);
    //
    //        break;
    //    }
    //
    //    switch (ambiguity (m, pos))
    //    {
    //    case AMB_NONE:                               break;
    //    case AMB_RANK: san += to_char (_file (org)); break;
    //    case AMB_FILE: san += to_char (_rank (org)); break;
    //    case AMB_SQR:  san += to_string (org);       break;
    //    default:       ASSERT (false);               break;
    //    }
    //
    //    if (pos.capture (m))
    //    {
    //        san += 'x';
    //    }
    //
    //    san += to_string (dst);
    //    // promote ????????
    //move_marker:
    //    // Marker for check & checkmate
    //    if (pos.gives_check (m, CheckInfo (pos)))
    //    {
    //        StateInfo sinfo;
    //        Position p = pos;
    //        p.do_move (m, sinfo);
    //        uint8_t legalmove = generate<LEGAL> (p).size ();
    //
    //        san += (legalmove ? '+' : '#');
    //    }

    MoveT mt = mtype (m);
    switch (mt)
    {
    case CASTLE:
        san = (dst > org) ? "O-O" : "O-O-O";
        break;

    default:
        {
            bool capture = pos.capture (m);
            if (PAWN == pt)
            {
                if (capture) san = to_char (_file (org));
            }
            else
            {
                san = CharPiece[pt];
                // Disambiguation if we have more then one piece of type 'pt'
                // that can reach 'dst' with a legal move.
                switch (ambiguity (m, pos))
                {
                case AMB_NONE:                               break;
                case AMB_RANK: san += to_char (_file (org)); break;
                case AMB_FILE: san += to_char (_rank (org)); break;
                case AMB_SQR:  san += to_string (org);       break;
                default:       ASSERT (false);               break;
                }
            }

            if (capture) san += 'x';
            san += to_string (dst);
            if (PROMOTE == mt && PAWN == pt) san += string ("=") + CharPiece[prom_type (m)];
        }
        break;
    }

    // Move marker for check & checkmate
    if (pos.gives_check (m, CheckInfo (pos)))
    {
        StateInfo si;
        pos.do_move (m, si);
        san += MoveList<LEGAL> (pos).size () ? "+" : "#";
        pos.undo_move ();
    }

    return san;
}

//// move_to_lan(m, pos) takes a position and a legal move as input
//// and returns its long algebraic notation representation.
//const string move_to_lan (Move m, Position &pos)
//{
//    string lan;
//
//
//    return lan;
//}

// score_uci() converts a value to a string suitable
// for use with the UCI protocol specifications:
//
// cp <x>     The score x from the engine's point of view in centipawns.
// mate <y>   Mate in y moves, not plies.
//            If the engine is getting mated use negative values for y.
string score_uci (Value v, Value alpha, Value beta)
{
    stringstream ss;

    int32_t abs_v = abs (int32_t (v));
    if (abs_v < VALUE_MATES_IN_MAX_PLY)
    {
        //if (abs_v <= VALUE_CHIK) v = VALUE_DRAW;
        ss << "cp " << int32_t (v) * 100 / VALUE_MG_PAWN;
    }
    else
    {
        ss << "mate " << int32_t (v > VALUE_ZERO ? VALUE_MATE - v + 1 : -(VALUE_MATE + v)) / 2;
    }

    ss << (beta <= v ? " lowerbound" : v <= alpha ? " upperbound" : "");

    return ss.str ();
}

namespace {

    // value to string
    string value_to_string (Value v)
    {
        stringstream ss;

        int32_t abs_v = abs (int32_t (v));
        if (abs_v < VALUE_MATES_IN_MAX_PLY)
        {
            //if (abs_v <= VALUE_CHIK) v = VALUE_DRAW;
            ss << setprecision (2) << fixed << showpos << double (v) / VALUE_MG_PAWN;
        }
        else
        {
            if (v > VALUE_ZERO) //if (v >= VALUE_MATES_IN_MAX_PLY)
            {
                ss <<  "#" << int32_t (VALUE_MATE - v + 1) / 2;
            }
            else                //if (v <= VALUE_MATED_IN_MAX_PLY)
            {
                ss << "-#" << int32_t (VALUE_MATE + v + 0) / 2;
            }
        }
        return ss.str ();
    }

    // time to string
    string time_to_string (uint64_t msecs)
    {
        const uint32_t MSecMinute = M_SEC * 60;
        const uint32_t MSecHour   = MSecMinute * 60;

        uint64_t hours   =   msecs / MSecHour;
        uint64_t minutes =  (msecs % MSecHour) / MSecMinute;
        uint64_t seconds = ((msecs % MSecHour) % MSecMinute) / M_SEC;

        stringstream ss;

        if (hours) ss << hours << ':';
        ss << setfill ('0') 
            << setw (2) << minutes << ':' 
            << setw (2) << seconds;

        return ss.str ();
    }

}

// pretty_pv() formats human-readable search information, typically to be
// appended to the search log file. It uses the two helpers below to pretty
// format time and score respectively.
string pretty_pv (Position &pos, uint8_t depth, Value value, uint64_t msecs, const Move pv[])
{
    const uint64_t K = 1000;
    const uint64_t M = 1000000;

    stringstream spv;

    spv << setw (3) << uint32_t (depth)
        << setw (8) << value_to_string (value)
        << setw (8) << time_to_string (msecs);

    if (pos.game_nodes () < M)
    {
        spv << setw (8) << pos.game_nodes () / 1 << "  ";
    }
    else if (pos.game_nodes () < K * M)
    {
        spv << setw (7) << pos.game_nodes () / K << "K  ";
    }
    else
    {
        spv << setw (7) << pos.game_nodes () / M << "M  ";
    }

    string padding = string (spv.str ().length (), ' ');
    uint16_t length  = padding.length ();
    StateInfoStack states;

    const Move *m = pv;

    while (*m != MOVE_NONE)
    {
        string san = move_to_san (*m, pos);
        if (length + san.length () > 80)
        {
            spv << "\n" + padding;
            length = padding.length ();
        }
        spv << san << ' ';
        length += san.length () + 1;
        states.push (StateInfo ());
        pos.do_move (*m, states.top ());
        ++m;
    }

    while (m != pv)
    {
        pos.undo_move();
        --m;
    }

    return spv.str();
}
