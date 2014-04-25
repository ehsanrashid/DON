#include "MoveGenerator.h"

#include "Position.h"
#include "BitCount.h"

#ifdef _MSC_VER
#   pragma warning (disable: 4189) // 'argument' : local variable is initialized but not referenced
#endif

namespace MoveGenerator {

    using namespace std;
    using namespace BitBoard;

#undef SERIALIZE
#undef SERIALIZE_PAWNS

    // Fill moves in the list for any piece using a very common while loop, no fancy.
#define SERIALIZE(moves, org, attacks)         while (attacks != U64 (0)) { (moves++)->move = mk_move<NORMAL> (org, pop_lsq (attacks)); }
    // Fill moves in the list for pawns, where the 'delta' is the distance b/w 'org' and 'dst' square.
#define SERIALIZE_PAWNS(moves, delta, attacks) while (attacks != U64 (0)) { Square dst = pop_lsq (attacks); (moves++)->move = mk_move<NORMAL> (dst - delta, dst); }

    namespace {

        template<GenT GT, Color C, PieceT PT>
        // Move Generator for PIECE
        struct Generator
        {

        private:

            Generator () {}

        public:
            // template<GenT GT, Color C, PieceT PT>
            // void Generator<GT, C, PT>::generate()
            // Generates piece common move
            static INLINE void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                ASSERT (KING != PT && PAWN != PT);

                const Bitboard occ = pos.pieces ();

                const Square *pl = pos.list<PT> (C);
                Square s;
                while ((s = *pl++) != SQ_NO)
                {
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci)
                        {
                            if (    (BSHP == PT || ROOK == PT || QUEN == PT)
                                && !(PieceAttacks[PT][s] & targets & ci->checking_bb[PT]))
                            {
                                continue;
                            }
                            if (UNLIKELY (ci->discoverers) && (ci->discoverers & s))
                            {
                                continue;
                            }
                        }
                    }

                    Bitboard attacks = attacks_bb<PT> (s, occ) & targets;
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci) attacks &= ci->checking_bb[PT];
                    }

                    SERIALIZE (moves, s, attacks);
                }
            }

        };

        template<GenT GT, Color C>
        // Move Generator for KING
        struct Generator<GT, C, KING>
        {

        private:
            
            Generator () {}

            // template<GenT GT, Color C>
            // template<CSide SIDE, bool CHESS960>
            // void Generator<GT, KING>::generate_castling()
            template<CRight CR, bool CHESS960>
            // Generates KING castling move
            static INLINE void generate_castling (ValMove *&moves, const Position &pos, const CheckInfo *ci /*= NULL*/)
            {
                ASSERT (EVASION != GT);
                ASSERT (!pos.castle_impeded (CR) && pos.can_castle (CR) && pos.checkers () == U64 (0));
                
                if (EVASION == GT) return;
                if (pos.castle_impeded (CR) || !pos.can_castle (CR) || pos.checkers () != U64 (0)) return;

                const bool KingSide = (CR == CR_WK || CR == CR_BK);
                const Color C_ = (WHITE == C) ? BLACK : WHITE;

                Square org_king = pos.king_sq (C);
                Square org_rook = pos.castle_rook (CR);
                if (ROOK != ptype (pos[org_rook])) return;

                Square dst_king = rel_sq (C, KingSide ? SQ_G1 : SQ_C1);

                Bitboard enemies = pos.pieces (C_);

                Delta step = CHESS960 ? 
                    (dst_king > org_king ? DEL_W : DEL_E) :
                    (KingSide            ? DEL_W : DEL_E);

                for (i08 s = dst_king; s != org_king; s += step)
                {
                    if (pos.attackers_to (Square (s)) & enemies)
                    {
                        return;
                    }
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

                if (CHECK == GT || QUIET_CHECK == GT)
                {
                    if (UNLIKELY (ci) && !pos.gives_check (m, *ci))
                    {
                        return;
                    }
                }

                (moves++)->move = m;
            }

        public:
            // template<GenT GT, Color C>
            // void Generator<GT, C, KING>::generate()
            // Generates KING common move
            static INLINE void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                if (EVASION == GT) return;

                if (CHECK != GT && QUIET_CHECK != GT)
                {
                    const Color C_ = (WHITE == C) ? BLACK : WHITE;
                    Square king_sq = pos.king_sq (C);
                    Bitboard attacks = PieceAttacks[KING][king_sq] & ~PieceAttacks[KING][pos.king_sq (C_)] & targets;
                    SERIALIZE (moves, king_sq, attacks);
                }

                if (CAPTURE != GT)
                {
                    if (pos.can_castle (C) && pos.checkers () == U64 (0))
                    {
                        CheckInfo cc;
                        if (ci == NULL)
                        {
                            cc = CheckInfo (pos);
                            ci = &cc;
                        }

                        if (!pos.castle_impeded (Castling<C, CS_K>::Right) && pos.can_castle (Castling<C, CS_K>::Right))
                        {
                            pos.chess960 () ?
                                generate_castling<Castling<C, CS_K>::Right,  true> (moves, pos, ci) :
                                generate_castling<Castling<C, CS_K>::Right, false> (moves, pos, ci);
                        }

                        if (!pos.castle_impeded (Castling<C, CS_Q>::Right) && pos.can_castle (Castling<C, CS_Q>::Right))
                        {
                            pos.chess960 () ?
                                generate_castling<Castling<C, CS_Q>::Right,  true> (moves, pos, ci) :
                                generate_castling<Castling<C, CS_Q>::Right, false> (moves, pos, ci);
                        }
                    }
                }
            }

        };

        template<GenT GT, Color C>
        // Move Generator for PAWN
        struct Generator<GT, C, PAWN>
        {

        private:
            
            Generator () {}

            // template<GenT GT, Color C>
            // template<Delta D>
            // void Generator<GT, C, PAWN>::generate_promotion()
            template<Delta D>
            // Generates PAWN promotion move
            static INLINE void generate_promotion (ValMove *&moves, Bitboard pawns_on_R7, Bitboard targets, const CheckInfo *ci)
            {
                ASSERT ((DEL_NE == D || DEL_NW == D || DEL_SE == D || DEL_SW == D || DEL_N == D || DEL_S == D));

                Bitboard promotes = shift_del<D> (pawns_on_R7) & targets;
                while (promotes != U64 (0))
                {
                    Square dst = pop_lsq (promotes);
                    Square org = dst - D;

                    if (RELAX == GT || EVASION == GT || CAPTURE == GT)
                    {
                        (moves++)->move = mk_move<PROMOTE> (org, dst, QUEN);
                    }

                    if (RELAX == GT || EVASION == GT || QUIET == GT)
                    {
                        (moves++)->move = mk_move<PROMOTE> (org, dst, ROOK);
                        (moves++)->move = mk_move<PROMOTE> (org, dst, BSHP);
                        (moves++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                    }

                    // Knight-promotion is the only one that can give a direct check
                    // not already included in the queen-promotion (queening).
                    if (QUIET_CHECK == GT || CHECK == GT)
                    {
                        if (ci)
                        {
                            if (PieceAttacks[NIHT][dst] & ci->king_sq) (moves++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                        }

                        if (CHECK == GT)
                        {
                            if (ci)
                            {
                                //if (PieceAttacks[NIHT][dst] & ci->king_sq) (moves++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                                if (attacks_bb<BSHP> (dst, targets) & ci->king_sq) (moves++)->move = mk_move<PROMOTE> (org, dst, BSHP);
                                if (attacks_bb<ROOK> (dst, targets) & ci->king_sq) (moves++)->move = mk_move<PROMOTE> (org, dst, ROOK);
                                if (attacks_bb<QUEN> (dst, targets) & ci->king_sq) (moves++)->move = mk_move<PROMOTE> (org, dst, QUEN);
                            }
                        }
                    }
                    else
                    {
                        (void) ci; // silence a warning under MSVC
                    }
                }
            }

        public:
            // template<GenT GT, Color C>
            // void Generator<GT, C, PAWN>::generate()
            // Generates PAWN common move
            static INLINE void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                const Color C_   = (WHITE == C) ? BLACK : WHITE;
                const Delta PUSH = (WHITE == C) ? DEL_N  : DEL_S;
                const Delta RCAP = (WHITE == C) ? DEL_NE : DEL_SW;
                const Delta LCAP = (WHITE == C) ? DEL_NW : DEL_SE;

                Bitboard pawns = pos.pieces<PAWN> (C);

                Bitboard RR7_bb = rel_rank_bb (C, R_7);
                Bitboard pawns_on_R7 = pawns &  RR7_bb;
                Bitboard pawns_on_Rx = pawns & ~RR7_bb;

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
                    empties = (QUIET == GT || QUIET_CHECK == GT) ? targets : ~pos.pieces ();
                    
                    Bitboard RR3_bb = rel_rank_bb (C, R_3);
                    Bitboard push_1 = shift_del<PUSH> (pawns_on_Rx    ) & empties;
                    Bitboard push_2 = shift_del<PUSH> (push_1 & RR3_bb) & empties;

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
                            Bitboard pawn_attacks = PawnAttacks[C_][ci->king_sq];

                            push_1 &= pawn_attacks;
                            push_2 &= pawn_attacks;

                            // pawns which give discovered check
                            Bitboard pawns_chk_dis = pawns_on_Rx & ci->discoverers;
                            // Add pawn pushes which give discovered check. This is possible only
                            // if the pawn is not on the same file as the enemy king, because we
                            // don't generate captures. Note that a possible discovery check
                            // promotion has been already generated among captures.
                            if (pawns_chk_dis != U64 (0))
                            {
                                Bitboard push_cd_1 = shift_del<PUSH> (pawns_chk_dis     ) & empties;
                                Bitboard push_cd_2 = shift_del<PUSH> (push_cd_1 & RR3_bb) & empties;

                                push_1 |= push_cd_1;
                                push_2 |= push_cd_2;
                            }
                        }
                        break;

                    default: break;
                    }

                    SERIALIZE_PAWNS (moves, Delta (PUSH << 0), push_1);
                    SERIALIZE_PAWNS (moves, Delta (PUSH << 1), push_2);
                }
                // Pawn normal and en-passant captures, no promotions
                if (QUIET != GT && QUIET_CHECK != GT)
                {
                    Bitboard l_attacks = shift_del<LCAP> (pawns_on_Rx) & enemies;
                    Bitboard r_attacks = shift_del<RCAP> (pawns_on_Rx) & enemies;;

                    SERIALIZE_PAWNS (moves, LCAP, l_attacks);
                    SERIALIZE_PAWNS (moves, RCAP, r_attacks);

                    Square ep_sq = pos.en_passant_sq ();
                    if (SQ_NO != ep_sq)
                    {
                        ASSERT (_rank (ep_sq) == rel_rank (C, R_6));
                        // RR5_bb
                        Bitboard pawns_on_R5 = pawns_on_Rx & rel_rank_bb (C, R_5);
                        if (pawns_on_R5 != U64 (0))
                        {
                            // An en-passant capture can be an evasion only if the checking piece
                            // is the double pushed pawn and so is in the target. Otherwise this
                            // is a discovery check and we are forced to do otherwise.
                            // All time except when EVASION then 2nd condition must true
                            if (EVASION != GT || (targets & (ep_sq - PUSH)))
                            {
                                Bitboard pawns_ep = PawnAttacks[C_][ep_sq] & pawns_on_R5;
                                ASSERT (pawns_ep != U64 (0));
                                ASSERT (pop_count<MAX15> (pawns_ep) <= 2);

                                while (pawns_ep != U64 (0))
                                {
                                    (moves++)->move = mk_move<ENPASSANT> (pop_lsq (pawns_ep), ep_sq);
                                }
                            }
                        }
                    }
                }

                // Promotions (queening and under-promotions)
                if (pawns_on_R7 != U64 (0))
                {
                    Bitboard RR8_bb = rel_rank_bb (C, R_8);
                    
                    // All time except when EVASION then 2nd condition must true
                    if (EVASION != GT || (targets & RR8_bb))
                    {
                        if      (CAPTURE == GT) empties = ~pos.pieces ();
                        else if (EVASION == GT) empties &= targets;

                        generate_promotion<LCAP> (moves, pawns_on_R7, enemies, ci);
                        generate_promotion<RCAP> (moves, pawns_on_R7, enemies, ci);
                        generate_promotion<PUSH> (moves, pawns_on_R7, empties, ci);
                    }
                }
            }

        };

        template<GenT GT, Color C>
        // Generates all pseudo-legal moves of color for targets.
        INLINE ValMove* generate_moves (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
        {
            Generator<GT, C, PAWN>::generate (moves, pos, targets, ci);
            Generator<GT, C, NIHT>::generate (moves, pos, targets, ci);
            Generator<GT, C, BSHP>::generate (moves, pos, targets, ci);
            Generator<GT, C, ROOK>::generate (moves, pos, targets, ci);
            Generator<GT, C, QUEN>::generate (moves, pos, targets, ci);
            Generator<GT, C, KING>::generate (moves, pos, targets, ci);

            return moves;
        }

        //INLINE void filter_illegal (ValMove *beg, ValMove *&end, const Position &pos)
        //{
        //    Square king_sq = pos.king_sq (pos.active ());
        //    Bitboard pinneds = pos.pinneds (pos.active ());
        //
        //    //moves.erase (
        //    //    remove_if (moves.begin (), moves.end (), [&] (Move m)
        //    //{
        //    //    return ((org_sq (m) == king_sq) || pinneds != U64 (0) || (ENPASSANT == mtype (m))) && !pos.legal (m, pinneds); 
        //    //}), moves.end ());
        //
        //    while (beg != end)
        //    {
        //        Move m = beg->move;
        //        if (   ((org_sq (m) == king_sq) || pinneds != U64 (0) || (ENPASSANT == mtype (m)))
        //            && !pos.legal (m, pinneds))
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

    template<GenT GT>
    // Generates all pseudo-legal moves.
    ValMove* generate (ValMove *moves, const Position &pos)
    {
        ASSERT (RELAX == GT || CAPTURE == GT || QUIET == GT);
        ASSERT (pos.checkers () == U64 (0));

        Color active = pos.active ();

        Bitboard targets = 
              CAPTURE == GT ?  pos.pieces (~active)
            : QUIET   == GT ? ~pos.pieces ()
            : RELAX   == GT ? ~pos.pieces (active)
            : U64 (0);

        return WHITE == active ? generate_moves<GT, WHITE> (moves, pos, targets)
            :  BLACK == active ? generate_moves<GT, BLACK> (moves, pos, targets)
            :  moves;
    }

    // --------------------------------
    // explicit template instantiations

    // generate<RELAX> generates all pseudo-legal captures and non-captures.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<RELAX>   (ValMove *moves, const Position &pos);
    // generate<CAPTURES> generates all pseudo-legal captures and queen promotions.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<CAPTURE> (ValMove *moves, const Position &pos);
    // generate<QUIETS> generates all pseudo-legal non-captures and underpromotions.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<QUIET>   (ValMove *moves, const Position &pos);
    // --------------------------------

    template<>
    // Generates all pseudo-legal non-captures and knight underpromotions moves that give check.
    // Returns a pointer to the end of the move list.
    ValMove* generate<QUIET_CHECK> (ValMove *moves, const Position &pos)
    {
        ASSERT (pos.checkers () == U64 (0));

        Color active    = pos.active ();
        Bitboard occ    = pos.pieces ();
        Bitboard empties= ~occ;
        CheckInfo ci (pos);
        // Pawns excluded will be generated together with direct checks
        Bitboard discovers = ci.discoverers & ~pos.pieces<PAWN> (active);
        while (discovers != U64 (0))
        {
            Square org = pop_lsq (discovers);
            PieceT pt  = ptype (pos[org]);
            Bitboard attacks = attacks_bb (Piece (pt), org, occ) & empties;

            if (KING == pt) attacks &= ~PieceAttacks[QUEN][ci.king_sq];

            SERIALIZE (moves, org, attacks);
        }

        return WHITE == active ? generate_moves<QUIET_CHECK, WHITE> (moves, pos, empties, &ci)
            :  BLACK == active ? generate_moves<QUIET_CHECK, BLACK> (moves, pos, empties, &ci)
            :  moves;
    }

    template<>
    // Generates all pseudo-legal check giving moves.
    // Returns a pointer to the end of the move list.
    ValMove* generate<CHECK>       (ValMove *moves, const Position &pos)
    {
        Color active    = pos.active ();
        Bitboard occ    = pos.pieces ();
        Bitboard targets= ~pos.pieces (active);
        CheckInfo ci (pos);
        // Pawns excluded, will be generated together with direct checks
        Bitboard discovers = ci.discoverers & ~pos.pieces<PAWN> (active);
        while (discovers != U64 (0))
        {
            Square org = pop_lsq (discovers);
            PieceT pt  = ptype (pos[org]);
            Bitboard attacks = attacks_bb (Piece (pt), org, occ) & targets;

            if (KING == pt) attacks &= ~PieceAttacks[QUEN][ci.king_sq];

            SERIALIZE (moves, org, attacks);
        }

        return WHITE == active ? generate_moves<CHECK, WHITE> (moves, pos, targets, &ci)
            :  BLACK == active ? generate_moves<CHECK, BLACK> (moves, pos, targets, &ci)
            :  moves;
    }

    template<>
    // Generates all pseudo-legal check evasions moves when the side to move is in check.
    // Returns a pointer to the end of the move list.
    ValMove* generate<EVASION>     (ValMove *moves, const Position &pos)
    {
        Bitboard checkers = pos.checkers ();
        ASSERT (checkers != U64 (0)); // If any checker exists

        Color active = pos.active ();

        Square  king_sq = pos.king_sq (active);
        Square check_sq;

        //// Generates evasions for king, capture and non-capture moves excluding friends
        //Bitboard attacks = PieceAttacks[KING][king_sq] & ~pos.pieces (active);
        //check_sq = pop_lsq (checkers);
        //
        //Bitboard enemies = pos.pieces (~active);
        //Bitboard mocc    = pos.pieces () - king_sq;
        //// Remove squares attacked by enemies, from the king evasions.
        //// so to skip known illegal moves avoiding useless legality check later.
        //for (u08 k = 0; PieceDeltas[KING][k]; ++k)
        //{
        //    Square sq = king_sq + PieceDeltas[KING][k];
        //    if (_ok (sq))
        //    {
        //        if ((attacks & sq) && (pos.attackers_to (sq, mocc) & enemies))
        //        {
        //            attacks -= sq;
        //        }
        //    }
        //}

        check_sq = SQ_NO;
        Bitboard slid_attacks = U64 (0);
        Bitboard sliders = checkers & ~(pos.pieces (NIHT, PAWN));
        // Find squares attacked by slider checkers, we will remove them from the king
        // evasions so to skip known illegal moves avoiding useless legality check later.
        while (sliders != U64 (0))
        {
            check_sq = pop_lsq (sliders);

            ASSERT (color (pos[check_sq]) == ~active);
            slid_attacks |= LineRay_bb[check_sq][king_sq] - check_sq;
        }

        // Generate evasions for king, capture and non capture moves
        Bitboard attacks =  PieceAttacks[KING][king_sq]
            & ~(pos.pieces (active) | PieceAttacks[KING][pos.king_sq (~active)] | slid_attacks);

        SERIALIZE (moves, king_sq, attacks);

        // If double check, then only a king move can save the day
        if (more_than_one (checkers) || pos.count<NONE> (active) == 1)
        {
            return moves;
        }

        if (check_sq == SQ_NO) check_sq = scan_lsq (checkers);
        // Generates blocking evasions or captures of the checking piece
        Bitboard targets = Between_bb[check_sq][king_sq] + check_sq;

        return WHITE == active ? generate_moves<EVASION, WHITE> (moves, pos, targets)
            :  BLACK == active ? generate_moves<EVASION, BLACK> (moves, pos, targets)
            :  moves;
    }

    template<>
    // Generates all legal moves.
    ValMove* generate<LEGAL>       (ValMove *moves, const Position &pos)
    {
        ValMove *end = pos.checkers () != U64 (0)
            ? generate<EVASION> (moves, pos)
            : generate<RELAX  > (moves, pos);

        Square   king_sq = pos.king_sq (pos.active ());
        Bitboard pinneds = pos.pinneds (pos.active ());

        ValMove *cur = moves;
        while (cur != end)
        {
            Move m = cur->move;
            if (  ((org_sq (m) == king_sq) || pinneds  != U64 (0) || ENPASSANT == mtype (m))
                && !pos.legal (m, pinneds))
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

#undef SERIALIZE
#undef SERIALIZE_PAWNS

}
