#pragma once

#ifndef _WIN32
#include <pthread.h>
#endif

#include <chrono>
#include <thread>

#include "health.hpp"
#include "utils.hpp"

struct CpuTimes {
    unsigned long long idle_all = 0;
    unsigned long long total = 0;
    bool valid = false;
};

inline CpuTimes readCpuTimes() {
    CpuTimes times;
#ifdef _WIN32
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULARGE_INTEGER i, k, u;
        i.LowPart = idle.dwLowDateTime;
        i.HighPart = idle.dwHighDateTime;
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;

        times.idle_all = i.QuadPart;
        times.total = k.QuadPart + u.QuadPart;
        times.valid = true;
    }
#else
    std::ifstream input("/proc/stat");
    if (!input) {
        return times;
    }

    std::string line;
    std::getline(input, line);
    std::istringstream ss(line);
    std::string label;
    ss >> label;
    if (label != "cpu") {
        return times;
    }

    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    unsigned long long guest = 0;
    unsigned long long guest_nice = 0;

    ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >>
        guest_nice;
    if (ss.fail()) {
        return times;
    }

    times.idle_all = idle + iowait;
    times.total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
    times.valid = true;
#endif
    return times;
}

inline std::optional<double> sampleCpuUsagePercent() {
    const CpuTimes first = readCpuTimes();
    if (!first.valid) {
        return std::nullopt;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const CpuTimes second = readCpuTimes();
    if (!second.valid || second.total <= first.total || second.idle_all < first.idle_all) {
        return std::nullopt;
    }

    const auto delta_total = static_cast<double>(second.total - first.total);
    const auto delta_idle = static_cast<double>(second.idle_all - first.idle_all);
    if (delta_total <= 0.0) {
        return std::nullopt;
    }
    const double usage = ((delta_total - delta_idle) / delta_total) * 100.0;
    return usage;
}

struct CpuIdentity {
    std::string model = "N/A";
    int cores = 0;
};

inline CpuIdentity getCpuIdentity() {
    CpuIdentity identity;
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    identity.cores = sysinfo.dwNumberOfProcessors;
    const auto cmd = runCommand("wmic cpu get name 2>nul");
    if (cmd.exit_code == 0) {
        const auto lines = splitLines(cmd.output);
        if (lines.size() >= 2) {
            identity.model = lines[1];
        }
    }
#else
    std::ifstream input("/proc/cpuinfo");
    if (input) {
        std::string line;
        while (std::getline(input, line)) {
            if (startsWith(line, "model name")) {
                const auto pos = line.find(':');
                if (pos != std::string::npos && identity.model == "N/A") {
                    identity.model = trim(line.substr(pos + 1));
                }
            } else if (startsWith(line, "processor")) {
                ++identity.cores;
            }
        }
    }

    if ((identity.model == "N/A" || identity.cores == 0) && commandExists("lscpu")) {
        const auto cmd = runCommand("lscpu 2>/dev/null");
        if (cmd.exit_code == 0) {
            const auto lines = splitLines(cmd.output);
            for (const auto& line : lines) {
                if (identity.model == "N/A" && startsWith(line, "Model name:")) {
                    identity.model = trim(line.substr(std::strlen("Model name:")));
                } else if (identity.cores == 0 && startsWith(line, "CPU(s):")) {
                    const std::string value = trim(line.substr(std::strlen("CPU(s):")));
                    const auto parsed = parseIntPrefix(value);
                    if (parsed) {
                        identity.cores = *parsed;
                    }
                }
            }
        }
    }
#endif
    return identity;
}

inline std::optional<std::array<double, 3>> readLoadAverage() {
#ifdef _WIN32
    return std::nullopt;
#else
    std::ifstream input("/proc/loadavg");
    if (!input) {
        return std::nullopt;
    }

    std::array<double, 3> values{};
    input >> values[0] >> values[1] >> values[2];
    if (input.fail()) {
        return std::nullopt;
    }
    return values;
#endif
}

inline std::optional<double> runBenchmarkOnCore(int core_id) {
    std::optional<double> result;
    std::thread t([&result, core_id]() {
#ifdef _WIN32
        DWORD_PTR mask = (DWORD_PTR)1 << core_id;
        SetThreadAffinityMask(GetCurrentThread(), mask);
#else
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
            return;
        }
#endif

        auto start = std::chrono::steady_clock::now();
        volatile double dummy = 1.0;
        for (int i = 0; i < 100000000; ++i) {
            dummy *= 1.000001;
        }
        auto end = std::chrono::steady_clock::now();
        result = std::chrono::duration<double, std::milli>(end - start).count();
    });
    t.join();
    return result;
}

inline void printCpuSection(const std::vector<ProcessUsage>& top_cpu) {
    printSectionHeader("CPU");

    const CpuIdentity cpu = getCpuIdentity();
    printKeyValue("CPU Model", cpu.model);
    printKeyValue("CPU Cores",
                  cpu.cores > 0 ? std::to_string(cpu.cores) : colorize("N/A", ansi::YELLOW));

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

    const auto load = readLoadAverage();
    if (load) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << (*load)[0] << ", " << (*load)[1] << ", "
            << (*load)[2];
        printKeyValue("Load Average (1/5/15m)", out.str());
    } else {
        printKeyValue("Load Average (1/5/15m)", colorize("N/A", ansi::YELLOW));
    }

    if (cpu.cores > 0) {
        printSubHeader("Per-Core Benchmark (100M ops)");
        for (int i = 0; i < cpu.cores; ++i) {
            const auto bench = runBenchmarkOnCore(i);
            std::ostringstream label;
            label << "  Core " << i;
            if (bench) {
                std::ostringstream out;
                out << std::fixed << std::setprecision(2) << *bench << " ms ("
                    << colorize("WORKING", ansi::GREEN) << ")";
                printKeyValue(label.str(), out.str());
            } else {
                printKeyValue(label.str(), colorize("FAIL", ansi::RED));
            }
        }
    }

    if (top_cpu.empty()) {
        printKeyValue("Top CPU Processes", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by CPU)");
        printTopProcessTable(top_cpu);
    }
}
