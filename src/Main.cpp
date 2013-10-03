#include <iostream>
#include <iomanip>
#include <sstream>

#include "xcstring.h"
#include "xstring.h"
#include "Type.h"
#include "BitBoard.h"
#include "Position.h"
#include "Transposition.h"
#include "Tester.h"
#include "Zobrist.h"
#include "Engine.h"
#include "PolyglotBook.h"
#include "MoveGenerator.h"
#include "io_logger.h"
#include "tri_logger.h"
#include "Time.h"

using namespace std;
using namespace BitBoard;
using namespace MoveGenerator;


namespace {

    ::std::string string_args (size_t argc, const char* const argv[])
    {
        ::std::string args;
        for (size_t i = 1; i < argc; ++i)
        {
            args += args.empty () ? ::std::string (argv[i]) : " " + ::std::string (argv[i]);
        }
        return args;
    }

    void initialize_IO ()
    {
        //size_t size_buf = 0;
        //char *buffer = NULL;
        //setvbuf (stdin, buffer, (buffer) ? _IOFBF : _IONBF, size_buf);
        //setvbuf (stdout, buffer, (buffer) ? _IOFBF : _IONBF, size_buf); // _IOLBF breaks on Windows!

        //cout.unsetf(ios_base::dec);
        //::std::cout.setf (std::ios_base::boolalpha);
        ::std::cout.setf (
            //    ::std::ios_base::showpos |
            //    ::std::ios_base::boolalpha |
            ::std::ios_base::hex |
            //    ::std::ios_base::uppercase |
            //    ::std::ios_base::fixed |
            //    //::std::ios_base::showpoint |
            ::std::ios_base::unitbuf);
        ::std::cout.precision (2);
    }

    void print_fill_hex (Key key)
    {
        ::std::cout.width (16);
        ::std::cout.fill ('0');
        ::std::cout << key << ::std::endl;
    }

    void print_move (Move m)
    {
        Square org = sq_org (m);
        Square dst = sq_dst (m);
        ::std::cout << org << ":" << dst;
        if (PROMOTE == _mtype (m)) ::std::cout << " " << to_char (prom_type (m));
    }
}


int main (int argc, const char* const argv[])
{
    //std::string args = string_args (argc, argv);

    //Engine::start ();
    
    //TRI_LOG_ON ();
    //TRI_LOG_MSG ("hello");

    log_io (true);

    cout << Time::now() << endl;

    //cout << "hello" << endl;

    //Bitboard bb = 
    //    U64 (0x0067560000000000);
    //    // (0x00000000675600);
    //print (bb);
    ////cout << (int) pop_count<FULL> (bb) << endl;
    ////cout << (int) scan_lsb (bb) << endl;
    ////cout << (int) scan_msb (bb) << endl;
    //
    //log_io (false);

    //TranspositionTable tt;
    //tt.resize (1);

    //Key key         = 893333454547544;
    //Move move       = Move (345);
    //Depth depth     = DEPTH_NONE;
    //Bound bound     = EXACT;
    //Score score     = SCORE_INFINITE;
    //uint64_t nodes  = 40;
    //tt.store (key, move, depth, bound, score, nodes);
    //
    //const TranspositionEntry *te = tt.retrieve (key);
    //tt.clear();

    //cout << te->key();
    //cout << (int)++te;

    //initialize_IO        ();
    //std::cout.setf (ios_base::boolalpha);
    //std::cout.unsetf (ios_base::dec);
    //std::cout.setf (ios_base::hex | ios_base::uppercase);


    //PolyglotBook pg ("Book.bin", ios_base::in);

    //Position pos (FEN_N);
    ////cout << pos <<endl;

    ////pg.find_index (KEY_POSI);
    //cout << pg.probe_move (pos);

    

    cout << "-------------------" << endl;

    //"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w  KQkq  - 3  5";
    string fen = "2r1nrk1/p2q1ppp/1p1p4/n1pPp3/P1P1P3/2PBB1N1/4QPPP/R4RK1 w - - 0 1";

        //"r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14";
        //"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11";
    //"6k1/2R3p1/8/4K2r/3P3b/2b5/6N1/8 w - - 0 1";
    //"1Q6/3q4/1R5p/q5pk/P4pP1/7P/8/3bB2K b - g3 0 10";
    //"8/8/1R5p/q5pk/P4pP1/7P/8/3B3K b - g3 0 10";
    //"8/8/1R5p/q5pk/P4pP1/7P/8/4B2K b - g3 0 10";
    //"8/8/1R5p/q5p1/P4pPk/7P/8/4B2K b - g3 0 10";
    //"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQK2R w KQkq - 0 1";
    //"2k5/1r6/P3n1r1/8/4B3/8/8/1K1Q3r w - - 0 0";
    //"1rk5/8/4n3/5B2/1N6/8/8/1K1Q4 w - - 0 0";
    //"1rk5/8/4n3/5B2/1N6/8/8/1K1Q4 w - - 0 0";
    FEN_N; //"1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - -";
    //"r1b1k3/p4p2/1p1pq2n/2n1p1rp/1RP2P2/N5bN/PP3Q1P/2B1K1BR w Kq - 0 1";
    //"rnb1kbnr/pppppppp/8/5P2/2B1q3/8/PPPP2PP/RN1QKBNR w KQkq - 0 1";
    //"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR   w   KQkq  - 3  5";
    //"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 4 5";
    //"rnbqkbnr/pppppppp/8/8/1P2P1P1/1N3N2/2P2PP1/R1BQKB1R w KQkq - 0 1";

    //Position pos;
    //Move m;
    //StateInfoStack stk_si;

    //pos.setup (fen);
    //
    //cout << pos << endl;

    //m = mk_move (SQ_B6, SQ_B1);
    //m = mk_move (SQ_F2, SQ_F4);
    //stk_si.push (StateInfo ());
    //pos.do_move (m, stk_si.top ());

    //cout << pos << endl;
    //m = mk_move (SQ_A5, SQ_B3);
    //stk_si.push (StateInfo ());
    //pos.do_move (m, stk_si.top ());

    //cout << pos << endl;
    //m = mk_move (SQ_A1, SQ_A3);
    //stk_si.push (StateInfo ());
    //pos.do_move (m, stk_si.top ());

    //cout << pos << endl;
    //m = mk_move (SQ_B3, SQ_A5);
    //stk_si.push (StateInfo ());
    //pos.do_move (m, stk_si.top ());

    //cout << pos << endl;
    //m = mk_move (SQ_G3, SQ_F5);
    //stk_si.push (StateInfo ());
    //pos.do_move (m, stk_si.top ());

    //cout << pos << endl;
    //m = mk_move (SQ_G8, SQ_H8);
    //stk_si.push (StateInfo ());
    //pos.do_move (m, stk_si.top ());
    //
    //cout << pos << endl;

    //cout << "=========================" << endl;
    //
    //pos.undo_move ();
    //stk_si.pop ();
    //cout << pos << endl;
    //pos.undo_move ();
    //stk_si.pop ();
    //cout << pos << endl;
    //pos.undo_move ();
    //stk_si.pop ();
    //cout << pos << endl;
    //pos.undo_move ();
    //stk_si.pop ();
    //cout << pos << endl;
    //pos.undo_move ();
    //stk_si.pop ();
    //cout << pos << endl;
    //pos.undo_move ();
    //stk_si.pop ();
    //cout << pos << endl;

    //MoveList ml = generate <QUIET> (pos);
        //generate <RELAX> (pos);
        //generate<EVASION> (pos);
        //generate<LEGAL> (pos);

    //cout << ml;

    
    //print_move (m);
    //cout << endl;
    //cout << move_to_can (m) << endl;

    //std::string sm = "e2e4";
    //cout << (int) move_from_can (sm, pos) << endl;

    Engine::stop ();

    //atexit(report_leak());
    system ("PAUSE");
    return EXIT_SUCCESS;
}




