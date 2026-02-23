#pragma once

#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

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

inline bool commandExists(const std::string& cmd) {
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
}

inline CommandResult runCommand(const std::string& cmd) {
    CommandResult result;
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return result;
    }

    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

    const int status = ::pclose(pipe);
    if (status == -1) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = -1;
    }

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

// Struct and helper functions for processes that might be needed by both runtime and other modules
struct ProcessUsage {
    int pid = -1;
    std::string command;
    double cpu = 0.0;
    double mem = 0.0;
};

inline std::vector<ProcessUsage> getTopProcesses(const std::string& sort_key, std::size_t top_n) {
    std::vector<ProcessUsage> rows;
    if (!commandExists("ps")) {
        return rows;
    }

    std::string cmd = "ps -eo pid=,comm=,%cpu=,%mem= --sort=-" + sort_key + " 2>/dev/null";
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
        rows.push_back(process);
        if (rows.size() >= top_n) {
            break;
        }
    }
    return rows;
}

inline void printTopProcessTable(const std::vector<ProcessUsage>& rows) {
    std::cout << "    " << std::right << std::setw(8) << "PID"
              << " " << std::left << std::setw(16) << "COMMAND" << std::right << std::setw(6)
              << "%CPU" << std::setw(6) << "%MEM" << "\n";

    for (const auto& row : rows) {
        std::ostringstream cpu_text;
        cpu_text << std::fixed << std::setprecision(1) << row.cpu;

        std::ostringstream mem_text;
        mem_text << std::fixed << std::setprecision(1) << row.mem;

        std::cout << "    " << std::right << std::setw(8) << row.pid << " " << std::left
                  << std::setw(16) << row.command << std::right << std::setw(6) << cpu_text.str()
                  << std::setw(6) << mem_text.str() << "\n";
    }
    std::cout << std::left;
}
