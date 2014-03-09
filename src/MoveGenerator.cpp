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
#define SERIALIZE(m_list, org, moves)         while (moves) { (m_list++)->move = mk_move<NORMAL> (org, pop_lsq (moves)); }
    // Fill moves in the list for pawns, where the 'delta' is the distance b/w 'org' and 'dst' square.
#define SERIALIZE_PAWNS(m_list, delta, moves) while (moves) { Square dst = pop_lsq (moves); (m_list++)->move = mk_move<NORMAL> (dst - delta, dst); }

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
            static INLINE void generate (ValMove *&m_list, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                static_assert (KING != PT && PAWN != PT, "PT must not be 'KING | PAWN'");

                Bitboard occ = pos.pieces ();

                const Square *pl = pos.list<PT> (C);
                Square s;
                while ((s = *pl++) != SQ_NO)
                {
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci)
                        {
                            if (   (BSHP == PT || ROOK == PT || QUEN == PT)
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

                    Bitboard moves = attacks_bb<PT> (s, occ) & targets;
                    if (CHECK == GT || QUIET_CHECK == GT)
                    {
                        if (ci) moves &= ci->checking_bb[PT];
                    }

                    SERIALIZE (m_list, s, moves);
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
            static INLINE void generate_castling (ValMove *&m_list, const Position &pos, const CheckInfo *ci /*= NULL*/)
            {
                //static_assert (EVASION != GT, "GT must not be EVASION");
                ASSERT (!pos.castle_impeded (CR) && pos.can_castle (CR) && !pos.checkers ());
                if (pos.castle_impeded (CR) || !pos.can_castle (CR) || pos.checkers ())
                {
                    return;
                }
                const bool KingSide = (CR == CR_W_K || CR == CR_B_K);
                const Color C_  = ((WHITE == C) ? BLACK : WHITE);

                Square org_king = pos.king_sq (C);
                Square org_rook = pos.castle_rook (CR);
                if (ROOK != _ptype (pos[org_rook])) return;

                Square dst_king = rel_sq (C, KingSide ? SQ_WK_K : SQ_WK_Q);

                Bitboard enemies = pos.pieces (C_);

                Delta step = CHESS960 ? 
                    (dst_king < org_king ? DEL_E : DEL_W) :
                    (KingSide            ? DEL_W : DEL_E);

                for (Square s = dst_king; s != org_king; s += step)
                {
                    if (pos.attackers_to (s) & enemies)
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

                (m_list++)->move = m;
            }

        public:
            // template<GenT GT, Color C>
            // void Generator<GT, C, KING>::generate()
            // Generates KING common move
            static INLINE void generate (ValMove *&m_list, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                //static_assert (EVASION != GT, "GT must not be EVASION");
                if (EVASION == GT) return;

                if (CHECK != GT && QUIET_CHECK != GT)
                {
                    const Color C_ = ((WHITE == C) ? BLACK : WHITE);
                    Square king_sq = pos.king_sq (C);
                    Bitboard moves = PieceAttacks[KING][king_sq] & ~PieceAttacks[KING][pos.king_sq (C_)] & targets;
                    SERIALIZE (m_list, king_sq, moves);
                }

                if (CAPTURE != GT)
                {
                    if (pos.can_castle (C) && !pos.checkers ())
                    {
                        CheckInfo cc;
                        if (!ci)
                        {
                            cc = CheckInfo (pos);
                            ci = &cc;
                        }

                        if (!pos.castle_impeded (MakeCastling<C,  CS_K>::right) && pos.can_castle (MakeCastling<C,  CS_K>::right))
                        {
                            pos.chess960 () ?
                                generate_castling<MakeCastling<C,  CS_K>::right,  true> (m_list, pos, ci) :
                                generate_castling<MakeCastling<C,  CS_K>::right, false> (m_list, pos, ci);
                        }

                        if (!pos.castle_impeded (MakeCastling<C,  CS_Q>::right) && pos.can_castle (MakeCastling<C,  CS_Q>::right))
                        {
                            pos.chess960 () ?
                                generate_castling<MakeCastling<C,  CS_Q>::right,  true> (m_list, pos, ci) :
                                generate_castling<MakeCastling<C,  CS_Q>::right, false> (m_list, pos, ci);
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
                            if (PieceAttacks[NIHT][dst] & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                        }

                        if (CHECK == GT)
                        {
                            if (ci)
                            {
                                //if (PieceAttacks[NIHT][dst] & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, NIHT);
                                if (attacks_bb<BSHP> (dst, targets) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, BSHP);
                                if (attacks_bb<ROOK> (dst, targets) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, ROOK);
                                if (attacks_bb<QUEN> (dst, targets) & ci->king_sq) (m_list++)->move = mk_move<PROMOTE> (org, dst, QUEN);
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
            static INLINE void generate (ValMove *&m_list, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
            {
                const Color C_   = ((WHITE == C) ? BLACK : WHITE);
                const Delta PUSH = ((WHITE == C) ? DEL_N  : DEL_S);
                const Delta RCAP = ((WHITE == C) ? DEL_NE : DEL_SW);
                const Delta LCAP = ((WHITE == C) ? DEL_NW : DEL_SE);

                Bitboard RR7_bb = rel_rank_bb (C, R_7);
                Bitboard RR3_bb = rel_rank_bb (C, R_3);

                Bitboard pawns = pos.pieces<PAWN> (C);
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

                    Bitboard push_1 = shift_del<PUSH> (pawns_on_Rx   ) & empties;
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
                            if (pawns_chk_dis)
                            {
                                Bitboard push_cd_1 = shift_del<PUSH> (pawns_chk_dis    ) & empties;
                                Bitboard push_cd_2 = shift_del<PUSH> (push_cd_1 & RR3_bb) & empties;

                                push_1 |= push_cd_1;
                                push_2 |= push_cd_2;
                            }
                        }
                        break;

                    default: break;
                    }

                    SERIALIZE_PAWNS (m_list, Delta (PUSH << 0), push_1);
                    SERIALIZE_PAWNS (m_list, Delta (PUSH << 1), push_2);
                }
                // Pawn normal and en-passant captures, no promotions
                if (QUIET != GT && QUIET_CHECK != GT)
                {
                    Bitboard l_attacks = shift_del<LCAP> (pawns_on_Rx) & enemies;
                    Bitboard r_attacks = shift_del<RCAP> (pawns_on_Rx) & enemies;;

                    SERIALIZE_PAWNS (m_list, LCAP, l_attacks);
                    SERIALIZE_PAWNS (m_list, RCAP, r_attacks);

                    Square ep_sq = pos.en_passant_sq ();
                    if (SQ_NO != ep_sq)
                    {
                        ASSERT (_rank (ep_sq) == rel_rank (C, R_6));
                        // RR5_bb
                        Bitboard pawns_on_R5 = pawns_on_Rx & rel_rank_bb (C, R_5);
                        if (pawns_on_R5)
                        {
                            // An en-passant capture can be an evasion only if the checking piece
                            // is the double pushed pawn and so is in the target. Otherwise this
                            // is a discovery check and we are forced to do otherwise.
                            // All time except when EVASION then 2nd condition must true
                            if (EVASION != GT || (targets & (ep_sq - PUSH)))
                            {
                                Bitboard pawns_ep = PawnAttacks[C_][ep_sq] & pawns_on_R5;
                                ASSERT (pawns_ep);
                                ASSERT (pop_count<MAX15> (pawns_ep) <= 2);

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
                    Bitboard RR8_bb = rel_rank_bb (C, R_8);
                    
                    // All time except when EVASION then 2nd condition must true
                    if (EVASION != GT || (targets & RR8_bb))
                    {
                        if      (CAPTURE == GT) empties = ~pos.pieces ();
                        else if (EVASION == GT) empties &= targets;

                        generate_promotion<LCAP> (m_list, pawns_on_R7, enemies, ci);
                        generate_promotion<RCAP> (m_list, pawns_on_R7, enemies, ci);
                        generate_promotion<PUSH> (m_list, pawns_on_R7, empties, ci);
                    }
                }
            }

        };

        template<GenT GT, Color C>
        // Generates all pseudo-legal moves of color for targets.
        INLINE ValMove* generate_moves (ValMove *&m_list, const Position &pos, Bitboard targets, const CheckInfo *ci = NULL)
        {
            Generator<GT, C, PAWN>::generate (m_list, pos, targets, ci);
            Generator<GT, C, NIHT>::generate (m_list, pos, targets, ci);
            Generator<GT, C, BSHP>::generate (m_list, pos, targets, ci);
            Generator<GT, C, ROOK>::generate (m_list, pos, targets, ci);
            Generator<GT, C, QUEN>::generate (m_list, pos, targets, ci);
            Generator<GT, C, KING>::generate (m_list, pos, targets, ci);

            return m_list;
        }

        //INLINE void filter_illegal (ValMove *beg, ValMove *&end, const Position &pos)
        //{
        //    Square king_sq = pos.king_sq (pos.active ());
        //    Bitboard pinneds = pos.pinneds (pos.active ());
        //
        //    //m_list.erase (
        //    //    remove_if (m_list.begin (), m_list.end (), [&] (Move m)
        //    //{
        //    //    return ((org_sq (m) == king_sq) || pinneds || (ENPASSANT == mtype (m))) && !pos.legal (m, pinneds); 
        //    //}), m_list.end ());
        //
        //    while (beg != end)
        //    {
        //        Move m = beg->move;
        //        if (   ((org_sq (m) == king_sq) || pinneds || (ENPASSANT == mtype (m)))
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
    ValMove* generate (ValMove *m_list, const Position &pos)
    {
        //ASSERT (RELAX == GT || CAPTURE == GT || QUIET == GT);
        static_assert (RELAX == GT || CAPTURE == GT || QUIET == GT, "GT must be 'RELAX | CAPTURE | QUIET'");
        ASSERT (!pos.checkers ());

        Color active    = pos.active ();

        Bitboard targets = CAPTURE == GT ?  pos.pieces (~active)
            :              QUIET   == GT ? ~pos.pieces ()
            :              RELAX   == GT ? ~pos.pieces (active)
            :              U64 (0);

        return WHITE == active ? generate_moves<GT, WHITE> (m_list, pos, targets)
            :  BLACK == active ? generate_moves<GT, BLACK> (m_list, pos, targets)
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
        Bitboard occ    = pos.pieces ();
        Bitboard empties= ~occ;
        CheckInfo ci (pos);
        // Pawns excluded will be generated together with direct checks
        Bitboard discovers = ci.discoverers & ~pos.pieces<PAWN> (active);
        while (discovers)
        {
            Square org = pop_lsq (discovers);
            PieceT pt  = _ptype (pos[org]);
            Bitboard moves = attacks_bb (Piece (pt), org, occ) & empties;

            if (KING == pt) moves &= ~PieceAttacks[QUEN][ci.king_sq];

            SERIALIZE (m_list, org, moves);
        }

        return WHITE == active ? generate_moves<QUIET_CHECK, WHITE> (m_list, pos, empties, &ci)
            :  BLACK == active ? generate_moves<QUIET_CHECK, BLACK> (m_list, pos, empties, &ci)
            :  m_list;
    }

    template<>
    // Generates all pseudo-legal check giving moves.
    // Returns a pointer to the end of the move list.
    ValMove* generate<CHECK>       (ValMove *m_list, const Position &pos)
    {
        Color active    = pos.active ();
        Bitboard occ    = pos.pieces ();
        Bitboard targets= ~pos.pieces (active);
        CheckInfo ci (pos);
        // Pawns excluded, will be generated together with direct checks
        Bitboard discovers = ci.discoverers & ~pos.pieces<PAWN> (active);
        while (discovers)
        {
            Square org = pop_lsq (discovers);
            PieceT pt  = _ptype (pos[org]);
            Bitboard moves = attacks_bb (Piece (pt), org, occ) & targets;

            if (KING == pt) moves &= ~PieceAttacks[QUEN][ci.king_sq];

            SERIALIZE (m_list, org, moves);
        }

        return WHITE == active ? generate_moves<CHECK, WHITE> (m_list, pos, targets, &ci)
            :  BLACK == active ? generate_moves<CHECK, BLACK> (m_list, pos, targets, &ci)
            :  m_list;
    }

    template<>
    // Generates all pseudo-legal check evasions moves when the side to move is in check.
    // Returns a pointer to the end of the move list.
    ValMove* generate<EVASION>     (ValMove *m_list, const Position &pos)
    {
        Bitboard checkers = pos.checkers ();
        ASSERT (checkers); // If any checker exists

        Color active = pos.active ();

        Square  king_sq = pos.king_sq (active);
        Square check_sq;

        //// Generates evasions for king, capture and non-capture moves excluding friends
        //Bitboard moves = PieceAttacks[KING][king_sq] & ~pos.pieces (active);
        //check_sq = pop_lsq (checkers);
        //
        //Bitboard enemies = pos.pieces (~active);
        //Bitboard mocc    = pos.pieces () - king_sq;
        //// Remove squares attacked by enemies, from the king evasions.
        //// so to skip known illegal moves avoiding useless legality check later.
        //for (uint8_t k = 0; PieceDeltas[KING][k]; ++k)
        //{
        //    Square sq = king_sq + PieceDeltas[KING][k];
        //    if (_ok (sq))
        //    {
        //        if ((moves & sq) && (pos.attackers_to (sq, mocc) & enemies))
        //        {
        //            moves -= sq;
        //        }
        //    }
        //}

        check_sq = SQ_NO;
        Bitboard slid_attacks = U64 (0);
        Bitboard sliders = checkers & ~(pos.pieces (NIHT, PAWN));
        // Find squares attacked by slider checkers, we will remove them from the king
        // evasions so to skip known illegal moves avoiding useless legality check later.
        while (sliders)
        {
            check_sq = pop_lsq (sliders);

            ASSERT (_color (pos[check_sq]) == ~active);
            slid_attacks |= LineRay_bb[check_sq][king_sq] - check_sq;
        }

        // Generate evasions for king, capture and non capture moves
        Bitboard moves =  PieceAttacks[KING][king_sq]
            & ~(pos.pieces (active) | PieceAttacks[KING][pos.king_sq (~active)] | slid_attacks);

        SERIALIZE (m_list, king_sq, moves);

        // If double check, then only a king move can save the day
        if (more_than_one (checkers) || pos.count (active) == 1)
        {
            return m_list;
        }

        if (check_sq == SQ_NO) check_sq = scan_lsq (checkers);
        // Generates blocking evasions or captures of the checking piece
        Bitboard targets = Between_bb[check_sq][king_sq] + check_sq;

        return WHITE == active ? generate_moves<EVASION, WHITE> (m_list, pos, targets)
            :  BLACK == active ? generate_moves<EVASION, BLACK> (m_list, pos, targets)
            :  m_list;
    }

    template<>
    // Generates all legal moves.
    ValMove* generate<LEGAL>       (ValMove *m_list, const Position &pos)
    {
        ValMove *end = pos.checkers ()
            ? generate<EVASION> (m_list, pos)
            : generate<RELAX  > (m_list, pos);

        Square   king_sq = pos.king_sq (pos.active ());
        Bitboard pinneds = pos.pinneds (pos.active ());

        ValMove *cur = m_list;
        while (cur != end)
        {
            Move m = cur->move;
            if (  ((org_sq (m) == king_sq) || pinneds || ENPASSANT == mtype (m))
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
