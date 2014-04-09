#include "Notation.h"

#include <sstream>
#include <iomanip>

#include "Position.h"
#include "MoveGenerator.h"
#include "Time.h"

namespace Notation {

    using namespace std;
    using namespace BitBoard;
    using namespace MoveGenerator;
    using namespace Time;

    namespace {

        // value to string
        const string pretty_value (Value v)
        {
            ostringstream oss;

            if (abs (v) < VALUE_MATES_IN_MAX_PLY)
            {
                oss << setprecision (2) << fixed << showpos << double (v) / VALUE_EG_PAWN;
            }
            else
            {
                if (v > VALUE_ZERO) //if (v >= VALUE_MATES_IN_MAX_PLY)
                {
                    oss << "+#" << i32 (VALUE_MATE - v + 1) / 2;
                }
                else                //if (v <= VALUE_MATED_IN_MAX_PLY)
                {
                    oss << "-#" << i32 (VALUE_MATE + v + 0) / 2;
                }
            }

            return oss.str ();
        }

        // time to string
        const string pretty_time (u64 msecs)
        {
            const u32 MSecMinute = M_SEC * 60;
            const u32 MSecHour   = MSecMinute * 60;

            u32 hours   =   msecs / MSecHour;
            u32 minutes =  (msecs % MSecHour) / MSecMinute;
            u32 seconds = ((msecs % MSecHour) % MSecMinute) / M_SEC;

            ostringstream oss;

            if (hours) oss << hours << ':';
            oss << setfill ('0')
                << setw (2) << minutes << ':'
                << setw (2) << seconds;

            return oss.str ();
        }

    }

    // Ambiguity if more then one piece of same type can reach 'dst' with a legal move.
    // NOTE: for pawns it is not needed because 'org' file is explicit.
    AmbiguityT ambiguity (Move m, const Position &pos)
    {
        ASSERT (pos.legal (m));

        Square org = org_sq (m);
        Square dst = dst_sq (m);
        Piece p    = pos[org];

        // Disambiguation if we have more then one piece with destination 'dst'
        // note that for pawns is not needed because starting file is explicit.

        Bitboard pinneds = pos.pinneds (pos.active ());

        Bitboard amb, pcs;
        amb = pcs = (attacks_bb (p, dst, pos.pieces ()) & pos.pieces (pos.active (), ptype (p))) - org;
        while (pcs != U64 (0))
        {
            Square amb_org = pop_lsq (pcs);
            Move move = mk_move<NORMAL> (amb_org, dst);
            if (!pos.legal (move, pinneds))
            {
                amb -= amb_org;
            }
        }

        //if (!(amb)) return AMB_NONE;
        //if (!(amb & file_bb (org))) return AMB_RANK;
        //if (!(amb & rank_bb (org))) return AMB_FILE;
        //return AMB_SQR;

        if (amb != U64 (0))
        {
            if (!(amb & file_bb (org))) return AMB_RANK;
            if (!(amb & rank_bb (org))) return AMB_FILE;
            return AMB_SQR;
        }
        return AMB_NONE;
    }

    // move_from_can(can, pos) takes a position and a string representing a move in
    // simple coordinate algebraic notation and returns an equivalent legal move if any.
    Move move_from_can (const string &can, const Position &pos)
    {
        string scan = can;
        if (5 == scan.length ())
        {
            // promotion piece in lowercase
            if (isupper (u08 (scan[4])))
            {
                scan[4] = u08 (tolower (scan[4]));
            }
        }

        for (MoveList<LEGAL> moves (pos); *moves != MOVE_NONE; ++moves)
        {
            Move m = *moves;
            if (scan == move_to_can (m, pos.chess960 ()))
            {
                return m;
            }
        }
        return MOVE_NONE;
    }

    // move_from_san(can, pos) takes a position and a string representing a move in
    // single algebraic notation and returns an equivalent legal move if any.
    Move move_from_san (const string &san, Position &pos)
    {
        for (MoveList<LEGAL> moves (pos); *moves != MOVE_NONE; ++moves)
        {
            Move m = *moves;
            if (san == move_to_san (m, pos))
            {
                return m;
            }
        }
        return MOVE_NONE;
    }
    //Move move_from_lan (const string &lan, const Position &pos)
    //{
    //    return MOVE_NONE;
    //}
    //Move move_from_fan (const string &fan, const Position &pos)
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
        if (!_ok (m))       return "(xxxx)";

        Square org = org_sq (m);
        Square dst = dst_sq (m);
        MoveT mt   = mtype (m);
        if (!c960 && (CASTLE == mt)) dst = (((dst > org) ? F_G : F_C) | _rank (org));
        string can = to_string (org) + to_string (dst);
        if (PROMOTE == mt) can += PieceChar[(BLACK|promote (m))]; // lower case
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
        Piece  p   = pos[org];
        PieceT pt  = ptype (p);

        MoveT mt = mtype (m);

        if (mt == CASTLE)
        {
            san = (dst > org) ? "O-O" : "O-O-O";
        }
        else
        {
            if (PAWN != pt)
            {
                san = PieceChar[pt];
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
            if (pos.capture (m))
            {
                if (PAWN == pt) san = to_char (_file (org));
                san += 'x';
            }
            san += to_string (dst);
            if (PROMOTE == mt && PAWN == pt)
            { 
                san += '=';
                san += PieceChar[promote (m)];
            }
        }

        CheckInfo ci (pos);
        // Move marker for check & checkmate
        if (pos.gives_check (m, ci))
        {
            StateInfo si;
            pos.do_move (m, si, &ci);
            san += ((MoveList<LEGAL> (pos).size () != 0) ? '+' : '#');
            pos.undo_move ();
        }

        return san;
    }

    // TODO::
    //// move_to_lan(m, pos) takes a position and a legal move as input
    //// and returns its long algebraic notation representation.
    //const string move_to_lan (Move m, Position &pos)
    //{
    //    string lan;
    //    return lan;
    //}
    //const string move_to_fan (Move m, Position &pos)
    //{
    //    string fan;
    //    return fan;
    //}

    // score_uci() converts a value to a string suitable
    // for use with the UCI protocol specifications:
    //
    // cp   <x>   The score x from the engine's point of view in centipawns.
    // mate <y>   Mate in y moves, not plies.
    //            If the engine is getting mated use negative values for y.
    const string score_uci (Value v, Value alpha, Value beta)
    {
        ostringstream oss;

        if (abs (v) < VALUE_MATES_IN_MAX_PLY)
        {
            oss << "cp " << 100 * i32 (v) / i32 (VALUE_EG_PAWN);
        }
        else
        {
            oss << "mate " << i32 (v > VALUE_ZERO ? (VALUE_MATE - v + 1) : -(VALUE_MATE + v)) / 2;
        }

        oss << (beta <= v ? " lowerbound" : v <= alpha ? " upperbound" : "");

        return oss.str ();
    }

    // pretty_pv() returns formated human-readable search information, typically to be
    // appended to the search log file. It uses the two helpers below to pretty
    // format the time and score respectively.
    const string pretty_pv (Position &pos, u08 depth, Value value, u64 msecs, const Move pv[])
    {
        const u64 K = 1000;
        const u64 M = 1000000;

        ostringstream oss;

        oss << setw (3) << u32 (depth)
            << setw (8) << pretty_value (value)
            << setw (8) << pretty_time (msecs);

        if      (pos.game_nodes () < M)
        {
            oss << setw (8) << pos.game_nodes () / 1 << "  ";
        }
        else if (pos.game_nodes () < K * M)
        {
            oss << setw (7) << pos.game_nodes () / K << "K  ";
        }
        else
        {
            oss << setw (7) << pos.game_nodes () / M << "M  ";
        }

        string spv = oss.str ();
        string padding = string (spv.length (), ' ');

        StateInfoStack states;
        const Move *m = pv;
        while (*m != MOVE_NONE)
        {
            string san = move_to_san (*m, pos) + ' ';
            if ((spv.length () + san.length ()) % 80 <= san.length ()) // Exceed 80 cols
            {
                spv += "\n" + padding;
            }
            spv += san;
            states.push (StateInfo ());
            pos.do_move (*m, states.top ());
            ++m;
        }

        while (m != pv)
        {
            pos.undo_move ();
            --m;
        }

        return spv;
    }

}
