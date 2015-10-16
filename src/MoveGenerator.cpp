#include "MoveGenerator.h"

namespace MoveGen {

    using namespace std;
    using namespace BitBoard;

    namespace {

        template<GenT GT, Color Own, PieceT PT>
        // Move Generator for PIECE
        struct Generator
        {
        private:
            Generator () = delete;

        public:
            // Generates piece common move
            static void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = nullptr)
            {
                assert (KING != PT && PAWN != PT);

                const auto *pl = pos.squares<PT> (Own);
                Square s;
                while ((s = *pl++) != SQ_NO)
                {
                    if ((CHECK == GT || QUIET_CHECK == GT) && ci != nullptr)
                    {
                        if (    (BSHP == PT || ROOK == PT || QUEN == PT)
                            && !(PIECE_ATTACKS[PT][s] & targets & ci->checking_bb[PT])
                           )
                        {
                            continue;
                        }
                        if (ci->discoverers != U64(0) && (ci->discoverers & s) != U64(0))
                        {
                            continue;
                        }
                    }

                    auto attacks = attacks_bb<PT> (s, pos.pieces ()) & targets;
                    if ((CHECK == GT || QUIET_CHECK == GT) && ci != nullptr)
                    {
                        attacks &= ci->checking_bb[PT];
                    }

                    while (attacks != U64(0)) { *moves++ = mk_move<NORMAL> (s, pop_lsq (attacks)); }
                }
            }

        };

        template<GenT GT, Color Own>
        // Move Generator for KING
        struct Generator<GT, Own, KING>
        {
        private:
            Generator () = delete;

            template<CRight CR, bool Chess960>
            // Generates KING castling move
            static void generate_castling (ValMove *&moves, const Position &pos, const CheckInfo *ci)
            {
                assert (EVASION != GT);
                assert (!pos.castle_impeded (CR) && pos.can_castle (CR) && pos.checkers () == U64(0));

                const auto Opp = WHITE == Own ? BLACK : WHITE;

                auto king_org = pos.square<KING> (Own);
                auto rook_org = pos.castle_rook (CR);

                assert (ROOK == ptype (pos[rook_org]));

                auto king_dst = rel_sq (Own, CR == CR_WK || CR == CR_BK ? SQ_G1 : SQ_C1);
                auto step = king_dst > king_org ? DEL_E : DEL_W;
                for (auto s = king_dst; s != king_org; s -= step)
                {
                    if (pos.attackers_to (s, Opp) != U64(0)) return;
                }

                if (Chess960)
                {
                    // Because generate only legal castling moves needed to verify that
                    // when moving the castling rook do not discover some hidden checker.
                    // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
                    if ((attacks_bb<ROOK> (king_dst, pos.pieces () - rook_org) & pos.pieces (Opp, ROOK, QUEN)) != U64(0)) return;
                }

                auto m = mk_move<CASTLE> (king_org, rook_org);

                if (CHECK == GT || QUIET_CHECK == GT)
                {
                    if (ci != nullptr && !pos.gives_check (m, *ci)) return;
                }
                else
                {
                    (void) ci; // Silence a warning under MSVC
                }

                *moves++ = m;
            }

        public:
            // template<GenT GT, Color Own>
            // void Generator<GT, Own, KING>::generate()
            // Generates KING common move
            static void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = nullptr)
            {
                const auto Opp = WHITE == Own ? BLACK : WHITE;

                if (EVASION == GT) return;

                if (CHECK != GT && QUIET_CHECK != GT)
                {
                    auto king_sq = pos.square<KING> (Own);
                    auto attacks = PIECE_ATTACKS[KING][king_sq] & ~PIECE_ATTACKS[KING][pos.square<KING> (Opp)] & targets;
                    while (attacks != U64(0)) { *moves++ = mk_move<NORMAL> (king_sq, pop_lsq (attacks)); }
                }

                if (CAPTURE != GT && pos.can_castle (Own) && pos.checkers () == U64(0))
                {
                    CheckInfo cc;
                    if (ci == nullptr) { cc = CheckInfo (pos); ci = &cc; }

                    if (pos.can_castle (Castling<Own, CS_KING>::Right) && !pos.castle_impeded (Castling<Own, CS_KING>::Right))
                    {
                        pos.chess960 () ?
                            generate_castling<Castling<Own, CS_KING>::Right,  true> (moves, pos, ci) :
                            generate_castling<Castling<Own, CS_KING>::Right, false> (moves, pos, ci);
                    }

                    if (pos.can_castle (Castling<Own, CS_QUEN>::Right) && !pos.castle_impeded (Castling<Own, CS_QUEN>::Right))
                    {
                        pos.chess960 () ?
                            generate_castling<Castling<Own, CS_QUEN>::Right,  true> (moves, pos, ci) :
                            generate_castling<Castling<Own, CS_QUEN>::Right, false> (moves, pos, ci);
                    }
                }
            }

        };

        template<GenT GT, Color Own>
        // Move Generator for PAWN
        struct Generator<GT, Own, PAWN>
        {
        private:
            Generator () = delete;

            template<Delta Del>
            // Generates PAWN promotion move
            static void generate_promotion (ValMove *&moves, Square dst, const CheckInfo *ci)
            {
                assert ((DEL_NE == Del || DEL_NW == Del || DEL_SE == Del || DEL_SW == Del || DEL_N == Del || DEL_S == Del));

                if (RELAX == GT || EVASION == GT || CAPTURE == GT)
                {
                    *moves++ = mk_move<PROMOTE> (dst - Del, dst, QUEN);
                }

                if (RELAX == GT || EVASION == GT || QUIET == GT)
                {
                    *moves++ = mk_move<PROMOTE> (dst - Del, dst, ROOK);
                    *moves++ = mk_move<PROMOTE> (dst - Del, dst, BSHP);
                    *moves++ = mk_move<PROMOTE> (dst - Del, dst, NIHT);
                }

                // Knight-promotion is the only one that can give a direct check
                // not already included in the queen-promotion (queening).
                if (QUIET_CHECK == GT)
                {
                    if (ci != nullptr && (PIECE_ATTACKS[NIHT][dst] & ci->king_sq))
                    {
                        *moves++ = mk_move<PROMOTE> (dst - Del, dst, NIHT);
                    }
                }
                //else
                //if (CHECK == GT && ci != nullptr)
                //{
                //    if (PIECE_ATTACKS[NIHT][dst]        & ci->king_sq) *moves++ = mk_move<PROMOTE> (dst - Del, dst, NIHT);
                //    if (attacks_bb<BSHP> (dst, targets) & ci->king_sq) *moves++ = mk_move<PROMOTE> (dst - Del, dst, BSHP);
                //    if (attacks_bb<ROOK> (dst, targets) & ci->king_sq) *moves++ = mk_move<PROMOTE> (dst - Del, dst, ROOK);
                //    if (attacks_bb<QUEN> (dst, targets) & ci->king_sq) *moves++ = mk_move<PROMOTE> (dst - Del, dst, QUEN);
                //}
                else
                {
                    (void) ci; // Silence a warning under MSVC
                }
            }

        public:
            // Generates PAWN common move
            static void generate (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = nullptr)
            {
                const auto Opp   = WHITE == Own ? BLACK  : WHITE;
                const auto Push  = WHITE == Own ? DEL_N  : DEL_S;
                const auto Right = WHITE == Own ? DEL_NE : DEL_SW;
                const auto Left  = WHITE == Own ? DEL_NW : DEL_SE;

                auto R7_pawns = pos.pieces (Own, PAWN) &  rel_rank_bb (Own, R_7);
                auto Rx_pawns = pos.pieces (Own, PAWN) & ~rel_rank_bb (Own, R_7);

                auto enemies = EVASION == GT ? pos.pieces (Opp) & targets :
                               CAPTURE == GT ? targets : pos.pieces (Opp);

                auto empties = U64(0);
                // Pawn single-push and double-push, no promotions
                if (CAPTURE != GT)
                {
                    empties = QUIET == GT || QUIET_CHECK == GT ? targets : ~pos.pieces ();
                    
                    auto push_1 = empties & shift_bb<Push> (Rx_pawns);
                    auto push_2 = empties & shift_bb<Push> (push_1 & rel_rank_bb (Own, R_3));

                    switch (GT)
                    {
                    case EVASION:
                        // only blocking squares are important
                        push_1 &= targets;
                        push_2 &= targets;
                        break;

                    case CHECK:
                    case QUIET_CHECK:
                        if (ci != nullptr)
                        {
                            push_1 &= PAWN_ATTACKS[Opp][ci->king_sq];
                            push_2 &= PAWN_ATTACKS[Opp][ci->king_sq];

                            // Pawns which give discovered check
                            // Add pawn pushes which give discovered check. This is possible only
                            // if the pawn is not on the same file as the enemy king, because
                            // don't generate captures. Note that a possible discovery check
                            // promotion has been already generated among captures.
                            if ((Rx_pawns & ci->discoverers) != U64(0))
                            {
                                auto push_cd_1 = empties & shift_bb<Push> (Rx_pawns & ci->discoverers);
                                auto push_cd_2 = empties & shift_bb<Push> (push_cd_1 & rel_rank_bb (Own, R_3));

                                push_1 |= push_cd_1;
                                push_2 |= push_cd_2;
                            }
                        }
                        break;

                    default: break;
                    }
                    
                    while (push_1 != U64(0)) { auto dst = pop_lsq (push_1); *moves++ = mk_move<NORMAL> (dst - Push, dst); }
                    while (push_2 != U64(0)) { auto dst = pop_lsq (push_2); *moves++ = mk_move<NORMAL> (dst - Push-Push, dst); }
                }
                // Pawn normal and en-passant captures, no promotions
                if (RELAX == GT || CAPTURE == GT || EVASION == GT)
                {
                    auto l_attacks = enemies & shift_bb<Left > (Rx_pawns);
                    auto r_attacks = enemies & shift_bb<Right> (Rx_pawns);

                    while (l_attacks != U64(0)) { auto dst = pop_lsq (l_attacks); *moves++ = mk_move<NORMAL> (dst - Left , dst); }
                    while (r_attacks != U64(0)) { auto dst = pop_lsq (r_attacks); *moves++ = mk_move<NORMAL> (dst - Right, dst); }

                    auto ep_sq = pos.en_passant_sq ();
                    if (SQ_NO != ep_sq)
                    {
                        assert (_rank (ep_sq) == rel_rank (Own, R_6));
                        if ((Rx_pawns & rel_rank_bb (Own, R_5)) != U64(0))
                        {
                            // An en-passant capture can be an evasion only if the checking piece
                            // is the double pushed pawn and so is in the target. Otherwise this
                            // is a discovery check and are forced to do otherwise.
                            // All time except when EVASION then 2nd condition must true
                            if (EVASION != GT || (targets & (ep_sq - Push)) != U64(0))
                            {
                                auto ep_attacks = PAWN_ATTACKS[Opp][ep_sq] & Rx_pawns & rel_rank_bb (Own, R_5);
                                assert (ep_attacks != U64(0));
                                assert (pop_count<MAX15> (ep_attacks) <= 2);

                                while (ep_attacks != U64(0)) { *moves++ = mk_move<ENPASSANT> (pop_lsq (ep_attacks), ep_sq); }
                            }
                        }
                    }
                }

                // Promotions (queening and under-promotions)
                if (R7_pawns != U64(0))
                {
                    // All time except when EVASION then 2nd condition must true
                    if (EVASION != GT || (targets & rel_rank_bb (Own, R_8)) != U64(0))
                    {
                        empties = EVASION == GT ? empties & targets :
                                  CAPTURE == GT ? ~pos.pieces () : empties;

                        // Promoting pawns
                        Bitboard b;
                        b = shift_bb<Push > (R7_pawns) & empties;
                        while (b != U64(0)) generate_promotion<Push > (moves, pop_lsq (b), ci);

                        b = shift_bb<Right> (R7_pawns) & enemies;
                        while (b != U64(0)) generate_promotion<Right> (moves, pop_lsq (b), ci);

                        b = shift_bb<Left > (R7_pawns) & enemies;
                        while (b != U64(0)) generate_promotion<Left > (moves, pop_lsq (b), ci);
                    }
                }
            }

        };

        template<GenT GT, Color Own>
        // Generates all pseudo-legal moves of color for targets.
        ValMove* generate_moves (ValMove *&moves, const Position &pos, Bitboard targets, const CheckInfo *ci = nullptr)
        {
            Generator<GT, Own, PAWN>::generate (moves, pos, targets, ci);
            /*if (pos.count<NIHT> (Own) !=0)*/ Generator<GT, Own, NIHT>::generate (moves, pos, targets, ci);
            /*if (pos.count<BSHP> (Own) !=0)*/ Generator<GT, Own, BSHP>::generate (moves, pos, targets, ci);
            /*if (pos.count<ROOK> (Own) !=0)*/ Generator<GT, Own, ROOK>::generate (moves, pos, targets, ci);
            /*if (pos.count<QUEN> (Own) !=0)*/ Generator<GT, Own, QUEN>::generate (moves, pos, targets, ci);
            Generator<GT, Own, KING>::generate (moves, pos, targets, ci);

            return moves;
        }

    }

    template<GenT GT>
    // Generates all pseudo-legal moves.
    ValMove* generate (ValMove *moves, const Position &pos)
    {
        assert (RELAX == GT || CAPTURE == GT || QUIET == GT);
        assert (pos.checkers () == U64(0));

        auto active  = pos.active ();
        auto targets = 
            RELAX   == GT ? ~pos.pieces ( active) :
            CAPTURE == GT ?  pos.pieces (~active) :
            QUIET   == GT ? ~pos.pieces () :
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

        auto active =  pos.active ();
        auto targets= ~pos.pieces ();
        CheckInfo ci (pos);
        // Pawns excluded will be generated together with direct checks
        auto discovers = ci.discoverers & ~pos.pieces (active, PAWN);
        while (discovers != U64(0))
        {
            auto org = pop_lsq (discovers);
            auto pt  = ptype (pos[org]);
            auto attacks = attacks_bb (Piece(pt), org, pos.pieces ()) & targets;

            if (KING == pt) attacks &= ~PIECE_ATTACKS[QUEN][ci.king_sq];

            while (attacks != U64(0)) { *moves++ = mk_move<NORMAL> (org, pop_lsq (attacks)); }
        }

        return WHITE == active ? generate_moves<QUIET_CHECK, WHITE> (moves, pos, targets, &ci) :
               BLACK == active ? generate_moves<QUIET_CHECK, BLACK> (moves, pos, targets, &ci) :
               moves;
    }

    template<>
    // Generates all pseudo-legal check giving moves.
    // Returns a pointer to the end of the move list.
    ValMove* generate<CHECK      > (ValMove *moves, const Position &pos)
    {
        auto active =  pos.active ();
        auto targets= ~pos.pieces (active);
        CheckInfo ci (pos);
        // Pawns excluded, will be generated together with direct checks
        auto discovers = ci.discoverers & ~pos.pieces (active, PAWN);
        while (discovers != U64(0))
        {
            auto org = pop_lsq (discovers);
            auto pt  = ptype (pos[org]);
            auto attacks = attacks_bb (Piece(pt), org, pos.pieces ()) & targets;

            if (KING == pt) attacks &= ~PIECE_ATTACKS[QUEN][ci.king_sq];

            while (attacks != U64(0)) { *moves++ = mk_move<NORMAL> (org, pop_lsq (attacks)); }
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
        auto checkers = pos.checkers ();
        assert (checkers != U64(0)); // If any checker exists

        auto active  = pos.active ();
        auto king_sq = pos.square<KING> (active);

        auto check_sq = SQ_NO;

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
        //    auto sq = king_sq + PIECE_DELTAS[KING][k];
        //    if (_ok (sq))
        //    {
        //        if ((attacks & sq) && pos.attackers_to (sq, ~active, mocc))
        //        {
        //            attacks -= sq;
        //        }
        //    }
        //}

        auto slider_attacks = U64(0);
        auto sliders = checkers & ~pos.pieces (NIHT, PAWN);
        // Find squares attacked by slider checkers, will remove them from the king
        // evasions so to skip known illegal moves avoiding useless legality check later.
        while (sliders != U64(0))
        {
            check_sq = pop_lsq (sliders);
            assert (color (pos[check_sq]) == ~active);
            slider_attacks |= RAYLINE_bb[check_sq][king_sq] - check_sq;
        }

        // Generate evasions for king, capture and non capture moves
        auto attacks =
              PIECE_ATTACKS[KING][king_sq]
            & ~(  pos.pieces (active)
                | slider_attacks
                | PIECE_ATTACKS[KING][pos.square<KING> (~active)]
               );

        while (attacks != U64(0)) { *moves++ = mk_move<NORMAL> (king_sq, pop_lsq (attacks)); }

        // If double-check, then only a king move can save the day, triple+ check not possible
        if (more_than_one (checkers) || pos.count<NONE> (active) <= 1) return moves;

        check_sq = SQ_NO == check_sq ? scan_lsq (checkers) : check_sq;
        // Generates blocking evasions or captures of the checking piece
        auto targets = BETWEEN_bb[check_sq][king_sq] + check_sq;

        return WHITE == active ? generate_moves<EVASION, WHITE> (moves, pos, targets) :
               BLACK == active ? generate_moves<EVASION, BLACK> (moves, pos, targets) :
               moves;
    }

    template<>
    // Generates all legal moves.
    ValMove* generate<LEGAL      > (ValMove *moves, const Position &pos)
    {
        auto *moves_cur = moves;
        auto *moves_end = pos.checkers () != U64(0) ?
                            generate<EVASION> (moves, pos) :
                            generate<RELAX  > (moves, pos);

        auto king_sq = pos.square<KING> (pos.active ());
        auto pinneds = pos.pinneds (pos.active ());
        while (moves_cur != moves_end)
        {
            if (   (   pinneds != U64(0)
                    || org_sq (*moves_cur) == king_sq
                    || mtype (*moves_cur) == ENPASSANT
                   )
                && !pos.legal (*moves_cur, pinneds)
               )
            {
                *moves_cur = *(--moves_end);
                continue;
            }
            ++moves_cur;
        }

        return moves_end;
    }

}
