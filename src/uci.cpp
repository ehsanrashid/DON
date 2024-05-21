/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "uci.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <optional>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

#include "benchmark.h"
#include "evaluate.h"
#include "position.h"
#include "score.h"
#include "syzygy/tbprobe.h"

namespace DON {

namespace {

constexpr std::string_view START_FEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
constexpr std::string_view PIECE_CHAR(" PNBRQK  pnbrqk");

constexpr std::uint32_t MAX_HASH =
#if defined(IS_64BIT)
  0x2000000U;
#else
  0x800U;
#endif

template<typename... Ts>
struct overload: Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

Search::Limits parse_limits(std::istringstream& iss) noexcept {
    Search::Limits limits;

    limits.startTime = now();  // The search starts as early as possible

    std::string token;
    while (iss >> token)
    {
        token = to_lower(token);
        if (token == "wtime")
        {
            iss >> limits.clock[WHITE].time;
            limits.clock[WHITE].time = std::max(std::abs(limits.clock[WHITE].time), 1LL);
        }
        else if (token == "btime")
        {
            iss >> limits.clock[BLACK].time;
            limits.clock[BLACK].time = std::max(std::abs(limits.clock[BLACK].time), 1LL);
        }
        else if (token == "winc")
        {
            iss >> limits.clock[WHITE].inc;
            limits.clock[WHITE].inc = std::abs(limits.clock[WHITE].inc);
        }
        else if (token == "binc")
        {
            iss >> limits.clock[BLACK].inc;
            limits.clock[BLACK].inc = std::abs(limits.clock[BLACK].inc);
        }
        else if (token == "movetime")
        {
            iss >> limits.moveTime;
            limits.moveTime = std::max(std::abs(limits.moveTime), 1LL);
        }
        else if (token == "movestogo")
        {
            std::int16_t movesToGo;
            iss >> movesToGo;
            limits.movesToGo = std::max<std::uint8_t>(std::abs(movesToGo), 1);
        }
        else if (token == "mate")
        {
            std::int16_t mate;
            iss >> mate;
            limits.mate = std::max<std::uint8_t>(std::abs(mate), 1);
        }
        else if (token == "depth")
        {
            iss >> limits.depth;
            limits.depth = std::max<Depth>(std::abs(limits.depth), 1);
        }
        else if (token == "nodes")
        {
            iss >> limits.nodes;
            limits.nodes = std::max(limits.nodes, 1ULL);
            // When using nodes, ensure checking rate is not lower than 0.1% of nodes
            limits.hitRate = std::min(1.0 + std::ceil(limits.nodes / 1024.0), 0.0 + limits.hitRate);
        }
        else if (token == "infinite")
            limits.infinite = true;
        else if (token == "ponder")
            limits.ponder = true;
        else if (token == "perft")
        {
            limits.perft = true;
            iss >> limits.depth;
            limits.depth = std::abs(limits.depth);
            iss >> std::boolalpha >> limits.detail;
        }
        else if (token == "searchmoves")  // Needs to be the last command on the line
        {
            auto pos = iss.tellg();
            while (iss >> token && (token = to_lower(token)) != "ignoremoves")
            {
                limits.searchMoves.push_back(token);
                pos = iss.tellg();
            }
            if (token == "ignoremoves")
                iss.seekg(pos);
        }
        else if (token == "ignoremoves")  // Needs to be the last command on the line
        {
            auto pos = iss.tellg();
            while (iss >> token && (token = to_lower(token)) != "searchmoves")
            {
                limits.ignoreMoves.push_back(token);
                pos = iss.tellg();
            }
            if (token == "searchmoves")
                iss.seekg(pos);
        }
    }
    return limits;
}

}  // namespace

UCI::UCI(int argc, const char** argv) noexcept :
    engine(argv[0]),
    cmdLine(argc, argv) {

    auto& options = engine_options();
    // clang-format off
    options["Threads"]      << Option(1, 1, 1024, [this](const Option&) { engine.resize_threads(); });
    options["Hash"]         << Option(16, 4, MAX_HASH, [this](const Option& o) { engine.resize_tt(o); });
    options["Clear Hash"]   << Option([this](const Option&) { engine.clear(); });
    options["Retain Hash"]  << Option(false);
    options["HashFile"]     << Option("hash.dat");
    options["Save Hash"]    << Option([this](const Option&) {});
    options["Load Hash"]    << Option([this](const Option&) {});
    options["Ponder"]       << Option(false);
    options["MultiPV"]      << Option(1, 1, std::numeric_limits<std::uint8_t>::max());
    options["Skill Level"]  << Option(Search::Skill::MaxLevel, 0, Search::Skill::MaxLevel);
    options["Move Overhead"] << Option(10, 0, 5000);
    options["NodesTime"]    << Option(0, 0, 10000);
    options["DrawMoveCount"] << Option(Position::DrawMoveCount, 5, 50, [](const Option& o) { Position::DrawMoveCount = o; });
    options["UCI_Chess960"] << Option(Position::Chess960, [](const Option& o) { Position::Chess960 = o; });
    options["UCI_LimitStrength"] << Option(false);
    options["UCI_ELO"]      << Option(Search::Skill::MinELO, Search::Skill::MinELO, Search::Skill::MaxELO);
    options["UCI_ShowWDL"]  << Option(false);
    options["OwnBook"]      << Option(false);
    options["BookFile"]     << Option("book.bin", [this](const Option& o) { engine.init_book(o); });
    options["BookDepth"]    << Option(100, 1, MAX_MOVES);
    options["BookPickBest"] << Option(true);
    options["SyzygyPath"]   << Option("<empty>", [](const Option& o) { Tablebases::init(o); });
    options["SyzygyProbeLimit"] << Option(7, 0, 7);
    options["SyzygyProbeDepth"] << Option(1, 1, 100);
    options["Syzygy50MoveRule"] << Option(true);
    options["EvalFileBig"]  << Option(EvalFileDefaultNameBig, [this](const Option& o) { engine.load_big_network(o); });
    options["EvalFileSmall"] << Option(EvalFileDefaultNameSmall, [this](const Option& o) { engine.load_small_network(o); });
    options["ReportMinimal"] << Option(false);
    options["DebugLogFile"] << Option("<empty>", [](const Option& o) { start_logger(o); });
    // clang-format on
    engine.set_on_update_short(on_update_short);
    engine.set_on_update_full(on_update_full);
    engine.set_on_update_iteration(on_update_iteration);
    engine.set_on_update_bestmove(on_update_bestmove);

    engine.load_networks();
    engine.resize_threads();
    engine.clear();  // After threads are up

    engine.setup(START_FEN);
}

void UCI::handle_commands() noexcept {

    std::string cmd;
    for (int i = 1; i < cmdLine.argc; ++i)
        cmd += std::string(cmdLine.argv[i]) + " ";

    std::string token;
    do
    {
        if (cmdLine.argc == 1
            // Wait for an input or an end-of-file (EOF) indication
            && !std::getline(std::cin, cmd))
            cmd = "quit";

        std::istringstream iss(cmd);

        token.clear();  // Avoid a stale if std::getline() returns nothing or a blank line
        iss >> std::skipws >> token;
        token = to_lower(token);

        if (token == "stop" || token == "quit")
            engine.stop();

        // The GUI sends 'ponderhit' to tell that the user has played the expected move.
        // So, 'ponderhit' is sent if pondering was done on the same move that the user has played.
        // The search should continue, but should also switch from pondering to the normal search.
        else if (token == "ponderhit")
            engine.ponderhit();
        else if (token == "position")
            position(iss);
        else if (token == "go")
            go(iss);
        else if (token == "setoption")
            setoption(iss);
        else if (token == "uci")
            sync_cout << engine_info(true) << '\n'
                      << engine_options() << '\n'
                      << "uciok" << sync_endl;
        else if (token == "ucinewgame")
            engine.clear();
        else if (token == "isready")
            sync_cout << "readyok" << sync_endl;

        // Add custom non-UCI commands, mainly for debugging purposes.
        // These commands must not be used during a search!
        else if (token == "bench")
            bench(iss);
        else if (token == "show")
            engine.show();
        else if (token == "eval")
            engine.eval();
        else if (token == "flip")
            engine.flip();
        else if (token == "compiler")
            sync_cout << compiler_info() << sync_endl;
        else if (token == "export_net")
        {
            std::pair<std::optional<std::string>, std::string> files[2];

            if (iss >> std::ws >> files[0].second)
                files[0].first = files[0].second;

            if (iss >> std::ws >> files[1].second)
                files[1].first = files[1].second;

            engine.save_networks(files);
        }
        else if (token == "--help" || token == "help" || token == "--license" || token == "license")
            sync_cout
              << "\nDON is a powerful chess engine for playing and analyzing."
                 "\nIt is released as free software licensed under the GNU GPLv3 License."
                 "\nDON is normally used with a graphical user interface (GUI) and implements"
                 "\nthe Universal Chess Interface (UCI) protocol to communicate with a GUI, an API, etc."
                 "\nFor any further information, visit https://github.com/ehsanrashid/DON#readme"
                 "\nor read the corresponding README.md and Copying.txt files distributed along with this program.\n"
              << sync_endl;
        else if (!token.empty() && token[0] != '#')
            sync_cout << "Unknown command: '" << cmd << "'. Type help for more information."
                      << sync_endl;

    } while (cmdLine.argc == 1 && token != "quit");  // The command-line arguments are one-shot
}

void UCI::position(std::istringstream& iss) noexcept {
    std::string token, fen;

    iss >> token;
    token = to_lower(token);
    if (token == "startpos")
    {
        fen = START_FEN;
        iss >> token;  // Consume the "moves" token, if any
    }
    else if (token == "fen")
        while (iss >> token && to_lower(token) != "moves")
            fen += token + " ";
    else
        return;

    std::vector<std::string> moves;
    while (iss >> token)
        moves.push_back(token);

    engine.setup(fen, moves);
}

void UCI::go(std::istringstream& iss) noexcept {
    auto limits = parse_limits(iss);

    if (limits.perft)
        engine.perft(limits.depth, limits.detail);
    else
        engine.start(limits);
}

void UCI::setoption(std::istringstream& iss) noexcept {
    //engine.wait_finish();

    std::string token, name, value;

    iss >> token;  // Consume the "name" token
    token = to_lower(token);
    assert(token == "name");
    // Read the option name (can contain spaces)
    while (iss >> token && (token = to_lower(token)) != "value")
        name += (name.empty() ? "" : " ") + token;
    assert(token == "value");
    // Read the option value (can contain spaces)
    while (iss >> token)
        value += (value.empty() ? "" : " ") + token;

    engine_options().setoption(name, value);
}

void UCI::bench(std::istringstream& iss) noexcept {

    std::uint64_t infoNodes = 0;
    engine.set_on_update_full([&infoNodes](const auto& info) {
        infoNodes = info.nodes;
        on_update_full(info);
    });

#if !defined(NDEBUG)
    dbg_init();
#endif
    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    std::uint64_t nodes = 0;

    const std::vector<std::string> list = Benchmark::setup_bench(iss, engine.fen());

    std::size_t num = std::count_if(list.begin(), list.end(), [](const std::string& cmd) {
        return cmd.find("go ") == 0 || cmd.find("eval") == 0;
    });

    std::size_t cnt = 0;
    for (const std::string& cmd : list)
    {
        std::istringstream is(cmd);
        std::string        token;
        is >> std::skipws >> token;
        token = to_lower(token);

        if (token == "go" || token == "eval")
        {
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")\n";
            if (token == "go")
            {
                auto limits = parse_limits(is);

                if (limits.perft)
                    nodes += engine.perft(limits.depth, limits.detail);
                else
                {
                    engine.start(limits);
                    engine.wait_finish();
                    nodes += infoNodes;
                }
            }
            else
            {
                engine.eval();
            }
        }
        else if (token == "position")
            position(is);
        else if (token == "setoption")
            setoption(is);
        else if (token == "ucinewgame")
        {
            elapsedTime += now() - startTime;
            engine.clear();  // May take a while
            startTime = now();
        }
    }

    // Ensure non-zero to avoid a 'divide by zero'
    elapsedTime += std::max(now() - startTime, 1LL);
#if !defined(NDEBUG)
    dbg_print();
#endif
    std::cerr << "\n==========================="
              << "\nTotal time (ms) : " << elapsedTime  //
              << "\nTotal Nodes     : " << nodes        //
              << "\nNodes/second    : " << 1000 * nodes / elapsedTime << '\n';

    // Reset callback, to not capture a dangling reference to infoNodes
    engine.set_on_update_full(on_update_full);
}

namespace {

struct WinRateParams final {
    double a;
    double b;
};

WinRateParams win_rate_params(const Position& pos) noexcept {

    // The fitted model only uses data for material counts in [10, 78], and is anchored at count 58 (0.017241).
    double m = 0.017241 * std::clamp<std::uint16_t>(pos.material(), 10, 78);

    // Return a = p_a(material) and b = p_b(material).
    // clang-format off
    constexpr double as[4]{-150.77043883, 394.96159472, -321.73403766, 406.15850091};
    constexpr double bs[4]{  62.33245393, -91.02264855,   45.88486850,  51.63461272};
    // clang-format on
    double a = (((as[0] * m + as[1]) * m + as[2]) * m) + as[3];
    double b = (((bs[0] * m + bs[1]) * m + bs[2]) * m) + bs[3];

    return {a, b};
}

// The win rate model is 1 / (1 + exp((a - eval) / b)), where a = p_a(material) and b = p_b(material).
// It fits the LTC fishtest statistics rather accurately.
int win_rate_model(Value v, const Position& pos) noexcept {

    auto [a, b] = win_rate_params(pos);

    // Return the win rate in per mille units, rounded to the nearest integer.
    return int(0.5 + 1000.0 / (1.0 + std::exp((a - v) / b)));
}
}  // namespace

// Turns a Value to an integer centipawn number,
// without treatment of mate and similar special scores.
int UCI::to_cp(Value v, const Position& pos) noexcept {

    // In general, the score can be defined via the WDL as
    // (log(1/L - 1) - log(1/W - 1)) / ((log(1/L - 1) + log(1/W - 1))
    // Based on our win_rate_model, this simply yields v / a.

    auto [a, b] = win_rate_params(pos);

    return std::round(100 * v / a);
}

std::string UCI::to_wdl(Value v, const Position& pos) noexcept {
    assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);
    std::ostringstream oss;

    auto wdlW = win_rate_model(+v, pos);
    auto wdlL = win_rate_model(-v, pos);
    auto wdlD = 1000 - wdlW - wdlL;
    oss << wdlW << " " << wdlD << " " << wdlL;

    return oss.str();
}

std::string UCI::format_score(const Score& score) noexcept {
    constexpr int TB_CP = 20000;

    const auto format =
      overload{[](Score::Mate mate) -> std::string {
                   return "mate " + std::to_string(((mate.ply > 0) + mate.ply) / 2);
               },
               [](Score::Tablebase tb) -> std::string {
                   return "cp " + std::to_string((tb.win ? +TB_CP : -TB_CP) - tb.ply);
               },
               [](Score::Unit unit) -> std::string { return "cp " + std::to_string(unit.value); }};

    return score.visit(format);
}

char UCI::piece(PieceType pt) noexcept { return is_ok(pt) ? PIECE_CHAR[pt] : ' '; }
char UCI::piece(Piece pc) noexcept { return is_ok(pc) ? PIECE_CHAR[pc] : ' '; }

Piece UCI::piece(char pc) noexcept {
    auto pos = PIECE_CHAR.find(pc);
    return pos != std::string_view::npos ? Piece(pos) : NO_PIECE;
}

char UCI::file(File f, bool caseLower) noexcept { return int(f) + 'A' + 0x20 * caseLower; }

char UCI::rank(Rank r) noexcept { return int(r) + '1'; }

std::string UCI::square(Square s) noexcept {
    assert(is_ok(s));
    return std::string{file(file_of(s)), rank(rank_of(s))};
}

std::string UCI::move_to_can(Move m) noexcept {
    if (m == Move::None())
        return "(none)";
    if (m == Move::Null())
        return "0000";

    Square org = m.org_sq(), dst = m.dst_sq();
    if (m.type_of() == CASTLING && !Position::Chess960)
    {
        assert(rank_of(org) == rank_of(dst));
        dst = make_square(org < dst ? FILE_G : FILE_C, rank_of(org));
    }

    std::string can = square(org) + square(dst);
    if (m.type_of() == PROMOTION)
        can += char(std::tolower(piece(m.promotion_type())));

    return can;
}

// Converts a string representing a move in coordinate notation
// (g1f3, a7a8q) to the corresponding legal move, if any.
Move UCI::can_to_move(const std::string& can, const MoveList<LEGAL>& legalMoves) noexcept {
    assert(4 <= can.length() && can.length() <= 5);
    std::string ccan = to_lower(can);

    for (auto m : legalMoves)
        if (ccan == move_to_can(m))
            return m;

    return Move::None();
}

Move UCI::can_to_move(const std::string& can, const Position& pos) noexcept {
    return can_to_move(can, MoveList<LEGAL>(pos));
}

void UCI::on_update_short(const Search::InfoShort& info) noexcept {
    sync_cout << "info depth 0 score " << (info.inCheck ? "mate" : "cp") << " 0" << sync_endl;
}

void UCI::on_update_full(const Search::InfoFull& info) noexcept {
    std::ostringstream oss;
    oss << "info"                                              //
        << " depth " << info.depth                             //
        << " seldepth " << info.rootMove.selDepth              //
        << " multipv " << info.multiPV                         //
        << " score " << format_score({info.value, info.pos});  //
    if (info.showBound)
        oss << (info.rootMove.lowerBound   ? " lowerbound"
                : info.rootMove.upperBound ? " upperbound"
                                           : "");
    if (info.showWDL)
        oss << " wdl " << to_wdl(info.value, info.pos);
    oss << " time " << info.time                     //
        << " nodes " << info.nodes                   //
        << " nps " << 1000 * info.nodes / info.time  //
        << " hashfull " << info.hashfull             //
        << " tbhits " << info.tbHits                 //
        << " pv";
    for (Move m : info.rootMove)
        oss << " " << move_to_can(m);
    sync_cout << oss.str() << sync_endl;
}

void UCI::on_update_iteration(const Search::InfoIteration& info) noexcept {
    std::ostringstream oss;
    oss << "info"                                      //
        << " depth " << info.depth                     //
        << " currmove " << move_to_can(info.currMove)  //
        << " currmovenumber " << info.currMoveNumber;  //
    sync_cout << oss.str() << sync_endl;
}

void UCI::on_update_bestmove(const Search::InfoBestMove& info) noexcept {
    sync_cout << "bestmove " << move_to_can(info.bestMove);
    if (info.ponderMove != Move::None())
        std::cout << " ponder " << move_to_can(info.ponderMove);
    std::cout << sync_endl;
}

namespace {

enum Ambiguity : std::uint8_t {
    AMBIGUITY_NONE,
    AMBIGUITY_RANK,
    AMBIGUITY_FILE,
    AMBIGUITY_SQUARE,
};

// Ambiguity if more then one piece of same type can reach 'to' with a legal move.
// NOTE: for pawns it is not needed because 'from' file is explicit.
Ambiguity ambiguity(Move m, const Position& pos) noexcept {
    assert(pos.pseudo_legal(m) && pos.legal(m));

    Color stm = pos.side_to_move();

    const Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(pos.piece_on(org)) == stm);
    PieceType pt = type_of(pos.piece_on(org));

    // If there is only one piece 'pc' then move cannot be ambiguous
    if (pos.count(stm, pt) == 1)
        return AMBIGUITY_NONE;

    // Disambiguation if have more then one piece with destination
    // note that for pawns is not needed because starting file is explicit.
    Bitboard piece = (attacks_bb(pt, dst, pos.pieces()) & pos.pieces(stm, pt)) ^ org;

    if (!piece)
        return AMBIGUITY_NONE;

    Bitboard b = piece;
    // If pinned piece is considered as ambiguous
    //& ~pos.blockers(stm);
    while (b)
    {
        Square sq = pop_lsb(b);

        Move mm = Move(sq, dst);
        if (!(pos.pseudo_legal(mm) && pos.legal(mm)))
            piece ^= sq;
    }
    if (!(piece & file_bb(org)))
        return AMBIGUITY_RANK;
    if (!(piece & rank_bb(org)))
        return AMBIGUITY_FILE;

    return AMBIGUITY_SQUARE;
}

}  // namespace

std::string UCI::move_to_san(Move m, Position& pos) noexcept {
    if (m == Move::None())
        return "(none)";
    if (m == Move::Null())
        return "0000";
    assert(MoveList<LEGAL>(pos).contains(m));

    std::ostringstream oss;

    const Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(pos.piece_on(org)) == pos.side_to_move());
    PieceType pt = type_of(pos.piece_on(org));

    if (m.type_of() != CASTLING)
    {
        if (pt != PAWN)
        {
            oss << piece(pt);
            if (pt != KING)
            {
                // Disambiguation if have more then one piece of type 'pt' that can reach 'to' with a legal move.
                switch (ambiguity(m, pos))
                {
                case AMBIGUITY_RANK :
                    oss << file(file_of(org));
                    break;
                case AMBIGUITY_FILE :
                    oss << rank(rank_of(org));
                    break;
                case AMBIGUITY_SQUARE :
                    oss << square(org);
                    break;
                default :;
                }
            }
        }

        if (pos.capture(m))
        {
            if (pt == PAWN)
                oss << file(file_of(org));
            oss << 'x';
        }

        oss << square(dst);

        if (pt == PAWN && m.type_of() == PROMOTION)
            oss << '=' << piece(m.promotion_type());
    }
    else
    {
        assert(pt == KING && rank_of(org) == rank_of(dst));
        oss << (org < dst ? "O-O" : "O-O-O");
    }

    // Move marker for check & checkmate
    if (pos.gives_check(m))
    {
        StateInfo st;
        ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

        pos.do_move(m, st, true);
        oss << (MoveList<LEGAL>(pos).size() != 0 ? '+' : '#');
        pos.undo_move(m);
    }

    return oss.str();
}

Move UCI::san_to_move(const std::string& san, Position& pos) noexcept {
    assert(3 <= san.length() && san.length() <= 9);
    for (auto m : MoveList<LEGAL>(pos))
        if (san == move_to_san(m, pos))
            return m;

    return Move::None();
}

}  // namespace DON
