
#include "numa.h"

namespace DON {

usize hardware_concurrency() noexcept {
    usize hardwareConcurrency = std::thread::hardware_concurrency();

    // Get all processors across all processor groups on windows, since
    // ::hardware_concurrency() only returns the number of processors in
    // the first group, because only these are available to std::thread.
#if defined(_WIN64)
    hardwareConcurrency =
      std::max<usize>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), hardwareConcurrency);
#endif

    return hardwareConcurrency;
}

}  // namespace DON
