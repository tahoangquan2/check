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
#include <chrono>
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
}

enum class CheckState { Pass, Fail, Unavailable };

struct SimpleCheck {
    CheckState state = CheckState::Unavailable;
    std::string detail;
};

inline bool& terminalColorEnabled() {
    static bool enabled = false;
    return enabled;
}

inline bool stdoutIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

inline void configureTerminal(bool no_color) {
    terminalColorEnabled() = !no_color && stdoutIsTerminal();
#ifdef _WIN32
    if (terminalColorEnabled()) {
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (output == INVALID_HANDLE_VALUE || !GetConsoleMode(output, &mode) ||
            !SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            terminalColorEnabled() = false;
        }
    }
#endif
}

inline std::string colorize(const std::string& text, const char* color) {
    return terminalColorEnabled() ? std::string(color) + text + ansi::RESET : text;
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

inline char lowerCharacter(char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

inline bool digitCharacter(char value) {
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
}

inline std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), lowerCharacter);
    return value;
}

inline bool isDigits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), digitCharacter);
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

inline std::optional<std::string> readWindowsRegistryString(HKEY root, const char* path,
                                                            const char* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return std::nullopt;
    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegQueryValueExA(key, name, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }
    std::vector<char> buffer(size + 1, '\0');
    status = RegQueryValueExA(key, name, nullptr, &type,
                              reinterpret_cast<unsigned char*>(buffer.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return std::nullopt;
    return trim(std::string(buffer.data()));
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

struct DirectoryListing {
    std::vector<fs::path> entries;
    std::error_code error;
};

inline bool pathExists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error) && !error;
}

inline DirectoryListing listDirectory(const fs::path& path) {
    DirectoryListing listing;

    std::error_code ec;
    fs::directory_iterator iter(path, ec);
    if (ec) {
        listing.error = ec;
        return listing;
    }
    fs::directory_iterator end;
    while (iter != end) {
        listing.entries.push_back(iter->path());
        iter.increment(ec);
        if (ec) {
            listing.error = ec;
            break;
        }
    }

    std::sort(listing.entries.begin(), listing.entries.end());
    return listing;
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

enum class PercentLevel { Good, Warning, Critical };

inline PercentLevel capacityPercentLevel(double value) {
    if (value >= 85.0) return PercentLevel::Good;
    if (value >= 60.0) return PercentLevel::Warning;
    return PercentLevel::Critical;
}

inline PercentLevel usagePercentLevel(double value) {
    if (value >= 85.0) return PercentLevel::Critical;
    if (value >= 60.0) return PercentLevel::Warning;
    return PercentLevel::Good;
}

inline std::string colorCapacityPercent(double value) {
    const std::string text = formatPercent(value, 1);
    if (capacityPercentLevel(value) == PercentLevel::Good) {
        return colorize(text, ansi::GREEN);
    }
    if (capacityPercentLevel(value) == PercentLevel::Warning) {
        return colorize(text, ansi::YELLOW);
    }
    return colorize(text, ansi::RED);
}

inline std::string colorUsagePercent(double value) {
    const std::string text = formatPercent(value, 1);
    if (usagePercentLevel(value) == PercentLevel::Critical) {
        return colorize(text, ansi::RED);
    }
    if (usagePercentLevel(value) == PercentLevel::Warning) {
        return colorize(text, ansi::YELLOW);
    }
    return colorize(text, ansi::GREEN);
}

struct ReportCounts {
    int passed = 0;
    int failed = 0;
    int unavailable = 0;
};

inline ReportCounts& reportCounts() {
    static ReportCounts counts;
    return counts;
}

inline void recordCheck(CheckState state) {
    if (state == CheckState::Pass) {
        ++reportCounts().passed;
    } else if (state == CheckState::Fail) {
        ++reportCounts().failed;
    } else {
        ++reportCounts().unavailable;
    }
}

inline std::string stateLabel(CheckState state) {
    recordCheck(state);
    if (state == CheckState::Pass) {
        return colorize("PASS", ansi::GREEN);
    }
    if (state == CheckState::Fail) {
        return colorize("FAIL", ansi::RED);
    }
    return colorize("UNAVAILABLE", ansi::YELLOW);
}

inline bool isAnsiSgr(const std::string& value, std::size_t start, std::size_t& end) {
    if (start + 2 >= value.size() || value[start] != '\x1b' || value[start + 1] != '[') {
        return false;
    }
    std::size_t pos = start + 2;
    while (pos < value.size() &&
           (std::isdigit(static_cast<unsigned char>(value[pos])) != 0 || value[pos] == ';')) {
        ++pos;
    }
    if (pos >= value.size() || value[pos] != 'm') {
        return false;
    }
    end = pos + 1;
    return true;
}

inline std::string sanitizeTerminalText(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size();) {
        std::size_t ansi_end = i;
        if (terminalColorEnabled() && isAnsiSgr(value, i, ansi_end)) {
            result.append(value, i, ansi_end - i);
            i = ansi_end;
            continue;
        }
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        if (byte < 0x20 || byte == 0x7f) {
            result.push_back(' ');
        } else {
            result.push_back(value[i]);
        }
        ++i;
    }
    return result;
}

inline unsigned int decodeUtf8(const std::string& value, std::size_t& position) {
    const unsigned char lead = static_cast<unsigned char>(value[position++]);
    if (lead < 0x80U) return lead;
    int continuation_count = 0;
    unsigned int codepoint = 0;
    if ((lead & 0xe0U) == 0xc0U) {
        continuation_count = 1;
        codepoint = lead & 0x1fU;
    } else if ((lead & 0xf0U) == 0xe0U) {
        continuation_count = 2;
        codepoint = lead & 0x0fU;
    } else if ((lead & 0xf8U) == 0xf0U) {
        continuation_count = 3;
        codepoint = lead & 0x07U;
    } else {
        return 0xfffdU;
    }
    if (position + static_cast<std::size_t>(continuation_count) > value.size()) return 0xfffdU;
    for (int index = 0; index < continuation_count; ++index) {
        const unsigned char continuation = static_cast<unsigned char>(value[position]);
        if ((continuation & 0xc0U) != 0x80U) return 0xfffdU;
        ++position;
        codepoint = (codepoint << 6U) | (continuation & 0x3fU);
    }
    return codepoint;
}

inline std::size_t unicodeColumnWidth(unsigned int codepoint) {
    if ((codepoint >= 0x0300U && codepoint <= 0x036fU) ||
        (codepoint >= 0x1ab0U && codepoint <= 0x1affU) ||
        (codepoint >= 0x1dc0U && codepoint <= 0x1dffU) ||
        (codepoint >= 0x20d0U && codepoint <= 0x20ffU) || codepoint == 0x200dU ||
        (codepoint >= 0xfe00U && codepoint <= 0xfe0fU) ||
        (codepoint >= 0xfe20U && codepoint <= 0xfe2fU)) {
        return 0;
    }
    if ((codepoint >= 0x1100U && codepoint <= 0x115fU) || codepoint == 0x2329U ||
        codepoint == 0x232aU || (codepoint >= 0x2e80U && codepoint <= 0xa4cfU) ||
        (codepoint >= 0xac00U && codepoint <= 0xd7a3U) ||
        (codepoint >= 0xf900U && codepoint <= 0xfaffU) ||
        (codepoint >= 0xfe10U && codepoint <= 0xfe6fU) ||
        (codepoint >= 0xff00U && codepoint <= 0xff60U) ||
        (codepoint >= 0x1f300U && codepoint <= 0x1faffU)) {
        return 2;
    }
    return 1;
}

inline std::size_t utf8DisplayWidth(const std::string& value) {
    std::size_t position = 0;
    std::size_t width = 0;
    while (position < value.size()) width += unicodeColumnWidth(decodeUtf8(value, position));
    return width;
}

inline std::string utf8Prefix(const std::string& value, std::size_t width) {
    std::size_t position = 0;
    std::size_t columns = 0;
    while (position < value.size()) {
        const std::size_t start = position;
        const std::size_t next_width = unicodeColumnWidth(decodeUtf8(value, position));
        if (columns + next_width > width) {
            position = start;
            break;
        }
        columns += next_width;
    }
    return value.substr(0, position);
}

inline std::string fitTableCell(const std::string& raw, std::size_t width) {
    const std::string value = sanitizeTerminalText(raw);
    if (utf8DisplayWidth(value) <= width) return value;
    if (width <= 3) return utf8Prefix(value, width);
    return utf8Prefix(value, width - 3) + "...";
}

inline void printSectionHeader(const std::string& title) {
    std::cout << "\n";
    if (terminalColorEnabled()) std::cout << ansi::BOLD << ansi::CYAN;
    std::cout << sanitizeTerminalText(title);
    if (terminalColorEnabled()) std::cout << ansi::RESET;
    std::cout << "\n";
}

inline void printSubHeader(const std::string& title) {
    std::cout << "  " << colorize(sanitizeTerminalText(title), ansi::MAGENTA) << "\n";
}

inline void printKeyValue(const std::string& key, const std::string& value) {
    std::cout << "  " << std::left << std::setw(28) << sanitizeTerminalText(key) << ": "
              << sanitizeTerminalText(value) << "\n";
}

inline std::size_t countNonEmptyLines(const std::vector<std::string>& lines) {
    std::size_t count = 0;
    for (const std::string& line : lines) {
        if (!line.empty()) ++count;
    }
    return count;
}

inline void printBlockLines(const std::string& text,
                            std::size_t limit = std::numeric_limits<std::size_t>::max()) {
    const auto lines = splitLines(text);
    const std::size_t non_empty_lines = countNonEmptyLines(lines);
    if (non_empty_lines == 0) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
        return;
    }
    std::size_t printed = 0;
    for (const auto& line : lines) {
        if (!line.empty()) {
            if (printed == limit) break;
            std::cout << "    " << sanitizeTerminalText(line) << "\n";
            ++printed;
        }
    }
    if (non_empty_lines > printed) {
        std::cout << "    ... " << non_empty_lines - printed << " more lines\n";
    }
}
