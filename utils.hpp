#pragma once

#ifndef _WIN32
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <fcntl.h>
#include <io.h>
#include <iphlpapi.h>
#include <process.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace ansi {
constexpr const char* RESET = "\033[0m";
constexpr const char* BOLD = "\033[1m";
constexpr const char* RED = "\033[31m";
constexpr const char* GREEN = "\033[32m";
constexpr const char* YELLOW = "\033[33m";
constexpr const char* MAGENTA = "\033[35m";
constexpr const char* CYAN = "\033[36m";
}  // namespace ansi

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

enum class CheckState { Pass, Fail, Unavailable };

struct SimpleCheck {
    CheckState state = CheckState::Unavailable;
    std::string detail;
};

inline std::string colorize(const std::string& text, const char* color) {
    return std::string(color) + text + ansi::RESET;
}

inline std::string trim(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }
    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }
    return input.substr(start, end - start);
}

inline bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

inline std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline bool isDigits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; });
}

inline std::optional<std::string> readFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << input.rdbuf();
    return ss.str();
}

inline std::optional<std::string> readFirstLine(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }
    std::string line;
    std::getline(input, line);
    return trim(line);
}

inline std::optional<long long> parseLongLongPrefix(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    if (end == value.c_str() || errno == ERANGE) {
        return std::nullopt;
    }
    return parsed;
}

inline std::optional<int> parseIntPrefix(const std::string& value) {
    const auto parsed = parseLongLongPrefix(value);
    if (!parsed) {
        return std::nullopt;
    }
    if (*parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
        *parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(*parsed);
}

inline std::optional<double> parseDoubleStrict(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE) {
        return std::nullopt;
    }
    if (*end != '\0') {
        return std::nullopt;
    }
    return parsed;
}

inline std::optional<long long> readLongFromFile(const std::string& path) {
    const auto line = readFirstLine(path);
    if (!line || line->empty()) {
        return std::nullopt;
    }
    return parseLongLongPrefix(*line);
}

inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(trim(line));
    }
    return lines;
}

#ifdef _WIN32
inline unsigned long long fileTimeToUint64(const FILETIME& value) {
    ULARGE_INTEGER u{};
    u.LowPart = value.dwLowDateTime;
    u.HighPart = value.dwHighDateTime;
    return u.QuadPart;
}

inline std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string out(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &out[0],
                            required, nullptr, nullptr) <= 0) {
        return {};
    }
    return out;
}

struct ProcessSnapshotRow {
    DWORD pid = 0;
    std::string command;
    unsigned long long cpu_time_100ns = 0;
    SIZE_T working_set = 0;
};

inline std::vector<ProcessSnapshotRow> captureProcessSnapshot() {
    std::vector<ProcessSnapshotRow> rows;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return rows;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const DWORD self_pid = GetCurrentProcessId();

    if (!Process32FirstW(snap, &entry)) {
        CloseHandle(snap);
        return rows;
    }

    do {
        if (entry.th32ProcessID == 0 || entry.th32ProcessID == self_pid) {
            continue;
        }

        ProcessSnapshotRow row;
        row.pid = entry.th32ProcessID;
        row.command = wideToUtf8(entry.szExeFile);

        HANDLE process =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, row.pid);
        if (process != nullptr) {
            FILETIME create_time{}, exit_time{}, kernel_time{}, user_time{};
            if (GetProcessTimes(process, &create_time, &exit_time, &kernel_time, &user_time)) {
                row.cpu_time_100ns = fileTimeToUint64(kernel_time) + fileTimeToUint64(user_time);
            }

            PROCESS_MEMORY_COUNTERS counters{};
            if (GetProcessMemoryInfo(process, &counters, sizeof(counters))) {
                row.working_set = counters.WorkingSetSize;
            }

            WCHAR image_name[MAX_PATH] = {0};
            DWORD image_len = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, image_name, &image_len) && image_len > 0) {
                std::wstring full_path(image_name, image_len);
                const auto slash = full_path.find_last_of(L"\\/");
                if (slash != std::wstring::npos && slash + 1 < full_path.size()) {
                    full_path = full_path.substr(slash + 1);
                }
                const std::string parsed = wideToUtf8(full_path);
                if (!parsed.empty()) {
                    row.command = parsed;
                }
            }
            CloseHandle(process);
        }

        if (row.command.empty()) {
            row.command = "pid_" + std::to_string(row.pid);
        }
        rows.push_back(row);
    } while (Process32NextW(snap, &entry));

    CloseHandle(snap);
    return rows;
}

inline void appendTcpSocketCounts(int family, std::unordered_map<DWORD, int>& counts) {
    ULONG size = 0;
    DWORD rc = GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
    if (rc != ERROR_INSUFFICIENT_BUFFER) {
        return;
    }

    std::vector<unsigned char> buffer(size);
    rc = GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
    if (rc != NO_ERROR) {
        return;
    }

    if (family == AF_INET) {
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            counts[table->table[i].dwOwningPid] += 1;
        }
    } else if (family == AF_INET6) {
        const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            counts[table->table[i].dwOwningPid] += 1;
        }
    }
}

inline void appendUdpSocketCounts(int family, std::unordered_map<DWORD, int>& counts) {
    ULONG size = 0;
    DWORD rc = GetExtendedUdpTable(nullptr, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
    if (rc != ERROR_INSUFFICIENT_BUFFER) {
        return;
    }

    std::vector<unsigned char> buffer(size);
    rc = GetExtendedUdpTable(buffer.data(), &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
    if (rc != NO_ERROR) {
        return;
    }

    if (family == AF_INET) {
        const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            counts[table->table[i].dwOwningPid] += 1;
        }
    } else if (family == AF_INET6) {
        const auto* table = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            counts[table->table[i].dwOwningPid] += 1;
        }
    }
}

inline std::unordered_map<DWORD, int> buildSocketCountByPid() {
    std::unordered_map<DWORD, int> counts;
    appendTcpSocketCounts(AF_INET, counts);
    appendTcpSocketCounts(AF_INET6, counts);
    appendUdpSocketCounts(AF_INET, counts);
    appendUdpSocketCounts(AF_INET6, counts);
    return counts;
}
#endif

inline bool commandExists(const std::string& cmd) {
#ifdef _WIN32
    if (cmd.find('/') != std::string::npos || cmd.find('\\') != std::string::npos) {
        return _access(cmd.c_str(), 0) == 0;
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return false;
    }

    std::stringstream ss(path_env);
    std::string dir;
    const std::string ext = ".exe";
    while (std::getline(ss, dir, ';')) {
        if (dir.empty()) {
            dir = ".";
        }
        std::string full = dir + "\\" + cmd;
        if (full.length() < 4 || toLower(full.substr(full.length() - 4)) != ext) {
            full += ext;
        }
        if (_access(full.c_str(), 0) == 0) {
            return true;
        }
    }
    return false;
#else
    if (cmd.find('/') != std::string::npos) {
        return ::access(cmd.c_str(), X_OK) == 0;
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return false;
    }

    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) {
            dir = ".";
        }
        const std::string full = dir + "/" + cmd;
        if (::access(full.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
#endif
}

inline CommandResult runCommand(const std::string& cmd) {
    CommandResult result;
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = ::popen(cmd.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return result;
    }

    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

#ifdef _WIN32
    const int status = _pclose(pipe);
    result.exit_code = status;
#else
    const int status = ::pclose(pipe);
    if (status == -1) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = -1;
    }
#endif

    return result;
}

inline std::vector<fs::path> listDirectory(const fs::path& path) {
    std::vector<fs::path> entries;

    std::error_code ec;
    fs::directory_iterator iter(path, ec);
    if (ec) {
        return {};
    }
    fs::directory_iterator end;
    while (iter != end) {
        entries.push_back(iter->path());
        iter.increment(ec);
        if (ec) {
            break;
        }
    }

    std::sort(entries.begin(), entries.end());
    return entries;
}

inline std::string formatBytes(long double bytes) {
    static const std::array<const char*, 5> units = {"B", "KiB", "MiB", "GiB", "TiB"};
    std::size_t unit_index = 0;
    while (bytes >= 1024.0L && unit_index + 1 < units.size()) {
        bytes /= 1024.0L;
        ++unit_index;
    }

    std::ostringstream out;
    out << std::fixed;
    if (unit_index == 0) {
        out << std::setprecision(0);
    } else {
        out << std::setprecision(2);
    }
    out << bytes << " " << units[unit_index];
    return out.str();
}

inline std::string formatKilobytes(long long kib) {
    const long double bytes = static_cast<long double>(kib) * 1024.0L;
    return formatBytes(bytes);
}

inline std::string formatUptime(double seconds) {
    long long total = static_cast<long long>(seconds);
    const long long days = total / 86400;
    total %= 86400;
    const long long hours = total / 3600;
    total %= 3600;
    const long long minutes = total / 60;
    const long long secs = total % 60;

    std::ostringstream out;
    out << days << "d " << hours << "h " << minutes << "m " << secs << "s";
    return out.str();
}

inline std::string formatPercent(double value, int precision = 1) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value << "%";
    return out.str();
}

inline std::string colorByPercent(double value) {
    const std::string text = formatPercent(value, 1);
    if (value >= 85.0) {
        return colorize(text, ansi::GREEN);
    }
    if (value >= 60.0) {
        return colorize(text, ansi::YELLOW);
    }
    return colorize(text, ansi::RED);
}

inline std::string stateLabel(CheckState state) {
    if (state == CheckState::Pass) {
        return colorize("PASS", ansi::GREEN);
    }
    if (state == CheckState::Fail) {
        return colorize("FAIL", ansi::RED);
    }
    return colorize("UNAVAILABLE", ansi::YELLOW);
}

inline void printSectionHeader(const std::string& title) {
    std::cout << "\n" << ansi::BOLD << ansi::CYAN << title << ansi::RESET << "\n";
}

inline void printSubHeader(const std::string& title) {
    std::cout << "  " << ansi::MAGENTA << title << ansi::RESET << "\n";
}

inline void printKeyValue(const std::string& key, const std::string& value) {
    std::cout << "  " << std::left << std::setw(28) << key << ": " << value << "\n";
}

inline void printBlockLines(const std::string& text) {
    const auto lines = splitLines(text);
    if (lines.empty()) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
        return;
    }
    for (const auto& line : lines) {
        if (!line.empty()) {
            std::cout << "    " << line << "\n";
        }
    }
}

struct ProcessUsage {
    int pid = -1;
    std::string command;
    double cpu = 0.0;
    double mem = 0.0;
    int net_sockets = 0;
};

inline int countNetworkSockets(int pid) {
#ifdef _WIN32
    (void)pid;
    return 0;  // Stub for Windows
#else
    int count = 0;
    std::string fd_dir = "/proc/" + std::to_string(pid) + "/fd";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fd_dir, ec)) {
        std::error_code sym_ec;
        if (fs::is_symlink(entry, sym_ec)) {
            auto target = fs::read_symlink(entry, sym_ec);
            if (!sym_ec && target.string().find("socket:[") == 0) {
                count++;
            }
        }
    }
    return count;
#endif
}

inline std::vector<ProcessUsage> getTopProcesses(const std::string& sort_key, std::size_t top_n) {
    std::vector<ProcessUsage> rows;
#ifdef _WIN32
    const auto first_snapshot = captureProcessSnapshot();
    if (first_snapshot.empty()) {
        return rows;
    }

    const auto sample_start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto second_snapshot = captureProcessSnapshot();
    const auto sample_end = std::chrono::steady_clock::now();

    if (second_snapshot.empty()) {
        return rows;
    }

    std::unordered_map<DWORD, ProcessSnapshotRow> before;
    before.reserve(first_snapshot.size());
    for (const auto& row : first_snapshot) {
        before[row.pid] = row;
    }

    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    const bool has_mem = GlobalMemoryStatusEx(&mem_status) != 0;
    const double total_mem = has_mem ? static_cast<double>(mem_status.ullTotalPhys) : 0.0;
    const double wall_100ns =
        std::chrono::duration<double>(sample_end - sample_start).count() * 10000000.0;

    const auto socket_counts = buildSocketCountByPid();

    for (const auto& now : second_snapshot) {
        ProcessUsage row;
        row.pid = static_cast<int>(now.pid);
        row.command = now.command;

        const auto it = before.find(now.pid);
        if (it != before.end() && wall_100ns > 0.0 &&
            now.cpu_time_100ns >= it->second.cpu_time_100ns) {
            const double delta =
                static_cast<double>(now.cpu_time_100ns - it->second.cpu_time_100ns);
            row.cpu = (delta / wall_100ns) * 100.0;
        }

        if (total_mem > 0.0) {
            row.mem = (static_cast<double>(now.working_set) / total_mem) * 100.0;
        }

        const auto socket_it = socket_counts.find(now.pid);
        if (socket_it != socket_counts.end()) {
            row.net_sockets = socket_it->second;
        }

        const std::string lowered = toLower(row.command);
        if (lowered == "check" || lowered == "check.exe") {
            continue;
        }
        rows.push_back(row);
    }

    if (sort_key == "%cpu") {
        std::sort(rows.begin(), rows.end(),
                  [](const ProcessUsage& a, const ProcessUsage& b) { return a.cpu > b.cpu; });
    } else if (sort_key == "%mem") {
        std::sort(rows.begin(), rows.end(),
                  [](const ProcessUsage& a, const ProcessUsage& b) { return a.mem > b.mem; });
    } else if (sort_key == "net") {
        std::sort(rows.begin(), rows.end(), [](const ProcessUsage& a, const ProcessUsage& b) {
            if (a.net_sockets == b.net_sockets) {
                return a.cpu > b.cpu;
            }
            return a.net_sockets > b.net_sockets;
        });
    } else {
        std::sort(rows.begin(), rows.end(),
                  [](const ProcessUsage& a, const ProcessUsage& b) { return a.cpu > b.cpu; });
    }

    if (rows.size() > top_n) {
        rows.resize(top_n);
    }
    return rows;
#else
    if (!commandExists("ps")) {
        return rows;
    }

    std::string cmd;
    if (sort_key == "net") {
        cmd = "ps -eo pid=,comm=,%cpu=,%mem= 2>/dev/null";
    } else {
        cmd = "ps -eo pid=,comm=,%cpu=,%mem= --sort=-" + sort_key + " 2>/dev/null";
    }

    const auto result = runCommand(cmd);
    if (result.exit_code != 0) {
        return rows;
    }

    const int my_pid = ::getpid();
    const auto lines = splitLines(result.output);
    for (const auto& line : lines) {
        if (line.empty()) {
            continue;
        }
        std::istringstream parser(line);
        ProcessUsage process;
        if (!(parser >> process.pid >> process.command >> process.cpu >> process.mem)) {
            continue;
        }
        if (process.pid == my_pid || process.command == "check") {
            continue;
        }

        process.net_sockets = countNetworkSockets(process.pid);
        rows.push_back(process);

        if (sort_key != "net" && rows.size() >= top_n) {
            break;
        }
    }

    if (sort_key == "net") {
        std::sort(rows.begin(), rows.end(), [](const ProcessUsage& a, const ProcessUsage& b) {
            return a.net_sockets > b.net_sockets;
        });
        if (rows.size() > top_n) {
            rows.resize(top_n);
        }
    }

    return rows;
#endif
}

inline std::string fitTableCell(const std::string& value, std::size_t width) {
    if (value.size() <= width) {
        return value;
    }
    if (width <= 3) {
        return value.substr(0, width);
    }
    return value.substr(0, width - 3) + "...";
}

inline void printTopProcessTable(const std::vector<ProcessUsage>& rows) {
    std::size_t command_width = 16;
    for (const auto& row : rows) {
        command_width = std::max(command_width, row.command.size() + 1);
    }
    command_width = std::min<std::size_t>(command_width, 28);

    std::cout << "    " << std::right << std::setw(8) << "PID"
              << " " << std::left << std::setw(static_cast<int>(command_width)) << "COMMAND"
              << std::right << std::setw(6) << "%CPU" << std::setw(6) << "%MEM" << std::setw(6)
              << "NET" << "\n";

    for (const auto& row : rows) {
        std::ostringstream cpu_text;
        cpu_text << std::fixed << std::setprecision(1) << row.cpu;

        std::ostringstream mem_text;
        mem_text << std::fixed << std::setprecision(1) << row.mem;

        std::cout << "    " << std::right << std::setw(8) << row.pid << " " << std::left
                  << std::setw(static_cast<int>(command_width))
                  << fitTableCell(row.command, command_width) << std::right << std::setw(6)
                  << cpu_text.str() << std::setw(6) << mem_text.str() << std::setw(6)
                  << row.net_sockets << "\n";
    }
    std::cout << std::left;
}
