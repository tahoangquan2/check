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
    return usage;
}

inline std::optional<double> runSingleWriteBenchmark(std::size_t size_mb, int run_id) {
    const std::string file_path =
        "/tmp/check_bench_" + std::to_string(::getpid()) + "_" + std::to_string(run_id) + ".dat";
    const int fd = ::open(file_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
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
        const ssize_t rc = ::write(fd, buffer.data(), to_write);
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

    if (ok && ::fsync(fd) != 0) {
        ok = false;
    }
    ::close(fd);
    ::unlink(file_path.c_str());

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

struct ThermalInfo {
    std::string zone;
    std::string type;
    std::optional<double> temp_c;
};

inline std::vector<ThermalInfo> collectThermals() {
    std::vector<ThermalInfo> values;
    const auto entries = listDirectory("/sys/class/thermal");
    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        if (!startsWith(name, "thermal_zone")) {
            continue;
        }

        ThermalInfo info;
        info.zone = name;
        info.type = readFirstLine((entry / "type").string()).value_or("N/A");

        const auto temp_raw = readLongFromFile((entry / "temp").string());
        if (temp_raw) {
            const double value = static_cast<double>(*temp_raw);
            info.temp_c = (std::abs(value) >= 1000.0) ? (value / 1000.0) : value;
        }
        values.push_back(info);
    }
    return values;
}

inline void printPowerSupplySummary() {
    printSubHeader("Power Supply Devices");
    const auto entries = listDirectory("/sys/class/power_supply");
    if (entries.empty()) {
        std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        return;
    }

    bool printed = false;
    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        const std::string type = readFirstLine((entry / "type").string()).value_or("N/A");
        const std::string status = readFirstLine((entry / "status").string()).value_or("N/A");
        const auto online = readLongFromFile((entry / "online").string());
        const auto capacity = readLongFromFile((entry / "capacity").string());

        std::ostringstream line;
        line << name << " (" << type << ")";
        if (status != "N/A") {
            line << " status=" << status;
        }
        if (online) {
            line << " online=" << *online;
        }
        if (capacity) {
            line << " capacity=" << *capacity << "%";
        }
        std::cout << "    " << line.str() << "\n";
        printed = true;
    }

    if (!printed) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
    }
}

inline void printNetworkInterfacesFromSysfs() {
    printSubHeader("Network Interfaces");
    const auto entries = listDirectory("/sys/class/net");
    if (entries.empty()) {
        std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        return;
    }

    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        const std::string state = readFirstLine((entry / "operstate").string()).value_or("N/A");
        const std::string mac = readFirstLine((entry / "address").string()).value_or("N/A");
        std::cout << "    " << name << " state=" << state << " mac=" << mac << "\n";
    }
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

    printSubHeader("Disk Write Micro-Benchmark (128MB)");
    std::vector<double> benchmark_results;
    for (int i = 1; i <= 2; ++i) {
        const auto run = runSingleWriteBenchmark(128, i);
        if (run) {
            benchmark_results.push_back(*run);
            std::ostringstream label;
            label << "Run " << i;
            std::ostringstream value;
            value << std::fixed << std::setprecision(2) << *run << " MB/s";
            printKeyValue(label.str(), value.str());
        } else {
            std::ostringstream label;
            label << "Run " << i;
            printKeyValue(label.str(), colorize("UNAVAILABLE", ansi::YELLOW));
        }
    }

    if (!benchmark_results.empty()) {
        const double sum = std::accumulate(benchmark_results.begin(), benchmark_results.end(), 0.0);
        const double avg = sum / static_cast<double>(benchmark_results.size());
        std::ostringstream value;
        value << std::fixed << std::setprecision(2) << avg << " MB/s";
        printKeyValue("Average", value.str());
    } else {
        printKeyValue("Average", colorize("UNAVAILABLE", ansi::YELLOW));
    }

    printSubHeader("Thermal Zones");
    const auto thermals = collectThermals();
    if (thermals.empty()) {
        std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
    } else {
        for (const auto& thermal : thermals) {
            if (thermal.temp_c) {
                std::ostringstream temp;
                temp << std::fixed << std::setprecision(1) << *thermal.temp_c << " C";
                std::cout << "      " << thermal.zone << " (" << thermal.type
                          << ") = " << temp.str() << "\n";
            } else {
                std::cout << "      " << thermal.zone << " (" << thermal.type
                          << ") = " << colorize("N/A", ansi::YELLOW) << "\n";
            }
        }
    }

    printSubHeader("USB Devices");
    if (commandExists("lsusb")) {
        const auto usb = runCommand("lsusb 2>/dev/null");
        if (usb.exit_code == 0 && !trim(usb.output).empty()) {
            printBlockLines(usb.output);
        } else {
            std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        }
    } else {
        std::cout << "    " << colorize("UNAVAILABLE: lsusb command not found", ansi::YELLOW)
                  << "\n";
    }

    printSubHeader("Storage Devices");
    if (commandExists("lsblk")) {
        const auto blk = runCommand("lsblk -o NAME,TYPE,SIZE,MODEL,TRAN,MOUNTPOINT 2>/dev/null");
        if (blk.exit_code == 0 && !trim(blk.output).empty()) {
            printBlockLines(blk.output);
        } else {
            std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        }
    } else {
        std::cout << "    " << colorize("UNAVAILABLE: lsblk command not found", ansi::YELLOW)
                  << "\n";
    }

    printPowerSupplySummary();
    printNetworkInterfacesFromSysfs();
}
