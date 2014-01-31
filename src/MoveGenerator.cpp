#include "MoveGenerator.h"

#include "Position.h"
#include "BitCount.h"

namespace MoveGenerator {

    using namespace std;
    using namespace BitBoard;

#undef SERIALIZE
#undef SERIALIZE_PAWNS

    // Fill moves in the list for any piece using a very common while loop, no fancy.
#define SERIALIZE(m_list, org, moves)         while (moves) { (m_list++)->move = mk_move<NORMAL> ((org), pop_lsq (moves)); }
    // Fill moves in the list for pawns, where the 'delta' is the distance b/w 'org' and 'dst' square.
#define SERIALIZE_PAWNS(m_list, delta, moves) while (moves) { Square dst = pop_lsq (moves); (m_list++)->move = mk_move<NORMAL> (dst - (delta), dst); }

    namespace {

#pragma region Move Generators

        template<GType GT, PType PT>
        // Move Generator for PIECE
        struct Generator
        {

        public:
            // template<GType GT, PType PT>
            // void Generator<GT, PT>::generate()
            // Generates piece common move
            static INLINE void generate (ValMove *&m_list, const Position &pos, Color c, Bitboard targets, const CheckInfo *ci = NULL)
            {
                //ASSERT (KING != PT && PAWN != PT);
                static_assert (KING != PT && PAWN != PT, "PT must not be KING & PAWN");

                Bitboard occ = pos.pieces ();

                const Square *pl = pos.piece_list<PT> (c);
                Square s;
                while ((s = *pl++) != SQ_NO)
                {
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci)
                        {
                            if ( (BSHP == PT || ROOK == PT || QUEN == PT) &&
                                !(attacks_bb<PT> (s) & targets & ci->checking_sq[PT]))
                            {
                                continue;
                            }
                            if (UNLIKELY (ci->discoverers) && (ci->discoverers & s))
                            {
                                continue;
                            }
                        }
                    }

                    Bitboard moves = attacks_bb<PT> (s, occ) & targets;
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci) moves &= ci->checking_sq[PT];
                    }

                    SERIALIZE (m_list, s, moves);
                }
            }

        };

        template<GType GT>
        // Move Generator for KING
        struct Generator<GT, KING>
        {

        public:
            // template<GType GT>
            // void Generator<GT, KING>::generate()
            // Generates KING common move
            static INLINE void generate (ValMove *&m_list, const Position &pos, Color clr, Bitboard targets, const CheckInfo *ci = NULL)
            {
                //static_assert (EVASION != GT, "GT must not be EVASION");
                if (EVASION != GT)
                {
                    if (CHECK != GT && QUIET_CHECK != GT)
                    {
                        Square org_king= pos.king_sq (clr);
                        Bitboard moves = attacks_bb<KING> (org_king) & ~attacks_bb<KING> (pos.king_sq (~clr)) & targets;
                        SERIALIZE (m_list, org_king, moves);
                    }

                    if (CAPTURE != GT)
                    {
                        if (!pos.castle_impeded (clr) && pos.can_castle (clr) && !pos.checkers ())
                        {
                            CheckInfo cc;
                            if (NULL == ci)
                            {
                                cc = CheckInfo (pos);
                                ci = &cc;
                            }

                            if (!pos.castle_impeded (clr, CS_K) && pos.can_castle (clr, CS_K))
                            {
                                pos.chess960 () ?
                                    generate_castling<CS_K,  true> (m_list, pos, clr, ci) :
                                    generate_castling<CS_K, false> (m_list, pos, clr, ci);
                            }
                            if (!pos.castle_impeded (clr, CS_Q) && pos.can_castle (clr, CS_Q))
                            {
                                pos.chess960 () ?
                                    generate_castling<CS_Q,  true> (m_list, pos, clr, ci) :
                                    generate_castling<CS_Q, false> (m_list, pos, clr, ci);
                            }
                        }
                    }
                }
            }

            // template<GType GT>
            // template<CSide SIDE, bool CHESS960>
            // void Generator<GT, KING>::generate_castling()
            template<CSide SIDE, bool CHESS960>
            // Generates KING castling move
            static INLINE void generate_castling (ValMove *&m_list, const Position &pos, Color clr, const CheckInfo *ci /*= NULL*/)
            {
                //static_assert (EVASION != GT, "GT must not be EVASION");
                ASSERT (!pos.castle_impeded (clr, SIDE) && pos.can_castle (clr, SIDE) && !pos.checkers ());
                //if (pos.castle_impeded (clr, SIDE) || !pos.can_castle (clr, SIDE) || pos.checkers ()) return;

                Square org_king = pos.king_sq (clr);
                Square org_rook = pos.castle_rook (clr, SIDE);
                if (ROOK != _type (pos[org_rook])) return;

                Square dst_king = rel_sq (clr, (CS_Q == SIDE) ? SQ_WK_Q : SQ_WK_K);

                Bitboard enemies = pos.pieces (~clr);

                Delta step = CHESS960 ? 
                    dst_king < org_king ? DEL_W : DEL_E :
                    (CS_Q == SIDE)      ? DEL_W : DEL_E;

                Square s  = org_king + step;
                while (s != dst_king + step)
                {
                    if (pos.attackers_to (s) & enemies)
                    {
                        return;
                    }
                    s += step;
                }

                if (CHESS960)
                {
                    // Because we generate only legal castling moves we need to verify that
                    // when moving the castling rook we do not discover some hidden checker.
                    // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
                    if (pos.attackers_to (dst_king, pos.pieces () - org_rook) & pos.pieces (ROOK, QUEN) & enemies)
                    {
                        return;
                    }
                }

                Move m = mk_move<CASTLE> (org_king, org_rook);

                switch (GT)
                {
                case CHECK:
                case QUIET_CHECK:
                    if (UNLIKELY (ci))
                    {
                        if (pos.check (m, *ci))
                        {
                            (m_list++)->move = m;
                        }
                    }
                    break;

                default:
                    (m_list++)->move = m;
                    break;
                }
            }

        };

        template<GType GT>
        // Move Generator for PAWN
        struct Generator<GT, PAWN>
        {

        public:
            // template<GType GT>
            // template<Color C>
            // void Generator<GT, PAWN>::generate()
            template<Color C>
            // Generates PAWN common move
            static INLINE void generate (ValMove *&m_list, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                const Color C_   = ((WHITE == C) ? BLACK : WHITE);
                const Delta PUSH = ((WHITE == C) ? DEL_N  : DEL_S);
                const Delta RCAP = ((WHITE == C) ? DEL_NE : DEL_SW);
                const Delta LCAP = ((WHITE == C) ? DEL_NW : DEL_SE);

                Bitboard bbRR7 = rel_rank_bb (C, R_7);
                Bitboard bbRR3 = rel_rank_bb (C, R_3);

                Bitboard pawns = pos.pieces (C, PAWN);
                Bitboard pawns_on_R7 = pawns &  bbRR7;
                Bitboard pawns_on_Rx = pawns & ~bbRR7;
                Bitboard occ = pos.pieces ();

                Bitboard enemies;
                switch (GT)
                {
                case EVASION: enemies = pos.pieces (C_) & targets; break;
                case CAPTURE: enemies = targets;                   break;
                default:      enemies = pos.pieces (C_);           break;
                }

                Bitboard empties = U64 (0);
                // Pawn single-push and double-push, no promotions
                if (CAPTURE != GT)
                {
                    empties = (QUIET == GT || QUIET_CHECK == GT) ? targets : ~occ;

                    Bitboard push_1 = shift_del<PUSH> (pawns_on_Rx   ) & empties;
                    Bitboard push_2 = shift_del<PUSH> (push_1 & bbRR3) & empties;

                    switch (GT)
                    {
                    case EVASION:
                        // only blocking squares are important
                        push_1 &= targets;
                        push_2 &= targets;
                        break;

                    case CHECK:
                    case QUIET_CHECK:
                        if (ci)
                        {
                            Bitboard attack = attacks_bb<PAWN> (C_, ci->king_sq);

                            push_1 &= attack;
                            push_2 &= attack;

                            // pawns which give discovered check
                            Bitboard pawns_chk_dis = pawns_on_Rx & ci->discoverers;
                            // Add pawn pushes which give discovered check. This is possible only
                            // if the pawn is not on the same file as the enemy king, because we
                            // don't generate captures. Note that a possible discovery check
                            // promotion has been already generated among captures.
                            if (pawns_chk_dis)
                            {
                                Bitboard push_cd_1 = shift_del<PUSH> (pawns_chk_dis    ) & empties;
                                Bitboard push_cd_2 = shift_del<PUSH> (push_cd_1 & bbRR3) & empties;

                                push_1 |= push_cd_1;
                                push_2 |= push_cd_2;
                            }
                        }
                        break;
                    }

                    SERIALIZE_PAWNS (m_list, Delta (PUSH << 0), push_1);
                    SERIALIZE_PAWNS (m_list, Delta (PUSH << 1), push_2);
                }
                // Pawn normal and en-passant captures, no promotions
                if (QUIET != GT && QUIET_CHECK != GT)
                {
                    Bitboard attacksL = shift_del<LCAP> (pawns_on_Rx) & enemies;
                    Bitboard attacksR = shift_del<RCAP> (pawns_on_Rx) & enemies;;

                    SERIALIZE_PAWNS (m_list, LCAP, attacksL);
                    SERIALIZE_PAWNS (m_list, RCAP, attacksR);

                    Square ep_sq = pos.en_passant ();
                    if (SQ_NO != ep_sq)
                    {
                        ASSERT (_rank (ep_sq) == rel_rank (C, R_6));

                        Bitboard bbRR5 = rel_rank_bb (C, R_5);
                        Bitboard pawns_on_R5 = pawns_on_Rx & bbRR5;
                        if (pawns_on_R5)
                        {
                            // An en-passant capture can be an evasion only if the checking piece
                            // is the double pushed pawn and so is in the target. Otherwise this
                            // is a discovery check and we are forced to do otherwise.
                            // All time except when EVASION then 2nd condition must true
                            if (EVASION != GT || (targets & (ep_sq - PUSH)))
                            {
                                Bitboard pawns_ep = attacks_bb<PAWN> (C_, ep_sq) & pawns_on_R5;
                                ASSERT (pawns_ep);
                                ASSERT (pop_count<FULL> (pawns_ep) <= 2);

                                while (pawns_ep)
                                {
                                    (m_list++)->move = mk_move<ENPASSANT> (pop_lsq (pawns_ep), ep_sq);
                                }
                            }
                        }
                    }
                }

                // Promotions (queening and under-promotions)
                if (pawns_on_R7)
                {
                    Bitboard bbRR8 = rel_rank_bb (C, R_8);

                    // All time except when EVASION then 2nd condition must true
                    if (EVASION != GT || (targets & bbRR8))
                    {
                        switch (GT)
                        {
                        case EVASION: empties &= targets;        break;
                        case CAPTURE: empties = ~pos.pieces (); break;
                        }
                        //if (QUIET != GT && QUIET_CHECK != GT)
                        {
                            generate_promotion<LCAP> (m_list, pawns_on_R7, enemies, ci);
                            generate_promotion<RCAP> (m_list, pawns_on_R7, enemies, ci);
                        }
                        //if (CAPTURE != GT)
                        {
                            generate_promotion<PUSH> (m_list, pawns_on_R7, empties, ci);
                        }
                    }
                }
            }

            // template<GType GT>
            // template<Delta D>
            // void Generator<GT, PAWN>::generate_promotion()
            template<Delta D>
            // Generates PAWN promotion move
            static INLINE void generate_promotion (ValMove *&m_list, Bitboard pawns_on_R7, Bitboard targets, const CheckInfo *ci)
            {
                static_assert ((DEL_NE == D || DEL_NW == D || DEL_SE == D || DEL_SW == D || DEL_N == D || DEL_S == D), "Value of Delta is wrong");

                Bitboard promotes = shift_del<D> (pawns_on_R7) & targets;
                while (promotes)
                {
                    Square dst = pop_lsq (promotes);
                    Square org = dst - D;

                    if (RELAX == GT || EVASION == GT || CAPTURE == GT)
                    {
                        (m_list++)->move = mk_move<PROMOTE> (org, dst, QUEN);
                    }

                    if (RELAX == GT || EVASION == GT || QUIET == GT)
                    {
                        (m_list++)->move = mk_move<PROMOTE> (org, dst, ROOK);
                        (m_list++)->move = mk_move<PROMOTE> (org, dst, BSHP);
                        (m_list++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                    }

                    // Knight-promotion is the only one that can give a direct check
                    // not already included in the queen-promotion (queening).
                    if (QUIET_CHECK == GT || CHECK == GT)
                    {
                        if (ci)
                        {
                            if (attacks_bb<NIHT> (dst) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                        }

                        if (CHECK == GT)
                        {
                            if (ci)
                            {
                                //if (attacks_bb<NIHT> (dst) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                                if (attacks_bb<BSHP> (dst, targets) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, BSHP);
                                if (attacks_bb<ROOK> (dst, targets) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, ROOK);
                                if (attacks_bb<QUEN> (dst, targets) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, QUEN);
                            }
                        }
                    }
                    else
                    {
                        (void*) ci; // silence a warning under MSVC
                    }
                }
            }

        };

#pragma endregion

        template<Color C, GType GT>
        // Generates all pseudo-legal moves of color for targets.
        INLINE ValMove* generate_moves (ValMove *&m_list, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
        {
            Generator<GT, PAWN>::generate <C> (m_list, pos, targets, ci);
            Generator<GT, NIHT>::generate (m_list, pos, C, targets, ci);
            Generator<GT, BSHP>::generate (m_list, pos, C, targets, ci);
            Generator<GT, ROOK>::generate (m_list, pos, C, targets, ci);
            Generator<GT, QUEN>::generate (m_list, pos, C, targets, ci);
            if (EVASION != GT) Generator<GT, KING>::generate (m_list, pos, C, targets, ci);

            return m_list;
        }

        //INLINE void filter_illegal (ValMove *beg, ValMove *&end, const Position &pos)
        //{
        //    Square k_sq = pos.king_sq (pos.active ());
        //    Bitboard pinneds = pos.pinneds (pos.active ());
        //
        //    //m_list.erase (
        //    //    remove_if (m_list.begin (), m_list.end (), [&] (Move m)
        //    //{
        //    //    return ((org_sq (m) == k_sq) || pinneds || (ENPASSANT == m_type (m))) && !pos.legal (m, pinneds); 
        //    //}), m_list.end ());
        //
        //    while (beg != end)
        //    {
        //        Move m = beg->move;
        //        if (((org_sq (m) == k_sq) || pinneds || (ENPASSANT == m_type (m))) && !pos.legal (m, pinneds))
        //        {
        //            beg->move = (--end)->move;
        //        }
        //        else
        //        {
        //            ++beg;
        //        }
        //    }
        //}

    }

#pragma region Generates

    template<GType GT>
    // Generates all pseudo-legal moves.
    ValMove* generate (ValMove *m_list, const Position &pos)
    {
        //ASSERT (RELAX == GT || CAPTURE == GT || QUIET == GT);
        static_assert (RELAX == GT || CAPTURE == GT || QUIET == GT, "GT must be RELAX | CAPTURE | QUIET");
        ASSERT (!pos.checkers ());

        Color active    = pos.active ();

        Bitboard targets = 
            CAPTURE == GT ?  pos.pieces(~active) :
            QUIET   == GT ? ~pos.pieces() :
            RELAX   == GT ? ~pos.pieces(active) :
            U64 (0);

        return WHITE == active ? generate_moves<WHITE, GT> (m_list, pos, targets)
            :  BLACK == active ? generate_moves<BLACK, GT> (m_list, pos, targets)
            :  m_list;
    }

    // --------------------------------
    // explicit template instantiations

    // generate<RELAX> generates all pseudo-legal captures and non-captures.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<RELAX>   (ValMove *m_list, const Position &pos);
    // generate<CAPTURES> generates all pseudo-legal captures and queen promotions.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<CAPTURE> (ValMove *m_list, const Position &pos);
    // generate<QUIETS> generates all pseudo-legal non-captures and underpromotions.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<QUIET>   (ValMove *m_list, const Position &pos);
    // --------------------------------

    template<>
    // Generates all pseudo-legal non-captures and knight underpromotions moves that give check.
    // Returns a pointer to the end of the move list.
    ValMove* generate<QUIET_CHECK> (ValMove *m_list, const Position &pos)
    {
        ASSERT (!pos.checkers ());

        Color active    = pos.active ();
        Bitboard empties= ~pos.pieces ();
        CheckInfo ci (pos);

        Bitboard discovers = ci.discoverers & ~pos.pieces (active, PAWN);
        while (discovers)
        {
            Square org = pop_lsq (discovers);
            PType pt   = _type (pos[org]);

            if (PAWN == pt) continue; // Will be generated together with direct checks

            Bitboard moves = pos.attacks_from (Piece (pt), org) & empties;

            if (KING == pt) moves &= ~attacks_bb<QUEN> (ci.king_sq);

            SERIALIZE (m_list, org, moves);
        }

        return WHITE == active ? generate_moves<WHITE, QUIET_CHECK> (m_list, pos, empties, &ci)
            :  BLACK == active ? generate_moves<BLACK, QUIET_CHECK> (m_list, pos, empties, &ci)
            :  m_list;
    }

    template<>
    // Generates all pseudo-legal check giving moves.
    // Returns a pointer to the end of the move list.
    ValMove* generate<CHECK>       (ValMove *m_list, const Position &pos)
    {
        Color active    = pos.active ();
        Bitboard targets= ~pos.pieces (active);
        CheckInfo ci (pos);

        Bitboard discovers = ci.discoverers & ~pos.pieces (active, PAWN);
        while (discovers)
        {
            Square org = pop_lsq (discovers);
            PType pt   = _type (pos[org]);

            if (PAWN == pt) continue; // Will be generated together with direct checks

            Bitboard moves = pos.attacks_from (Piece (pt), org) & targets;

            if (KING == pt) moves &= ~attacks_bb<QUEN> (ci.king_sq);

            SERIALIZE (m_list, org, moves);
        }

        return WHITE == active ? generate_moves<WHITE, CHECK> (m_list, pos, targets, &ci)
            :  BLACK == active ? generate_moves<BLACK, CHECK> (m_list, pos, targets, &ci)
            :  m_list;
    }

    template<>
    // Generates all pseudo-legal check evasions moves when the side to move is in check.
    // Returns a pointer to the end of the move list.
    ValMove* generate<EVASION>     (ValMove *m_list, const Position &pos)
    {
        Color active = pos.active ();
        Bitboard checkers = pos.checkers ();
        ASSERT (checkers); // If any checker exists
        if (!checkers) return m_list;

        Square org_king  = pos.king_sq (active);
        Bitboard friends = pos.pieces (active);

        Square check_sq = SQ_NO;
        int32_t checker_count = 0;

        //// Generates evasions for king, capture and non-capture moves excluding friends
        //Bitboard moves = attacks_bb<KING> (org_king) & ~friends;
        //check_sq = pop_lsq (checkers);
        //checker_count = pop_count<MAX15> (checkers);
        //
        //Bitboard enemies = pos.pieces (~active);
        //Bitboard mocc    = pos.pieces () - org_king;
        //// Remove squares attacked by enemies, from the king evasions.
        //// so to skip known illegal moves avoiding useless legality check later.
        //for (uint32_t k = 0; _deltas_type[KING][k]; ++k)
        //{
        //    Square sq = org_king + _deltas_type[KING][k];
        //    if (_ok (sq))
        //    {
        //        if ((moves & sq) && (pos.attackers_to (sq, mocc) & enemies))
        //        {
        //            moves -= sq;
        //        }
        //    }
        //}

        Bitboard slid_attacks = U64 (0);
        // Find squares attacked by slider checkers, we will remove them from the king
        // evasions so to skip known illegal moves avoiding useless legality check later.
        while (checkers)
        {
            ++checker_count;
            check_sq = pop_lsq (checkers);

            ASSERT (_color (pos[check_sq]) == ~active);

            if (_type (pos[check_sq]) > NIHT) // A slider
            {
                slid_attacks |= _lines_sq_bb[check_sq][org_king] - check_sq;
            }
        }

        // Generate evasions for king, capture and non capture moves
        Bitboard moves = attacks_bb<KING> (org_king) & ~friends 
            &           ~attacks_bb<KING> (pos.king_sq (~active)) & ~slid_attacks;

        SERIALIZE (m_list, org_king, moves);

        // If double check, then only a king move can save the day
        if (1 == checker_count && pop_count<FULL> (friends) > 1)
        {
            // Generates blocking evasions or captures of the checking piece
            Bitboard targets = betwen_sq_bb (check_sq, org_king) + check_sq;

            return WHITE == active ? generate_moves<WHITE, EVASION> (m_list, pos, targets)
                :  BLACK == active ? generate_moves<BLACK, EVASION> (m_list, pos, targets)
                :  m_list;
        }

        return m_list;
    }

    template<>
    // Generates all legal moves.
    ValMove* generate<LEGAL>       (ValMove *m_list, const Position &pos)
    {
        ValMove *end = pos.checkers() ?
            generate<EVASION> (m_list, pos) :
            generate<RELAX  > (m_list, pos) ;

        Square k_sq = pos.king_sq (pos.active ());
        Bitboard pinneds = pos.pinneds (pos.active ());

        ValMove *cur = m_list;
        while (cur != end)
        {
            Move m = cur->move;
            if ((org_sq (m) == k_sq || pinneds || ENPASSANT == m_type (m)) &&
                !pos.legal (m, pinneds))
            {
                cur->move = (--end)->move;
            }
            else
            {
                ++cur;
            }
        }

        return end;
    }

#pragma endregion

#undef SERIALIZE
#undef SERIALIZE_PAWNS

}
