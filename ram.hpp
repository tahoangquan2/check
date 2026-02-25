#pragma once

#include <cstring>
#include <map>

#include "utils.hpp"

inline std::map<std::string, long long> readMemInfo() {
    std::map<std::string, long long> info;
    std::ifstream input("/proc/meminfo");
    if (!input) {
        return info;
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, colon);
        const std::string rest = trim(line.substr(colon + 1));
        std::istringstream ss(rest);
        long long value = 0;
        ss >> value;
        if (!ss.fail()) {
            info[key] = value;
        }
    }
    return info;
}

inline std::optional<double> runMemoryBenchmark() {
    constexpr std::size_t size_bytes = 256ULL * 1024ULL * 1024ULL;
    char* buf = static_cast<char*>(std::malloc(size_bytes));
    if (!buf) {
        return std::nullopt;
    }

    const auto start = std::chrono::steady_clock::now();
    std::memset(buf, 0xAA, size_bytes);

    volatile char dummy = 0;
    for (std::size_t i = 0; i < size_bytes; i += 4096) {
        dummy ^= buf[i];
    }

    const auto end = std::chrono::steady_clock::now();
    std::free(buf);

    const double seconds = std::chrono::duration<double>(end - start).count();
    if (seconds <= 0.0) {
        return std::nullopt;
    }

    const double mbps = static_cast<double>(size_bytes) / (1024.0 * 1024.0) / seconds;
    return mbps;
}

inline void printRamSection(const std::vector<ProcessUsage>& top_ram) {
    printSectionHeader("RAM");

    const auto mem = readMemInfo();
    const long long mem_total = mem.count("MemTotal") > 0 ? mem.at("MemTotal") : -1;
    long long mem_available = mem.count("MemAvailable") > 0 ? mem.at("MemAvailable") : -1;
    if (mem_available < 0 && mem.count("MemFree") > 0 && mem.count("Buffers") > 0 &&
        mem.count("Cached") > 0) {
        mem_available = mem.at("MemFree") + mem.at("Buffers") + mem.at("Cached");
    }
    const long long mem_used =
        (mem_total >= 0 && mem_available >= 0) ? (mem_total - mem_available) : -1;

    const long long swap_total = mem.count("SwapTotal") > 0 ? mem.at("SwapTotal") : -1;
    const long long swap_free = mem.count("SwapFree") > 0 ? mem.at("SwapFree") : -1;
    const long long swap_used = (swap_total >= 0 && swap_free >= 0) ? (swap_total - swap_free) : -1;

    printKeyValue("RAM Total",
                  mem_total >= 0 ? formatKilobytes(mem_total) : colorize("N/A", ansi::YELLOW));
    printKeyValue("RAM Used",
                  mem_used >= 0 ? formatKilobytes(mem_used) : colorize("N/A", ansi::YELLOW));
    printKeyValue("RAM Available", mem_available >= 0 ? formatKilobytes(mem_available)
                                                      : colorize("N/A", ansi::YELLOW));
    printKeyValue("Swap Total",
                  swap_total >= 0 ? formatKilobytes(swap_total) : colorize("N/A", ansi::YELLOW));
    printKeyValue("Swap Used",
                  swap_used >= 0 ? formatKilobytes(swap_used) : colorize("N/A", ansi::YELLOW));

    const auto bench = runMemoryBenchmark();
    if (bench) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << *bench << " MB/s";
        printKeyValue("RAM R/W Benchmark (256MB)", out.str());
    } else {
        printKeyValue("RAM R/W Benchmark (256MB)", colorize("FAIL", ansi::RED));
    }

    if (top_ram.empty()) {
        printKeyValue("Top RAM Processes", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by RAM)");
        printTopProcessTable(top_ram);
    }
}
