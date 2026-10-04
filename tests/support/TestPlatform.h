#pragma once

// Test-only platform helpers: the one place the test suite distinguishes
// Windows from POSIX for process ids and temporary directories.

#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ledgercore::testsupport {

// The current process id, for naming temporary files so concurrently
// running test processes never collide.
inline long long currentProcessId() {
#ifdef _WIN32
    return static_cast<long long>(::_getpid());
#else
    return static_cast<long long>(::getpid());
#endif
}

// Creates and returns a new, empty directory under the system temporary
// directory whose name starts with `label`. create_directory() succeeding
// guarantees the directory did not exist before, so the result is unique
// even across concurrently running processes.
inline std::filesystem::path makeUniqueTempDirectory(const std::string& label) {
    static int counter = 0;
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    while (true) {
        ++counter;
        const std::filesystem::path candidate =
            base / (label + "_" + std::to_string(currentProcessId()) + "_" + std::to_string(counter));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) {
            return candidate;
        }
        if (ec) {
            throw std::filesystem::filesystem_error("cannot create temporary directory", candidate, ec);
        }
    }
}

} // namespace ledgercore::testsupport
