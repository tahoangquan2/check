#pragma once

#include <cstring>
#include <map>

#include "process.hpp"
#include "utils.hpp"

inline std::map<std::string, long long> parseMemInfo(const std::string& text) {
    std::map<std::string, long long> info;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        std::istringstream value_parser(trim(line.substr(colon + 1)));
        long long value = 0;
        if (value_parser >> value) info[key] = value;
    }
    return info;
}

inline std::map<std::string, long long> readMemInfo() {
#ifdef _WIN32
    std::map<std::string, long long> info;
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        info["MemTotal"] = memInfo.ullTotalPhys / 1024;
        info["MemFree"] = memInfo.ullAvailPhys / 1024;
        info["MemAvailable"] = memInfo.ullAvailPhys / 1024;
        info["CommitLimit"] = memInfo.ullTotalPageFile / 1024;
        info["CommitAvailable"] = memInfo.ullAvailPageFile / 1024;
    }
    return info;
#else
    return parseMemInfo(readFile("/proc/meminfo").value_or(""));
#endif
}

inline std::optional<double> runMemoryBenchmark(std::size_t size_mebibytes = 64) {
    const std::size_t size_bytes = size_mebibytes * 1024ULL * 1024ULL;
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
    (void)dummy;

    const auto end = std::chrono::steady_clock::now();
    std::free(buf);

    const double seconds = std::chrono::duration<double>(end - start).count();
    if (seconds <= 0.0) {
        return std::nullopt;
    }

    const double mbps = static_cast<double>(size_bytes) / (1024.0 * 1024.0) / seconds;
    return mbps;
}

inline void printRamSection(const std::vector<ProcessUsage>& top_ram, bool run_benchmark) {
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

    const std::string unavailable = colorize("N/A", ansi::YELLOW);

    printKeyValue("RAM Total", mem_total >= 0 ? formatKilobytes(mem_total) : unavailable);
    printKeyValue("RAM Used", mem_used >= 0 ? formatKilobytes(mem_used) : unavailable);
    printKeyValue("RAM Available",
                  mem_available >= 0 ? formatKilobytes(mem_available) : unavailable);
#ifdef _WIN32
    const long long commit_limit = mem.count("CommitLimit") ? mem.at("CommitLimit") : -1;
    const long long commit_available =
        mem.count("CommitAvailable") ? mem.at("CommitAvailable") : -1;
    const long long commit_used =
        commit_limit >= 0 && commit_available >= 0 ? commit_limit - commit_available : -1;
    printKeyValue("Commit Limit", commit_limit >= 0 ? formatKilobytes(commit_limit) : unavailable);
    printKeyValue("Commit Used", commit_used >= 0 ? formatKilobytes(commit_used) : unavailable);
#else
    const long long swap_total = mem.count("SwapTotal") ? mem.at("SwapTotal") : -1;
    const long long swap_free = mem.count("SwapFree") ? mem.at("SwapFree") : -1;
    const long long swap_used = swap_total >= 0 && swap_free >= 0 ? swap_total - swap_free : -1;
    printKeyValue("Swap Total", swap_total >= 0 ? formatKilobytes(swap_total) : unavailable);
    printKeyValue("Swap Used", swap_used >= 0 ? formatKilobytes(swap_used) : unavailable);
#endif

    if (run_benchmark) {
        std::cerr << "[bench] ram: touching 64 MiB...\n";
        const auto start = std::chrono::steady_clock::now();
        const auto bench = runMemoryBenchmark();
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (bench) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << *bench << " MiB/s (" << elapsed << " s)";
            printKeyValue("RAM R/W Benchmark (64MiB)", out.str());
            recordCheck(CheckState::Pass);
        } else {
            printKeyValue("RAM R/W Benchmark (64MiB)", colorize("FAIL", ansi::RED));
            recordCheck(CheckState::Fail);
        }
    } else {
        printKeyValue("RAM Benchmark", "skipped (use --full)");
    }

    if (top_ram.empty()) {
        printKeyValue("Top RAM Processes", unavailable);
    } else {
        printSubHeader("Top 10 Processes (by RAM)");
        printTopProcessTable(top_ram);
    }
}
