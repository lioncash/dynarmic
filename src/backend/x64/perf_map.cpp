/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstddef>
#include <string>

#ifdef __linux__

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/format.h>

#include "common/common_types.h"

namespace Dynarmic::BackendX64 {

namespace {
std::mutex mutex;
std::FILE* file = nullptr;

void OpenFile() {
    const char* perf_dir = std::getenv("PERF_BUILDID_DIR");
    if (!perf_dir) {
        file = nullptr;
        return;
    }

    const pid_t pid = getpid();
    const std::string filename = fmt::format("{:s}/perf-{:d}.map", perf_dir, pid);

    file = std::fopen(filename.c_str(), "w");
    if (!file) {
        return;
    }

    std::setvbuf(file, nullptr, _IONBF, 0);
}
} // anonymous namespace

namespace detail {
void PerfMapRegister(const void* start, const void* end, const std::string& friendly_name) {
    std::lock_guard guard{mutex};

    if (!file) {
        OpenFile();
        if (!file) {
            return;
        }
    }

    const std::string line = fmt::format("{:016x} {:016x} {:s}\n", reinterpret_cast<u64>(start), reinterpret_cast<u64>(end) - reinterpret_cast<u64>(start), friendly_name);
    std::fwrite(line.data(), sizeof *line.data(), line.size(), file);
}
} // namespace detail

void PerfMapClear() {
    std::lock_guard guard{mutex};

    if (!file) {
        return;
    }

    std::fclose(file);
    file = nullptr;
    OpenFile();
}

} // namespace Dynarmic::BackendX64

#else

namespace Dynarmic::BackendX64 {

namespace detail {
void PerfMapRegister(const void*, const void*, const std::string&) {}
} // namespace detail

void PerfMapClear() {}

} // namespace Dynarmic::BackendX64

#endif
