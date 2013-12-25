#include "MoveGenerator.h"
#include "Position.h"
#include "BitCount.h"
#include "BitScan.h"

namespace MoveGenerator {

    using namespace std;
    using namespace BitBoard;

#undef SERIALIZE
#undef SERIALIZE_PAWNS

    // Fill moves in the list for any piece using a very common while loop, no fancy.
#define SERIALIZE(mov_lst, org, moves)         while (moves) { mov_lst.emplace_back (mk_move<NORMAL> ((org), pop_lsq (moves))); }
    // Fill moves in the list for pawns, where the 'delta' is the distance b/w 'org' and 'dst' square.
#define SERIALIZE_PAWNS(mov_lst, delta, moves) while (moves) { Square dst = pop_lsq (moves); mov_lst.emplace_back (mk_move<NORMAL> (dst - (delta), dst)); }

    namespace {

#pragma region Move Generators

        template<GType G, PType PT>
        // Move Generator for PIECE
        struct Generator
        {

        public:
            // Generates piece common move
            static inline void generate (MoveList &mov_lst, const Position &pos, Color c, Bitboard target, const CheckInfo *ci = NULL)
                //template<GType G, PType PT>
                //inline void Generator<G, PT>::generate (MoveList &mov_lst, const Position &pos, Color c, Bitboard target, const CheckInfo *ci)
            {
                //ASSERT ((KING != PT) && (PAWN != PT));
                static_assert ((KING != PT) && (PAWN != PT), "PT must not be KING & PAWN");

                Bitboard occ = pos.pieces ();
                const Square *pl = pos.piece_list<PT>(c);
                Square org;
                while ((org = *pl++) != SQ_NO)
                {
                    if ((CHECK == G) || (QUIET_CHECK == G))
                    {
                        if (ci)
                        {
                            if ((BSHP == PT) || (ROOK == PT) || (QUEN == PT) &&
                                !(attacks_bb<PT> (org) & target & ci->checking_bb[PT]))
                            {
                                continue;
                            }
                            if (UNLIKELY (ci->check_discovers) && (ci->check_discovers & org))
                            {
                                continue;
                            }
                        }
                    }
                    // TODO:: Remove if check
                    if (org < 0) continue;

                    Bitboard moves = attacks_bb<PT> (org, occ) & target;
                    if ((CHECK == G) || (QUIET_CHECK == G))
                    {
                        if (ci) moves &= ci->checking_bb[PT];
                    }

                    SERIALIZE (mov_lst, org, moves);
                }
            }

        };

        template<GType G>
        // Move Generator for KING
        struct Generator<G, KING>
        {

        public:
            // Generates KING common move
            static inline void generate (MoveList &mov_lst, const Position &pos, Color clr, Bitboard target, const CheckInfo *ci = NULL)
                //template<GType G>
                //void Generator<G, KING>::generate (MoveList &mov_lst, const Position &pos, Color clr, Bitboard target, const CheckInfo *ci)
            {
                //static_assert ((EVASION != G), "G must not be EVASION");
                if ((EVASION != G))
                {
                    if ((CHECK != G) && (QUIET_CHECK != G))
                    {
                        Square fk_sq   = pos.king_sq (clr);
                        Bitboard moves = attacks_bb<KING> (fk_sq) & target;
                        SERIALIZE (mov_lst, fk_sq, moves);
                    }

                    if ((CAPTURE != G))
                    {
                        if (!pos.castle_impeded (clr) && pos.can_castle (clr) && !pos.checkers ())
                        {
                            if (!pos.castle_impeded (clr, CS_K) && pos.can_castle (clr, CS_K))
                            {
                                pos.chess960 () ?
                                    generate_castling<CS_K,  true> (mov_lst, pos, clr, ci) :
                                    generate_castling<CS_K, false> (mov_lst, pos, clr, ci);
                            }
                            if (!pos.castle_impeded (clr, CS_Q) && pos.can_castle (clr, CS_Q))
                            {
                                pos.chess960 () ?
                                    generate_castling<CS_Q,  true> (mov_lst, pos, clr, ci) :
                                    generate_castling<CS_Q, false> (mov_lst, pos, clr, ci);
                            }
                        }
                    }
                }
            }

            template<CSide SIDE, bool CHESS960>
            // Generates KING castling move
            static inline void generate_castling (MoveList &mov_lst, const Position &pos, Color clr, const CheckInfo *ci /*= NULL*/)
                //template<GType G>
                //template<CSide SIDE, bool CHESS960>
                //void Generator<G, KING>::generate_castling (MoveList &mov_lst, const Position &pos, Color clr, const CheckInfo *ci)
            {
                //static_assert ((EVASION != G) && (CHECK != G), "G must not be EVASION & CHECK");

                ASSERT (!pos.castle_impeded (clr, SIDE));
                ASSERT (pos.can_castle (clr, SIDE));
                ASSERT (!pos.checkers ());

                if (pos.castle_impeded (clr, SIDE) || !pos.can_castle (clr, SIDE) || pos.checkers ()) return;

                Square org_king = pos.king_sq (clr);
                Square org_rook = pos.castle_rook (clr, SIDE);
                Square dst_king = rel_sq (clr, (CS_Q == SIDE) ? SQ_WK_Q : SQ_WK_K);

                Bitboard enemies = pos.pieces (~clr);

                Delta step = CHESS960 ? 
                    dst_king < org_king ? DEL_W : DEL_E :
                    (CS_Q == SIDE) ? DEL_W : DEL_E;

                for (Square s = org_king + step; s != dst_king; s += step)
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

                switch (G)
                {
                case CHECK:
                case QUIET_CHECK:
                    if (ci)
                    {
                        if (pos.check (m, *ci))
                        {
                            mov_lst.emplace_back (m);
                        }
                    }
                    break;

                default:
                    mov_lst.emplace_back (m);
                    break;
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
            static inline void generate (MoveList &mov_lst, const Position &pos, Bitboard target, const CheckInfo *ci = NULL)
                //template<GType G>
                //template<Color C>
                //void Generator<G, PAWN>::generate<> (MoveList &mov_lst, const Position &pos, Bitboard target, const CheckInfo *ci)
            {
                const Color C_   = ((WHITE == C) ? BLACK : WHITE);
                const Delta PUSH = ((WHITE == C) ? DEL_N  : DEL_S);
                const Delta RCAP = ((WHITE == C) ? DEL_NE : DEL_SW);
                const Delta LCAP = ((WHITE == C) ? DEL_NW : DEL_SE);

                Bitboard bbRR8 = rel_rank_bb (C, R_8);
                Bitboard bbRR7 = rel_rank_bb (C, R_7);
                Bitboard bbRR3 = rel_rank_bb (C, R_3);

                Bitboard pawns = pos.pieces (C, PAWN);
                Bitboard pawns_on_R7 = pawns &  bbRR7;
                Bitboard pawns_on_Rx = pawns & ~bbRR7;
                Bitboard occ = pos.pieces ();

                Bitboard enemies;
                switch (G)
                {
                case EVASION: enemies = pos.pieces (C_) & target; break;
                case CAPTURE: enemies = target;                   break;
                default:      enemies = pos.pieces (C_);          break;
                }

                Bitboard empty = U64 (0);
                // Pawn single-push and double-push, no promotions
                if ((CAPTURE != G))
                {
                    empty = ((QUIET == G) || (QUIET_CHECK == G) ? target : ~occ);

                    Bitboard push_1 = shift_del<PUSH> (pawns_on_Rx) & empty;
                    Bitboard push_2 = shift_del<PUSH> (push_1 & bbRR3) & empty;

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
                            Bitboard attack = attacks_bb<PAWN> (C_, ci->king_sq);

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
                                Bitboard push_cd_1 = shift_del<PUSH>(pawns_chk_dis) & empty;
                                Bitboard push_cd_2 = shift_del<PUSH>(push_cd_1 & bbRR3) & empty;

                                push_1 |= push_cd_1;
                                push_2 |= push_cd_2;
                            }
                        }
                        break;
                    }

                    SERIALIZE_PAWNS (mov_lst, Delta (PUSH << 0), push_1);
                    SERIALIZE_PAWNS (mov_lst, Delta (PUSH << 1), push_2);
                }
                // Pawn normal and en-passant captures, no promotions
                if ((QUIET != G) && (QUIET_CHECK != G))
                {
                    Bitboard attacksL = shift_del<LCAP> (pawns_on_Rx) & enemies;
                    Bitboard attacksR = shift_del<RCAP> (pawns_on_Rx) & enemies;;

                    SERIALIZE_PAWNS (mov_lst, LCAP, attacksL);
                    SERIALIZE_PAWNS (mov_lst, RCAP, attacksR);

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
                            if ((EVASION != G) || (target & (ep_sq - PUSH)))
                            {
                                Bitboard pawns_ep = attacks_bb<PAWN>(C_, ep_sq) & pawns_on_R5;
                                ASSERT (pawns_ep);
                                ASSERT (pop_count<FULL> (pawns_ep) <= 2);

                                while (pawns_ep)
                                {
                                    mov_lst.emplace_back (mk_move<ENPASSANT> (pop_lsq (pawns_ep), ep_sq));
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

                        //if ((CAPTURE != G))
                        {
                            generate_promotion<PUSH> (mov_lst, pawns_on_R7, empty, ci);
                        }

                        if ((QUIET != G) && (QUIET_CHECK != G))
                        {
                            generate_promotion<LCAP> (mov_lst, pawns_on_R7, enemies, ci);
                            generate_promotion<RCAP> (mov_lst, pawns_on_R7, enemies, ci);
                        }
                    }
                }
            }

            template<Delta D>
            // Generates PAWN promotion move
            static inline void generate_promotion (MoveList &mov_lst, Bitboard pawns_on_R7, Bitboard target, const CheckInfo *ci /*= NULL*/)
                //template<GType G>
                //template<Delta D>
                //void Generator<G, PAWN>::generate_promotion (MoveList &mov_lst, Bitboard pawns_on_R7, Bitboard target, const CheckInfo *ci)
            {
                static_assert ((DEL_NE == D || DEL_NW == D || DEL_SE == D || DEL_SW == D || DEL_N == D || DEL_S == D), "D may be wrong");

                //if (pawns_on_R7)
                {
                    Bitboard promotes = shift_del<D> (pawns_on_R7) & target;
                    while (promotes)
                    {
                        Square dst = pop_lsq (promotes);
                        Square org = dst - D;

                        if ( (RELAX == G) || (EVASION == G) ||
                            ((CAPTURE == G) && (DEL_NE == D || DEL_NW == D || DEL_SE == D || DEL_SW == D)))
                        {
                            mov_lst.emplace_back (mk_move<PROMOTE> (org, dst, QUEN));
                        }

                        if ( (RELAX == G) || (EVASION == G) ||
                            ((QUIET == G) && (DEL_N == D || DEL_S == D)))
                        {
                            mov_lst.emplace_back (mk_move<PROMOTE> (org, dst, ROOK));
                            mov_lst.emplace_back (mk_move<PROMOTE> (org, dst, BSHP));
                            mov_lst.emplace_back (mk_move<PROMOTE> (org, dst, NIHT));
                        }

                        // Knight-promotion is the only one that can give a direct check
                        // not already included in the queen-promotion (queening).
                        if ((CHECK == G) || (QUIET_CHECK == G))
                        {
                            if (ci && attacks_bb<NIHT> (dst) & ci->king_sq)
                            {
                                mov_lst.emplace_back (mk_move<PROMOTE> (org, dst, NIHT));
                            }
                        }
                        else
                        {
                            ci; // silence a warning under MSVC
                        }
                    }
                }
            }

        };

#pragma endregion

        template<Color C, GType G>
        // Generates all pseudo-legal moves of color for target.
        inline void generate_moves (MoveList &mov_lst, const Position &pos, Bitboard target, const CheckInfo *ci = NULL)
        {
            Generator<G, PAWN>::generate<C> (mov_lst, pos, target, ci);
            Generator<G, NIHT>::generate (mov_lst, pos, C, target, ci);
            Generator<G, BSHP>::generate (mov_lst, pos, C, target, ci);
            Generator<G, ROOK>::generate (mov_lst, pos, C, target, ci);
            Generator<G, QUEN>::generate (mov_lst, pos, C, target, ci);

            if ((EVASION != G))
            {
                Generator<G, KING>::generate (mov_lst, pos, C, target, ci);
            }
        }

        inline void filter_illegal (MoveList &mov_lst, const Position &pos)
        {
            Square fk_sq     = pos.king_sq (pos.active ());
            Bitboard pinneds = pos.pinneds (pos.active ());

            MoveList::iterator itr = mov_lst.begin ();
            while (itr != mov_lst.end ())
            {
                Move m = *itr;
                if (((org_sq (m) == fk_sq) || pinneds || (ENPASSANT == m_type (m))) &&
                    !pos.legal (m, pinneds))
                {
                    itr = mov_lst.erase (itr);
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
        //ASSERT (RELAX == G || CAPTURE == G || QUIET == G);
        static_assert (RELAX == G || CAPTURE == G || QUIET == G, "G must be RELAX | CAPTURE | QUIET");
        ASSERT (!pos.checkers ());

        MoveList mov_lst;

        Color active = pos.active ();

        Bitboard target = U64 (0);
        switch (G)
        {
        case CAPTURE: target = pos.pieces (~active); break;

        case QUIET  : target = pos.empties ();       break;

        case RELAX  : target = ~pos.pieces (active); break;
        }

        switch (active)
        {
        case WHITE: generate_moves<WHITE, G> (mov_lst, pos, target); break;
        case BLACK: generate_moves<BLACK, G> (mov_lst, pos, target); break;
        }

        return mov_lst;
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

        MoveList mov_lst;
        Color active = pos.active ();
        Bitboard occ = pos.pieces ();
        Bitboard empty = ~occ;

        CheckInfo ci (pos);
        Bitboard discovers = ci.check_discovers & ~pos.pieces (active, PAWN);
        while (discovers)
        {
            Square org = pop_lsq (discovers);
            PType type = p_type (pos[org]);

            Bitboard moves = U64 (0);
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

            SERIALIZE (mov_lst, org, moves);
        }

        switch (active)
        {
        case WHITE: generate_moves<WHITE, QUIET_CHECK> (mov_lst, pos, empty, &ci); break;
        case BLACK: generate_moves<BLACK, QUIET_CHECK> (mov_lst, pos, empty, &ci); break;
        }

        return mov_lst;
    }

    template<>
    // Generates all pseudo-legal check giving moves.
    MoveList generate<CHECK>       (const Position &pos)
    {
        MoveList mov_lst;

        Color active = pos.active ();
        Bitboard occ = pos.pieces ();
        Bitboard target = ~pos.pieces (active);

        CheckInfo ci (pos);
        Bitboard discovers = ci.check_discovers & ~pos.pieces (active, PAWN);
        while (discovers)
        {
            Square org = pop_lsq (discovers);
            PType type = p_type (pos[org]);

            Bitboard moves = U64 (0);
            switch (type)
            {
            case PAWN: continue; // Will be generated together with direct checks
            case NIHT: moves = attacks_bb<NIHT> (org)      & target; break;
            case BSHP: moves = attacks_bb<BSHP> (org, occ) & target; break;
            case ROOK: moves = attacks_bb<ROOK> (org, occ) & target; break;
            case QUEN: moves = attacks_bb<QUEN> (org, occ) & target; break;
            case KING: moves = attacks_bb<KING> (org)      & target &
                           ~attacks_bb<QUEN> (ci.king_sq);           break;
            }

            SERIALIZE (mov_lst, org, moves);
        }

        switch (active)
        {
        case WHITE: generate_moves<WHITE, CHECK> (mov_lst, pos, target, &ci); break;
        case BLACK: generate_moves<BLACK, CHECK> (mov_lst, pos, target, &ci); break;
        }

        return mov_lst;
    }

    template<>
    // Generates all pseudo-legal check evasions moves when the side to move is in check.
    MoveList generate<EVASION>     (const Position &pos)
    {
        MoveList mov_lst;

        Color active = pos.active ();
        Bitboard checkers = pos.checkers ();
        ASSERT (checkers); // If any checker exists

        Square fk_sq     = pos.king_sq (active);
        Bitboard friends = pos.pieces (active);
        
        Square check_sq;
        int32_t checker_count = 0;
        
        //// Generates evasions for king, capture and non-capture moves excluding friends
        //Bitboard moves = attacks_bb<KING> (fk_sq) & ~friends;
        //check_sq = pop_lsq (checkers);
        //checker_count = pop_count<MAX15>(checkers);
        //
        //Bitboard enemies = pos.pieces (~active);
        //Bitboard mocc    = pos.pieces () - fk_sq;
        //// Remove squares attacked by enemies, from the king evasions.
        //// so to skip known illegal moves avoiding useless legality check later.
        //for (uint32_t k = 0; _deltas_type[KING][k]; ++k)
        //{
        //    Square sq = fk_sq + _deltas_type[KING][k];
        //    if (_ok (sq))
        //    {
        //        if ((moves & sq) && (pos.attackers_to (sq, mocc) & enemies))
        //        {
        //            moves -= sq;
        //        }
        //    }
        //}

        Bitboard slid_attacks = 0;
        // Find squares attacked by slider checkers, we will remove them from the king
        // evasions so to skip known illegal moves avoiding useless legality check later.
        do
        {
            ++checker_count;
            check_sq = pop_lsq (checkers);

            ASSERT (p_color (pos[check_sq]) == ~active);

            if (p_type (pos[check_sq]) > NIHT) // A slider
            {
                slid_attacks |= _lines_sq_bb[check_sq][fk_sq] - check_sq;
            }
        }
        while (checkers);

        // Generate evasions for king, capture and non capture moves
        Bitboard moves = attacks_bb<KING> (fk_sq) & ~friends & ~slid_attacks;

        SERIALIZE (mov_lst, fk_sq, moves);

        // if Double check, then only a king move can save the day
        if ((1 == checker_count) && pop_count<FULL> (friends) > 1)
        {
            // Generates blocking evasions or captures of the checking piece
            Bitboard target = betwen_sq_bb (check_sq, fk_sq) + check_sq;
            switch (active)
            {
            case WHITE: generate_moves<WHITE, EVASION> (mov_lst, pos, target); break;
            case BLACK: generate_moves<BLACK, EVASION> (mov_lst, pos, target); break;
            }
        }

        return mov_lst;
    }

    template<>
    // Generates all legal moves.
    MoveList generate<LEGAL>       (const Position &pos)
    {
        MoveList mov_lst = pos.checkers () ?
            generate<EVASION> (pos) :
            generate<RELAX  > (pos);

        filter_illegal (mov_lst, pos);

        return mov_lst;
    }

#pragma endregion

#undef SERIALIZE
#undef SERIALIZE_PAWNS

}
