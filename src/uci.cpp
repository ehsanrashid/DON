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
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "benchmark.h"
#include "evaluate.h"
#include "movegen.h"
#include "position.h"
#include "score.h"
#include "search.h"

namespace DON {

namespace {

// clang-format off
constexpr inline std::string_view StartFEN{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"};
constexpr inline std::string_view PieceChar{" PNBRQK  pnbrqk"};
// clang-format on

enum UciCommand : std::uint8_t {
    CMD_STOP,
    CMD_QUIT,
    CMD_PONDERHIT,
    CMD_POSITION,
    CMD_GO,
    CMD_SETOPTION,
    CMD_UCI,
    CMD_UCINEWGAME,
    CMD_ISREADY,
    // Add custom non-UCI commands, mainly for debugging purposes.
    // These commands must not be used during a search!
    CMD_BENCH,
    CMD_SHOW,
    CMD_EVAL,
    CMD_FLIP,
    CMD_COMPILER,
    CMD_EXPORT_NET,
    CMD_HELP,
    // Unknown Command
    CMD_NONE,
};

// clang-format off
const inline std::unordered_map<std::string_view, UciCommand> UciCommandMap{
  {"stop",       CMD_STOP},
  {"quit",       CMD_QUIT},
  {"ponderhit",  CMD_PONDERHIT},
  {"position",   CMD_POSITION},
  {"go",         CMD_GO},
  {"setoption",  CMD_SETOPTION},
  {"uci",        CMD_UCI},
  {"ucinewgame", CMD_UCINEWGAME},
  {"isready",    CMD_ISREADY},
  {"bench",      CMD_BENCH},
  {"show",       CMD_SHOW},
  {"eval",       CMD_EVAL},
  {"flip",       CMD_FLIP},
  {"compiler",   CMD_COMPILER},
  {"--help",     CMD_HELP},
  {"help",       CMD_HELP},
  {"--license",  CMD_HELP},
  {"license",    CMD_HELP}};
// clang-format on

template<typename... Ts>
struct Overload final: Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

UciCommand uci_command(std::string_view cmd) noexcept {
    auto itr = UciCommandMap.find(cmd);
    return itr != UciCommandMap.end() ? itr->second : CMD_NONE;
}

Search::Limits parse_limits(std::istringstream& iss) noexcept {
    // The search starts as early as possible
    Search::Limits limits(now());

    std::string token;
    while (iss >> token)
    {
        token = to_lower(token);
        if (token == "wtime")
        {
            iss >> limits.clock[WHITE].time;
            limits.clock[WHITE].time = std::max(std::abs(limits.clock[WHITE].time), 1ll);
        }
        else if (token == "btime")
        {
            iss >> limits.clock[BLACK].time;
            limits.clock[BLACK].time = std::max(std::abs(limits.clock[BLACK].time), 1ll);
        }
        else if (token == "winc")
        {
            iss >> limits.clock[WHITE].inc;
            limits.clock[WHITE].inc = std::max(std::abs(limits.clock[WHITE].inc), 1ll);
        }
        else if (token == "binc")
        {
            iss >> limits.clock[BLACK].inc;
            limits.clock[BLACK].inc = std::max(std::abs(limits.clock[BLACK].inc), 1ll);
        }
        else if (token == "movetime")
        {
            iss >> limits.moveTime;
            limits.moveTime = std::max(std::abs(limits.moveTime), 1ll);
        }
        else if (token == "movestogo")
        {
            std::int16_t movesToGo;
            iss >> movesToGo;
            limits.movesToGo =
              std::clamp(std::abs(movesToGo), 1, +std::numeric_limits<std::uint8_t>::max());
        }
        else if (token == "mate")
        {
            std::int16_t mate;
            iss >> mate;
            limits.mate = std::clamp(std::abs(mate), 1, +std::numeric_limits<std::uint8_t>::max());
        }
        else if (token == "depth")
        {
            iss >> limits.depth;
            limits.depth = std::clamp(std::abs(limits.depth), 1, MAX_PLY - 1);
        }
        else if (token == "nodes")
        {
            iss >> limits.nodes;
            limits.nodes = std::max(limits.nodes, 1ull);
            // When using nodes, ensure checking rate is not lower than 0.1% of nodes
            limits.hitRate = std::min(+limits.hitRate, 1 + int(std::ceil(limits.nodes / 1024.0)));
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
        // "searchmoves" needs to be the last command on the line
        else if (starts_with(token, "search"))
        {
            auto pos = iss.tellg();
            while (iss >> token)
            {
                if (starts_with(to_lower(token), "ignore"))
                {
                    iss.seekg(pos);
                    break;
                }
                limits.searchMoves.push_back(token);
                pos = iss.tellg();
            }
        }
        // "ignoremoves" needs to be the last command on the line
        else if (starts_with(token, "ignore"))
        {
            auto pos = iss.tellg();
            while (iss >> token)
            {
                if (starts_with(to_lower(token), "search"))
                {
                    iss.seekg(pos);
                    break;
                }
                limits.ignoreMoves.push_back(token);
                pos = iss.tellg();
            }
        }
    }
    return limits;
}

void print_info_string(const std::string& infoStr) noexcept {
    sync_cout;
    for (const std::string& info : split(infoStr, "\n"))
        if (!is_whitespace(info))
            std::cout << "info string " << info << '\n';
    std::cout << sync_end;
}

void on_update_end(const Search::EndInfo& info) noexcept;
void on_update_full(const Search::FullInfo& info) noexcept;
void on_update_iter(const Search::IterInfo& info) noexcept;
void on_update_move(const Search::MoveInfo& info) noexcept;

}  // namespace

UCI::UCI(int argc, const char** argv) noexcept :
    engine(argv[0]),
    cmdLine(argc, argv) {

    engine_options().add_info_listener([](const std::optional<std::string>& infoStr) {
        if (infoStr.has_value())
            print_info_string(*infoStr);
    });

    engine.set_on_update_end(on_update_end);
    engine.set_on_update_full(on_update_full);
    engine.set_on_update_iter(on_update_iter);
    engine.set_on_update_move(on_update_move);

    engine.setup(StartFEN);
}

void UCI::handle_commands() noexcept {
    std::string cmd;
    for (int i = 1; i < cmdLine.argc; ++i)
    {
        if (i != 1)
            cmd += ' ';
        cmd += std::string(cmdLine.argv[i]);
    }
    // The command-line arguments are one-shot
    bool running = cmdLine.argc <= 1;
    if (!running && is_whitespace(cmd))
        return;

    do
    {
        if (running
            // Wait for an input or an end-of-file (EOF) indication
            && !std::getline(std::cin, cmd))
            cmd = "quit";

        std::istringstream iss(cmd);
        iss >> std::skipws;

        std::string token;
        iss >> token;
        if (token.empty())
            continue;

        auto uciCmd = uci_command(to_lower(token));
        switch (uciCmd)
        {
        case CMD_STOP :
        case CMD_QUIT :
            engine.stop();
            running &= uciCmd != CMD_QUIT;
            break;
        case CMD_PONDERHIT :
            // The GUI sends 'ponderhit' to tell that the user has played the expected move.
            // So, 'ponderhit' is sent if pondering was done on the same move that the user has played.
            // The search should continue, but should also switch from pondering to the normal search.
            engine.ponderhit();
            break;
        case CMD_POSITION :
            position(iss);
            break;
        case CMD_GO :
            // Send info strings after the go command is sent for old GUIs and python-chess
            print_info_string(engine.get_numa_config_info());
            print_info_string(engine.get_thread_binding_info());

            go(iss);
            break;
        case CMD_SETOPTION :
            setoption(iss);
            break;
        case CMD_UCI :
            sync_cout << engine_info(true) << '\n' << engine_options() << sync_endl;
            sync_cout << "uciok" << sync_endl;
            break;
        case CMD_UCINEWGAME :
            engine.init();
            break;
        case CMD_ISREADY :
            sync_cout << "readyok" << sync_endl;
            break;
        // Add custom non-UCI commands, mainly for debugging purposes.
        // These commands must not be used during a search!
        case CMD_BENCH :
            bench(iss);
            break;
        case CMD_SHOW :
            engine.show();
            break;
        case CMD_EVAL :
            engine.eval();
            break;
        case CMD_FLIP :
            engine.flip();
            break;
        case CMD_COMPILER :
            sync_cout << compiler_info() << sync_endl;
            break;
        case CMD_EXPORT_NET : {
            std::pair<std::optional<std::string>, std::string> files[2];

            if (iss >> files[0].second)
                files[0].first = files[0].second;

            if (iss >> files[1].second)
                files[1].first = files[1].second;

            engine.save_networks(files);
        }
        break;
        case CMD_HELP :
            sync_cout
              << "\nDON is a powerful chess engine for playing and analyzing."
                 "\nIt is released as free software licensed under the GNU GPLv3 License."
                 "\nDON is normally used with a graphical user interface (GUI) and implements"
                 "\nthe Universal Chess Interface (UCI) protocol to communicate with a GUI, an API, etc."
                 "\nFor any further information, visit https://github.com/ehsanrashid/DON#readme"
                 "\nor read the corresponding README.md and Copying.txt files distributed along with this program.\n"
              << sync_endl;
            break;
        default :
            if (token[0] != '#')
            {
                sync_cout << "Unknown command: '" << cmd << "'. "
                          << "Type help for more information." << sync_endl;
            }
        }
    } while (running);
}

void UCI::position(std::istringstream& iss) noexcept {
    std::string token, fen;

    iss >> token;
    token = to_lower(token);
    if (starts_with(token, "start"))  // "startpos"
    {
        fen = StartFEN;
        iss >> token;  // Consume the "moves" token, if any
    }
    else  //if (token == "fen")
    {
        int i = 0;
        while (iss >> token && i < 6)  // Consume the "moves" token, if any
        {
            if (i >= 2 && starts_with(to_lower(token), "moves"))
                break;
            if (i != 0)
                fen += ' ';
            fen += token;
            ++i;
        }
        while (i < 4)
        {
            fen += " -";
            ++i;
        }
    }

    std::deque<std::string> moves;
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

    bool isFirst;

    iss >> token;  // Consume the "name" token
    assert(to_lower(token) == "name");
    // Read the option name (can contain spaces)
    isFirst = true;
    while (iss >> token && to_lower(token) != "value")
    {
        if (!isFirst)
            name += ' ';
        name += token;
        isFirst = false;
    }
    assert(to_lower(token) == "value");
    // Read the option value (can contain spaces)
    isFirst = true;
    while (iss >> token)
    {
        if (!isFirst)
            value += ' ';
        value += token;
        isFirst = false;
    }

    engine_options().setoption(name, value);
}

void UCI::bench(std::istringstream& iss) noexcept {

    std::uint64_t infoNodes = 0;
    engine.set_on_update_full([&infoNodes](const auto& info) {
        infoNodes = info.nodes;
        on_update_full(info);
    });

    auto reportMinimal = bool_to_string(engine_options()["ReportMinimal"]);

    engine_options()["ReportMinimal"] = bool_to_string(true);

#if !defined(NDEBUG)
    Debug::init();
#endif

    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    std::uint64_t nodes = 0;

    const std::vector<std::string> cmds = Benchmark::setup_bench(iss, engine.fen());

    std::size_t num = std::count_if(cmds.begin(), cmds.end(), [](const std::string& cmd) {
        return cmd.find("go ") == 0 || cmd.find("eval") == 0;
    });

    std::size_t cnt = 0;
    for (const std::string& cmd : cmds)
    {
        std::istringstream is(cmd);

        std::string token;
        is >> std::skipws >> token;
        if (token.empty())
            continue;
        token = to_lower(token);

        if (token == "go" || token == "eval")
        {
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")\n";
            if (token == "go")
            {
                auto limits = parse_limits(is);

                if (limits.perft)
                    infoNodes = engine.perft(limits.depth, limits.detail);
                else
                {
                    engine.start(limits);
                    engine.wait_finish();
                }

                nodes += infoNodes;
                infoNodes = 0;
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
            engine.init();  // May take a while
            startTime = now();
        }
    }

    elapsedTime += now() - startTime;
    // Ensure non-zero to avoid a 'divide by zero'
    elapsedTime = std::max(elapsedTime, 1ll);

#if !defined(NDEBUG)
    Debug::print();
#endif

    std::cerr << "\n==========================="        //
              << "\nTotal time (ms) : " << elapsedTime  //
              << "\nTotal Nodes     : " << nodes        //
              << "\nNodes/second    : " << 1000 * nodes / elapsedTime << '\n';

    engine_options()["ReportMinimal"] = reportMinimal;
    // Reset callback, to not capture a dangling reference to infoNodes
    engine.set_on_update_full(on_update_full);
}

namespace {

struct WinRateParams final {
    double a;
    double b;
};

WinRateParams win_rate_params(const Position& pos) noexcept {

    // The fitted model only uses data for material counts in [17, 78], and is anchored at count 58 (0.017241).
    double m = 0.017241 * std::clamp<short>(pos.material(), 17, 78);

    // Return a = p_a(material) and b = p_b(material).
    // clang-format off
    constexpr double as[4]{-41.25712052,  121.47473115, -124.46958843, 411.84490997};
    constexpr double bs[4]{ 84.92998051, -143.66658718,   80.09988253,  49.80869370};
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
    // (log(1/L - 1) - log(1/W - 1)) / (log(1/L - 1) + log(1/W - 1)).
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
      Overload{[](Score::Mate mate) -> std::string {
                   return "mate " + std::to_string(((mate.ply > 0) + mate.ply) / 2);
               },
               [](Score::Tablebase tb) -> std::string {
                   return "cp " + std::to_string((tb.win ? +TB_CP : -TB_CP) - tb.ply);
               },
               [](Score::Unit unit) -> std::string { return "cp " + std::to_string(unit.value); }};

    return score.visit(format);
}

char UCI::piece(PieceType pt) noexcept { return is_ok(pt) ? PieceChar[pt] : ' '; }
char UCI::piece(Piece pc) noexcept { return is_ok(pc) ? PieceChar[pc] : ' '; }

Piece UCI::piece(char pc) noexcept {
    auto pos = PieceChar.find(pc);
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
        return "(null)";

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

    for (const auto& m : legalMoves)
        if (ccan == move_to_can(m))
            return m;

    return Move::None();
}

Move UCI::can_to_move(const std::string& can, const Position& pos) noexcept {
    return can_to_move(can, MoveList<LEGAL>(pos));
}

namespace {

void on_update_end(const Search::EndInfo& info) noexcept {
    sync_cout << "info depth 0 score " << (info.inCheck ? "mate" : "cp") << " 0" << sync_endl;
}

void on_update_full(const Search::FullInfo& info) noexcept {
    std::ostringstream oss;
    oss << "info"                                                   //
        << " depth " << info.depth                                  //
        << " seldepth " << info.rootMove.selDepth                   //
        << " multipv " << info.multiPV                              //
        << " score " << UCI::format_score({info.value, info.pos});  //
    if (info.showBound)
        oss << (info.rootMove.lowerBound   ? " lowerbound"
                : info.rootMove.upperBound ? " upperbound"
                                           : "");
    if (info.showWDL)
        oss << " wdl " << UCI::to_wdl(info.value, info.pos);
    oss << " time " << info.time                     //
        << " nodes " << info.nodes                   //
        << " nps " << 1000 * info.nodes / info.time  //
        << " hashfull " << info.hashfull             //
        << " tbhits " << info.tbHits                 //
        << " pv";
    for (const auto& m : info.rootMove)
        oss << " " << UCI::move_to_can(m);
    sync_cout << oss.str() << sync_endl;
}

void on_update_iter(const Search::IterInfo& info) noexcept {
    std::ostringstream oss;
    oss << "info"                                           //
        << " depth " << info.depth                          //
        << " currmove " << UCI::move_to_can(info.currMove)  //
        << " currmovenumber " << info.currMoveNumber;       //
    sync_cout << oss.str() << sync_endl;
}

void on_update_move(const Search::MoveInfo& info) noexcept {
    sync_cout << "bestmove " << UCI::move_to_can(info.bestMove);
    if (info.ponderMove != Move::None())
        std::cout << " ponder " << UCI::move_to_can(info.ponderMove);
    std::cout << sync_endl;
}

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
        return "(null)";
    assert(MoveList<LEGAL>(pos).contains(m));

    std::string san;

    const Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(pos.piece_on(org)) == pos.side_to_move());

    const PieceType pt = type_of(pos.piece_on(org));

    if (m.type_of() != CASTLING)
    {
        if (pt != PAWN)
        {
            san = piece(pt);
            if (pt != KING)
            {
                // Disambiguation if have more then one piece of type 'pt' that can reach 'to' with a legal move.
                switch (ambiguity(m, pos))
                {
                case AMBIGUITY_RANK :
                    san += file(file_of(org));
                    break;
                case AMBIGUITY_FILE :
                    san += rank(rank_of(org));
                    break;
                case AMBIGUITY_SQUARE :
                    san += square(org);
                    break;
                default :;
                }
            }
        }

        if (pos.capture(m))
        {
            if (pt == PAWN)
                san = file(file_of(org));
            san += 'x';
        }

        san += square(dst);

        if (m.type_of() == PROMOTION)
        {
            assert(pt == PAWN);
            san += {'=', piece(m.promotion_type())};
        }
    }
    else
    {
        assert(pt == KING && rank_of(org) == rank_of(dst));
        san = (org < dst ? "O-O" : "O-O-O");
    }

    // Move marker for check & checkmate
    if (pos.gives_check(m))
    {
        StateInfo st;
        ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

        pos.do_move(m, st, true);
        san += (MoveList<LEGAL>(pos).size() != 0 ? '+' : '#');
        pos.undo_move(m);
    }

    return san;
}

// clang-format off
Move UCI::san_to_move(const std::string& san, Position& pos, const MoveList<LEGAL>& legalMoves) noexcept {
    assert(2 <= san.length() && san.length() <= 9);
    std::string csan = san;
    if (starts_with(csan, "O-") || starts_with(csan, "o-") || starts_with(csan, "0-"))
        for (char ch : {'o', '0'})
            std::replace(csan.begin(), csan.end(), ch, 'O');

    for (const auto& m : legalMoves)
        if (csan == move_to_san(m, pos))
            return m;

    return Move::None();
}

Move UCI::san_to_move(const std::string& san, Position& pos) noexcept {
    return san_to_move(san, pos, MoveList<LEGAL>(pos));
}

Move UCI::mix_to_move(const std::string& mix, Position& pos, const MoveList<LEGAL>& legalMoves) noexcept
{
    Move m = Move::None();
    auto length = mix.length();
    if (length < 2)
        return m;
    if (length < 4 || starts_with(to_lower(mix), "o-") || starts_with(mix, "0-"))
        return UCI::san_to_move(mix, pos, legalMoves);
    if (length <= 5)
        m = UCI::can_to_move(mix, legalMoves);
    if (m == Move::None() && length <= 9)
        return UCI::san_to_move(mix, pos, legalMoves);
    return m;
}
// clang-format on

}  // namespace DON
