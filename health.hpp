#pragma once

#include <numeric>
#ifdef _WIN32
#include <sys/stat.h>
#endif

#include "thermal.hpp"
#include "utils.hpp"

struct DiskUsage {
    bool valid = false;
    unsigned long long total = 0;
    unsigned long long used = 0;
    unsigned long long free = 0;
};

inline DiskUsage diskUsageFromBlockCounts(unsigned long long blocks, unsigned long long free_blocks,
                                          unsigned long long available_blocks,
                                          unsigned long long block_size) {
    DiskUsage usage;
    usage.valid = block_size != 0;
    if (!usage.valid) return usage;
    usage.total = blocks * block_size;
    usage.used = (blocks > free_blocks ? blocks - free_blocks : 0) * block_size;
    usage.free = available_blocks * block_size;
    return usage;
}

inline DiskUsage getRootDiskUsage() {
    DiskUsage usage;
#ifdef _WIN32
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExA("C:\\", &freeBytesAvailable, &totalNumberOfBytes,
                            &totalNumberOfFreeBytes)) {
        usage.valid = true;
        usage.total = totalNumberOfBytes.QuadPart;
        usage.free = totalNumberOfFreeBytes.QuadPart;
        usage.used = usage.total > usage.free ? usage.total - usage.free : 0;
    }
#else
    struct statvfs stats{};
    if (::statvfs("/", &stats) != 0) {
        return usage;
    }

    usage = diskUsageFromBlockCounts(static_cast<unsigned long long>(stats.f_blocks),
                                     static_cast<unsigned long long>(stats.f_bfree),
                                     static_cast<unsigned long long>(stats.f_bavail),
                                     static_cast<unsigned long long>(stats.f_frsize));
#endif
    return usage;
}

class BenchmarkFileOwner {
public:
    BenchmarkFileOwner(int descriptor, fs::path path) : descriptor_(descriptor), path_(path) {}
    BenchmarkFileOwner(const BenchmarkFileOwner&) = delete;
    BenchmarkFileOwner& operator=(const BenchmarkFileOwner&) = delete;
    ~BenchmarkFileOwner() {
        if (descriptor_ >= 0) {
#ifdef _WIN32
            _close(descriptor_);
#else
            ::close(descriptor_);
#endif
        }
        std::error_code ignored;
        fs::remove(path_, ignored);
    }
    int descriptor() const { return descriptor_; }

private:
    int descriptor_ = -1;
    fs::path path_;
};

struct DiskBenchmarkResult {
    std::optional<double> write_mbps;
    std::optional<double> read_mbps;
    bool cached_read = true;
    std::string error;
};

inline std::optional<unsigned long long> freeSpaceAt(const fs::path& path) {
#ifdef _WIN32
    ULARGE_INTEGER available{}, total{}, free{};
    if (!GetDiskFreeSpaceExW(path.wstring().c_str(), &available, &total, &free)) {
        return std::nullopt;
    }
    return available.QuadPart;
#else
    struct statvfs stats{};
    if (::statvfs(path.c_str(), &stats) != 0) return std::nullopt;
    return static_cast<unsigned long long>(stats.f_bavail) *
           static_cast<unsigned long long>(stats.f_frsize);
#endif
}

inline int createBenchmarkFile(const fs::path& path) {
#ifdef _WIN32
    return _wopen(path.wstring().c_str(), _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                  _S_IREAD | _S_IWRITE);
#else
    return ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
#endif
}

inline bool seekBenchmarkStart(int descriptor) {
#ifdef _WIN32
    return _lseeki64(descriptor, 0, SEEK_SET) == 0;
#else
    return ::lseek(descriptor, 0, SEEK_SET) == 0;
#endif
}

inline std::optional<fs::path> automaticBenchmarkDirectory() {
    std::error_code path_error;
    fs::path directory = fs::current_path(path_error);
    if (!path_error) return directory;

    path_error.clear();
    directory = fs::temp_directory_path(path_error);
    if (!path_error) return directory;
    return std::nullopt;
}

inline DiskBenchmarkResult runDiskBenchmark(const fs::path& requested_directory,
                                            std::size_t size_mebibytes = 64) {
    DiskBenchmarkResult result;
    std::error_code path_error;
    const fs::path directory = fs::absolute(requested_directory, path_error);
    if (path_error) {
        result.error = "benchmark directory cannot be resolved";
        return result;
    }
    const fs::file_status status = fs::symlink_status(directory, path_error);
    if (path_error || !fs::is_directory(status) || fs::is_symlink(status)) {
        result.error = "benchmark directory must be an existing, non-link directory";
        return result;
    }

    const unsigned long long total_bytes =
        static_cast<unsigned long long>(size_mebibytes) * 1024ULL * 1024ULL;
    const auto free_bytes = freeSpaceAt(directory);
    if (!free_bytes || *free_bytes < total_bytes + 16ULL * 1024ULL * 1024ULL) {
        result.error = "insufficient free space for the bounded benchmark";
        return result;
    }

#ifdef _WIN32
    const int process_id = _getpid();
#else
    const int process_id = static_cast<int>(::getpid());
#endif
    const long long nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path file_path = directory / ("checkbench." + std::to_string(process_id) + "." +
                                            std::to_string(nonce) + ".dat");
    const int descriptor = createBenchmarkFile(file_path);
    if (descriptor < 0) {
        result.error = "atomic benchmark file creation failed";
        return result;
    }
    BenchmarkFileOwner file(descriptor, file_path);

    constexpr std::size_t chunk_size = 1024 * 1024;
    std::vector<char> buffer(chunk_size, 0x5a);
    unsigned long long written = 0;
    const auto write_start = std::chrono::steady_clock::now();
    while (written < total_bytes) {
        const std::size_t requested = static_cast<std::size_t>(
            std::min<unsigned long long>(chunk_size, total_bytes - written));
#ifdef _WIN32
        const int count = _write(descriptor, buffer.data(), static_cast<unsigned int>(requested));
#else
        const ssize_t count = ::write(descriptor, buffer.data(), requested);
#endif
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            result.error = "benchmark write failed";
            return result;
        }
        written += static_cast<unsigned long long>(count);
    }
#ifdef _WIN32
    if (_commit(descriptor) != 0) {
#else
    if (::fsync(descriptor) != 0) {
#endif
        result.error = "benchmark flush failed";
        return result;
    }
    const auto write_end = std::chrono::steady_clock::now();
    const double write_seconds = std::chrono::duration<double>(write_end - write_start).count();
    if (write_seconds <= 0.0 || !seekBenchmarkStart(descriptor)) {
        result.error = "benchmark seek failed";
        return result;
    }
    result.write_mbps = static_cast<double>(size_mebibytes) / write_seconds;

    unsigned long long read_bytes = 0;
    volatile unsigned char checksum = 0;
    const auto read_start = std::chrono::steady_clock::now();
    while (read_bytes < total_bytes) {
        const std::size_t requested = static_cast<std::size_t>(
            std::min<unsigned long long>(chunk_size, total_bytes - read_bytes));
#ifdef _WIN32
        const int count = _read(descriptor, buffer.data(), static_cast<unsigned int>(requested));
#else
        const ssize_t count = ::read(descriptor, buffer.data(), requested);
#endif
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            result.error = "benchmark read ended before the expected byte count";
            return result;
        }
        read_bytes += static_cast<unsigned long long>(count);
        for (int i = 0; i < count; i += 4096) checksum ^= static_cast<unsigned char>(buffer[i]);
    }
    const auto read_end = std::chrono::steady_clock::now();
    const double read_seconds = std::chrono::duration<double>(read_end - read_start).count();
    if (read_bytes != total_bytes || read_seconds <= 0.0) {
        result.error = "benchmark read verification failed";
        return result;
    }
    result.read_mbps = static_cast<double>(size_mebibytes) / read_seconds;
    (void)checksum;
    return result;
}

inline void printImportantHealthSection(bool run_benchmark,
                                        const std::optional<fs::path>& benchmark_directory,
                                        const ThermalSnapshot& thermal_snapshot) {
    printSectionHeader("OTHER IMPORTANT INFO");

    const DiskUsage disk = getRootDiskUsage();
    if (disk.valid) {
        printKeyValue("Root Disk Total", formatBytes(static_cast<long double>(disk.total)));
        printKeyValue("Root Disk Used", formatBytes(static_cast<long double>(disk.used)));
        printKeyValue("Root Disk Free", formatBytes(static_cast<long double>(disk.free)));
    } else {
        printKeyValue("Root Disk Usage",
#ifdef _WIN32
                      colorize("disk usage could not be queried", ansi::YELLOW)
#else
                      colorize("UNAVAILABLE", ansi::YELLOW)
#endif
        );
    }

    if (!run_benchmark) {
        printKeyValue("Disk Benchmark", "skipped (use --full)");
    } else if (!benchmark_directory) {
        printKeyValue("Disk Benchmark",
                      colorize("automatic benchmark directory unavailable", ansi::YELLOW));
        recordCheck(CheckState::Unavailable);
    } else {
        constexpr std::size_t benchmark_size_mebibytes = 256;
        constexpr std::size_t benchmark_runs = 3;
        std::cerr << "[bench] disk: three 256 MiB runs, cleanup guaranteed...\n";
        printKeyValue("Disk Benchmark Directory", benchmark_directory->string());
        const auto start = std::chrono::steady_clock::now();
        std::vector<double> write_results;
        std::vector<double> read_results;
        bool benchmark_failed = false;
        for (std::size_t run = 1; run <= benchmark_runs; ++run) {
            const DiskBenchmarkResult benchmark =
                runDiskBenchmark(*benchmark_directory, benchmark_size_mebibytes);
            const std::string run_number = std::to_string(run);
            if (!benchmark.error.empty()) {
                printKeyValue("Disk Benchmark Run " + run_number,
                              colorize(benchmark.error, ansi::RED));
                benchmark_failed = true;
                continue;
            }

            write_results.push_back(*benchmark.write_mbps);
            read_results.push_back(*benchmark.read_mbps);
            std::ostringstream write;
            write << std::fixed << std::setprecision(2) << *benchmark.write_mbps << " MiB/s";
            std::ostringstream read;
            read << std::fixed << std::setprecision(2) << *benchmark.read_mbps
                 << " MiB/s (cached)";
            printKeyValue("Disk Write Run " + run_number + " (256MiB)", write.str());
            printKeyValue("Disk Read Run " + run_number + " (256MiB)", read.str());
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        if (!write_results.empty()) {
            const double write_average =
                std::accumulate(write_results.begin(), write_results.end(), 0.0) /
                static_cast<double>(write_results.size());
            const double read_average =
                std::accumulate(read_results.begin(), read_results.end(), 0.0) /
                static_cast<double>(read_results.size());
            std::ostringstream write;
            write << std::fixed << std::setprecision(2) << write_average << " MiB/s";
            std::ostringstream read;
            read << std::fixed << std::setprecision(2) << read_average << " MiB/s (cached)";
            const std::string completed_runs =
                write_results.size() == benchmark_runs
                    ? std::to_string(benchmark_runs) + " runs"
                    : std::to_string(write_results.size()) + "/" +
                          std::to_string(benchmark_runs) + " runs";
            printKeyValue("Disk Write Average (" + completed_runs + ")", write.str());
            printKeyValue("Disk Read Average (" + completed_runs + ")", read.str());
        }

        std::ostringstream duration;
        duration << std::fixed << std::setprecision(2) << elapsed << " s";
        printKeyValue("Disk Benchmark Elapsed", duration.str());
        recordCheck(benchmark_failed || write_results.size() != benchmark_runs ? CheckState::Fail
                                                                               : CheckState::Pass);
    }

    printSubHeader("Thermal Zones");
    const std::vector<ThermalInfo>& thermals = thermal_snapshot.values;
    if (thermals.empty()) {
        std::cout << "  "
#ifdef _WIN32
                  << colorize(thermal_snapshot.error.empty() ? "No ACPI thermal sensors exposed"
                                                             : thermal_snapshot.error,
                              ansi::YELLOW)
#else
                  << colorize(
                         thermal_snapshot.error.empty() ? "UNAVAILABLE" : thermal_snapshot.error,
                         ansi::YELLOW)
#endif
                  << "\n";
    } else {
        for (const auto& thermal : thermals) {
            if (thermal.temp_c) {
                std::ostringstream temp;
                temp << std::fixed << std::setprecision(1) << *thermal.temp_c << " C";
                std::cout << "    " << sanitizeTerminalText(thermal.zone) << " ("
                          << sanitizeTerminalText(thermal.type) << ") = " << temp.str() << "\n";
            } else {
                std::cout << "    " << sanitizeTerminalText(thermal.zone) << " ("
                          << sanitizeTerminalText(thermal.type) << ") = "
#ifdef _WIN32
                          << colorize("temperature not reported", ansi::YELLOW)
#else
                          << colorize("N/A", ansi::YELLOW)
#endif
                          << "\n";
            }
        }
    }
}
