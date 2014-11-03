#include "MoveGenerator.h"

#include "Position.h"

namespace MoveGen {

    using namespace std;
    using namespace BitBoard;

    namespace {

        template<GenT GT, Color C, PieceT PT>
        // Move Generator for PIECE
        struct Generator
        {

        private:

            Generator () {}

        public:
            // Generates piece common move
            static INLINE void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                assert (KING != PT && PAWN != PT);

                const Square *pl = pos.list<PT> (C);
                Square s;
                while ((s = *pl++) != SQ_NO)
                {
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci != NULL)
                        {
                            if (  (BSHP == PT || ROOK == PT || QUEN == PT)
                               && !(PIECE_ATTACKS[PT][s] & targets & ci->checking_bb[PT])
                               )
                            {
                                continue;
                            }
                            if (ci->discoverers && ci->discoverers & s)
                            {
                                continue;
                            }
                        }
                    }

                    Bitboard attacks = attacks_bb<PT> (s, pos.pieces ()) & targets;
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci != NULL) attacks &= ci->checking_bb[PT];
                    }

                    while (attacks != U64(0)) { (moves++)->move = mk_move<NORMAL> (s, pop_lsq (attacks)); }
                }
            }

        };

        template<GenT GT, Color C>
        // Move Generator for KING
        struct Generator<GT, C, KING>
        {

        private:
            
            Generator () {}

            template<CRight CR, bool Chess960>
            // Generates KING castling move
            static INLINE void generate_castling (ValMove *&moves, const Position &pos, const CheckInfo *ci /*= NULL*/)
            {
                assert (EVASION != GT);
                assert (!pos.castle_impeded (CR) && pos.can_castle (CR) && pos.checkers () == U64(0));
                
                //if (EVASION == GT) return;
                //if (!pos.can_castle (CR) || pos.castle_impeded (CR) || pos.checkers () != U64(0)) return;

                const Color C_ = WHITE == C ? BLACK : WHITE;

                Square king_org = pos.king_sq (C);
                Square rook_org = pos.castle_rook (CR);

                assert (ROOK == ptype (pos[rook_org]));
                //if (ROOK != ptype (pos[rook_org])) return;

                Square king_dst = rel_sq (C, ((CR == CR_WK || CR == CR_BK) ? SQ_G1 : SQ_C1));
                Delta step = (king_dst > king_org ? DEL_E : DEL_W);
                for (i08 s = king_dst; s != king_org; s -= step)
                {
                    if (pos.attackers_to (Square(s), C_) != U64(0))
                    {
                        return;
                    }
                }

                if (Chess960)
                {
                    // Because generate only legal castling moves needed to verify that
                    // when moving the castling rook do not discover some hidden checker.
                    // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
                    if (pos.attackers_to (king_dst, C_, pos.pieces () - rook_org) & pos.pieces (ROOK, QUEN))
                    {
                        return;
                    }
                }

                Move m = mk_move<CASTLE> (king_org, rook_org);

                if (CHECK == GT || QUIET_CHECK == GT)
                {
                    if (ci != NULL && !pos.gives_check (m, *ci))
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
                    const Color C_ = WHITE == C ? BLACK : WHITE;
                    Square king_sq = pos.king_sq (C);
                    Bitboard attacks = PIECE_ATTACKS[KING][king_sq] & ~PIECE_ATTACKS[KING][pos.king_sq (C_)] & targets;
                    
                    while (attacks != U64(0)) { (moves++)->move = mk_move<NORMAL> (king_sq, pop_lsq (attacks)); }
                }

                if (CAPTURE != GT)
                {
                    if (pos.can_castle (C) && pos.checkers () == U64(0))
                    {
                        CheckInfo cc;
                        if (ci == NULL)
                        {
                            cc = CheckInfo (pos);
                            ci = &cc;
                        }

                        if (pos.can_castle (Castling<C, CS_K>::Right) && !pos.castle_impeded (Castling<C, CS_K>::Right))
                        {
                            pos.chess960 () ?
                                generate_castling<Castling<C, CS_K>::Right,  true> (moves, pos, ci) :
                                generate_castling<Castling<C, CS_K>::Right, false> (moves, pos, ci);
                        }

                        if (pos.can_castle (Castling<C, CS_Q>::Right) && !pos.castle_impeded (Castling<C, CS_Q>::Right))
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

            template<Delta D>
            // Generates PAWN promotion move
            static INLINE void generate_promotion (ValMove *&moves, Bitboard pawns_on_R7, Bitboard targets, const CheckInfo *ci)
            {
                assert ((DEL_NE == D || DEL_NW == D || DEL_SE == D || DEL_SW == D || DEL_N == D || DEL_S == D));

                Bitboard promotes = shift_del<D> (pawns_on_R7) & targets;
                while (promotes != U64(0))
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
                        if (ci != NULL)
                        {
                            if (PIECE_ATTACKS[NIHT][dst] & ci->king_sq) (moves++)->move = mk_move<PROMOTE> (org, dst, NIHT);

                            if (CHECK == GT)
                            {
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
            // Generates PAWN common move
            static INLINE void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                const Color C_   = WHITE == C ? BLACK : WHITE;
                const Delta PUSH = WHITE == C ? DEL_N  : DEL_S;
                const Delta RCAP = WHITE == C ? DEL_NE : DEL_SW;
                const Delta LCAP = WHITE == C ? DEL_NW : DEL_SE;

                Bitboard pawns = pos.pieces<PAWN> (C);

                Bitboard pawns_on_R7 = pawns &  rel_rank_bb (C, R_7);
                Bitboard pawns_on_Rx = pawns & ~pawns_on_R7;

                Bitboard enemies;
                switch (GT)
                {
                case EVASION:
                    enemies = pos.pieces (C_) & targets;
                break;
                case CAPTURE:
                    enemies = targets;
                break;
                default:
                    enemies = pos.pieces (C_);
                break;
                }

                Bitboard empties = U64(0);
                // Pawn single-push and double-push, no promotions
                if (CAPTURE != GT)
                {
                    empties = (QUIET == GT || QUIET_CHECK == GT) ? targets : ~pos.pieces ();
                    
                    Bitboard push_1 = shift_del<PUSH> (pawns_on_Rx                  ) & empties;
                    Bitboard push_2 = shift_del<PUSH> (push_1 & rel_rank_bb (C, R_3)) & empties;

                    switch (GT)
                    {
                    case EVASION:
                        // only blocking squares are important
                        push_1 &= targets;
                        push_2 &= targets;
                    break;

                    case CHECK:
                    case QUIET_CHECK:
                        if (ci != NULL)
                        {
                            Bitboard pawn_attacks = PAWN_ATTACKS[C_][ci->king_sq];

                            push_1 &= pawn_attacks;
                            push_2 &= pawn_attacks;

                            // Pawns which give discovered check
                            // Add pawn pushes which give discovered check. This is possible only
                            // if the pawn is not on the same file as the enemy king, because
                            // don't generate captures. Note that a possible discovery check
                            // promotion has been already generated among captures.
                            if ((pawns_on_Rx & ci->discoverers) != U64(0))
                            {
                                Bitboard push_cd_1 = shift_del<PUSH> (pawns_on_Rx & ci->discoverers   ) & empties;
                                Bitboard push_cd_2 = shift_del<PUSH> (push_cd_1 & rel_rank_bb (C, R_3)) & empties;

                                push_1 |= push_cd_1;
                                push_2 |= push_cd_2;
                            }
                        }
                    break;

                    default:
                    break;
                    }
                    
                    while (push_1 != U64(0)) { Square dst = pop_lsq (push_1); (moves++)->move = mk_move<NORMAL> (dst - PUSH, dst); }
                    while (push_2 != U64(0)) { Square dst = pop_lsq (push_2); (moves++)->move = mk_move<NORMAL> (dst - PUSH-PUSH, dst); }
                }
                // Pawn normal and en-passant captures, no promotions
                if (QUIET != GT && QUIET_CHECK != GT)
                {
                    Bitboard l_attacks = shift_del<LCAP> (pawns_on_Rx) & enemies;
                    Bitboard r_attacks = shift_del<RCAP> (pawns_on_Rx) & enemies;

                    while (l_attacks != U64(0)) { Square dst = pop_lsq (l_attacks); (moves++)->move = mk_move<NORMAL> (dst - LCAP, dst); }
                    while (r_attacks != U64(0)) { Square dst = pop_lsq (r_attacks); (moves++)->move = mk_move<NORMAL> (dst - RCAP, dst); }

                    Square ep_sq = pos.en_passant_sq ();
                    if (SQ_NO != ep_sq)
                    {
                        assert (_rank (ep_sq) == rel_rank (C, R_6));
                        if (pawns_on_Rx & rel_rank_bb (C, R_5))
                        {
                            // An en-passant capture can be an evasion only if the checking piece
                            // is the double pushed pawn and so is in the target. Otherwise this
                            // is a discovery check and are forced to do otherwise.
                            // All time except when EVASION then 2nd condition must true
                            if (EVASION != GT || (targets & (ep_sq - PUSH)))
                            {
                                Bitboard ep_attacks = PAWN_ATTACKS[C_][ep_sq] & pawns_on_Rx & rel_rank_bb (C, R_5);
                                assert (ep_attacks != U64(0));
                                assert (pop_count<MAX15> (ep_attacks) <= 2);

                                while (ep_attacks != U64(0)) { (moves++)->move = mk_move<ENPASSANT> (pop_lsq (ep_attacks), ep_sq); }
                            }
                        }
                    }
                }

                // Promotions (queening and under-promotions)
                if (pawns_on_R7 != U64(0))
                {
                    // All time except when EVASION then 2nd condition must true
                    if (EVASION != GT || (targets & rel_rank_bb (C, R_8)) != U64(0))
                    {
                        if (CAPTURE == GT) empties = ~pos.pieces ();
                        else
                        if (EVASION == GT) empties &= targets;

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
            /*if (pos.count<NIHT> (C) !=0)*/ Generator<GT, C, NIHT>::generate (moves, pos, targets, ci);
            /*if (pos.count<BSHP> (C) !=0)*/ Generator<GT, C, BSHP>::generate (moves, pos, targets, ci);
            /*if (pos.count<ROOK> (C) !=0)*/ Generator<GT, C, ROOK>::generate (moves, pos, targets, ci);
            /*if (pos.count<QUEN> (C) !=0)*/ Generator<GT, C, QUEN>::generate (moves, pos, targets, ci);
            Generator<GT, C, KING>::generate (moves, pos, targets, ci);

            return moves;
        }

    }

    template<GenT GT>
    // Generates all pseudo-legal moves.
    inline ValMove* generate (ValMove *moves, const Position &pos)
    {
        assert (RELAX == GT || CAPTURE == GT || QUIET == GT);
        assert (pos.checkers () == U64(0));

        Color active    = pos.active ();

        Bitboard targets = 
            CAPTURE == GT ?  pos.pieces (~active) :
            QUIET   == GT ? ~pos.pieces () :
            RELAX   == GT ? ~pos.pieces (active) :
            U64(0);

        return WHITE == active ? generate_moves<GT, WHITE> (moves, pos, targets) :
               BLACK == active ? generate_moves<GT, BLACK> (moves, pos, targets) :
               moves;
    }

    // --------------------------------
    // explicit template instantiations

    // generate<RELAX> generates all pseudo-legal captures and non-captures.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<RELAX  > (ValMove *moves, const Position &pos);
    // generate<CAPTURES> generates all pseudo-legal captures and queen promotions.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<CAPTURE> (ValMove *moves, const Position &pos);
    // generate<QUIETS> generates all pseudo-legal non-captures and underpromotions.
    // Returns a pointer to the end of the move list.
    template ValMove* generate<QUIET  > (ValMove *moves, const Position &pos);
    // --------------------------------

    template<>
    // Generates all pseudo-legal non-captures and knight underpromotions moves that give check.
    // Returns a pointer to the end of the move list.
    ValMove* generate<QUIET_CHECK> (ValMove *moves, const Position &pos)
    {
        assert (pos.checkers () == U64(0));

        Color active    = pos.active ();
        Bitboard empties= ~pos.pieces ();
        CheckInfo ci (pos);
        // Pawns excluded will be generated together with direct checks
        Bitboard discovers = ci.discoverers & ~pos.pieces<PAWN> (active);
        while (discovers != U64(0))
        {
            Square org = pop_lsq (discovers);
            PieceT pt  = ptype (pos[org]);
            Bitboard attacks = attacks_bb (Piece(pt), org, pos.pieces ()) & empties;

            if (KING == pt) attacks &= ~PIECE_ATTACKS[QUEN][ci.king_sq];

            while (attacks != U64(0)) { (moves++)->move = mk_move<NORMAL> (org, pop_lsq (attacks)); }
        }

        return WHITE == active ? generate_moves<QUIET_CHECK, WHITE> (moves, pos, empties, &ci) :
               BLACK == active ? generate_moves<QUIET_CHECK, BLACK> (moves, pos, empties, &ci) :
               moves;
    }

    template<>
    // Generates all pseudo-legal check giving moves.
    // Returns a pointer to the end of the move list.
    ValMove* generate<CHECK      > (ValMove *moves, const Position &pos)
    {
        Color active    = pos.active ();
        Bitboard targets= ~pos.pieces (active);
        CheckInfo ci (pos);
        // Pawns excluded, will be generated together with direct checks
        Bitboard discovers = ci.discoverers & ~pos.pieces<PAWN> (active);
        while (discovers != U64(0))
        {
            Square org = pop_lsq (discovers);
            PieceT pt  = ptype (pos[org]);
            Bitboard attacks = attacks_bb (Piece(pt), org, pos.pieces ()) & targets;

            if (KING == pt) attacks &= ~PIECE_ATTACKS[QUEN][ci.king_sq];

            while (attacks != U64(0)) { (moves++)->move = mk_move<NORMAL> (org, pop_lsq (attacks)); }
        }

        return WHITE == active ? generate_moves<CHECK, WHITE> (moves, pos, targets, &ci) :
               BLACK == active ? generate_moves<CHECK, BLACK> (moves, pos, targets, &ci) :
               moves;
    }

    template<>
    // Generates all pseudo-legal check evasions moves when the side to move is in check.
    // Returns a pointer to the end of the move list.
    ValMove* generate<EVASION    > (ValMove *moves, const Position &pos)
    {
        Bitboard checkers = pos.checkers ();
        assert (checkers); // If any checker exists

        Color active    = pos.active ();
        Square king_sq  = pos.king_sq (active);

        Square check_sq;

        //// Generates evasions for king, capture and non-capture moves excluding friends
        //Bitboard attacks = PIECE_ATTACKS[KING][king_sq] & ~pos.pieces (active);
        //check_sq = pop_lsq (checkers);
        //
        //Bitboard enemies = pos.pieces (~active);
        //Bitboard mocc    = pos.pieces () - king_sq;
        //// Remove squares attacked by enemies, from the king evasions.
        //// so to skip known illegal moves avoiding useless legality check later.
        //for (u08 k = 0; PIECE_DELTAS[KING][k]; ++k)
        //{
        //    Square sq = king_sq + PIECE_DELTAS[KING][k];
        //    if (_ok (sq))
        //    {
        //        if ((attacks & sq) && pos.attackers_to (sq, ~active, mocc))
        //        {
        //            attacks -= sq;
        //        }
        //    }
        //}

        check_sq = SQ_NO;
        Bitboard slid_attacks = U64(0);
        Bitboard sliders = checkers & ~pos.pieces (NIHT, PAWN);
        // Find squares attacked by slider checkers, will remove them from the king
        // evasions so to skip known illegal moves avoiding useless legality check later.
        while (sliders != U64(0))
        {
            check_sq = pop_lsq (sliders);
            assert (color (pos[check_sq]) == ~active);
            slid_attacks |= RAY_LINE_bb[check_sq][king_sq] - check_sq;
        }

        // Generate evasions for king, capture and non capture moves
        Bitboard attacks =  PIECE_ATTACKS[KING][king_sq]
            & ~(pos.pieces (active) | PIECE_ATTACKS[KING][pos.king_sq (~active)] | slid_attacks);

        while (attacks != U64(0)) { (moves++)->move = mk_move<NORMAL> (king_sq, pop_lsq (attacks)); }

        // If double-check, then only a king move can save the day, triple+ check not possible
        if (more_than_one (checkers) || pos.count<NONE> (active) <= 1)
        {
            return moves;
        }

        if (check_sq == SQ_NO) check_sq = scan_lsq (checkers);
        // Generates blocking evasions or captures of the checking piece
        Bitboard targets = BETWEEN_SQRS_bb[check_sq][king_sq] + check_sq;

        return WHITE == active ? generate_moves<EVASION, WHITE> (moves, pos, targets) :
               BLACK == active ? generate_moves<EVASION, BLACK> (moves, pos, targets) :
               moves;
    }

    template<>
    // Generates all legal moves.
    ValMove* generate<LEGAL      > (ValMove *moves, const Position &pos)
    {
        ValMove *end = pos.checkers () != U64(0) ?
            generate<EVASION> (moves, pos) :
            generate<RELAX  > (moves, pos);

        Square   king_sq = pos.king_sq (pos.active ());
        Bitboard pinneds = pos.pinneds (pos.active ());

        ValMove *cur = moves;
        while (cur != end)
        {
            Move m = cur->move;
            if (  (ENPASSANT == mtype (m) || pinneds != U64(0) || org_sq (m) == king_sq)
               && !pos.legal (m, pinneds)
               )
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

}
