/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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

#include "../utility/sync_ostream.h"

#include "../utility/store.h"
#include "../utility/types.h"

namespace DON {

namespace {

// OStreamMutexes
//
// Stores a dedicated mutex for each std::ostream pointer, allowing multiple
// threads to synchronize access to the same stream without unnecessarily
// locking unrelated streams.
//
// Key features:
//  - Thread-safe: access is synchronized by Store.
//  - Per-stream synchronization: each ostream has its own mutex.
//  - Lazy initialization: entries are created when first requested.
//  - Stable lifetime: entries remain stored for the lifetime of Store.
//
// Lifetime:
//  - Store does not own the std::ostream objects.
//  - Mutexes remain stored until Store is destroyed.
using OStreamMutexStore = Store<std::ostream*, std::mutex>;

OStreamMutexStore& ostream_mutex_store() noexcept {
    static OStreamMutexStore ostreamMutexStore{usize{64}, 0.75f};
    return ostreamMutexStore;
}

std::mutex& get_ostream_mutex(std::ostream* const os) noexcept {
    return ostream_mutex_store().read_or_write_default(os);
}

}  // namespace

SyncOStream::SyncOStream(std::ostream& osRef) :
    os{&osRef},
    uniqueLock{get_ostream_mutex(os)} {}

SyncOStream::SyncOStream(SyncOStream&& syncOs) noexcept :
    os{std::exchange(syncOs.os, nullptr)},
    uniqueLock{std::move(syncOs.uniqueLock)} {}

SyncOStream& SyncOStream::operator<<(IOSManip manip) & {
    assert(os != nullptr && "Cannot use a moved-from SyncOStream");

    manip(*os);
    return *this;
}

SyncOStream&& SyncOStream::operator<<(IOSManip manip) && {
    assert(os != nullptr && "Cannot use a moved-from SyncOStream");

    manip(*os);
    return std::move(*this);
}

SyncOStream& SyncOStream::operator<<(OStreamManip manip) & {
    assert(os != nullptr && "Cannot use a moved-from SyncOStream");

    manip(*os);
    return *this;
}

SyncOStream&& SyncOStream::operator<<(OStreamManip manip) && {
    assert(os != nullptr && "Cannot use a moved-from SyncOStream");

    manip(*os);
    return std::move(*this);
}

// ----------------------------------

SyncOStream sync_os(std::ostream& os) { return SyncOStream(os); }

}  // namespace DON
