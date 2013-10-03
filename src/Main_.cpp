#include <iostream>
#include <iomanip>
#include <string>

#include "xcstring.h"
#include "xstring.h"

#include "FEN.h"
#include "Attack.h"
//#include "UCI.h"
#include "ECO.h"
#include "Forest.h"
#include "Tree.h"
//#include "LeakDetector.h"
#include "BitBoard.h"
//#include "PGN.h"
#include "MoveGenerator.h"
#include "Book.h"
#include "Tester.h"
//#include "Logger.h"

//#include "TieBuf.h"

//#include "io_logger.h"

//#include <regex>

using namespace std;
//using namespace UCI;
//using namespace FEN;
using namespace BitBoard;
using namespace Attack;
using namespace MoveGenerator;
////using namespace Searcher;

static string ArgumentString (const int argc, const char* const argv[]);

static void InitializeIO ();

void f ()
{
    //const char *fen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w  KQkq  - 3  5";
    string fen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR   w   KQkq  - 3  5";

    Position pos (int8_t (0));
    getECO();
    //char buf[100];
    ////
    //FEN::Set (pos, fen);
    //cout << FEN::Get (pos) << endl;
    //cout << pos;

    //Tester::MainTest ();
    
    //Book b("ss");
    //b.SIZE_BENTRY;
    //cout << (int)Book::SIZE_BENTRY;
    //int_least8_t

    //ASSERT_MSG (1==2, "hello 2344 55");
    //Bitboard bb = 4444443;
    //cout << ToBBString(bb);
    //cout << Generate<LEGAL> (pos).size();

    //int * ptr = (int*) malloc(10);
    
    //char *s = "0x5A20FE309410205A";
    //char r[100];
    //char *p = r;
    //int32_t radix  = 16;
    //Bitboard bb = _strtoui64 (s, &p, radix);
    //cout << bb;

    //cout << BitBoard::ToBitString(5, 6) << " =" << 5;
    //cout << _I64_MIN;
    //free(ptr);


    //if (sub)
    //{
    //    cout << sub << "." << endl;
    //    //erase(sub);
    //}
    //string s = "abcfgh.exe/mhj";
    //string s = "abcfgh\\ss.exe";
    //cout << remove(s, 'g');

    //cout << extract_dir(s) << endl;
    //cout << extract_fn (s) << endl;
    //cout << change_extension(s, ".txt") << endl;
    //cout << (int) find_fn_sep(s) << endl;
}

int _main (const int argc, const char* const argv[])
{
    //string args = ArgumentString(argc, argv);

    //InitializeIO        ();

    InitializeDistance ();
    InitializeBitboard ();
    InitializeAttacks ();

    //InitializeZobRand   ();
    //InitializeOptions   ();

    //std::unique_ptr<int> d;
    //std::shared_ptr<int> d;
    //std::default_delete<int> d;

    //ofstream file("io_log.txt", ifstream::out | ifstream::app);
    //TieBuf t(cout.rdbuf(), &file);
    //io_logger log;
    //log.Start();

    //Bitboard occ = 7777854454342250;
    //Print(occ);
    //Print(AttacksPiece<PT_Q> (SQ_H5, occ));


    f ();

    //string fen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w  KQkq  d6 0  2";
    //Position pos;
    //Setup(pos, fen);

    //Tester::MainTest();


    //Position pos(FEN_N);
    //string fen = //"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2";
    //    "8/8/8/8/k1Pp2R1/8/6K1/8 b - c3 0 1";
    //Position pos(fen);

    //cout.setf(ios_base::hex, ios_base::basefield);
    ////cout.setf(ios_base::showbase);
    //cout.setf(ios_base::uppercase);
    //cout.width(16);
    //cout.fill('0');

    //cout << ZobGlob.KeyMatl(pos) << endl;
    //cout << ZobGlob.KeyPawn(pos) << endl;
    //cout << ZobGlob.KeyPosi (pos) << endl;
    //cout << ZobGlob.KeyFEN (fen) << endl;

    ////cout.fill(' ');
    ////cout.width(0);

    //Thread<void>::Create(f);
    //cout << sizeof (TposEntry) << endl;
    //cout << sizeof (TposCluster) << endl;

    //Position pos(FEN_N);
    //Position pos("1r1b1n2/3k1pp1/p1pP2q1/1p2B2p/1P2P1P1/P2Q3P/1N2K3/5R2 w - - 0 1");
    //Position pos("1r1b1n2/3k2pp/p1pP2q1/1p2B2p/1PNQP1P1/P6P/4K3/5R2 w - - 0 1"); // QUET CHECK
    //Position pos("3r1rk1/p1n4p/1pP1ppp1/2q1b1B1/2P5/6P1/P1Q2PBP/1R3RK1 w - - 0 1"); // Pinned
    //Position pos("r5k1/6pp/5p2/3N4/8/1B6/P1P5/3K4 w - - 0 15"); // DIscover
    //Position pos("rnbqr1k1/pp1p1ppp/8/3PB3/1P6/PQb3P1/1P1KNPBP/2R4R w - - 0 1"); // EVASION
    //Position pos("8/1q2k3/6p1/r7/7p/1P2B3/P1P1R3/1K6 w - - 0 1"); // DBl CHECK Discover
    //Position pos("rn1r4/pbR2Pk1/1p5p/3nP3/3B4/P4P2/1P2N1P1/4KB1R w K - 0 1"); // Discover an promotion check for quite pos
    //Position pos("5k1b/7P/3K1P1P/8/1B6/p7/P7/8 w - - 0 1"); // king move give discover check
    //Position pos("5rb1/1q6/nN6/4k3/7Q/B2K4/8/6R1 w - - 0 0"); // ONLY CHECK
    //Position pos("3r2b1/1q6/n7/5k2/7Q/8/2N5/1B2K2R w K - 0 1"); // ONLY CHECK WITH MT_CASTLE and discover
    //Position pos("3r1rk1/2n5/4bpp1/2q5/2P5/8/4PPP1/5BK1 w - - 0 1"); // legal
    //Position pos("r1b2rk1/2q1b1pp/p2ppn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1 b - - 0 1");
    //Position pos("4k3/5q2/8/8/8/5N2/3p1K2/1N6 w - - 0 1");
    //Position pos("rnbqrkr1/ppppp1pp/8/8/8/3B4/PPPPP1PP/RNBQK2R w KQ - 0 1"); // castle CHECK mate
    //Position pos("6k1/5q2/8/8/8/1N3N2/5K2/1N3N2 w - - 0 1"); // KNIGHT + bishop AMB

    //Position pos("6k1/8/8/8/2p5/1P6/8/6K1 w - - 0 0");
    //Position pos("rnb1qknr/pppp2pp/3b4/8/8/3BP2N/PPPP3P/RNBQK2R w KQkq - 3 10"); //castle CHECK

    //Position pos("rnb1qknr/pppp2pp/3b4/8/8/3BP2N/PPPP3P/RNBQK2R w KQ - 0 1"); //for only check
    //Position pos("k7/8/5n2/8/7Q/8/1R6/5K2 w - - 0 5");
    //Position pos("rnb1qknr/ppppb1pp/8/8/8/3BP2N/PPPP3P/RNBQK2R w KQ - 0 1");// for checkmate

    //Position pos("8/8/6k1/1Ppp4/2P5/8/4K3/8 w - c6 0 1");
    ////Position p = pos;
    //Position pos("8/8/8/8/k1Pp2R1/8/6K1/8 b - c3 0 0");
    //Position pos("8/8/8/8/k1Pp2R1/8/6K1/8 b - c3 0 1");

    //cout << pos;
    //MoveList lstMove = Generate<RELAX> (pos);
    //print(lstMove);
    //cout << find_max(lstMove);
    //cout << pos;
    //cout << String(pos);
    //cout << Hash(pos);
    //cout << FEN_N;
    //Position pos;
    //Setup(pos, FEN_N);
    //cout << pos;

    //pos.Flip();

    //cout << pos;

    //// cout<<pos;                                   
    ////Depth depth = (Depth) 3;                      
    ////                                              
    ////                                             
    //Move m = 
    //    _Move<MT_NORMAL> (SQ_C4, SQ_D5);
    ////    _Move<MT_CASTLE> (SQ_E1, SQ_C1);
    //      //_Move<MT_ENPASANT> (SQ_C4, SQ_D5);
    ////   _Move<MT_PROMOTE> (SQ_A5, SQ_A4);

    ////cout << pos._SAN(m);

    //if (pos.IsMovePseudoLegal(m))
    //{
    //    cout << "Pseudo-Legal" << endl;
    //}

    //if (pos.IsMoveLegal(m, Pinneds(pos)))
    //{
    //    cout << "Legal" << endl;
    //}

    //MoveList lstMove = Generate<LEGAL> (pos);
    //MoveList lstMove = Generate<EVASION> (pos);
    //MoveList lstMove = Generate<> (pos);


    //cout << lstMove;

    //cout<<"\nCheck Mate:"<<IsCheckMate(m,pos,CheckInfo(pos));
    //cout<<"\nCheck:"<<IsCheck(m,pos, CheckInfo(pos));


    //cout << pos;

    //Generate<CHECK> (pos);
    //Move m = (Move) 16646;
    //IsLegal(m, pos, Pinneds(pos));

    //Generate<EVASION> (pos);
    //MoveList lstMove = Generate<LEGAL> (pos);
    //cout << lstMove;
    //Move m = _Move<MT_NORMAL> (SQ_B1, SQ_D2);
    //Ambiguity(m, pos);
    //cout << IsLegal(m, pos, Pinneds(pos));

    //cout << pos._SAN(m);

    //Bitboard occ   = U64(0x2849110294a00);
    //Bitboard pawns = U64(0x41010204a00);

    //Print(occ);
    //Print(pawns);

    //Print(PushablePawns<WHITE> (pawns, occ));
    //Print(DblPushablePawns<BLACK> (pawns, occ));
    //Print(PawnsPushes<WHITE> (pawns, occ));

    //MoveGenerator<RELAX, PT_K>::Generate(pos, WHITE, 0, 0);
    //Generate<RELAX, CS_K> (pos, WHITE);
    //Generate<RELAX, FRONT> (pos, WHITE, 0);

    //cout << pos;

    //StartUCI(ArgumentString(argc, argv));

    //try
    //{
    //    Tree<int> t(9);
    //    t.appendBranch(4);
    //    t.appendBranch(5);
    //    t.appendBranch(7);
    //    int a = 3;
    //    t[0].appendBranch(a);
    //    t[0].appendBranch(6);
    //    t[0].appendBranch(7);
    //    t[0][0].appendBranch(2);
    //    t[0][1].appendBranch(1);
    //    t[1].appendBranch(8);
    //    //Tree<int> *tt = new Tree<int> (10);
    //    //tt->appendBranch(2);

    //    //t.appendBranch(tt);
    //    //t.appendBranch(tt);
    //    //t.removeBranch(tt);
    //    //t.removeBranch(5);
    //    cout << endl << t << endl;
    //    //cout << *tt;
    //    //delete tt;
    //    //

    //    Forest<int> f;
    //    f.appendTree(new Tree<int> (5));
    //    cout << f << endl;

    //}
    //catch (const exception &e)
    //{
    //    cout << e.what() << endl;
    //}

    //
    //atexit(report_memleakage);
    //system("pause");
    //return 0;

    //string s = "a;;;b;c;;;d;";
    //auto ss = split(s, ';');

    //char s[] = "a;b;c;d;";
    //int a = 0;
    //char **ss = split(s, ';');
    //if (ss)
    //{
    //    int i;
    //    for (i = 0; ss[i]; i++)
    //    {
    //        printf("ss = [%s]\n", ss[i]);
    //        free(ss[i]);
    //    }
    //    printf("\n");
    //    free(ss);
    //}


    //Bitboard occ    = U64(0x5A20FE309410205A);
    //    //U64(0xFFBF40009075EFFF);
    ////Bitboard wpawns = U64(0x000000001000EF00);
    ////Bitboard bpawns = U64(0x00BF400000000000);
    //////Bitboard test   = U64(61184);
    //Print(occ);

    /* Bitboard moves;

    //std::string s("this subject has a submarine as a subsequence");
    //std::smatch m;
    //std::regex e("\\b(sub)([^ ]*)");   // matches words beginning by "sub"
    //
    //std::cout << "Target sequence: " << s << std::endl;
    //std::cout << "Regular expression: /\\b(sub)([^ ]*)/" << std::endl;
    //std::cout << "The following matches and submatches were found:" << std::endl;
    //
    //while (std::regex_search(s, m, e))
    //{
    //    for (auto x : m)
    //        std::cout << x << " ";
    //    std::cout << std::endl;
    //    s = m.suffix().str();
    //}

    ////string seq("[Event \"Blitz 4m+2s\"]\n[Site \"?\"]\n[Date \"2001.12.05\"]\n[Round \"4\"]\n[White \"Deep Fritz 13\"]\n[Black \"aquil, muzaffar\"]\n[Result \"1/2-1/2\"]\n[ECO \"C80\"]\n[WhiteElo \"2839\"]\n[BlackElo \"2808\"]\n[PlyCount \"37\"]\n");

    //char *pat =
    //    //"[Event \"Blitz 4m+2s\"]\n[Site \"?\"]\n";
    //    "1. e4 e5 2. Nf3 {a}  {b} Nc6 3. Bb5 a6 4. g4 O-O";
    ////"11... Bxe3 12. Qxe3 Nxc3  13. Qxc3 {dfs} {sfsf} Qd7 14. Rad1 Nd8";
    //string seq(pat);
    //string reg_esp =
    //    /// tag
    //    //"(?:^\\s*\\[\\s*(\\w+)\\s+\"([^\"]+)\"\\s*\\]\\s*)";
    //    ///move
    //    // backtracking
    //    //"(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(\\d+)(\\.|\\.{3})\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*((?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?))\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*((?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?))\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*)?)";
    //    // no backtracking
    //    //"(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(\\d+)(\\.|\\.{3})\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*)?)";
    //    // no comment
    //    "(?:\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(\\d+)(\\.|\\.{3})\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(?:\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*)?)";


    //regex rgx(reg_esp);//, regex_constants::match_flag_type::match_continuous);
    //smatch match;
    //cout << "Target sequence: " << endl << seq << endl;
    //cout << "Regular expression: /" << reg_esp << "/" << endl;
    //cout << "The following matches were found:" << endl;

    ////sregex_iterator begin(seq.begin(), seq.end(), rgx), end;
    ////for (auto itr = begin; itr != end; ++itr)
    ////{
    ////    bool first = true;
    ////    for (auto x : (*itr))
    ////    {
    ////        if (first)
    ////        {
    ////            first = false;
    ////            continue;
    ////        }
    ////        cout << x << "   ";
    ////    }
    ////    cout << endl;
    ////}
    //

    //while (regex_search(seq, match, rgx, regex_constants::match_flag_type::match_not_null))
    //{
    //    bool first = true;
    //    for (auto x : match)
    //    {
    //        if (first)
    //        {
    //            first = false;
    //            continue;
    //        }
    //        cout << setw(4) << x << "   ";
    //    }
    //    cout << endl;
    //    seq = match.suffix().str();
    //}

    //cout << "--------" << endl;

    ////if (regex_search(seq, match, rgx))
    //////if (regex_match(seq.cbegin(), seq.cend(), match, rgx))
    //////if (regex_search(seq, match, rgx, regex_constants::match_flag_type::match_continuous));
    //////if (regex_match(seq, match, rgx))
    ////{
    ////    //cout << match.str() << endl;
    ////    //cout << match.position() << endl;
    ////    //cout << match.length() << endl;
    ////    for (auto x : match)
    ////        cout << x << ";" << endl;
    ////}
    //

    //Game games;
    //games.AddTag("fen", "ff");
    //cout << RelativeSquare(BLACK, SQ_E8);

    //Position pos;
    //char fen[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 20   100";
    //pos.Setup(fen);
    //printf("%016I64X\n", pos.Key);
    //cout << pos.Key;

    //string s =
    //    "0 1 0 0 0 0 0 0 \
    //     0 1 0 0 0 0 0 0 \
    //     0 0 0 0 0 0 0 0 \
    //     0 0 0 0 0 0 0 0 \
    //     0 1 0 0 0 0 0 0 \
    //     0 0 0 0 0 0 0 0 \
    //     0 1 0 0 0 0 0 0 \
    //     0 1 0 0 0 0 0 0";

    //string M2 = ToHexString(s);
    //printf("%s\n", M2.c_str());
    */
    //Timer t;
    //t.Start();

    //PGN
    ////p("pgn/test.pgn");
    ////p("pgn/variation.pgn");
    ////p("pgn/WAChamp08.pgn");
    ////p("pgn/WAChamp09.pgn");
    ////p("pgn/WAChamp10.pgn");
    //p("pgn/30000 games.pgn");
    ////p("extra/11lac.pgn");
    ////
    //cout << "reading done";
    ////cout << p.GameCount() << endl;
    /*
    ////cout << t.Elapsed_msec() << endl;

    //string rtext = p.GameText(1000);
    //cout << rtext;

    //Game g = Game::Parse(rtext);
    //Game g = p.Game(1);

    //char fen[] =
    //"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    //"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    //"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - h6 0 1";
    //"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq f6 0 1";
    //"r1bqkbnr/1p1pp2p/2n5/8/8/2N5/P2PP1PP/R1BQKBNR w KQkq - 0 5";
    //"rnbqkb1r/p1p2ppp/4pn2/1pPp4/3P4/5N2/PP2PPPP/RNBQKB1R w KQkq b6 0 5";
    //"8/5K2/kp6/p1p5/P2p4/1P3P2/2P5/8 b - - 0 55";

    //"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR   w KQkq e3 5 9";
    ////"8/2k5/8/4Pp2/8/8/2K5/8 w - f6 0 0"; // enPassant
    ////"1r6/P6k/8/8/8/4K3/8/8 w - - 0 1"; // promote
    //"1r6/P6k/8/8/8/4K3/1p6/8 b - - 0 1";
    //////    "r1n1k1B1/1r1b1q1n/1p1p1p1p/p1p1p1p1/1P1P1P1P/P1P1P1P1/1R1B1Q1N/R1N1K1b1 w Qq - 12 200";

    //Position pos(fen);

    //uint64_t key = Hash(fen);
    //printf("%016I64X\n", key);
    */


    cout << endl;

    atexit(LeakDetector::report_memleakage);
    system ("PAUSE");
    return EXIT_SUCCESS;
}

void InitializeIO ()
{
    //size_t size_buf = 0;
    //char *buffer = NULL;
    //setvbuf (stdin, buffer, (buffer) ? _IOFBF : _IONBF, size_buf);
    //setvbuf (stdout, buffer, (buffer) ? _IOFBF : _IONBF, size_buf); // _IOLBF breaks on Windows!

    //cout.unsetf(ios_base::dec);
    //std::cout.setf(std::ios_base::boolalpha);
    cout.setf (
        //    ios_base::showpos |
        //    ios_base::boolalpha |
        ios_base::hex |
        //    ios_base::uppercase |
        //    ios_base::fixed |
        //    //ios_base::showpoint |
        ios_base::unitbuf);
    cout.precision (2);

}


//const uint64_t BitScanMagic = U64(0x07EDD5E59A4E28C2);
////const uint64_t BitScanMagic = U64(0X0218A392CD3D5DBF);
//uint8_t _BitScanLookup[SQ_NO];
//void InitializeFirstOne()
//{
//
//    Bitboard bit = 1;
//    for (uint8_t i = 0; i < 0x40; ++i) //64
//    {
//        _BitScanLookup[(bit * BitScanMagic) >> 58] = i;
//        bit <<= 1;
//    }
//}
//
//#define FirstOne(X) BitScanDatabase [ (((X)&-(X)) * BitScanMagic)>>58]

//void Test()
//{
//
//    //clock_t timeStart = clock();
//
//    //PGNFile
//    ////p("pgn/test.pgn");
//    ////p("pgn/variation.pgn");
//    ////p("pgn/WAChamp08.pgn");
//    ////p("pgn/WAChamp09.pgn");
//    ////p("pgn/WAChamp10.pgn");
//    //p("pgn/30000 games.pgn");
//    ////p("pgn/Millions games.pgn");
//
//
//    //clock_t timeEnd = clock();
//    //double span = (double) (timeEnd - timeStart) / CLOCKS_PER_SEC;
//    //printf("%f seconds.\n\n", span);
//
//    //time(&end);
//    //duration = difftime(end, start);
//    //printf("%f mili-seconds.\n\n", duration);
//
//    //// ---
//
//for (int i = 0; i < 64; ++i)
//{
//    Print(QueenMovesMask((Square) i, 2));
//    if (i%8 == 0)
//        getch();
//}
//
//    //string s =
//    //    "0 0 0 0 0 0 0 0 \
//    //     0 0 0 0 0 0 0 0 \
//    //     0 0 0 0 0 0 0 0 \
//    //     0 0 0 1 1 0 0 0 \
//    //     0 0 0 1 1 0 0 0 \
//    //     0 0 0 0 0 0 0 0 \
//    //     0 0 0 0 0 0 0 0 \
//    //     0 0 0 0 0 0 0 0";
//
//    //string M2 = ToHexString(s);
//    //Bitboard bb;
//    ////printf("%I64X\n", bb);
//    //Print(bb);
//    //
//
//
//}

static string ArgumentString (int argc, const char* const argv[])
{
    string args = "";
    foreach (int8_t, 1, argc - 1, i)
    {
        args += iswhitespace (args) ? string (argv[i]) : " " + string (argv[i]);
    }
    return args;
}

