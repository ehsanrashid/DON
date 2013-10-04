#include "MoveGenerator.h"
#include "Position.h"
#include "BitCount.h"
#include "BitScan.h"

namespace MoveGenerator {

    using namespace BitBoard;

#undef SERIALIZE
#undef SERIALIZE_PAWNS

    // Fill moves in the list for any piece using a very common while loop, no fancy.
#define SERIALIZE(lst_move, org, moves)         while (moves) { lst_move.emplace_back (mk_move<NORMAL> (org, pop_lsb(moves))); }
    // Fill moves in the list for pawns, where the 'delta' is the distance b/w 'org' and 'dst' square.
#define SERIALIZE_PAWNS(lst_move, delta, moves) while (moves) { Square dst = pop_lsb (moves); lst_move.emplace_back (mk_move<NORMAL> (dst - (delta), dst)); }

    namespace {

#pragma region old
        //// Fill moves in the list for any piece using a very common while loop, no fancy.
        //static inline void SERIALIZE (MoveList &lst_move, Square org, Bitboard moves)
        //{
        //    while (moves)
        //    {
        //        lst_move.emplace_back (mk_move<NORMAL> (org, pop_lsb (moves)));
        //    }
        //}
        //// Fill moves in the list for pawns, where the 'delta' is the distance b/w 'org' and 'dst' square.
        //static inline void SERIALIZE_PAWNS (MoveList &lst_move, Delta delta, Bitboard moves)
        //{
        //    while (moves)
        //    {
        //        Square dst = pop_lsb (moves);
        //        lst_move.emplace_back (mk_move<NORMAL> (dst - (delta), dst));
        //    }
        //}

#pragma endregion

#pragma region Move Generators

        template<GType G, PType P>
        // Move Generator for PIECE
        struct Generator
        {
        public:
            // Generates piece common move
            static inline void generate (MoveList &lst_move, const Position &pos, Color c, Bitboard target, const CheckInfo *ci = NULL)
                //template<GType G, PType P>
                //inline void Generator<G, P>::generate (MoveList &lst_move, const Position &pos, Color c, Bitboard target, const CheckInfo *ci)
            {
                ASSERT ((KING != P) && (PAWN != P));

                Bitboard occ = pos.pieces ();
                Piece piece = (c | P);
                const SquareList orgs = pos[piece];

                SquareList::const_iterator itr = orgs.cbegin ();
                for (; itr != orgs.cend (); ++itr)
                {
                    Square org = *itr;

                    if ((CHECK == G) || (QUIET_CHECK == G))
                    {
                        if (ci)
                        {
                            if ((BSHP == P) || (ROOK == P) || (QUEN == P) &&
                                !(attacks_bb<P> (org) & target & ci->checking_bb[P])) continue;

                            if (UNLIKELY (ci->check_discovers) &&
                                (ci->check_discovers & org)) continue;
                        }
                    }

                    Bitboard moves = attacks_bb<P> (org, occ) & target;
                    if ((CHECK == G) || (QUIET_CHECK == G))
                    {
                        if (ci) moves &= ci->checking_bb[P];
                    }

                    SERIALIZE (lst_move, org, moves);
                }
            }

        };

        template<GType G>
        // Move Generator for KING
        struct Generator<G, KING>
        {
        public:
            // Generates KING common move
            static inline void generate (MoveList &lst_move, const Position &pos, Color clr, Bitboard target, const CheckInfo *ci = NULL)
                //template<GType G>
                //void Generator<G, KING>::generate (MoveList &lst_move, const Position &pos, Color clr, Bitboard target, const CheckInfo *ci)
            {
                if ((EVASION != G) && (CHECK != G) && (QUIET_CHECK != G))
                {
                    Square sq_king = pos.king_sq (clr);
                    Bitboard moves = attacks_bb<KING> (sq_king) & target;
                    SERIALIZE (lst_move, sq_king, moves);
                }

                if ((EVASION != G) && (CAPTURE != G))
                {
                    if (!pos.castle_impeded (clr) && pos.can_castle (clr) && !pos.checkers ())
                    {
                        if (!pos.castle_impeded (clr, CS_K) && pos.can_castle (clr, CS_K))
                        {
                            generate_castling<CS_K> (lst_move, pos, clr, ci);
                        }
                        if (!pos.castle_impeded (clr, CS_Q) && pos.can_castle (clr, CS_Q))
                        {
                            generate_castling<CS_Q> (lst_move, pos, clr, ci);
                        }
                    }
                }
            }

            template<CSide SIDE>
            // Generates KING castling move
            static inline void generate_castling (MoveList &lst_move, const Position &pos, Color clr, const CheckInfo *ci /*= NULL*/)
                //template<GType G>
                //template<CSide SIDE>
                //void Generator<G, KING>::generate_castling (MoveList &lst_move, const Position &pos, Color clr, const CheckInfo *ci)
            {
                ASSERT (!pos.castle_impeded (clr, SIDE));
                ASSERT (pos.can_castle (clr, SIDE));
                ASSERT (!pos.checkers ());

                if (pos.castle_impeded (clr, SIDE) || !pos.can_castle (clr, SIDE) || pos.checkers ()) return;

                Square org_king = pos.king_sq (clr);
                Square org_rook = pos.castle_rook (clr, SIDE);
                Square dst_king = rel_sq (clr, (CS_Q == SIDE) ? SQ_WK_Q : SQ_WK_K);

                Bitboard enemies = pos.pieces (~clr);

                Delta step = (CS_Q == SIDE) ? DEL_W : DEL_E;
                for (Square s = org_king + step; s != dst_king; s += step)
                {
                    if (pos.attackers_to (s) & enemies)
                    {
                        return;
                    }
                }

                // Because we generate only legal castling moves we need to verify that
                // when moving the castling rook we do not discover some hidden checker.
                // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
                Move m = mk_move<CASTLE> (org_king, org_rook);

                switch (G)
                {
                case CHECK:
                case QUIET_CHECK:
                    if (ci)
                    {
                        if (pos.is_move_check (m, *ci))
                        {
                            lst_move.emplace_back (m);
                        }
                    }
                    break;

                default: lst_move.emplace_back (m);    break;
                }
            }

        };

        template<GType G>
        // Move Generator for PAWN
        struct Generator<G, PAWN>
        {
        public:
            template<Color C>
            // Generates PAWN common move
            static inline void generate (MoveList &lst_move, const Position &pos, Bitboard target, const CheckInfo *ci = NULL)
                //template<GType G>
                //template<Color C>
                //void Generator<G, PAWN>::generate<> (MoveList &lst_move, const Position &pos, Bitboard target, const CheckInfo *ci)
            {
                Color _C = ~C;
                const Delta FRONT = (WHITE == C ? DEL_N  : DEL_S);
                const Delta RIHT = (WHITE == C ? DEL_NE : DEL_SW);
                const Delta LEFT = (WHITE == C ? DEL_NW : DEL_SE);

                Bitboard bbRR8 = mask_rel_rank (C, R_8);
                Bitboard bbRR7 = mask_rel_rank (C, R_7);
                Bitboard bbRR3 = mask_rel_rank (C, R_3);

                Bitboard pawns = pos.pieces (C, PAWN);
                Bitboard pawns_on_R7 = pawns &  bbRR7;
                Bitboard pawns_on_Rx = pawns & ~bbRR7;
                Bitboard occ = pos.pieces ();

                Bitboard enemies;
                switch (G)
                {
                case EVASION: enemies = pos.pieces (_C) & target; break;
                case CAPTURE: enemies = target;                     break;
                default:      enemies = pos.pieces (_C);          break;
                }

                Bitboard empty = bb_NULL;
                // Pawn single-push and double-push, no promotions
                if ((CAPTURE != G))
                {
                    empty = ((QUIET == G) || (QUIET_CHECK == G) ? target : ~occ);

                    Bitboard push_1 = shift_del<FRONT> (pawns_on_Rx) & empty;
                    Bitboard push_2 = shift_del<FRONT> (push_1 & bbRR3) & empty;

                    switch (G)
                    {
                    case EVASION:
                        // only blocking squares are important
                        push_1 &= target;
                        push_2 &= target;
                        break;

                    case CHECK:
                    case QUIET_CHECK:
                        if (ci)
                        {
                            Bitboard attack = attacks_bb<PAWN> (_C, ci->king_sq);

                            push_1 &= attack;
                            push_2 &= attack;

                            // pawns which give discovered check
                            Bitboard pawns_chk_dis = pawns_on_Rx & ci->check_discovers;
                            // Add pawn pushes which give discovered check. This is possible only
                            // if the pawn is not on the same file as the enemy king, because we
                            // don't generate captures. Note that a possible discovery check
                            // promotion has been already generated among captures.
                            if (pawns_chk_dis)
                            {
                                Bitboard push_cd_1 = shift_del<FRONT>(pawns_chk_dis) & empty;
                                Bitboard push_cd_2 = shift_del<FRONT>(push_cd_1 & bbRR3) & empty;

                                push_1 |= push_cd_1;
                                push_2 |= push_cd_2;
                            }
                        }
                        break;
                    }

                    SERIALIZE_PAWNS (lst_move, Delta (FRONT << 0), push_1);
                    SERIALIZE_PAWNS (lst_move, Delta (FRONT << 1), push_2);
                }
                // Pawn normal and en-passant captures, no promotions
                if ((QUIET != G) && (QUIET_CHECK != G))
                {
                    Bitboard attacksL = shift_del<LEFT> (pawns_on_Rx) & enemies;
                    Bitboard attacksR = shift_del<RIHT> (pawns_on_Rx) & enemies;;

                    SERIALIZE_PAWNS (lst_move, LEFT, attacksL);
                    SERIALIZE_PAWNS (lst_move, RIHT, attacksR);

                    Square ep_sq = pos.en_passant ();
                    if (SQ_NO != ep_sq)
                    {
                        ASSERT (_rank (ep_sq) == rel_rank (C, R_6));

                        Bitboard bbRR5 = mask_rel_rank (C, R_5);
                        Bitboard pawns_on_R5 = pawns_on_Rx & bbRR5;
                        if (pawns_on_R5)
                        {
                            // An en-passant capture can be an evasion only if the checking piece
                            // is the double pushed pawn and so is in the target. Otherwise this
                            // is a discovery check and we are forced to do otherwise.
                            // All time except when EVASION then 2nd condition must true
                            if ((EVASION != G) || (target & (ep_sq - FRONT)))
                            {
                                Bitboard pawns_ep = attacks_bb<PAWN>(_C, ep_sq) & pawns_on_R5;
                                ASSERT (pawns_ep);
                                ASSERT (pop_count<FULL> (pawns_ep) <= 2);
                                while (pawns_ep)
                                {
                                    lst_move.emplace_back (mk_move<ENPASSANT> (pop_lsb (pawns_ep), ep_sq));
                                }
                            }
                        }
                    }
                }
                // Promotions (queening and under-promotions)
                if (pawns_on_R7)
                {
                    // All time except when EVASION then 2nd condition must true
                    if ((EVASION != G) || (target & bbRR8))
                    {
                        switch (G)
                        {
                        case EVASION: empty &= target;        break;
                        case CAPTURE: empty = ~pos.pieces (); break;
                        }

                        if ((CAPTURE != G))
                        {
                            generate_promotion<FRONT> (lst_move, pawns_on_R7, empty, ci);
                        }

                        if ((QUIET != G) && (QUIET_CHECK != G))
                        {
                            generate_promotion<LEFT> (lst_move, pawns_on_R7, enemies, ci);
                            generate_promotion<RIHT> (lst_move, pawns_on_R7, enemies, ci);
                        }
                    }
                }
            }

            template<Delta D>
            // Generates PAWN promotion move
            static inline void generate_promotion (MoveList &lst_move, Bitboard pawns_on_R7, Bitboard target, const CheckInfo *ci /*= NULL*/)
                //template<GType G>
                //template<Delta D>
                //void Generator<G, PAWN>::generate_promotion (MoveList &lst_move, Bitboard pawns_on_R7, Bitboard target, const CheckInfo *ci)
            {
                //if (pawns_on_R7)
                //{
                Bitboard promotes = shift_del<D> (pawns_on_R7) & target;
                while (promotes)
                {
                    Square dst = pop_lsb (promotes);
                    Square org = dst - D;

                    if ((RELAX == G) || (EVASION == G) ||
                        ((CAPTURE == G) && (DEL_NE == D || DEL_NW == D || DEL_SE == D || DEL_SW == D)))
                    {
                        lst_move.emplace_back (mk_move<PROMOTE> (org, dst, QUEN));
                    }

                    if ((RELAX == G) || (EVASION == G) ||
                        ((QUIET == G) && (DEL_N == D || DEL_S == D)))
                    {
                        lst_move.emplace_back (mk_move<PROMOTE> (org, dst, ROOK));
                        lst_move.emplace_back (mk_move<PROMOTE> (org, dst, BSHP));
                        lst_move.emplace_back (mk_move<PROMOTE> (org, dst, NIHT));
                    }

                    // Knight-promotion is the only one that can give a direct check
                    // not already included in the queen-promotion (queening).
                    if ((CHECK == G) || (QUIET_CHECK == G))
                    {
                        if (ci)
                        {
                            if (attacks_bb<NIHT> (dst) & ci->king_sq)
                            {
                                lst_move.emplace_back (mk_move<PROMOTE> (org, dst, NIHT));
                            }
                        }
                    }
                    else
                    {
                        (void) ci; // silence a warning under MSVC
                    }
                }
                //}
            }

        };

#pragma endregion

        template<Color C, GType G>
        // Generates all pseudo-legal moves of color for target.
        inline void generate_color (MoveList &lst_move, const Position &pos, Bitboard target, const CheckInfo *ci = NULL)
        {
            Generator<G, PAWN>::generate<C> (lst_move, pos, target, ci);
            Generator<G, NIHT>::generate (lst_move, pos, C, target, ci);
            Generator<G, BSHP>::generate (lst_move, pos, C, target, ci);
            Generator<G, ROOK>::generate (lst_move, pos, C, target, ci);
            Generator<G, QUEN>::generate (lst_move, pos, C, target, ci);
            Generator<G, KING>::generate (lst_move, pos, C, target, ci);
        }

        inline void filter_illegal (MoveList &lst_move, const Position &pos)
        {
            Square sq_king = pos.king_sq (pos.active ());
            Bitboard pinneds = pos.pinneds ();

            MoveList::iterator itr = lst_move.begin ();
            while (itr != lst_move.end ())
            {
                Move m = *itr;
                if (((sq_org (m) == sq_king) || pinneds || (ENPASSANT == _mtype (m))) &&
                    !pos.is_move_legal (m, pinneds))
                {
                    itr = lst_move.erase (itr);
                }
                else
                {
                    ++itr;
                }
            }
        }

    }

#pragma region Generates

    template<GType G>
    // Generates all pseudo-legal moves.
    MoveList generate (const Position &pos)
    {
        MoveList lst_move;

        ASSERT (RELAX == G || CAPTURE == G || QUIET == G);
        ASSERT (!pos.checkers ());
        //if (!pos.checkers ())
        //{
        Color active = pos.active ();
        Color pasive = ~active;

        Bitboard target = bb_NULL;
        //CheckInfo *ci = NULL;

        switch (G)
        {
        case CAPTURE:
            target = pos.pieces (pasive);
            break;

        case QUIET:
            target = pos.empties ();
            break;

        case RELAX:
            target = ~pos.pieces (active);
            break;
        }

        switch (active)
        {
        case WHITE: generate_color<WHITE, G> (lst_move, pos, target); break;
        case BLACK: generate_color<BLACK, G> (lst_move, pos, target); break;
        }
        //}

        return lst_move;
    }

    // --------------------------------
    // explicit template instantiations
    template MoveList generate<RELAX>   (const Position &pos);
    template MoveList generate<CAPTURE> (const Position &pos);
    template MoveList generate<QUIET>   (const Position &pos);
    // --------------------------------

    template<>
    // Generates all pseudo-legal non-captures and knight underpromotions moves that give check.
    MoveList generate<QUIET_CHECK> (const Position &pos)
    {
        ASSERT (!pos.checkers ());
        MoveList lst_move;

        //if (!pos.checkers ())
        //{
        Color active = pos.active ();
        //Color pasive = ~active;
        Bitboard occ = pos.pieces ();
        Bitboard empty = ~occ;

        CheckInfo ci (pos);
        Bitboard discovers = ci.check_discovers & ~pos.pieces (active, PAWN);
        while (discovers)
        {
            Square org = pop_lsb (discovers);
            PType type = _ptype (pos[org]);

            Bitboard moves = bb_NULL;
            switch (type)
            {
            case PAWN: continue; // Will be generated together with direct checks
            case NIHT: moves = attacks_bb<NIHT> (org) & empty;      break;
            case BSHP: moves = attacks_bb<BSHP> (org, occ) & empty; break;
            case ROOK: moves = attacks_bb<ROOK> (org, occ) & empty; break;
            case QUEN: moves = attacks_bb<QUEN> (org, occ) & empty; break;
            case KING: moves = attacks_bb<KING> (org) & empty &
                           ~attacks_bb<QUEN> (ci.king_sq);          break;
            }

            SERIALIZE (lst_move, org, moves);
        }

        switch (active)
        {
        case WHITE: generate_color<WHITE, QUIET_CHECK> (lst_move, pos, empty, &ci); break;
        case BLACK: generate_color<BLACK, QUIET_CHECK> (lst_move, pos, empty, &ci); break;
        }
        //}
        return lst_move;
    }

    template<>
    // Generates all pseudo-legal check giving moves.
    MoveList generate<CHECK>       (const Position &pos)
    {
        MoveList lst_move;

        Color active = pos.active ();
        Color pasive = ~active;
        Bitboard occ = pos.pieces ();
        Bitboard target = ~pos.pieces (active);

        CheckInfo ci (pos);
        Bitboard discovers = ci.check_discovers & ~pos.pieces (active, PAWN);
        while (discovers)
        {
            Square org = pop_lsb (discovers);
            PType type = _ptype (pos[org]);

            Bitboard moves = bb_NULL;
            switch (type)
            {
            case PAWN: continue; // Will be generated together with direct checks
            case NIHT: moves = attacks_bb<NIHT> (org)      & target; break;
            case BSHP: moves = attacks_bb<BSHP> (org, occ) & target; break;
            case ROOK: moves = attacks_bb<ROOK> (org, occ) & target; break;
            case QUEN: moves = attacks_bb<QUEN> (org, occ) & target; break;
            case KING: moves = attacks_bb<KING> (org)      & target & ~attacks_bb<QUEN> (ci.king_sq); break;
            }

            SERIALIZE (lst_move, org, moves);
        }

        switch (active)
        {
        case WHITE: generate_color<WHITE, CHECK> (lst_move, pos, target, &ci); break;
        case BLACK: generate_color<BLACK, CHECK> (lst_move, pos, target, &ci); break;
        }
        return lst_move;
    }

    template<>
    // Generates all pseudo-legal check evasions moves when the side to move is in check.
    MoveList generate<EVASION>     (const Position &pos)
    {
        MoveList lst_move;
        Color active = pos.active ();
        Color pasive = ~active;
        Bitboard checkers = pos.checkers ();
        uint8_t num_checkers = pop_count<FULL> (checkers);
        ASSERT (num_checkers != 0); // If any checker exists
        //if (num_checkers != 0)
        //{
        Square sq_king = pos.king_sq (active);
        Bitboard mocc = pos.pieces () - sq_king;
        Bitboard friends = pos.pieces (active);
        Bitboard enemies = pos.pieces (pasive);

        // Generates evasions for king, capture and non-capture moves excluding friends
        Bitboard moves = attacks_bb<KING> (sq_king) & ~friends;

        // Remove squares attacked by enemies, from the king evasions.
        // so to skip known illegal moves avoiding useless legality check later.
        for (size_t k = 0; _deltas_type[KING][k]; ++k)
        {
            Square sq = sq_king + _deltas_type[KING][k];
            if (_ok (sq))
            {
                if ((moves & sq) && (pos.attackers_to (sq, mocc) & enemies))
                {
                    moves -= sq;
                }
            }
        }

        SERIALIZE (lst_move, sq_king, moves);
        if (1 == num_checkers && pop_count<FULL> (friends) > 1)
        {
            // Generates blocking evasions or captures of the checking piece
            Bitboard target = checkers | mask_btw_sq (scan_lsb (checkers), sq_king);
            switch (active)
            {
            case WHITE: generate_color<WHITE, EVASION> (lst_move, pos, target); break;
            case BLACK: generate_color<BLACK, EVASION> (lst_move, pos, target); break;
            }
        }
        //}
        return lst_move;
    }

    template<>
    // Generates all legal moves.
    MoveList generate<LEGAL>       (const Position &pos)
    {
        MoveList lst_move = 
            pos.checkers () ?
            generate<EVASION> (pos) :
            generate<RELAX> (pos);

        filter_illegal (lst_move, pos);
        return lst_move;
    }

#pragma endregion

#undef SERIALIZE
#undef SERIALIZE_PAWNS

}
