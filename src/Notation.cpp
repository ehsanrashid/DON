#include "Notation.h"

#include <sstream>
#include <iomanip>

#include "Position.h"
#include "MoveGenerator.h"

namespace Notation {

    using namespace std;
    using namespace BitBoard;
    using namespace MoveGen;

    namespace {

        // Type of the Ambiguity
        enum AmbiguityT { AMB_NONE, AMB_RANK, AMB_FILE, AMB_SQR };

        // Ambiguity if more then one piece of same type can reach 'dst' with a legal move.
        // NOTE: for pawns it is not needed because 'org' file is explicit.
        AmbiguityT ambiguity (Move m, const Position &pos)
        {
            assert (pos.legal (m));

            auto org = org_sq (m);
            auto dst = dst_sq (m);

            // Disambiguation if have more then one piece with destination 'dst'
            // note that for pawns is not needed because starting file is explicit.

            Bitboard pinneds = pos.pinneds (pos.active ());

            Bitboard amb, pcs;
            amb = pcs = (attacks_bb (pos[org], dst, pos.pieces ()) & pos.pieces (pos.active (), ptype (pos[org]))) - org;
            while (pcs != U64(0))
            {
                auto sq = pop_lsq (pcs);
                if (!pos.legal (mk_move<NORMAL> (sq, dst), pinneds))
                {
                    amb -= sq;
                }
            }

            if (amb != U64(0))
            {
                if ((amb & file_bb (org)) == U64(0)) return AMB_RANK;
                if ((amb & rank_bb (org)) == U64(0)) return AMB_FILE;
                return AMB_SQR;
            }
            return AMB_NONE;
        }

        // value to string
        string pretty_value (Value v, const Position &pos)
        {
            ostringstream oss;

            if (abs (v) < +VALUE_MATE_IN_MAX_DEPTH)
            {
                oss << setprecision (2) << fixed << showpos << value_to_cp (pos.active () == WHITE ? +v : -v);
            }
            else
            {
                oss << "#" << showpos << i32(v > VALUE_ZERO ? +(VALUE_MATE - v + 1) : -(VALUE_MATE + v + 0)) / 2;
            }

            return oss.str ();
        }

        // time to string
        string pretty_time (TimePoint time)
        {
            u32 hours  = u32(time / HOUR_MILLI_SEC);
            time      %= HOUR_MILLI_SEC;
            u32 minutes= u32(time / MINUTE_MILLI_SEC);
            time      %= MINUTE_MILLI_SEC;
            u32 seconds= u32(time / MILLI_SEC);
            time      %= MILLI_SEC;
            time      /= 10;

            ostringstream oss;

            oss << setfill ('0')
                << setw (2) << hours   << ":"
                << setw (2) << minutes << ":"
                << setw (2) << seconds << "."
                << setw (2) << time
                << setfill (' ');

            return oss.str ();
        }

    }

    // move_from_can(can, pos) converts a string representing a move in coordinate algebraic notation
    // to the corresponding legal move, if any.
    Move move_from_can (const string &can, const Position &pos)
    {
        auto ccan = can;
        if (5 == ccan.length ())
        {
            // Promotion piece in lowercase
            if (isupper (u08(ccan[4]))) ccan[4] = u08(tolower (ccan[4]));
        }

        for (const auto &m : MoveList<LEGAL> (pos))
        {
            if (ccan == move_to_can (m, pos.chess960 ()))
            {
                return m;
            }
        }
        return MOVE_NONE;
    }

    // move_from_san(san, pos) converts a string representing a move in short algebraic notation
    // to the corresponding legal move, if any.
    Move move_from_san (const string &san,       Position &pos)
    {
        for (const auto &m : MoveList<LEGAL> (pos))
        {
            if (san == move_to_san (m, pos))
            {
                return m;
            }
        }
        return MOVE_NONE;
    }

    //// move_from_lan(lan, pos) converts a string representing a move in long algebraic notation
    //// to the corresponding legal move, if any.
    //Move move_from_lan (const string &lan, const Position &pos)
    //{
    //    return MOVE_NONE;
    //}

    // move_to_can(m, c960) converts a move to a string in coordinate algebraic notation representation.
    // The only special case is castling moves,
    //  - e1g1 notation in normal chess mode,
    //  - e1h1 notation in chess960 mode.
    // Internally castle moves are always coded as "king captures rook".
    string move_to_can (Move m, bool c960)
    {
        if (MOVE_NONE == m) return "(none)";
        if (MOVE_NULL == m) return "(null)";
        auto org = org_sq (m);
        auto dst = dst_sq (m);
        if (!c960 && CASTLE == mtype (m)) dst = (dst > org ? F_G : F_C) | _rank (org);
        auto can = to_string (org) + to_string (dst);
        if (PROMOTE == mtype (m)) can += PIECE_CHAR[BLACK|promote (m)]; // Lowercase
        return can;
    }

    // move_to_san(m, pos) converts a move to a string in short algebraic notation representation.
    string move_to_san (Move m, Position &pos)
    {
        if (MOVE_NONE == m) return "(none)";
        if (MOVE_NULL == m) return "(null)";
        assert (pos.legal (m));
        assert (MoveList<LEGAL> (pos).contains (m));

        string san;

        auto org = org_sq (m);
        auto dst = dst_sq (m);

        if (CASTLE == mtype (m))
        {
            san = (dst > org ? "O-O" : "O-O-O");
        }
        else
        {
            auto pt = ptype (pos[org]);

            if (PAWN != pt)
            {
                san = PIECE_CHAR[pt];
                // Disambiguation if have more then one piece of type 'pt'
                // that can reach 'dst' with a legal move.
                switch (ambiguity (m, pos))
                {
                case AMB_NONE:                               break;
                case AMB_RANK: san += to_char (_file (org)); break;
                case AMB_FILE: san += to_char (_rank (org)); break;
                case AMB_SQR:  san += to_string (org);       break;
                default:       assert (false);               break;
                }
            }

            if (pos.capture (m))
            {
                if (PAWN == pt)
                {
                    san += to_char (_file (org));
                }
                san += "x";
            }

            san += to_string (dst);

            if (PROMOTE == mtype (m) && PAWN == pt)
            {
                san += "=";
                san += PIECE_CHAR[promote (m)];
            }
        }

        // Move marker for check & checkmate
        if (pos.gives_check (m, CheckInfo (pos)))
        {
            StateInfo si;
            pos.do_move (m, si, true);
            san += (MoveList<LEGAL> (pos).size () != 0 ? "+" : "#");
            pos.undo_move ();
        }

        return san;
    }

    //// move_to_lan(m, pos) converts a move to a string in long algebraic notation representation.
    //string move_to_lan (Move m, Position &pos)
    //{
    //    string lan;
    //    return lan;
    //}

    // to_string() converts a value to a string suitable
    // for use with the UCI protocol specifications:
    //
    // cp   <x>   The score x from the engine's point of view in centipawns.
    // mate <y>   Mate in y moves, not plies.
    //            If the engine is getting mated use negative values for y.
    string to_string (Value v)
    {
        ostringstream oss;

        if (abs (v) < +VALUE_MATE_IN_MAX_DEPTH)
        {
            oss << "cp " << i32(100 * value_to_cp (v));
        }
        else
        {
            oss << "mate " << i32(v > VALUE_ZERO ? +(VALUE_MATE - v + 1) : -(VALUE_MATE + v + 0)) / 2;
        }

        return oss.str ();
    }

    // pretty_pv_info() returns formated human-readable search information, typically to be
    // appended to the search log file.
    // It uses the two helpers to pretty format the value and time respectively.
    string pretty_pv_info (Position &pos, i32 depth, Value value, TimePoint time, const MoveVector &pv)
    {
        const u64 K = 1000;
        const u64 M = K*K;

        ostringstream oss;

        oss << setw ( 4) << depth
            << setw ( 8) << pretty_value (value, pos)
            << setw (12) << pretty_time (time);

        u64 game_nodes = pos.game_nodes ();
        if (game_nodes < 1*M) oss << setw (8) << game_nodes / 1 << "  ";
        else
        if (game_nodes < K*M) oss << setw (7) << game_nodes / K << "K  ";
        else
                              oss << setw (7) << game_nodes / M << "M  ";

        StateStack states;
        u08 ply = 0;
        for (auto m : pv)
        {
            oss << move_to_san (m, pos) << " ";
            states.push (StateInfo ());
            pos.do_move (m, states.top (), pos.gives_check (m, CheckInfo (pos)));
            ++ply;
            ////---------------------------------
            //oss << move_to_can (m, pos.chess960 ()) << " ";
        }

        while (ply != 0)
        {
            pos.undo_move ();
            states.pop ();
            --ply;
        }

        return oss.str ();
    }

}
