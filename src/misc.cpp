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

#include "misc.h"

#if defined(_WIN32)
    #if _WIN32_WINNT < 0x0601
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Force to include needed API prototypes
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX  // Disable macros min() and max()
    #endif
    #include <windows.h>
// The needed Windows API for processor groups could be missed from old Windows
// versions, so instead of calling them directly (forcing the linker to resolve
// the calls at compile time), try to load them at runtime. To do this we need
// first to define the corresponding function pointers.
extern "C" {
using Fun1_t = bool (*)(LOGICAL_PROCESSOR_RELATIONSHIP,
                        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,
                        PDWORD);
using Fun2_t = bool (*)(USHORT, PGROUP_AFFINITY);
using Fun3_t = bool (*)(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY);
using Fun4_t = bool (*)(USHORT, PGROUP_AFFINITY, USHORT, PUSHORT);
using Fun5_t = WORD (*)();
using Fun6_t = bool (*)(HANDLE, DWORD, PHANDLE);
using Fun7_t = bool (*)(LPCSTR, LPCSTR, PLUID);
using Fun8_t = bool (*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
}
#endif

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>

#include "types.h"

#if defined(__linux__) && !defined(__ANDROID__)
    #include <sys/mman.h>
#endif

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) \
  || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32)) \
  || defined(__e2k__)
    #define POSIX_ALIGNED_ALLOC
    #include <stdlib.h>
#endif

namespace DON {

namespace {

// Version number or dev.
constexpr std::string_view Version("dev");

// Our fancy logging facility. The trick here is to replace cin.rdbuf() and
// cout.rdbuf() with two Tie objects that tie cin and cout to a file stream. We
// can toggle the logging of std::cout and std:cin at runtime whilst preserving
// usual I/O functionality, all without changing a single line of code!
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
class Tie final: public std::streambuf {  // MSVC requires split streambuf for cin and cout
   public:
    Tie(std::streambuf* b1, std::streambuf* b2) noexcept :
        buf1(b1),
        buf2(b2) {}

    int sync() noexcept override { return buf2->pubsync(), buf1->pubsync(); }
    int overflow(int ch) noexcept override { return log(buf1->sputc(char(ch)), "<< "); }
    int underflow() noexcept override { return buf1->sgetc(); }
    int uflow() noexcept override { return log(buf1->sbumpc(), ">> "); }

    int log(int ch, const char* prefix) noexcept {
        static int lastCh = '\n';  // Single log file

        if (lastCh == '\n')
            buf2->sputn(prefix, std::strlen(prefix));

        return lastCh = buf2->sputc(char(ch));
    }

    std::streambuf *buf1, *buf2;
};

class Logger final {

    Logger(std::istream& is, std::ostream& os) noexcept :
        istream(is),
        ostream(os),
        iTie(is.rdbuf(), ofstream.rdbuf()),
        oTie(os.rdbuf(), ofstream.rdbuf()) {}

    ~Logger() noexcept { start(""); }

    std::istream& istream;
    std::ostream& ostream;
    Tie           iTie, oTie;
    std::ofstream ofstream;

   public:
    static void start(std::string_view fname) noexcept {
        static Logger logger(std::cin, std::cout);  // Tie std::cin and std::cout to a file.

        if (logger.ofstream.is_open())
        {
            logger.istream.rdbuf(logger.iTie.buf1);
            logger.ostream.rdbuf(logger.oTie.buf1);

            logger.ofstream << "[" << SystemClock::now() << "] <-\n";
            logger.ofstream.close();
        }

        if (fname.empty() || fname == "<empty>")
            return;

        logger.ofstream.open(fname.data(), std::ios_base::out | std::ios_base::app);

        if (!logger.ofstream.is_open())
        {
            std::cerr << "Unable to open debug log file: " << fname << '\n';
            exit(EXIT_FAILURE);
        }
        logger.ofstream << "[" << SystemClock::now() << "] ->\n";

        logger.istream.rdbuf(&logger.iTie);
        logger.ostream.rdbuf(&logger.oTie);
    }
};

}  // namespace

// Returns the full name of the current DON version.
// For local dev compiles we try to append the commit sha and commit date
// from git if that fails only the local compilation date is set and "nogit" is specified:
// DON dev-YYYYMMDD-SHA
// or
// DON dev-YYYYMMDD-nogit
//
// For releases (non-dev builds) we only include the version number:
// DON version
std::string engine_info(bool uci) noexcept {
    std::ostringstream oss;
    oss << (uci ? "id name " : "") << "DON"
        << " " << Version;

    if constexpr (Version == "dev")
    {
        oss << "-";
#if defined(GIT_DATE)
        oss << STRINGIFY(GIT_DATE);
#else
        constexpr std::string_view Months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");

        std::istringstream iss(__DATE__);  // From compiler, format is "Sep 21 2008"
        std::string        month, day, year;
        iss >> month >> day >> year;
        oss << std::setfill('0') << std::setw(4) << year << std::setfill('0') << std::setw(2)
            << (1 + Months.find(month) / 4) << std::setfill('0') << std::setw(2) << day;
#endif

        oss << "-";

#if defined(GIT_SHA)
        oss << STRINGIFY(GIT_SHA);
#else
        oss << "nogit";
#endif
    }

    oss << (uci ? "\nid author " : " by ") << "Ehsan Rashid & Stockfish developers";

    return oss.str();
}

// Returns a string trying to describe the compiler we use
std::string compiler_info() noexcept {

#define MAKE_VERSION_STRING(major, minor, patch) \
    STRINGIFY(major) "." STRINGIFY(minor) "." STRINGIFY(patch)

    // Predefined macros hell:
    //
    // __GNUC__                Compiler is GCC, Clang or ICX
    // __clang__               Compiler is Clang or ICX
    // __INTEL_LLVM_COMPILER   Compiler is ICX
    // _MSC_VER                Compiler is MSVC
    // _WIN32                  Building on Windows (any)
    // _WIN64                  Building on Windows 64 bit

    std::string compiler = "\nCompiled by                : ";

#if defined(__INTEL_LLVM_COMPILER)
    compiler += "ICX ";
    compiler += STRINGIFY(__INTEL_LLVM_COMPILER);
#elif defined(__clang__)
    compiler += "clang++ ";
    compiler += MAKE_VERSION_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    compiler += "MSVC ";
    compiler += "(version ";
    compiler += STRINGIFY(_MSC_FULL_VER) "." STRINGIFY(_MSC_BUILD);
    compiler += ")";
#elif defined(__e2k__) && defined(__LCC__)
    #define DOT_VER(n) \
        compiler += char('.'); \
        compiler += char('0' + (n) / 10); \
        compiler += char('0' + (n) % 10);

    compiler += "MCST LCC ";
    compiler += "(version ";
    compiler += std::to_string(__LCC__ / 100);
    DOT_VER(__LCC__ % 100) DOT_VER(__LCC_MINOR__) compiler += ")";
#elif __GNUC__
    compiler += "g++ (GNUC) ";
    compiler += MAKE_VERSION_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    compiler += "Unknown compiler ";
    compiler += "(unknown version)";
#endif

#if defined(__APPLE__)
    compiler += " on Apple";
#elif defined(__CYGWIN__)
    compiler += " on Cygwin";
#elif defined(__MINGW64__)
    compiler += " on MinGW64";
#elif defined(__MINGW32__)
    compiler += " on MinGW32";
#elif defined(__ANDROID__)
    compiler += " on Android";
#elif defined(__linux__)
    compiler += " on Linux";
#elif defined(_WIN64)
    compiler += " on Microsoft Windows 64-bit";
#elif defined(_WIN32)
    compiler += " on Microsoft Windows 32-bit";
#else
    compiler += " on unknown system";
#endif

    compiler += "\nCompilation architecture   : ";
#if defined(ARCH)
    compiler += STRINGIFY(ARCH);
#else
    compiler += "(undefined architecture)";
#endif

    compiler += "\nCompilation settings       : ";
    compiler += (Is64Bit ? "64bit" : "32bit");
#if defined(USE_VNNI)
    compiler += " VNNI";
#endif
#if defined(USE_AVX512)
    compiler += " AVX512";
#endif
#if defined(USE_PEXT)
    compiler += " BMI2";
#endif
#if defined(USE_AVX2)
    compiler += " AVX2";
#endif
#if defined(USE_SSE41)
    compiler += " SSE41";
#endif
#if defined(USE_SSSE3)
    compiler += " SSSE3";
#endif
#if defined(USE_SSE2)
    compiler += " SSE2";
#endif
#if defined(USE_POPCNT)
    compiler += " POPCNT";
#endif
#if defined(USE_NEON_DOTPROD)
    compiler += " NEON_DOTPROD";
#elif defined(USE_NEON)
    compiler += " NEON";
#endif

#if !defined(NDEBUG)
    compiler += " DEBUG";
#endif

    compiler += "\nCompiler __VERSION__ macro : ";
#if defined(__VERSION__)
    compiler += __VERSION__;
#else
    compiler += "(undefined macro)";
#endif

    compiler += '\n';

    return compiler;
}


// Debug functions used mainly to collect run-time statistics
namespace {

template<std::size_t N>
struct DebugInfo {
    std::atomic_int64_t data[N] = {0};

    constexpr inline std::atomic_int64_t& operator[](std::uint16_t index) noexcept {
        return data[index];
    }
};

constexpr std::uint8_t MaxSlot = 32;

DebugInfo<2> hit[MaxSlot];
DebugInfo<2> mean[MaxSlot];
DebugInfo<3> stdev[MaxSlot];
DebugInfo<6> correl[MaxSlot];

}  // namespace

void dbg_hit_on(bool cond, std::uint8_t slot) noexcept {

    ++hit[slot][0];
    if (cond)
        ++hit[slot][1];
}

void dbg_mean_of(std::int64_t value, std::uint8_t slot) noexcept {

    ++mean[slot][0];
    mean[slot][1] += value;
}

void dbg_stdev_of(std::int64_t value, std::uint8_t slot) noexcept {

    ++stdev[slot][0];
    stdev[slot][1] += value;
    stdev[slot][2] += value * value;
}

void dbg_correl_of(std::int64_t value1, std::int64_t value2, std::uint8_t slot) noexcept {

    ++correl[slot][0];
    correl[slot][1] += value1;
    correl[slot][2] += value1 * value1;
    correl[slot][3] += value2;
    correl[slot][4] += value2 * value2;
    correl[slot][5] += value1 * value2;
}

void dbg_print() noexcept {

    std::uint64_t n;

    auto avg = [&n](std::int64_t x) { return double(x) / n; };
    auto sqr = [](double x) { return x * x; };

    for (std::uint8_t i = 0; i < MaxSlot; ++i)
        if ((n = hit[i][0]))
            std::cerr << "Hit #" << int(i) << ": Total " << n << " Hits " << hit[i][1]
                      << " Hit Rate (%) " << 100.0 * avg(hit[i][1]) << '\n';

    for (std::uint8_t i = 0; i < MaxSlot; ++i)
        if ((n = mean[i][0]))
            std::cerr << "Mean #" << int(i) << ": Total " << n << " Mean " << avg(mean[i][1])
                      << '\n';

    for (std::uint8_t i = 0; i < MaxSlot; ++i)
        if ((n = stdev[i][0]))
        {
            double r = std::sqrt(avg(stdev[i][2]) - sqr(avg(stdev[i][1])));
            std::cerr << "Stdev #" << int(i) << ": Total " << n << " Stdev " << r << '\n';
        }

    for (std::uint8_t i = 0; i < MaxSlot; ++i)
        if ((n = correl[i][0]))
        {
            double r = (avg(correl[i][5]) - avg(correl[i][1]) * avg(correl[i][3]))
                     / (std::sqrt(avg(correl[i][2]) - sqr(avg(correl[i][1])))
                        * std::sqrt(avg(correl[i][4]) - sqr(avg(correl[i][3]))));
            std::cerr << "Correl #" << int(i) << ": Total " << n << " Coefficient " << r << '\n';
        }
}

std::ostream& operator<<(std::ostream&                            os,
                         [[maybe_unused]] SystemClock::time_point timePoint) noexcept {

#if defined(_WIN32)
    std::string str;

    time_t rawTime = SystemClock::to_time_t(timePoint);
    tm     localTm;
    localtime_s(&localTm, &rawTime);
    auto format = "%Y.%m.%d-%H.%M.%S";

    constexpr std::size_t BuffSize = 32;
    char                  buffer[BuffSize];
    strftime(buffer, BuffSize, format, &localTm);
    str += buffer;
    // Milli-second
    auto ms =
      std::chrono::duration_cast<MilliSeconds>(timePoint - SystemClock::from_time_t(rawTime))
        .count();
    str += "." + std::to_string(ms);
    os << str;
#endif

    return os;
}

// Used to serialize access to std::cout
// to avoid multiple threads writing at the same time.
std::ostream& operator<<(std::ostream& os, InOut io) noexcept {
    static std::mutex mutex;
    switch (io)
    {
    case IO_LOCK :
        mutex.lock();
        break;
    case IO_UNLOCK :
        mutex.unlock();
        break;
    }

    return os;
}

// Trampoline helper to avoid moving Logger to misc.h
void start_logger(const std::string& fname) noexcept { Logger::start(fname); }

void prefetch([[maybe_unused]] void* addr) noexcept {

#if defined(USE_PREFETCH)
    #if defined(_MSC_VER)
    _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
    #else
    __builtin_prefetch(addr);
    #endif
#endif
}

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with std_aligned_alloc() must be freed with std_aligned_free().
void* std_aligned_alloc(std::size_t alignment, std::size_t allocSize) noexcept {

#if defined(POSIX_ALIGNED_ALLOC)
    void* mem;
    return posix_memalign(&mem, alignment, allocSize) ? nullptr : mem;
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    return _mm_malloc(allocSize, alignment);
#elif defined(_WIN32)
    return _aligned_malloc(allocSize, alignment);
#else
    return std::aligned_alloc(alignment, allocSize);
#endif
}

void std_aligned_free(void* mem) noexcept {

#if defined(POSIX_ALIGNED_ALLOC)
    free(mem);
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    _mm_free(mem);
#elif defined(_WIN32)
    _aligned_free(mem);
#else
    free(mem);
#endif
}

// Return suitably aligned memory, if possible using large pages.
#if defined(_WIN32)

namespace {

void* aligned_large_pages_alloc_windows([[maybe_unused]] std::size_t allocSize) noexcept {

    #if !defined(_WIN64)
    return nullptr;
    #else

    std::size_t largePageSize = GetLargePageMinimum();
    if (!largePageSize)
        return nullptr;

    // Dynamically link OpenProcessToken, LookupPrivilegeValue and AdjustTokenPrivileges

    HMODULE hAdvapi32 = GetModuleHandle(TEXT("advapi32.dll"));
    if (!hAdvapi32)
        hAdvapi32 = LoadLibrary(TEXT("advapi32.dll"));

    auto openProcessToken =
      Fun6_t(reinterpret_cast<void (*)()>(GetProcAddress(hAdvapi32, "OpenProcessToken")));
    if (!openProcessToken)
        return nullptr;
    auto lookupPrivilegeValueA =
      Fun7_t(reinterpret_cast<void (*)()>(GetProcAddress(hAdvapi32, "LookupPrivilegeValueA")));
    if (!lookupPrivilegeValueA)
        return nullptr;
    auto adjustTokenPrivileges =
      Fun8_t(reinterpret_cast<void (*)()>(GetProcAddress(hAdvapi32, "AdjustTokenPrivileges")));
    if (!adjustTokenPrivileges)
        return nullptr;

    HANDLE hProcessToken{};

    // We need SeLockMemoryPrivilege, so try to enable it for the process
    if (!openProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hProcessToken))
        return nullptr;

    LUID  luid{};
    void* mem = nullptr;
    if (lookupPrivilegeValueA(nullptr, "SeLockMemoryPrivilege", &luid))
    {
        TOKEN_PRIVILEGES tp{};
        TOKEN_PRIVILEGES prevTp{};
        DWORD            prevTpLen = 0;

        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
        // we still need to query GetLastError() to ensure that the privileges were actually obtained.
        if (adjustTokenPrivileges(hProcessToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prevTp,
                                  &prevTpLen)
            && GetLastError() == ERROR_SUCCESS)
        {
            // Round up size to full pages and allocate
            allocSize = (allocSize + largePageSize - 1) & ~std::size_t(largePageSize - 1);
            mem       = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                                     PAGE_READWRITE);

            // Privilege no longer needed, restore previous state
            adjustTokenPrivileges(hProcessToken, FALSE, &prevTp, 0, nullptr, nullptr);
        }
    }

    CloseHandle(hProcessToken);

    return mem;
    #endif
}
}  // namespace
#endif

void* aligned_large_pages_alloc(std::size_t allocSize) noexcept {

#if defined(_WIN32)
    // Try to allocate large pages
    void* mem = aligned_large_pages_alloc_windows(allocSize);

    // Fall back to regular, page-aligned, allocation if necessary
    if (!mem)
        mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    return mem;
#else
    #if defined(__linux__)
    constexpr std::size_t Alignment = 2 * 1024 * 1024;  // assumed 2MB page size
    #else
    constexpr std::size_t Alignment = 4 * 1024;  // assumed small page size
    #endif

    // Round up to multiples of Alignment
    std::size_t roundAllocSize = ((allocSize + Alignment - 1) / Alignment) * Alignment;

    void* mem = std_aligned_alloc(Alignment, roundAllocSize);
    #if defined(MADV_HUGEPAGE)
    if (mem)
        madvise(mem, roundAllocSize, MADV_HUGEPAGE);
    #endif
    return mem;
#endif
}

// Free the previously allocated memory
// nop if mem == nullptr
void aligned_large_pages_free(void* mem) noexcept {
#if defined(_WIN32)
    if (mem && !VirtualFree(mem, 0, MEM_RELEASE))
    {
        DWORD err = GetLastError();
        std::cerr << "Failed to free large page memory. Error code: 0x" << std::hex << err
                  << std::dec << '\n';
        exit(EXIT_FAILURE);
    }
#else
    std_aligned_free(mem);
#endif
}

namespace WinProcGroup {

#if defined(_WIN32)

namespace {
// Retrieves logical processor information using Windows-specific
// API and returns the best node id for the thread with index idx.
// Original code from Texel by Peter Österlund.
std::int16_t best_node(std::uint16_t idx) noexcept {
    // Early exit if the needed API is not available at runtime
    HMODULE k32 = GetModuleHandle(TEXT("Kernel32.dll"));

    auto getLogicalProcessorInformationEx =
      Fun1_t(reinterpret_cast<void (*)()>(GetProcAddress(k32, "GetLogicalProcessorInformationEx")));
    if (!getLogicalProcessorInformationEx)
        return -1;

    DWORD returnLength;
    // First call to GetLogicalProcessorInformationEx() to get returnLength.
    // We expect the call to fail due to null buffer.
    if (getLogicalProcessorInformationEx(RelationAll, nullptr, &returnLength))
        return -1;

    // Once we know returnLength, allocate the buffer
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buffer, *ptr;
    buffer = ptr = static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(malloc(returnLength));

    // Second call to GetLogicalProcessorInformationEx(), now we expect to succeed
    if (!getLogicalProcessorInformationEx(RelationAll, buffer, &returnLength))
    {
        free(buffer);
        return -1;
    }

    std::uint16_t nodes   = 0;
    std::uint16_t cores   = 0;
    std::uint16_t threads = 0;

    DWORD byteOffset = 0;
    while (byteOffset < returnLength)
    {
        switch (ptr->Relationship)
        {
        case RelationProcessorCore :
            ++cores;
            threads += (ptr->Processor.Flags == LTP_PC_SMT) ? 2 : 1;
            break;
        case RelationNumaNode :
            ++nodes;
            break;
        default :;
        }

        assert(ptr->Size);
        byteOffset += ptr->Size;
        ptr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) (((char*) ptr) + ptr->Size);
    }
    free(buffer);

    std::vector<std::uint16_t> groups;

    // Run as many threads as possible on the same node until the core limit is
    // reached, then move on to filling the next node.
    for (std::uint16_t n = 0; n < nodes; ++n)
        for (std::uint16_t c = 0; c < cores / nodes; ++c)
            groups.push_back(n);

    // In case a core has more than one logical processor (we assume 2) and we
    // still have threads to allocate, spread them evenly across available nodes.
    for (std::uint16_t t = 0; t < threads - cores; ++t)
        groups.push_back(t % nodes);

    // If we still have more threads than the total number of logical processors
    // then return -1 and let the OS to decide what to do.
    return idx < groups.size() ? groups[idx] : -1;
}
}  // namespace
#endif

// Sets the group affinity of the current thread
void bind_thread([[maybe_unused]] std::uint16_t idx) noexcept {

#if defined(_WIN32)
    // Use only local variables to be thread-safe
    std::int16_t node = best_node(idx);

    if (node < 0)
        return;

    // Early exit if the needed API are not available at runtime
    HMODULE k32 = GetModuleHandle(TEXT("Kernel32.dll"));

    auto getNumaNodeProcessorMaskEx =
      Fun2_t(reinterpret_cast<void (*)()>(GetProcAddress(k32, "GetNumaNodeProcessorMaskEx")));
    auto setThreadGroupAffinity =
      Fun3_t(reinterpret_cast<void (*)()>(GetProcAddress(k32, "SetThreadGroupAffinity")));
    auto getNumaNodeProcessorMask2 =
      Fun4_t(reinterpret_cast<void (*)()>(GetProcAddress(k32, "GetNumaNodeProcessorMask2")));
    auto getMaximumProcessorGroupCount =
      Fun5_t(reinterpret_cast<void (*)()>(GetProcAddress(k32, "GetMaximumProcessorGroupCount")));

    if (!getNumaNodeProcessorMaskEx || !setThreadGroupAffinity)
        return;

    if (!getNumaNodeProcessorMask2 || !getMaximumProcessorGroupCount)
    {
        GROUP_AFFINITY affinity;
        if (getNumaNodeProcessorMaskEx(node, &affinity))
            setThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr);
    }
    else
    {
        // If a numa node has more than one processor group, we assume they are
        // sized equal and we spread threads evenly across the groups.
        USHORT elements = getMaximumProcessorGroupCount();
        auto   affinity = static_cast<GROUP_AFFINITY*>(malloc(elements * sizeof(GROUP_AFFINITY)));

        USHORT returnedElements;
        if (getNumaNodeProcessorMask2(node, affinity, elements, &returnedElements))
            setThreadGroupAffinity(GetCurrentThread(), &affinity[idx % returnedElements], nullptr);

        free(affinity);
    }
#endif
}

}  // namespace WinProcGroup

#if defined(_WIN32)
    #include <direct.h>
    #define GETCWD _getcwd
#else
    #include <unistd.h>
    #define GETCWD getcwd
#endif

std::string CommandLine::get_binary_directory(const std::string& path) noexcept {

#if defined(_WIN32)
    std::string pathSeparator = "\\";
    #if defined(_MSC_VER)
    // Under windows path may not have the extension.
    // Also _get_pgmptr() had issues in some Windows 10 versions,
    // so check returned values carefully.
    char* pgmptr = nullptr;
    if (!_get_pgmptr(&pgmptr) && pgmptr != nullptr && *pgmptr)
        path = pgmptr;
    #endif
#else
    std::string pathSeparator = "/";
#endif

    // Extract the binary directory path
    std::string binaryDirectory = path;

    std::size_t pos = binaryDirectory.find_last_of("\\/");
    if (pos == std::string::npos)
        binaryDirectory = "." + pathSeparator;
    else
        binaryDirectory.resize(pos + 1);

    // Pattern replacement: "./" at the start of path is replaced by the working directory
    if (binaryDirectory.find("." + pathSeparator) == 0)
    {
        // Extract the working directory
        std::string workingDirectory = CommandLine::get_working_directory();

        binaryDirectory.replace(0, 1, workingDirectory);
    }

    return binaryDirectory;
}

std::string CommandLine::get_working_directory() noexcept {
    std::string workingDirectory;

    constexpr std::uint32_t BuffSize = 40000;
    char                    buffer[BuffSize];
    char*                   cwd = GETCWD(buffer, BuffSize);
    if (cwd)
        workingDirectory = cwd;

    return workingDirectory;
}

}  // namespace DON
