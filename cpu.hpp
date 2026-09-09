#pragma once

#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

#include <chrono>
#include <thread>

#include "command.hpp"
#include "process.hpp"
#include "thermal.hpp"
#include "utils.hpp"

struct CpuTimes {
    unsigned long long idle_all = 0;
    unsigned long long total = 0;
    bool valid = false;
};

inline CpuTimes parseProcStatCpuLine(const std::string& line) {
    CpuTimes times;
    std::istringstream parser(line);
    std::string label;
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    parser >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    if (parser.fail() || label != "cpu") return times;
    times.idle_all = idle + iowait;
    times.total = user + nice + system + idle + iowait + irq + softirq + steal;
    times.valid = true;
    return times;
}

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
    if (!input) return {};
    std::string line;
    std::getline(input, line);
    return parseProcStatCpuLine(line);
#endif
    return times;
}

inline std::optional<double> sampleCpuUsagePercent() {
    const CpuTimes first = readCpuTimes();
    if (!first.valid) {
        return std::nullopt;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));

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
    int logical_processors = 0;
};

inline CpuIdentity getCpuIdentity() {
    CpuIdentity identity;
#ifdef _WIN32
    identity.logical_processors = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    const auto registry_model = readWindowsRegistryString(
        HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "ProcessorNameString");
    if (registry_model && !registry_model->empty()) {
        identity.model = *registry_model;
    } else {
        const char* env_cpu = std::getenv("PROCESSOR_IDENTIFIER");
        if (env_cpu != nullptr && std::strlen(env_cpu) > 0) {
            identity.model = env_cpu;
        } else {
            identity.model = "Windows processor information not exposed";
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
                ++identity.logical_processors;
            }
        }
    }

    if ((identity.model == "N/A" || identity.logical_processors == 0) && commandExists("lscpu")) {
        const CommandResult cmd = runCommand({"lscpu"});
        if (cmd.ok()) {
            const auto lines = splitLines(cmd.output);
            for (const auto& line : lines) {
                if (identity.model == "N/A" && startsWith(line, "Model name:")) {
                    identity.model = trim(line.substr(std::strlen("Model name:")));
                } else if (identity.logical_processors == 0 && startsWith(line, "CPU(s):")) {
                    const std::string value = trim(line.substr(std::strlen("CPU(s):")));
                    const auto parsed = parseIntPrefix(value);
                    if (parsed) {
                        identity.logical_processors = *parsed;
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

inline std::optional<double> readProcessorQueueLength() {
#ifdef _WIN32
    const CommandResult result = runPowerShell(
        "[Console]::Out.WriteLine((Get-Counter '\\System\\Processor Queue Length' "
        "-ErrorAction Stop).CounterSamples.CookedValue)");
    if (!result.ok()) {
        return std::nullopt;
    }
    const auto lines = splitLines(result.output);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (it->empty()) {
            continue;
        }
        const auto value = parseDoubleStrict(*it);
        if (value) {
            return *value;
        }
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

struct CpuTarget {
    int logical_id = 0;
#ifdef _WIN32
    WORD group = 0;
    BYTE processor = 0;
#else
    int processor = 0;
#endif
};

inline std::vector<CpuTarget> benchmarkCpuTargets() {
    std::vector<CpuTarget> targets;
#ifdef _WIN32
    const WORD groups = GetActiveProcessorGroupCount();
    int logical_id = 0;
    for (WORD group = 0; group < groups; ++group) {
        const DWORD count = GetActiveProcessorCount(group);
        const DWORD usable = std::min<DWORD>(count, static_cast<DWORD>(sizeof(KAFFINITY) * 8));
        for (DWORD processor = 0; processor < usable; ++processor) {
            targets.push_back({logical_id++, group, static_cast<BYTE>(processor)});
        }
        logical_id += static_cast<int>(count - usable);
    }
#else
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (::sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return targets;
    for (int processor = 0; processor < CPU_SETSIZE; ++processor) {
        if (CPU_ISSET(processor, &allowed)) targets.push_back({processor, processor});
    }
#endif
    return targets;
}

struct CpuBenchmarkWorker {
    CpuTarget target;
    std::optional<double>* result = nullptr;

    void operator()() const {
#ifdef _WIN32
        GROUP_AFFINITY affinity{};
        affinity.Group = target.group;
        affinity.Mask = static_cast<KAFFINITY>(1) << target.processor;
        if (!SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr)) return;
#else
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(target.processor, &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) return;
#endif
        const auto start = std::chrono::steady_clock::now();
        volatile double value = 1.0;
        for (int i = 0; i < 100000000; ++i) value *= 1.000001;
        const auto end = std::chrono::steady_clock::now();
        *result = std::chrono::duration<double, std::milli>(end - start).count();
        (void)value;
    }
};

inline std::optional<double> runBenchmarkOnTarget(const CpuTarget& target) {
    std::optional<double> result;
    const CpuBenchmarkWorker worker{target, &result};
    std::thread thread(worker);
    thread.join();
    return result;
}

inline std::optional<double> selectCpuTemperature(const std::vector<ThermalInfo>& thermals) {
#ifdef _WIN32
    std::optional<double> fallback;
    for (const ThermalInfo& thermal : thermals) {
        if (!thermal.temp_c) continue;
        const std::string label = toLower(thermal.type + " " + thermal.zone);
        if (label.find("cpu") != std::string::npos || label.find("pkg") != std::string::npos ||
            label.find("core") != std::string::npos) {
            return thermal.temp_c;
        }
        if (!fallback) fallback = thermal.temp_c;
    }
    return fallback;
#else
    for (const ThermalInfo& thermal : thermals) {
        if (thermal.temp_c && toLower(thermal.type).find("x86_pkg_temp") != std::string::npos) {
            return thermal.temp_c;
        }
    }
    return std::nullopt;
#endif
}

inline void printCpuSection(const std::vector<ProcessUsage>& top_cpu, bool run_benchmark,
                            bool extended, const ThermalSnapshot& thermal_snapshot) {
    printSectionHeader("CPU");

    const CpuIdentity cpu = getCpuIdentity();
    printKeyValue("CPU Model", cpu.model);
    printKeyValue("Logical Processors",
                  cpu.logical_processors > 0
                      ? std::to_string(cpu.logical_processors)
                      : colorize("logical processor count not exposed", ansi::YELLOW));

    const auto cpu_usage = sampleCpuUsagePercent();
    if (cpu_usage) {
        printKeyValue("CPU Usage (250ms sample)", colorUsagePercent(*cpu_usage));
    } else {
        printKeyValue("CPU Usage (250ms sample)", colorize("sampling failed", ansi::YELLOW));
    }

    const std::optional<double> cpu_temperature =
        extended ? selectCpuTemperature(thermal_snapshot.values) : std::nullopt;
    if (cpu_temperature) {
        std::ostringstream temp;
        temp << std::fixed << std::setprecision(1) << *cpu_temperature << " C";
        printKeyValue("CPU Temp", temp.str());
    } else {
        printKeyValue("CPU Temp", !extended ? "skipped (included in --full)"
                                            : colorize(thermal_snapshot.error.empty()
                                                           ? "not exposed by firmware"
                                                           : thermal_snapshot.error,
                                                       ansi::YELLOW));
    }

#ifdef _WIN32
    if (extended) {
        const auto queue = readProcessorQueueLength();
        if (queue) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << *queue;
            printKeyValue("Processor Queue Length", out.str());
        } else {
            printKeyValue("Processor Queue Length", colorize("counter not exposed", ansi::YELLOW));
        }
    } else {
        printKeyValue("Processor Queue Length", "skipped (included in --full)");
    }
#else
    const auto load = readLoadAverage();
    if (load) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << (*load)[0] << ", " << (*load)[1] << ", "
            << (*load)[2];
        printKeyValue("Load Average (1/5/15m)", out.str());
    } else {
        printKeyValue("Load Average (1/5/15m)", colorize("N/A", ansi::YELLOW));
    }
#endif

    if (run_benchmark) {
        const std::vector<CpuTarget> targets = benchmarkCpuTargets();
        printSubHeader("CPU Benchmark (100M ops per available logical processor)");
        std::cerr << "[bench] cpu: " + std::to_string(targets.size()) + " logical processors...\n";
        bool all_targets_worked = true;
        for (const CpuTarget& target : targets) {
            const auto bench = runBenchmarkOnTarget(target);
            std::ostringstream label;
            label << "  Logical Processor " << target.logical_id;
            if (bench) {
                std::ostringstream out;
                out << std::fixed << std::setprecision(2) << *bench << " ms ("
                    << colorize("WORKING", ansi::GREEN) << ")";
                printKeyValue(label.str(), out.str());
            } else {
                printKeyValue(label.str(), colorize("FAIL", ansi::RED));
                all_targets_worked = false;
            }
        }
        if (targets.empty()) {
            printKeyValue("CPU Benchmark", colorize("no legal affinity targets", ansi::YELLOW));
            recordCheck(CheckState::Unavailable);
        } else {
            recordCheck(all_targets_worked ? CheckState::Pass : CheckState::Fail);
        }
    } else {
        printKeyValue("CPU Benchmark", "skipped (use --full)");
    }

    if (top_cpu.empty()) {
        printKeyValue("Top CPU Processes", colorize("process telemetry not exposed", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by CPU)");
        printTopProcessTable(top_cpu);
    }
}
