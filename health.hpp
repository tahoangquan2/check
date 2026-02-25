#pragma once

#include <numeric>

#include "utils.hpp"

struct DiskUsage {
    bool valid = false;
    unsigned long long total = 0;
    unsigned long long used = 0;
    unsigned long long free = 0;
};

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

    const unsigned long long block_size = static_cast<unsigned long long>(stats.f_frsize);
    const unsigned long long total = static_cast<unsigned long long>(stats.f_blocks) * block_size;
    const unsigned long long free = static_cast<unsigned long long>(stats.f_bavail) * block_size;
    const unsigned long long used = total > free ? total - free : 0;

    usage.valid = true;
    usage.total = total;
    usage.used = used;
    usage.free = free;
#endif
    return usage;
}

inline std::optional<double> runSingleWriteBenchmark(std::size_t size_mb, int run_id,
                                                     bool keep_file = false) {
    const std::string file_path =
#ifdef _WIN32
        "check_bench_" + std::to_string(getpid()) + "_" + std::to_string(run_id) + ".dat";
    const int fd = _open(file_path.c_str(), _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY, 0600);
#else
        "/tmp/check_bench_" + std::to_string(::getpid()) + "_" + std::to_string(run_id) + ".dat";
    const int fd = ::open(file_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
#endif
    if (fd < 0) {
        return std::nullopt;
    }

    constexpr std::size_t chunk_size = 1024 * 1024;
    std::vector<char> buffer(chunk_size, 0);
    const unsigned long long total_bytes =
        static_cast<unsigned long long>(size_mb) * 1024ULL * 1024ULL;

    unsigned long long written = 0;
    const auto start = std::chrono::steady_clock::now();
    bool ok = true;

    while (written < total_bytes) {
        const std::size_t to_write = static_cast<std::size_t>(
            std::min<unsigned long long>(chunk_size, total_bytes - written));
#ifdef _WIN32
        const ssize_t rc = _write(fd, buffer.data(), static_cast<unsigned int>(to_write));
#else
        const ssize_t rc = ::write(fd, buffer.data(), to_write);
#endif
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (rc == 0) {
            ok = false;
            break;
        }
        written += static_cast<unsigned long long>(rc);
    }

    if (ok) {
#ifdef _WIN32
        if (_commit(fd) != 0) ok = false;
#else
        if (::fsync(fd) != 0) ok = false;
#endif
    }
#ifdef _WIN32
    _close(fd);
    if (!keep_file) {
        _unlink(file_path.c_str());
    }
#else
    ::close(fd);
    if (!keep_file) {
        ::unlink(file_path.c_str());
    }
#endif

    if (!ok) {
        return std::nullopt;
    }

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    if (seconds <= 0.0) {
        return std::nullopt;
    }

    const double mbps = static_cast<double>(total_bytes) / (1024.0 * 1024.0) / seconds;
    return mbps;
}

inline std::optional<double> runSingleReadBenchmark(std::size_t size_mb, int run_id) {
    const std::string file_path =
#ifdef _WIN32
        "check_bench_" + std::to_string(getpid()) + "_" + std::to_string(run_id) + ".dat";
    const int fd = _open(file_path.c_str(), _O_RDONLY | _O_BINARY);
#else
        "/tmp/check_bench_" + std::to_string(::getpid()) + "_" + std::to_string(run_id) + ".dat";
    const int fd = ::open(file_path.c_str(), O_RDONLY);
#endif
    if (fd < 0) {
        return std::nullopt;
    }

    // drop page cache for accurate read testing if running as root
#ifndef _WIN32
    if (::geteuid() == 0) {
        runCommand("sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null");
    }
#endif

    constexpr std::size_t chunk_size = 1024 * 1024;
    std::vector<char> buffer(chunk_size);
    const unsigned long long total_bytes =
        static_cast<unsigned long long>(size_mb) * 1024ULL * 1024ULL;

    unsigned long long read_bytes = 0;
    const auto start = std::chrono::steady_clock::now();
    bool ok = true;

    while (read_bytes < total_bytes) {
        const std::size_t to_read = static_cast<std::size_t>(
            std::min<unsigned long long>(chunk_size, total_bytes - read_bytes));
#ifdef _WIN32
        const ssize_t rc = _read(fd, buffer.data(), static_cast<unsigned int>(to_read));
#else
        const ssize_t rc = ::read(fd, buffer.data(), to_read);
#endif
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (rc == 0) {
            break;  // EOF
        }
        read_bytes += static_cast<unsigned long long>(rc);

        // Access the data to prevent optimization
        volatile char dummy = 0;
        for (ssize_t i = 0; i < rc; i += 4096) dummy ^= buffer[i];
    }

#ifdef _WIN32
    _close(fd);
    _unlink(file_path.c_str());
#else
    ::close(fd);
    ::unlink(file_path.c_str());
#endif

    if (!ok || read_bytes == 0) {
        return std::nullopt;
    }

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    if (seconds <= 0.0) {
        return std::nullopt;
    }

    const double mbps = static_cast<double>(read_bytes) / (1024.0 * 1024.0) / seconds;
    return mbps;
}

struct ThermalInfo {
    std::string zone;
    std::string type;
    std::optional<double> temp_c;
};

inline std::vector<ThermalInfo> collectThermals() {
    std::vector<ThermalInfo> values;
#ifdef _WIN32
    return values;  // Unsupported natively
#else
    std::error_code ec;
    auto thermal_it = fs::directory_iterator("/sys/class/thermal", ec);
    if (!ec) {
        for (const auto& entry : thermal_it) {
            const std::string name = entry.path().filename().string();
            if (startsWith(name, "thermal_zone")) {
                ThermalInfo info;
                info.zone = name;
                info.type = readFirstLine((entry.path() / "type").string()).value_or("N/A");
                const auto temp_raw = readLongFromFile((entry.path() / "temp").string());
                if (temp_raw) {
                    const double value = static_cast<double>(*temp_raw);
                    info.temp_c = (std::abs(value) >= 1000.0) ? (value / 1000.0) : value;
                }
                values.push_back(info);
            }
        }
    }

    auto hwmon_it = fs::directory_iterator("/sys/class/hwmon", ec);
    if (!ec) {
        for (const auto& hwmon_entry : hwmon_it) {
            const std::string hwmon_name =
                readFirstLine((hwmon_entry.path() / "name").string()).value_or("hwmon");
            std::error_code ec2;
            for (const auto& file_entry : fs::directory_iterator(hwmon_entry.path(), ec2)) {
                const std::string filename = file_entry.path().filename().string();
                if (startsWith(filename, "temp") && filename.find("_input") != std::string::npos) {
                    std::string prefix = filename.substr(0, filename.find("_input"));
                    std::string label_file = (hwmon_entry.path() / (prefix + "_label")).string();
                    std::string label =
                        readFirstLine(label_file).value_or(hwmon_name + " " + prefix);

                    ThermalInfo info;
                    info.zone = hwmon_entry.path().filename().string() + "/" + prefix;
                    info.type = label;
                    const auto temp_raw = readLongFromFile(file_entry.path().string());
                    if (temp_raw) {
                        const double value = static_cast<double>(*temp_raw);
                        info.temp_c = (std::abs(value) >= 1000.0) ? (value / 1000.0) : value;
                    }
                    values.push_back(info);
                }
            }
        }
    }

    return values;
#endif
}

inline void printImportantHealthSection() {
    printSectionHeader("OTHER IMPORTANT INFO");

    const DiskUsage disk = getRootDiskUsage();
    if (disk.valid) {
        printKeyValue("Root Disk Total", formatBytes(static_cast<long double>(disk.total)));
        printKeyValue("Root Disk Used", formatBytes(static_cast<long double>(disk.used)));
        printKeyValue("Root Disk Free", formatBytes(static_cast<long double>(disk.free)));
    } else {
        printKeyValue("Root Disk Usage", colorize("UNAVAILABLE", ansi::YELLOW));
    }

    printSubHeader("Disk Write Benchmark (256MB)");
    std::vector<double> benchmark_results;
    for (int i = 1; i <= 3; ++i) {
        const auto run = runSingleWriteBenchmark(256, i, true);
        if (run) {
            benchmark_results.push_back(*run);
            std::ostringstream label;
            label << "  Run " << i;
            std::ostringstream value;
            value << std::fixed << std::setprecision(2) << *run << " MB/s";
            printKeyValue(label.str(), value.str());
        } else {
            std::ostringstream label;
            label << "  Run " << i;
            printKeyValue(label.str(), colorize("UNAVAILABLE", ansi::YELLOW));
        }
    }

    if (!benchmark_results.empty()) {
        const double sum = std::accumulate(benchmark_results.begin(), benchmark_results.end(), 0.0);
        const double avg = sum / static_cast<double>(benchmark_results.size());
        std::ostringstream value;
        value << std::fixed << std::setprecision(2) << avg << " MB/s";
        printKeyValue("  Write Average", value.str());
    } else {
        printKeyValue("  Write Average", colorize("UNAVAILABLE", ansi::YELLOW));
    }

    printSubHeader("Disk Read Benchmark (256MB)");
#ifdef _WIN32
    std::cout << "  "
              << colorize(
                     "Warning: Cannot drop caches easily on Windows, read benchmark results may be "
                     "artificially high.",
                     ansi::YELLOW)
              << "\n";
#else
    if (::geteuid() != 0) {
        std::cout << "  "
                  << colorize(
                         "Warning: Cannot drop caches without root privileges, read benchmark "
                         "results may be artificially high.",
                         ansi::YELLOW)
                  << "\n";
    }
#endif
    std::vector<double> read_results;
    for (int i = 1; i <= 3; ++i) {
        const auto run = runSingleReadBenchmark(256, i);
        if (run) {
            read_results.push_back(*run);
            std::ostringstream label;
            label << "  Run " << i;
            std::ostringstream value;
            value << std::fixed << std::setprecision(2) << *run << " MB/s";
            printKeyValue(label.str(), value.str());
        } else {
            std::ostringstream label;
            label << "  Run " << i;
            printKeyValue(label.str(), colorize("UNAVAILABLE", ansi::YELLOW));
        }
    }

    if (!read_results.empty()) {
        const double sum = std::accumulate(read_results.begin(), read_results.end(), 0.0);
        const double avg = sum / static_cast<double>(read_results.size());
        std::ostringstream value;
        value << std::fixed << std::setprecision(2) << avg << " MB/s";
        printKeyValue("  Read Average", value.str());
    } else {
        printKeyValue("  Read Average", colorize("UNAVAILABLE", ansi::YELLOW));
    }

    printSubHeader("Thermal Zones");
    const auto thermals = collectThermals();
    if (thermals.empty()) {
        std::cout << "  " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
    } else {
        for (const auto& thermal : thermals) {
            if (thermal.temp_c) {
                std::ostringstream temp;
                temp << std::fixed << std::setprecision(1) << *thermal.temp_c << " C";
                std::cout << "    " << thermal.zone << " (" << thermal.type << ") = " << temp.str()
                          << "\n";
            } else {
                std::cout << "    " << thermal.zone << " (" << thermal.type
                          << ") = " << colorize("N/A", ansi::YELLOW) << "\n";
            }
        }
    }
}
