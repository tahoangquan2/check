#pragma once

#include "utils.hpp"

inline std::optional<double> readUptimeSeconds() {
    std::ifstream input("/proc/uptime");
    if (!input) {
        return std::nullopt;
    }
    double seconds = 0.0;
    input >> seconds;
    if (input.fail()) {
        return std::nullopt;
    }
    return seconds;
}

inline std::pair<int, long long> countProcessesAndThreads() {
    int process_count = 0;
    long long thread_count = 0;

    const auto entries = listDirectory("/proc");
    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        if (!isDigits(name)) {
            continue;
        }
        ++process_count;

        std::ifstream status_file((entry / "status").string());
        if (!status_file) {
            continue;
        }
        std::string line;
        while (std::getline(status_file, line)) {
            if (startsWith(line, "Threads:")) {
                const std::string value = trim(line.substr(std::strlen("Threads:")));
                const auto parsed = parseLongLongPrefix(value);
                if (parsed) {
                    thread_count += *parsed;
                }
                break;
            }
        }
    }

    return {process_count, thread_count};
}

#include "cpu.hpp"
#include "health.hpp"

inline void printRuntimeSection() {
    printSectionHeader("RUNTIME");

    const auto uptime = readUptimeSeconds();
    printKeyValue("Uptime", uptime ? formatUptime(*uptime) : colorize("N/A", ansi::YELLOW));

    const auto [processes, threads] = countProcessesAndThreads();
    printKeyValue("Process Count", std::to_string(processes));
    printKeyValue("Thread Count", std::to_string(threads));

    const auto cpu_usage = sampleCpuUsagePercent();
    printKeyValue("CPU Usage (500ms sample)",
                  cpu_usage ? colorByPercent(*cpu_usage) : colorize("N/A", ansi::YELLOW));

    const auto thermals = collectThermals();
    bool found_temp = false;
    for (const auto& thermal : thermals) {
        if (thermal.type.find("x86_pkg_temp") != std::string::npos && thermal.temp_c) {
            std::ostringstream temp;
            temp << std::fixed << std::setprecision(1) << *thermal.temp_c << " C";
            printKeyValue("CPU Temp", temp.str());
            found_temp = true;
            break;
        }
    }
    if (!found_temp) {
        printKeyValue("CPU Temp", colorize("N/A", ansi::YELLOW));
    }
}
