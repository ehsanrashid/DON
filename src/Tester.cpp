#include "Tester.h"
#include <iostream>

#ifdef _DEBUG
#include "xcstring.h"
#endif
#include "xstring.h"

#include "Castle.h"
#include "BitBoard.h"
#include "BitCount.h"
#include "Position.h"
#include "Zobrist.h"

namespace Tester {

#ifdef _DEBUG
    using namespace std;

    namespace {

        using namespace BitBoard;

        void test_type ()
        {
            ASSERT (CR_W == mk_castle_right (WHITE));
            ASSERT (CR_B == mk_castle_right (BLACK));

            ASSERT (CR_W_K == mk_castle_right (WHITE, CS_K));
            ASSERT (CR_W_Q == mk_castle_right (WHITE, CS_Q));
            ASSERT (CR_B_K == mk_castle_right (BLACK, CS_K));
            ASSERT (CR_B_Q == mk_castle_right (BLACK, CS_Q));

            ASSERT (CR_B_K == ~CR_W_K);
            ASSERT (CR_B_Q == ~CR_W_Q);
            ASSERT (CR_W_K == ~CR_B_K);
            ASSERT (CR_W_Q == ~CR_B_Q);

            ASSERT (CR_B == ~CR_W);
            ASSERT (CR_W == ~CR_B);

            cout << "Type     ...done !!!" << endl;
        }

        void test_bitboard ()
        {

            ASSERT (0x04 == rank_dist (SQ_C2, SQ_E6));
            ASSERT (0x03 == rank_dist (SQ_A4, SQ_G7));

            ASSERT (0x02 == file_dist (SQ_C2, SQ_E6));
            ASSERT (0x06 == file_dist (SQ_A4, SQ_G7));

            ASSERT (0x05 == square_dist (SQ_C3, SQ_H8));
            ASSERT (0x05 == square_dist (SQ_C3, SQ_H8));

            ASSERT (0x09 == taxi_dist (SQ_B2, SQ_F7));
            ASSERT (0x08 == taxi_dist (SQ_G3, SQ_B6));
            ASSERT (0x04 == taxi_dist (SQ_H5, SQ_E4));

            Bitboard b;

            b = 0;
            b = b + SQ_D4 + SQ_H8;
            ASSERT (b == U64 (0x8000000008000000));

            //ASSERT (pop_count<FULL> (U64 (0x0000)) == 0x00);
            //ASSERT (pop_count<FULL> (U64 (0x5555)) == 0x08);
            //ASSERT (pop_count<FULL> (U64 (0xAAAA)) == 0x08);
            //ASSERT (pop_count<FULL> (U64 (0xFFFF)) == 0x10);

            //ASSERT (scan_msq (0x000F) == SQ_D1);
            //ASSERT (scan_msq (0xFFFF) == SQ_H2);

            //ASSERT (CollapsedFILEsIndex (U64 (0x0000000000008143)) == 0xC3);
            //ASSERT (CollapsedFILEsIndex (U64 (0x1080000001000010)) == 0x91);

            //ASSERT (U64 (0x7680562c40030281) == rotate_90C (U64 (0x82150905080D65A0)));
            //ASSERT (U64 (0x8140C002346A016E) == rotate_90A (U64 (0x82150905080D65A0)));
            //ASSERT (U64 (0x0C030125601D818C) == rotate_45C (U64 (0x82150905080D65A0)));
            //ASSERT (U64 (0xAD29050411010EC0) == rotate_45A (U64 (0x82150905080D65A0)));
            //ASSERT (U64 (0x05A6B010A090A841) == rotate_180 (U64 (0x82150905080D65A0)));


            //ASSERT (BitShiftGap (0, F_F) == 0);
            //ASSERT (BitShiftGap (129, F_B) == 1);
            //ASSERT (BitShiftGap (129, F_E) == 3);
            //ASSERT (BitShiftGap (8, F_H) == 4);
            //ASSERT (BitShiftGap (253, F_B) == 1);
            //ASSERT (BitShiftGap (215, F_F) == 1);
            //ASSERT (BitShiftGap (94, F_H) == 1);
            //ASSERT (BitShiftGap (37, F_E) == 1);

            ////ASSERT(getNextSquare(&b) == SQ_NO);
            ////setSquare(b, SQ_H8);
            ////ASSERT(getNextSquare(&b) == SQ_H8);
            ////ASSERT(getNextSquare(&b) == SQ_NO);

            ////ASSERT(IsSquareOn(squaresBehind[ SQ_D4 ][ SQ_C3 ], SQ_E5));
            ////ASSERT(IsSquareOn(squaresBehind[ SQ_D4 ][ SQ_C3 ], SQ_F6));
            ////ASSERT(IsSquareOn(squaresBehind[ SQ_D4 ][ SQ_C3 ], SQ_G7));
            ////ASSERT(IsSquareOn(squaresBehind[ SQ_D4 ][ SQ_C3 ], SQ_H8));
            ////ASSERT(getNumberOfSetSquares(squaresBehind[ SQ_D4 ][ SQ_C3 ]) == 4);

            ////ASSERT(IsSquareOn(squaresBetween[ SQ_B3 ][ SQ_F7 ], SQ_C4));
            ////ASSERT(IsSquareOn(squaresBetween[ SQ_B3 ][ SQ_F7 ], SQ_D5));
            ////ASSERT(IsSquareOn(squaresBetween[ SQ_B3 ][ SQ_F7 ], SQ_E6));
            ////ASSERT(getNumberOfSetSquares(squaresBetween[ SQ_B3 ][ SQ_F7 ]) == 3);

            ////ASSERT(IsSquareOn(squaresInDistance[ 1 ][ SQ_C3 ], SQ_C4));
            ////ASSERT(IsSquareOff(squaresInDistance[ 1 ][ SQ_C3 ], SQ_C5));
            ////ASSERT(IsSquareOn(squaresInDistance[ 3 ][ SQ_E5 ], SQ_E2));
            ////ASSERT(IsSquareOff(squaresInDistance[ 3 ][ SQ_E5 ], SQ_E1));
            ////ASSERT(IsSquareOn(squaresInDistance[ 5 ][ SQ_H8 ], SQ_C3));
            ////ASSERT(IsSquareOff(squaresInDistance[ 5 ][ SQ_H8 ], SQ_B2));

            cout << "Bitboard ...done !!!" << endl;
        }

        void test_attacks ()
        {
            Square   s = SQ_D5;
            Bitboard m = square_bb (s);

            Bitboard attacks;
            uint8_t count;

            // --- KING
            attacks = attacks_bb<KING> (s);
            count = 0;

            if (!(FA_bb & m))
            {
                ASSERT (attacks & (s + DEL_W));
                ++count;
            }
            if (!((FH_bb | R1_bb) & m))
            {
                ASSERT (attacks & (s + DEL_SE));
                ++count;
            }
            if (!(R1_bb & m))
            {
                ASSERT (attacks & (s + DEL_S));
                ++count;
            }
            if (!((FA_bb | R1_bb) & m))
            {
                ASSERT (attacks & (s + DEL_SW));
                ++count;
            }
            if (!(FH_bb & m))
            {
                ASSERT (attacks & (s + DEL_E));
                ++count;
            }
            if (!((FA_bb | R8_bb) & m))
            {
                ASSERT (attacks & (s + DEL_NW));
                ++count;
            }
            if (!(R8_bb & m))
            {
                ASSERT (attacks & (s + DEL_N));
                ++count;
            }
            if (!((FH_bb | R8_bb) & m))
            {
                ASSERT (attacks & (s + DEL_NE));
                ++count;
            }

            ASSERT (pop_count<FULL> (attacks) == count);

            // --- KNIGHT
            attacks = attacks_bb<NIHT> (s);
            count = 0;

            if (!((FH_bb | FG_bb | R1_bb) & m))
            {
                ASSERT (attacks & (s + DEL_EES));
                ++count;
            }
            if (!((FA_bb | FB_bb | R1_bb) & m))
            {
                ASSERT (attacks & (s + DEL_WWS));
                ++count;
            }
            if (!((R1_bb | R2_bb | FH_bb) & m))
            {
                ASSERT (attacks & (s + DEL_SSE));
                ++count;
            }
            if (!((R1_bb | R2_bb | FA_bb) & m))
            {
                ASSERT (attacks & (s + DEL_SSW));
                ++count;
            }
            if (!((FA_bb | FB_bb | R8_bb) & m))
            {
                ASSERT (attacks & (s + DEL_WWN));
                ++count;
            }
            if (!((FH_bb | FG_bb | R8_bb) & m))
            {
                ASSERT (attacks & (s + DEL_EEN));
                ++count;
            }
            if (!((R8_bb | R7_bb | FA_bb) & m))
            {
                ASSERT (attacks & (s + DEL_NNW));
                ++count;
            }
            if (!((R8_bb | R7_bb | FH_bb) & m))
            {
                ASSERT (attacks & (s + DEL_NNE));
                ++count;
            }

            ASSERT (pop_count<FULL> (attacks) == count);

            cout << "Attacks  ...done !!!" << endl;
        }

        void test_fen ()
        {
            const char *fen;
            char buf[MAX_FEN];
            Position pos (int8_t (0));
            Square s;

            fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
            Position::parse (pos, string (fen));
            pos.fen (buf);

            ASSERT (equals (buf, fen));

            ASSERT (pos[SQ_A1] == W_ROOK);
            ASSERT (pos[SQ_B1] == W_NIHT);
            ASSERT (pos[SQ_C1] == W_BSHP);
            ASSERT (pos[SQ_D1] == W_QUEN);
            ASSERT (pos[SQ_E1] == W_KING);
            ASSERT (pos[SQ_F1] == W_BSHP);
            ASSERT (pos[SQ_G1] == W_NIHT);
            ASSERT (pos[SQ_H1] == W_ROOK);
            for (s = SQ_A2; s <= SQ_H2; ++s)
            {
                ASSERT (pos[s] == W_PAWN);
            }
            for (s = SQ_A3; s <= SQ_H6; ++s)
            {
                ASSERT (pos[s] == PS_NO);
            }
            for (s = SQ_A7; s <= SQ_H7; ++s)
            {
                ASSERT (pos[s] == B_PAWN);
            }
            ASSERT (pos[SQ_A8] == B_ROOK);
            ASSERT (pos[SQ_B8] == B_NIHT);
            ASSERT (pos[SQ_C8] == B_BSHP);
            ASSERT (pos[SQ_D8] == B_QUEN);
            ASSERT (pos[SQ_E8] == B_KING);
            ASSERT (pos[SQ_F8] == B_BSHP);
            ASSERT (pos[SQ_G8] == B_NIHT);
            ASSERT (pos[SQ_H8] == B_ROOK);

            ASSERT (pos.active () == WHITE);
            ASSERT (pos.castle_rights () == CR_A);
            ASSERT (pos.en_passant () == SQ_NO);
            ASSERT (pos.clock50 () == 0);
            ASSERT (pos.game_move () == 1);

            // ----

            fen = "rn3rk1/pbppq1pp/1p2pb2/4N2Q/3PN3/3B4/PPP2PPP/R3K2R w KQ - 4 11";
            Position::parse (pos, string (fen));
            pos.fen (buf);

            ASSERT (equals (buf, fen));

            ASSERT (pos[SQ_A1] == W_ROOK);
            ASSERT (pos[SQ_E1] == W_KING);
            ASSERT (pos[SQ_H1] == W_ROOK);
            ASSERT (pos[SQ_A2] == W_PAWN);
            ASSERT (pos[SQ_B2] == W_PAWN);
            ASSERT (pos[SQ_C2] == W_PAWN);
            ASSERT (pos[SQ_F2] == W_PAWN);
            ASSERT (pos[SQ_G2] == W_PAWN);
            ASSERT (pos[SQ_H2] == W_PAWN);
            ASSERT (pos[SQ_D3] == W_BSHP);
            ASSERT (pos[SQ_D4] == W_PAWN);
            ASSERT (pos[SQ_E4] == W_NIHT);
            ASSERT (pos[SQ_E5] == W_NIHT);
            ASSERT (pos[SQ_H5] == W_QUEN);

            ASSERT (pos[SQ_A8] == B_ROOK);
            ASSERT (pos[SQ_B8] == B_NIHT);
            ASSERT (pos[SQ_F8] == B_ROOK);
            ASSERT (pos[SQ_G8] == B_KING);
            ASSERT (pos[SQ_A7] == B_PAWN);
            ASSERT (pos[SQ_B7] == B_BSHP);
            ASSERT (pos[SQ_C7] == B_PAWN);
            ASSERT (pos[SQ_D7] == B_PAWN);
            ASSERT (pos[SQ_E7] == B_QUEN);
            ASSERT (pos[SQ_G7] == B_PAWN);
            ASSERT (pos[SQ_H7] == B_PAWN);
            ASSERT (pos[SQ_B6] == B_PAWN);
            ASSERT (pos[SQ_E6] == B_PAWN);

            ASSERT (pos[SQ_F6] == B_BSHP);

            ASSERT (pos.castle_rights () == CR_W);
            ASSERT (pos.en_passant () == SQ_NO);
            ASSERT (pos.clock50 () == 4);
            ASSERT (pos.game_move () == 11);

            // ----

            fen = "8/8/1R5p/q5pk/PR3pP1/7P/8/7K b - g3 2 10";
            Position::parse (pos, string (fen));
            pos.fen (buf);

            //ASSERT (!equals (buf, fen));
            ASSERT (equals (buf, "8/8/1R5p/q5pk/PR3pP1/7P/8/7K b - g3 0 10"));
            ASSERT (pos.active () == BLACK);
            ASSERT (pos.castle_rights () == CR_NO);
            ASSERT (pos.en_passant () == SQ_G3);
            ASSERT (pos.clock50 () == 0);
            ASSERT (pos.game_move () == 10);

            //----

            fen = "r4r2/3b1pk1/p1p5/4p1p1/1PQbPq1p/P2P4/3RBP1P/2R3K1 w - - 1 25";
            Position::parse (pos, string (fen));
            pos.fen (buf);

            ASSERT (equals (buf, fen));

            ASSERT (pos[SQ_C1] == W_ROOK);
            ASSERT (pos[SQ_G1] == W_KING);
            ASSERT (pos[SQ_D2] == W_ROOK);
            ASSERT (pos[SQ_E2] == W_BSHP);
            ASSERT (pos[SQ_F2] == W_PAWN);
            ASSERT (pos[SQ_H2] == W_PAWN);
            ASSERT (pos[SQ_A3] == W_PAWN);
            ASSERT (pos[SQ_D3] == W_PAWN);
            ASSERT (pos[SQ_B4] == W_PAWN);
            ASSERT (pos[SQ_C4] == W_QUEN);

            ASSERT (pos[SQ_A8] == B_ROOK);
            ASSERT (pos[SQ_F8] == B_ROOK);
            ASSERT (pos[SQ_D7] == B_BSHP);
            ASSERT (pos[SQ_F7] == B_PAWN);
            ASSERT (pos[SQ_G7] == B_KING);
            ASSERT (pos[SQ_A6] == B_PAWN);
            ASSERT (pos[SQ_C6] == B_PAWN);
            ASSERT (pos[SQ_E5] == B_PAWN);
            ASSERT (pos[SQ_G5] == B_PAWN);
            ASSERT (pos[SQ_D4] == B_BSHP);
            ASSERT (pos[SQ_F4] == B_QUEN);
            ASSERT (pos[SQ_H4] == B_PAWN);

            ASSERT (pos.active () == WHITE);
            ASSERT (pos.castle_rights () == CR_NO);
            ASSERT (pos.en_passant () == SQ_NO);
            ASSERT (pos.clock50 () == 1);
            ASSERT (pos.game_move () == 25);

            // ----

            fen = "r1bqr1k1/p1p2ppp/2p5/3p4/2PQn3/1B6/P1P2PPP/R1B2RK1 b - - 3 12";
            Position::parse (pos, string (fen));
            pos.fen (buf);

            ASSERT (equals (buf, fen));

            ASSERT (pos[SQ_A1] == W_ROOK);
            ASSERT (pos[SQ_C1] == W_BSHP);
            ASSERT (pos[SQ_F1] == W_ROOK);
            ASSERT (pos[SQ_G1] == W_KING);
            ASSERT (pos[SQ_A2] == W_PAWN);
            ASSERT (pos[SQ_C2] == W_PAWN);
            ASSERT (pos[SQ_F2] == W_PAWN);
            ASSERT (pos[SQ_G2] == W_PAWN);
            ASSERT (pos[SQ_H2] == W_PAWN);
            ASSERT (pos[SQ_B3] == W_BSHP);
            ASSERT (pos[SQ_C4] == W_PAWN);
            ASSERT (pos[SQ_D4] == W_QUEN);

            ASSERT (pos[SQ_A8] == B_ROOK);
            ASSERT (pos[SQ_C8] == B_BSHP);
            ASSERT (pos[SQ_D8] == B_QUEN);
            ASSERT (pos[SQ_E8] == B_ROOK);
            ASSERT (pos[SQ_G8] == B_KING);
            ASSERT (pos[SQ_A7] == B_PAWN);
            ASSERT (pos[SQ_C7] == B_PAWN);
            ASSERT (pos[SQ_F7] == B_PAWN);
            ASSERT (pos[SQ_G7] == B_PAWN);
            ASSERT (pos[SQ_H7] == B_PAWN);
            ASSERT (pos[SQ_C6] == B_PAWN);
            ASSERT (pos[SQ_D5] == B_PAWN);
            ASSERT (pos[SQ_E4] == B_NIHT);

            ASSERT (pos.active () == BLACK);
            ASSERT (pos.castle_rights () == CR_NO);
            ASSERT (pos.en_passant () == SQ_NO);
            ASSERT (pos.clock50 () == 3);
            ASSERT (pos.game_move () == 12);

            // =========
            // CHESS-960
            // =========

            fen = "rkbnrnqb/pppppppp/8/8/8/8/PPPPPPPP/RKBNRNQB w EAea - 0 1";
            Position::parse (pos, string (fen), NULL, true);
            pos.fen (buf, true);

            ASSERT (equals (buf, fen));

            cout << "FEN      ...done !!!" << endl;

        }

        void test_position ()
        {
            string fen;
            Position pos (int8_t (0));

            //Test pinned position in pinned()
            fen = "8/8/8/8/4n3/1kb5/3R4/4K3 w - - 0 1";
            pos.setup (fen);
            ASSERT (U64 (0x0000000000000800) == pos.pinneds (pos.active ()));

            fen = "8/1q6/8/1k3BR1/p1p4P/8/5K2/8 w - - 0 1";
            pos.setup (fen);
            ASSERT (U64 (0x0000002000000000) == pos.discoverers (pos.active ()));

            cout << "Position ...done !!!" << endl;
        }

        // Test polyglot zobrist
        void test_zobrist ()
        {
            ASSERT ((ZobPG._.mover_side >> 32) == U32 (0xF8D626AA));
            //if ((ZobPG._.mover_side >> 32) != U32(0xF8D626AA))
            //{ // upper half of the hash Color WHITE
            //    exit(EXIT_FAILURE);
            //}

            const char *fen;
            Position pos (int8_t (0));

            fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
            Position::parse (pos, fen);

            ASSERT (Zobrist::MATL_KEY_PG == ZobPG.compute_matl_key (pos));
            ASSERT (Zobrist::PAWN_KEY_PG == ZobPG.compute_pawn_key (pos));
            ASSERT (Zobrist::POSI_KEY_PG == ZobPG.compute_posi_key (pos));
            ASSERT (Zobrist::POSI_KEY_PG == ZobPG.compute_fen_key (fen));

            fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1";
            Position::parse (pos, fen, NULL, true);

            ASSERT (Zobrist::MATL_KEY_PG == ZobPG.compute_matl_key (pos));
            ASSERT (Zobrist::PAWN_KEY_PG == ZobPG.compute_pawn_key (pos));
            ASSERT (Zobrist::POSI_KEY_PG == ZobPG.compute_posi_key (pos));
            ASSERT (Zobrist::POSI_KEY_PG == ZobPG.compute_fen_key (fen, true));

            fen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2";
            Position::parse (pos, fen);

            ASSERT (pos.ok ());
            ASSERT (U64 (0xC1D58449E708A0AD) == ZobPG.compute_matl_key (pos));
            ASSERT (U64 (0x76916F86F34AE5BE) == ZobPG.compute_pawn_key (pos));
            ASSERT (U64 (0x0756B94461C50FB0) == ZobPG.compute_posi_key (pos));
            ASSERT (U64 (0x1BCF67975D7D9F11) == ZobPG.compute_fen_key (fen));

            fen = "8/8/8/8/k1Pp2R1/8/6K1/8 b - c3 0 1";
            Position::parse (pos, fen);

            ASSERT (U64 (0x6EF251F2C474D658) == ZobPG.compute_matl_key (pos));
            ASSERT (U64 (0xB7B954171FD65613) == ZobPG.compute_pawn_key (pos));
            ASSERT (U64 (0xE230E747697ABB10) == ZobPG.compute_posi_key (pos));
            ASSERT (U64 (0xE20A749FDBFAD272) == ZobPG.compute_fen_key (fen));

            cout << "Zobrist  ...done !!!" << endl;
        }

        void test_move ()
        {
            string fen;
            Position pos (int8_t (0));
            StateInfo states[50], *si; 
            Move m;

            fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
            pos.setup (fen);
            si = states;

            ASSERT (U64 (0x463B96181691FC9C) == pos.posi_key ());

            m =  mk_move (SQ_E2, SQ_E4);
            pos.do_move (m, *si++);
            ASSERT (U64 (0x823C9B50FD114196) == pos.posi_key ());

            m =  mk_move (SQ_D7, SQ_D5);
            pos.do_move (m, *si++);
            ASSERT (U64 (0x0756B94461C50FB0) == pos.posi_key ());

            m = mk_move (SQ_E4, SQ_E5);
            pos.do_move (m, *si++);
            ASSERT (U64 (0x662FAFB965DB29D4) == pos.posi_key ());

            m = mk_move (SQ_F7, SQ_F5);
            pos.do_move (m, *si++);
            ASSERT (U64 (0x22A48B5A8E47FF78) == pos.posi_key ());

            m = mk_move (SQ_E1, SQ_E2);
            pos.do_move (m, *si++);
            ASSERT (U64 (0x652A607CA3F242C1) == pos.posi_key ());

            m = mk_move (SQ_E8, SQ_F7);
            pos.do_move (m, *si++);
            ASSERT (U64 (0x00FDD303C946BDD9) == pos.posi_key ());

            pos.undo_move ();
            ASSERT (U64 (0x652A607CA3F242C1) == pos.posi_key ());
            pos.undo_move ();
            ASSERT (U64 (0x22A48B5A8E47FF78) == pos.posi_key ());
            pos.undo_move ();
            ASSERT (U64 (0x662FAFB965DB29D4) == pos.posi_key ());
            pos.undo_move ();
            ASSERT (U64 (0x0756B94461C50FB0) == pos.posi_key ());
            pos.undo_move ();
            ASSERT (U64 (0x823C9B50FD114196) == pos.posi_key ());
            pos.undo_move ();
            ASSERT (U64 (0x463B96181691FC9C) == pos.posi_key ());

            // castling do/undo
            ////"rnbqk2r/pppppppp/8/8/8/8/PPPPPPPP/RNBQK2R w KQkq - 0 1";
            //si = states;
            //m = mk_move<CASTLE> (SQ_E1, SQ_H1);
            //pos.do_move (m, *si++);
            //m = mk_move<CASTLE> (SQ_E8, SQ_H8);
            //pos.do_move (m, *si++);
            //pos.undo_move ();
            //pos.undo_move ();

            fen = "2r1nrk1/p2q1ppp/1p1p4/n1pPp3/P1P1P3/2PBB1N1/4QPPP/R4RK1 w - - 0 1";
            pos.setup (fen);

            for (uint32_t i = 0; i < 50; ++i)
            {
                si = states;

                m = mk_move (SQ_F2, SQ_F4);
                pos.do_move (m, *si++);
                m = mk_move (SQ_A5, SQ_B3);
                pos.do_move (m, *si++);
                m = mk_move (SQ_A1, SQ_A3);
                pos.do_move (m, *si++);
                m = mk_move (SQ_B3, SQ_A5);
                pos.do_move (m, *si++);
                m = mk_move (SQ_G3, SQ_F5);
                pos.do_move (m, *si++);
                m = mk_move (SQ_G8, SQ_H8);
                pos.do_move (m, *si++);
                m = mk_move (SQ_D3, SQ_B1);
                pos.do_move (m, *si++);
                m = mk_move (SQ_D7, SQ_A4);
                pos.do_move (m, *si++);
                m = mk_move (SQ_A3, SQ_A4);
                pos.do_move (m, *si++);
                m = mk_move (SQ_A5, SQ_C4);
                pos.do_move (m, *si++);
                m = mk_move (SQ_E3, SQ_C5);
                pos.do_move (m, *si++);
                m = mk_move (SQ_E8, SQ_C7);
                pos.do_move (m, *si++);

                //cout << pos;

                pos.undo_move ();
                pos.undo_move ();

                m = mk_move (SQ_E2, SQ_C2);
                pos.do_move (m, *si++);
                m = mk_move (SQ_E8, SQ_C7);
                pos.do_move (m, *si++);

                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();
                pos.undo_move ();

                //cout << pos;
            }

            cout << "Move     ...done !!!" << endl;
        }

        void test_uci ()
        {

            cout << "UCI      ...done !!!" << endl;
        }

    }


    void main_test ()
    {
        test_type ();

        test_bitboard ();

        test_attacks ();

        test_fen ();

        test_position ();

        test_zobrist ();

        test_move ();

        test_uci ();
    }

#endif

}
