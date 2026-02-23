#pragma once

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

inline void printRamSection() {
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

    const auto top_ram = getTopProcesses("%mem", 10);
    if (top_ram.empty()) {
        printKeyValue("Top RAM Processes", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by RAM)");
        printTopProcessTable(top_ram);
    }
}
