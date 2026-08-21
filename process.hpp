#pragma once

#include <chrono>
#include <thread>
#include <unordered_map>

#include "utils.hpp"

struct ProcessUsage {
    int pid = -1;
    std::string command;
    double cpu = 0.0;
    double mem = 0.0;
    int net_sockets = 0;
};

struct ProcessTelemetry {
    std::vector<ProcessUsage> rows;
    std::string error;
    std::size_t permission_denied = 0;
};

struct RawProcessRow {
    unsigned long long pid = 0;
    std::string command;
    unsigned long long cpu_time = 0;
    unsigned long long resident_bytes = 0;
};

#ifdef _WIN32
inline unsigned long long processFileTime(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

inline std::vector<RawProcessRow> captureRawProcessRows(std::string& error) {
    std::vector<RawProcessRow> rows;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        error = "process snapshot failed";
        return rows;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        error = "process enumeration failed";
        return rows;
    }

    const DWORD own_pid = GetCurrentProcessId();
    do {
        if (entry.th32ProcessID == 0 || entry.th32ProcessID == own_pid) {
            continue;
        }
        RawProcessRow row;
        row.pid = entry.th32ProcessID;
        row.command = wideToUtf8(entry.szExeFile);

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (process != nullptr) {
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
                row.cpu_time = processFileTime(kernel) + processFileTime(user);
            }
            PROCESS_MEMORY_COUNTERS counters{};
            if (GetProcessMemoryInfo(process, &counters, sizeof(counters))) {
                row.resident_bytes = static_cast<unsigned long long>(counters.WorkingSetSize);
            }
            CloseHandle(process);
        }
        if (row.command.empty()) {
            row.command = "pid " + std::to_string(row.pid);
        }
        rows.push_back(row);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return rows;
}

inline void appendTcpSocketCounts(int family, std::unordered_map<unsigned long long, int>& counts) {
    ULONG size = 0;
    DWORD status = GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
    if (status != ERROR_INSUFFICIENT_BUFFER) return;
    std::vector<unsigned char> buffer(size);
    status = GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
    if (status != NO_ERROR) return;

    if (family == AF_INET) {
        const MIB_TCPTABLE_OWNER_PID* table =
            reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            ++counts[table->table[i].dwOwningPid];
        }
    } else {
        const MIB_TCP6TABLE_OWNER_PID* table =
            reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            ++counts[table->table[i].dwOwningPid];
        }
    }
}

inline void appendUdpSocketCounts(int family, std::unordered_map<unsigned long long, int>& counts) {
    ULONG size = 0;
    DWORD status = GetExtendedUdpTable(nullptr, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
    if (status != ERROR_INSUFFICIENT_BUFFER) return;
    std::vector<unsigned char> buffer(size);
    status = GetExtendedUdpTable(buffer.data(), &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
    if (status != NO_ERROR) return;

    if (family == AF_INET) {
        const MIB_UDPTABLE_OWNER_PID* table =
            reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            ++counts[table->table[i].dwOwningPid];
        }
    } else {
        const MIB_UDP6TABLE_OWNER_PID* table =
            reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            ++counts[table->table[i].dwOwningPid];
        }
    }
}

inline std::unordered_map<unsigned long long, int> captureSocketCounts() {
    std::unordered_map<unsigned long long, int> counts;
    appendTcpSocketCounts(AF_INET, counts);
    appendTcpSocketCounts(AF_INET6, counts);
    appendUdpSocketCounts(AF_INET, counts);
    appendUdpSocketCounts(AF_INET6, counts);
    return counts;
}
#else
inline unsigned long long readSystemCpuTicks() {
    std::ifstream input("/proc/stat");
    std::string label;
    input >> label;
    if (!input || label != "cpu") return 0;
    unsigned long long value = 0;
    unsigned long long total = 0;
    for (int field = 0; field < 8 && input >> value; ++field) {
        total += value;
    }
    return total;
}

inline long long readLinuxTotalMemoryKib() {
    std::ifstream input("/proc/meminfo");
    std::string key;
    long long value = 0;
    input >> key >> value;
    return input && key == "MemTotal:" ? value : 0LL;
}

inline std::optional<RawProcessRow> readLinuxProcessRow(const fs::path& process_path) {
    const auto stat_text = readFirstLine((process_path / "stat").string());
    if (!stat_text) return std::nullopt;
    const std::size_t open = stat_text->find('(');
    const std::size_t close = stat_text->rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return std::nullopt;
    }

    RawProcessRow row;
    const auto parsed_pid = parseLongLongPrefix(stat_text->substr(0, open));
    if (!parsed_pid || *parsed_pid <= 0) return std::nullopt;
    row.pid = static_cast<unsigned long long>(*parsed_pid);
    row.command = stat_text->substr(open + 1, close - open - 1);

    std::istringstream fields(stat_text->substr(close + 2));
    std::string token;
    unsigned long long user_ticks = 0;
    unsigned long long system_ticks = 0;
    for (int index = 0; fields >> token; ++index) {
        if (index == 11) {
            const auto parsed = parseLongLongPrefix(token);
            if (!parsed || *parsed < 0) return std::nullopt;
            user_ticks = static_cast<unsigned long long>(*parsed);
        } else if (index == 12) {
            const auto parsed = parseLongLongPrefix(token);
            if (!parsed || *parsed < 0) return std::nullopt;
            system_ticks = static_cast<unsigned long long>(*parsed);
            break;
        }
    }
    row.cpu_time = user_ticks + system_ticks;

    std::ifstream status((process_path / "status").string());
    std::string line;
    while (std::getline(status, line)) {
        if (!startsWith(line, "VmRSS:")) continue;
        const auto kib = parseLongLongPrefix(trim(line.substr(6)));
        if (kib && *kib >= 0) {
            row.resident_bytes = static_cast<unsigned long long>(*kib) * 1024ULL;
        }
        break;
    }
    return row;
}

inline std::vector<RawProcessRow> captureRawProcessRows(std::string& error) {
    std::vector<RawProcessRow> rows;
    const DirectoryListing processes = listDirectory("/proc");
    if (processes.error) {
        error = "cannot enumerate /proc: " + processes.error.message();
        return rows;
    }
    for (const fs::path& path : processes.entries) {
        if (!isDigits(path.filename().string())) continue;
        const auto row = readLinuxProcessRow(path);
        if (row) rows.push_back(*row);
    }
    return rows;
}

inline int countLinuxProcessSockets(unsigned long long pid, std::size_t& permission_denied) {
    const fs::path directory = fs::path("/proc") / std::to_string(pid) / "fd";
    const DirectoryListing descriptors = listDirectory(directory);
    if (descriptors.error) {
        if (descriptors.error == std::errc::permission_denied) ++permission_denied;
        return 0;
    }
    int count = 0;
    for (const fs::path& descriptor : descriptors.entries) {
        std::error_code status_error;
        const fs::file_status status = fs::symlink_status(descriptor, status_error);
        if (status_error || !fs::is_symlink(status)) continue;
        std::error_code link_error;
        const fs::path target = fs::read_symlink(descriptor, link_error);
        if (!link_error && startsWith(target.string(), "socket:[")) ++count;
    }
    return count;
}
#endif

inline ProcessTelemetry collectProcessTelemetry() {
    ProcessTelemetry telemetry;
    std::string first_error;
#ifndef _WIN32
    const unsigned long long first_total = readSystemCpuTicks();
#endif
    const std::vector<RawProcessRow> first = captureRawProcessRows(first_error);
    if (first.empty() && !first_error.empty()) {
        telemetry.error = first_error;
        return telemetry;
    }

#ifdef _WIN32
    const auto start = std::chrono::steady_clock::now();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string second_error;
    const std::vector<RawProcessRow> second = captureRawProcessRows(second_error);
#ifdef _WIN32
    const auto end = std::chrono::steady_clock::now();
#endif
    if (second.empty() && !second_error.empty()) {
        telemetry.error = second_error;
        return telemetry;
    }

    std::unordered_map<unsigned long long, RawProcessRow> before;
    for (const RawProcessRow& row : first) before[row.pid] = row;

#ifdef _WIN32
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const double total_memory =
        GlobalMemoryStatusEx(&memory) ? static_cast<double>(memory.ullTotalPhys) : 0.0;
    const double elapsed_units = std::chrono::duration<double>(end - start).count() * 10000000.0;
    const std::unordered_map<unsigned long long, int> sockets = captureSocketCounts();
#else
    const unsigned long long second_total = readSystemCpuTicks();
    const double total_delta =
        second_total > first_total ? static_cast<double>(second_total - first_total) : 0.0;
    const long long total_kib = readLinuxTotalMemoryKib();
    const double total_memory = static_cast<double>(total_kib) * 1024.0;
    const double processor_count =
        static_cast<double>(std::max(1u, std::thread::hardware_concurrency()));
#endif

    for (const RawProcessRow& current : second) {
        ProcessUsage row;
        row.pid = static_cast<int>(current.pid);
        row.command = current.command;
        const auto previous = before.find(current.pid);
        if (previous != before.end() && current.cpu_time >= previous->second.cpu_time) {
            const double delta = static_cast<double>(current.cpu_time - previous->second.cpu_time);
#ifdef _WIN32
            if (elapsed_units > 0.0) row.cpu = (delta / elapsed_units) * 100.0;
#else
            if (total_delta > 0.0) row.cpu = (delta / total_delta) * processor_count * 100.0;
#endif
        }
        if (total_memory > 0.0) {
            row.mem = static_cast<double>(current.resident_bytes) / total_memory * 100.0;
        }
#ifdef _WIN32
        const auto socket = sockets.find(current.pid);
        if (socket != sockets.end()) row.net_sockets = socket->second;
#else
        row.net_sockets = countLinuxProcessSockets(current.pid, telemetry.permission_denied);
#endif
        const std::string lowered = toLower(row.command);
        if (lowered != "check" && lowered != "check.exe") telemetry.rows.push_back(row);
    }
    if (telemetry.rows.empty() && telemetry.error.empty()) {
        telemetry.error = "no accessible process telemetry";
    } else if (telemetry.permission_denied != 0) {
        telemetry.error = std::to_string(telemetry.permission_denied) +
                          " process socket lists were permission-denied";
    }
    return telemetry;
}

enum class ProcessSort { Cpu, Memory, Network };

inline bool processCpuDescending(const ProcessUsage& left, const ProcessUsage& right) {
    return left.cpu > right.cpu;
}

inline bool processMemoryDescending(const ProcessUsage& left, const ProcessUsage& right) {
    return left.mem > right.mem;
}

inline bool processNetworkDescending(const ProcessUsage& left, const ProcessUsage& right) {
    return left.net_sockets == right.net_sockets ? left.cpu > right.cpu
                                                 : left.net_sockets > right.net_sockets;
}

inline std::vector<ProcessUsage> topProcesses(const ProcessTelemetry& telemetry, ProcessSort sort,
                                              std::size_t limit) {
    std::vector<ProcessUsage> rows = telemetry.rows;
    if (sort == ProcessSort::Memory) {
        std::sort(rows.begin(), rows.end(), processMemoryDescending);
    } else if (sort == ProcessSort::Network) {
        std::sort(rows.begin(), rows.end(), processNetworkDescending);
    } else {
        std::sort(rows.begin(), rows.end(), processCpuDescending);
    }
    if (rows.size() > limit) rows.resize(limit);
    return rows;
}

inline void printTopProcessTable(const std::vector<ProcessUsage>& rows) {
    constexpr std::size_t command_width = 28;
    std::cout << "    " << std::right << std::setw(8) << "PID" << " " << std::left
              << std::setw(static_cast<int>(command_width)) << "COMMAND" << std::right
              << std::setw(6) << "%CPU" << std::setw(6) << "%MEM" << std::setw(6) << "NET"
              << "\n";
    for (const ProcessUsage& row : rows) {
        std::ostringstream cpu;
        cpu << std::fixed << std::setprecision(1) << row.cpu;
        std::ostringstream memory;
        memory << std::fixed << std::setprecision(1) << row.mem;
        const std::string command = fitTableCell(row.command, command_width);
        const std::size_t padding = command_width - utf8DisplayWidth(command);
        std::cout << "    " << std::right << std::setw(8) << row.pid << " " << command
                  << std::string(padding, ' ') << std::right << std::setw(6) << cpu.str()
                  << std::setw(6) << memory.str() << std::setw(6) << row.net_sockets << "\n";
    }
    std::cout << std::left;
}
