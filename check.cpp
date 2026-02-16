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
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace ansi {
constexpr const char *RESET = "\033[0m";
constexpr const char *BOLD = "\033[1m";
constexpr const char *RED = "\033[31m";
constexpr const char *GREEN = "\033[32m";
constexpr const char *YELLOW = "\033[33m";
constexpr const char *MAGENTA = "\033[35m";
constexpr const char *CYAN = "\033[36m";
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

std::string colorize(const std::string &text, const char *color) {
    return std::string(color) + text + ansi::RESET;
}

std::string trim(const std::string &input) {
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

bool startsWith(const std::string &value, const std::string &prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool isDigits(const std::string &value) {
    return !value.empty() && std::all_of(value.begin(), value.end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::optional<std::string> readFile(const std::string &path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << input.rdbuf();
    return ss.str();
}

std::optional<std::string> readFirstLine(const std::string &path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }
    std::string line;
    std::getline(input, line);
    return trim(line);
}

std::optional<long long> parseLongLongPrefix(const std::string &value) {
    if (value.empty()) {
        return std::nullopt;
    }

    errno = 0;
    char *end = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    if (end == value.c_str() || errno == ERANGE) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<int> parseIntPrefix(const std::string &value) {
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

std::optional<double> parseDoubleStrict(const std::string &value) {
    if (value.empty()) {
        return std::nullopt;
    }

    errno = 0;
    char *end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE) {
        return std::nullopt;
    }
    if (*end != '\0') {
        return std::nullopt;
    }
    return parsed;
}

std::optional<long long> readLongFromFile(const std::string &path) {
    const auto line = readFirstLine(path);
    if (!line || line->empty()) {
        return std::nullopt;
    }
    return parseLongLongPrefix(*line);
}

std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> lines;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(trim(line));
    }
    return lines;
}

bool commandExists(const std::string &cmd) {
    if (cmd.find('/') != std::string::npos) {
        return ::access(cmd.c_str(), X_OK) == 0;
    }

    const char *path_env = std::getenv("PATH");
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

CommandResult runCommand(const std::string &cmd) {
    CommandResult result;
    FILE *pipe = ::popen(cmd.c_str(), "r");
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

std::vector<fs::path> listDirectory(const fs::path &path) {
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

std::string formatBytes(long double bytes) {
    static const std::array<const char *, 5> units = {"B", "KiB", "MiB", "GiB", "TiB"};
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

std::string formatKilobytes(long long kib) {
    const long double bytes = static_cast<long double>(kib) * 1024.0L;
    return formatBytes(bytes);
}

std::string formatUptime(double seconds) {
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

std::string formatPercent(double value, int precision = 1) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value << "%";
    return out.str();
}

std::string colorByPercent(double value) {
    const std::string text = formatPercent(value, 1);
    if (value >= 85.0) {
        return colorize(text, ansi::GREEN);
    }
    if (value >= 60.0) {
        return colorize(text, ansi::YELLOW);
    }
    return colorize(text, ansi::RED);
}

std::string stateLabel(CheckState state) {
    if (state == CheckState::Pass) {
        return colorize("PASS", ansi::GREEN);
    }
    if (state == CheckState::Fail) {
        return colorize("FAIL", ansi::RED);
    }
    return colorize("UNAVAILABLE", ansi::YELLOW);
}

void printSectionHeader(const std::string &title) {
    std::cout << "\n" << ansi::BOLD << ansi::CYAN << title << ansi::RESET << "\n";
}

void printSubHeader(const std::string &title) {
    std::cout << "  " << ansi::MAGENTA << title << ansi::RESET << "\n";
}

void printKeyValue(const std::string &key, const std::string &value) {
    std::cout << "  " << std::left << std::setw(28) << key << ": " << value << "\n";
}

void printBlockLines(const std::string &text) {
    const auto lines = splitLines(text);
    if (lines.empty()) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
        return;
    }
    for (const auto &line : lines) {
        if (!line.empty()) {
            std::cout << "    " << line << "\n";
        }
    }
}

struct BatteryInfo {
    std::string name;
    std::string status = "N/A";
    std::optional<long long> capacity;
    std::optional<long long> cycle_count;
    std::optional<long long> charge_full;
    std::optional<long long> charge_full_design;
    std::optional<long long> energy_full;
    std::optional<long long> energy_full_design;
    std::optional<long long> voltage_now;
    std::optional<double> health_percent;
};

std::vector<BatteryInfo> collectBatteries() {
    std::vector<BatteryInfo> batteries;
    const auto entries = listDirectory("/sys/class/power_supply");
    for (const auto &entry : entries) {
        const std::string name = entry.filename().string();
        if (!startsWith(name, "BAT")) {
            continue;
        }

        BatteryInfo info;
        info.name = name;
        info.status = readFirstLine((entry / "status").string()).value_or("N/A");
        info.capacity = readLongFromFile((entry / "capacity").string());
        info.cycle_count = readLongFromFile((entry / "cycle_count").string());
        info.charge_full = readLongFromFile((entry / "charge_full").string());
        info.charge_full_design = readLongFromFile((entry / "charge_full_design").string());
        info.energy_full = readLongFromFile((entry / "energy_full").string());
        info.energy_full_design = readLongFromFile((entry / "energy_full_design").string());
        info.voltage_now = readLongFromFile((entry / "voltage_now").string());

        if (info.charge_full && info.charge_full_design && *info.charge_full_design > 0) {
            info.health_percent = (100.0 * static_cast<double>(*info.charge_full)) /
                                  static_cast<double>(*info.charge_full_design);
        } else if (info.energy_full && info.energy_full_design && *info.energy_full_design > 0) {
            info.health_percent = (100.0 * static_cast<double>(*info.energy_full)) /
                                  static_cast<double>(*info.energy_full_design);
        }
        batteries.push_back(info);
    }
    return batteries;
}

std::vector<std::pair<std::string, std::optional<long long>>> collectAcAdapters() {
    std::vector<std::pair<std::string, std::optional<long long>>> adapters;
    const auto entries = listDirectory("/sys/class/power_supply");
    for (const auto &entry : entries) {
        const std::string name = entry.filename().string();
        const std::string type = readFirstLine((entry / "type").string()).value_or("");
        if (!startsWith(name, "AC") && type != "Mains") {
            continue;
        }
        adapters.push_back({name, readLongFromFile((entry / "online").string())});
    }
    return adapters;
}

void printBatterySection() {
    printSectionHeader("BATTERY HEALTH");
    const auto batteries = collectBatteries();
    if (batteries.empty()) {
        printKeyValue("Battery", colorize("No battery detected", ansi::YELLOW));
    } else {
        for (const auto &battery : batteries) {
            printSubHeader("Battery " + battery.name);
            printKeyValue("Status", battery.status);
            printKeyValue("Capacity", battery.capacity
                                          ? colorByPercent(static_cast<double>(*battery.capacity))
                                          : colorize("N/A", ansi::YELLOW));
            printKeyValue("Cycle Count", battery.cycle_count ? std::to_string(*battery.cycle_count)
                                                             : colorize("N/A", ansi::YELLOW));
            printKeyValue("Health", battery.health_percent ? colorByPercent(*battery.health_percent)
                                                           : colorize("N/A", ansi::YELLOW));
            if (battery.voltage_now) {
                std::ostringstream voltage;
                voltage << std::fixed << std::setprecision(2)
                        << (static_cast<double>(*battery.voltage_now) / 1000000.0) << " V";
                printKeyValue("Voltage", voltage.str());
            } else {
                printKeyValue("Voltage", colorize("N/A", ansi::YELLOW));
            }
        }
    }

    const auto adapters = collectAcAdapters();
    if (adapters.empty()) {
        printKeyValue("AC Adapter", colorize("N/A", ansi::YELLOW));
    } else {
        printSubHeader("AC Adapters");
        for (const auto &adapter : adapters) {
            const std::string state = adapter.second
                                          ? (*adapter.second == 1 ? colorize("Online", ansi::GREEN)
                                                                  : colorize("Offline", ansi::RED))
                                          : colorize("N/A", ansi::YELLOW);
            printKeyValue(adapter.first, state);
        }
    }
}

struct CpuTimes {
    unsigned long long idle_all = 0;
    unsigned long long total = 0;
    bool valid = false;
};

CpuTimes readCpuTimes() {
    CpuTimes times;
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
    return times;
}

std::optional<double> sampleCpuUsagePercent() {
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

CpuIdentity getCpuIdentity() {
    CpuIdentity identity;
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
            for (const auto &line : lines) {
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

    return identity;
}

std::optional<std::array<double, 3>> readLoadAverage() {
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
}

std::map<std::string, long long> readMemInfo() {
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

std::optional<double> readUptimeSeconds() {
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

std::pair<int, long long> countProcessesAndThreads() {
    int process_count = 0;
    long long thread_count = 0;

    const auto entries = listDirectory("/proc");
    for (const auto &entry : entries) {
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

struct ProcessUsage {
    int pid = -1;
    std::string command;
    double cpu = 0.0;
    double mem = 0.0;
};

std::vector<ProcessUsage> getTopProcesses(const std::string &sort_key, std::size_t top_n) {
    std::vector<ProcessUsage> rows;
    if (!commandExists("ps")) {
        return rows;
    }

    std::string cmd = "ps -eo pid=,comm=,%cpu=,%mem= --sort=-" + sort_key + " 2>/dev/null";
    const auto result = runCommand(cmd);
    if (result.exit_code != 0) {
        return rows;
    }

    const auto lines = splitLines(result.output);
    for (const auto &line : lines) {
        if (line.empty()) {
            continue;
        }
        std::istringstream parser(line);
        ProcessUsage process;
        if (!(parser >> process.pid >> process.command >> process.cpu >> process.mem)) {
            continue;
        }
        rows.push_back(process);
        if (rows.size() >= top_n) {
            break;
        }
    }
    return rows;
}

void printTopProcessTable(const std::vector<ProcessUsage> &rows) {
    std::cout << "    " << std::right << std::setw(8) << "PID"
              << " " << std::left << std::setw(16) << "COMMAND" << std::right << std::setw(6)
              << "%CPU" << std::setw(6) << "%MEM" << "\n";

    for (const auto &row : rows) {
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

void printRuntimeSection() {
    printSectionHeader("CPU / RAM / RUNTIME");

    const CpuIdentity cpu = getCpuIdentity();
    printKeyValue("CPU Model", cpu.model);
    printKeyValue("CPU Cores",
                  cpu.cores > 0 ? std::to_string(cpu.cores) : colorize("N/A", ansi::YELLOW));

    const auto cpu_usage = sampleCpuUsagePercent();
    printKeyValue("CPU Usage (500ms sample)",
                  cpu_usage ? colorByPercent(*cpu_usage) : colorize("N/A", ansi::YELLOW));

    const auto load = readLoadAverage();
    if (load) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << (*load)[0] << ", " << (*load)[1] << ", "
            << (*load)[2];
        printKeyValue("Load Average (1/5/15m)", out.str());
    } else {
        printKeyValue("Load Average (1/5/15m)", colorize("N/A", ansi::YELLOW));
    }

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

    const auto uptime = readUptimeSeconds();
    printKeyValue("Uptime", uptime ? formatUptime(*uptime) : colorize("N/A", ansi::YELLOW));

    const auto [processes, threads] = countProcessesAndThreads();
    printKeyValue("Process Count", std::to_string(processes));
    printKeyValue("Thread Count", std::to_string(threads));

    const auto top_cpu = getTopProcesses("%cpu", 10);
    if (top_cpu.empty()) {
        printKeyValue("Top CPU Processes", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by CPU)");
        printTopProcessTable(top_cpu);
    }

    const auto top_ram = getTopProcesses("%mem", 10);
    if (top_ram.empty()) {
        printKeyValue("Top RAM Processes", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by RAM)");
        printTopProcessTable(top_ram);
    }
}

SimpleCheck checkPing(const std::string &host, int probe_count = 3) {
    if (!commandExists("ping")) {
        return {CheckState::Unavailable, "ping command not found"};
    }

    const int probes = std::max(1, probe_count);
    const std::string cmd = "ping -c " + std::to_string(probes) + " -W 2 " + host + " 2>/dev/null";
    const auto result = runCommand(cmd);
    if (result.exit_code != 0) {
        return {CheckState::Fail, "host unreachable"};
    }

    const auto lines = splitLines(result.output);
    for (const auto &line : lines) {
        if (line.find("min/avg/max") != std::string::npos) {
            const auto eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string values = trim(line.substr(eq_pos + 1));
                const auto space_pos = values.find(' ');
                if (space_pos != std::string::npos) {
                    values = values.substr(0, space_pos);
                }

                std::vector<std::string> parts;
                std::stringstream parser(values);
                std::string item;
                while (std::getline(parser, item, '/')) {
                    parts.push_back(item);
                }
                if (parts.size() >= 2) {
                    return {CheckState::Pass, parts[1] + " ms avg"};
                }
            }
        }
    }

    return {CheckState::Pass, "reachable"};
}

SimpleCheck checkDns() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    const int rc = ::getaddrinfo("example.com", "80", &hints, &result);
    if (rc != 0 || result == nullptr) {
        return {CheckState::Fail, rc != 0 ? ::gai_strerror(rc) : "no address returned"};
    }

    char host[NI_MAXHOST] = {0};
    std::string detail = "resolved";
    if (::getnameinfo(result->ai_addr, result->ai_addrlen, host, sizeof(host), nullptr, 0,
                      NI_NUMERICHOST) == 0) {
        detail = std::string("resolved to ") + host;
    }

    ::freeaddrinfo(result);
    return {CheckState::Pass, detail};
}

SimpleCheck checkHttpLatency() {
    if (!commandExists("curl")) {
        return {CheckState::Unavailable, "curl command not found"};
    }

    const auto result = runCommand(
        "curl -s -o /dev/null -w \"%{time_total}\" --max-time 5 https://example.com 2>/dev/null");
    if (result.exit_code != 0) {
        return {CheckState::Fail, "HTTP request failed"};
    }

    const std::string value = trim(result.output);
    const auto seconds = parseDoubleStrict(value);
    if (seconds) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << (*seconds * 1000.0) << " ms";
        return {CheckState::Pass, out.str()};
    }
    return {CheckState::Fail, "unable to parse latency"};
}

void printTailscaleInternetInfo() {
    printSubHeader("Tailscale");

    if (fs::exists("/sys/class/net/tailscale0")) {
        const std::string state =
            readFirstLine("/sys/class/net/tailscale0/operstate").value_or("N/A");
        const std::string mac = readFirstLine("/sys/class/net/tailscale0/address").value_or("N/A");
        printKeyValue("tailscale0 Interface", "present (state=" + state + ", mac=" + mac + ")");
    } else {
        printKeyValue("tailscale0 Interface", colorize("not found", ansi::YELLOW));
    }

    if (!commandExists("tailscale")) {
        printKeyValue("tailscale CLI", colorize("UNAVAILABLE", ansi::YELLOW));
        return;
    }

    printKeyValue("tailscale CLI", colorize("available", ansi::GREEN));

    const auto ip4 = runCommand("tailscale ip -4 2>/dev/null");
    if (ip4.exit_code == 0 && !trim(ip4.output).empty()) {
        printKeyValue("Tailscale IPv4", trim(ip4.output));
    } else {
        printKeyValue("Tailscale IPv4", colorize("N/A", ansi::YELLOW));
    }

    const auto ip6 = runCommand("tailscale ip -6 2>/dev/null");
    if (ip6.exit_code == 0 && !trim(ip6.output).empty()) {
        printKeyValue("Tailscale IPv6", trim(ip6.output));
    } else {
        printKeyValue("Tailscale IPv6", colorize("N/A", ansi::YELLOW));
    }

    const auto netcheck = runCommand("tailscale netcheck 2>/dev/null | head -n 12");
    if (netcheck.exit_code == 0 && !trim(netcheck.output).empty()) {
        printSubHeader("Tailscale Netcheck");
        printBlockLines(netcheck.output);
    } else {
        printKeyValue("Tailscale Netcheck", colorize("UNAVAILABLE", ansi::YELLOW));
    }
}

void printInternetSection() {
    printSectionHeader("INTERNET");

    printSubHeader("Ping Stress Check (3 probes each)");
    const std::vector<std::string> ping_hosts = {"1.1.1.1",        "8.8.8.8",    "youtube.com",
                                                 "codeforces.com", "github.com", "quanquanque.dev",
                                                 "atcoder.jp"};
    for (const auto &host : ping_hosts) {
        const auto ping = checkPing(host, 3);
        printKeyValue("Ping " + host, stateLabel(ping.state) + " - " + ping.detail);
    }

    const SimpleCheck dns = checkDns();
    printKeyValue("DNS (example.com)", stateLabel(dns.state) + " - " + dns.detail);

    const SimpleCheck http = checkHttpLatency();
    printKeyValue("HTTP (https://example.com)", stateLabel(http.state) + " - " + http.detail);

    printTailscaleInternetInfo();
}

struct DiskUsage {
    bool valid = false;
    unsigned long long total = 0;
    unsigned long long used = 0;
    unsigned long long free = 0;
};

DiskUsage getRootDiskUsage() {
    DiskUsage usage;
    struct statvfs stats{};
    if (::statvfs("/", &stats) != 0) {
        return usage;
    }

    const unsigned long long block_size = static_cast<unsigned long long>(stats.f_frsize);
    const unsigned long long total = static_cast<unsigned long long>(stats.f_blocks) * block_size;
    const unsigned long long free = static_cast<unsigned long long>(stats.f_bavail) * block_size;
    const unsigned long long used = total > free ? total - free : 0;

    usage.valid = true;
    usage.total = total;
    usage.used = used;
    usage.free = free;
    return usage;
}

std::optional<double> runSingleWriteBenchmark(std::size_t size_mb, int run_id) {
    const std::string file_path =
        "/tmp/check_bench_" + std::to_string(::getpid()) + "_" + std::to_string(run_id) + ".dat";
    const int fd = ::open(file_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        return std::nullopt;
    }

    constexpr std::size_t chunk_size = 1024 * 1024;
    std::vector<char> buffer(chunk_size, 0);
    const unsigned long long total_bytes =
        static_cast<unsigned long long>(size_mb) * 1024ULL * 1024ULL;

    unsigned long long written = 0;
    const auto start = std::chrono::steady_clock::now();
    bool ok = true;

    while (written < total_bytes) {
        const std::size_t to_write = static_cast<std::size_t>(
            std::min<unsigned long long>(chunk_size, total_bytes - written));
        const ssize_t rc = ::write(fd, buffer.data(), to_write);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (rc == 0) {
            ok = false;
            break;
        }
        written += static_cast<unsigned long long>(rc);
    }

    if (ok && ::fsync(fd) != 0) {
        ok = false;
    }
    ::close(fd);
    ::unlink(file_path.c_str());

    if (!ok) {
        return std::nullopt;
    }

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    if (seconds <= 0.0) {
        return std::nullopt;
    }

    const double mbps = static_cast<double>(total_bytes) / (1024.0 * 1024.0) / seconds;
    return mbps;
}

struct ThermalInfo {
    std::string zone;
    std::string type;
    std::optional<double> temp_c;
};

std::vector<ThermalInfo> collectThermals() {
    std::vector<ThermalInfo> values;
    const auto entries = listDirectory("/sys/class/thermal");
    for (const auto &entry : entries) {
        const std::string name = entry.filename().string();
        if (!startsWith(name, "thermal_zone")) {
            continue;
        }

        ThermalInfo info;
        info.zone = name;
        info.type = readFirstLine((entry / "type").string()).value_or("N/A");

        const auto temp_raw = readLongFromFile((entry / "temp").string());
        if (temp_raw) {
            const double value = static_cast<double>(*temp_raw);
            info.temp_c = (std::abs(value) >= 1000.0) ? (value / 1000.0) : value;
        }
        values.push_back(info);
    }
    return values;
}

void printPowerSupplySummary() {
    printSubHeader("Power Supply Devices");
    const auto entries = listDirectory("/sys/class/power_supply");
    if (entries.empty()) {
        std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        return;
    }

    bool printed = false;
    for (const auto &entry : entries) {
        const std::string name = entry.filename().string();
        const std::string type = readFirstLine((entry / "type").string()).value_or("N/A");
        const std::string status = readFirstLine((entry / "status").string()).value_or("N/A");
        const auto online = readLongFromFile((entry / "online").string());
        const auto capacity = readLongFromFile((entry / "capacity").string());

        std::ostringstream line;
        line << name << " (" << type << ")";
        if (status != "N/A") {
            line << " status=" << status;
        }
        if (online) {
            line << " online=" << *online;
        }
        if (capacity) {
            line << " capacity=" << *capacity << "%";
        }
        std::cout << "    " << line.str() << "\n";
        printed = true;
    }

    if (!printed) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
    }
}

void printNetworkInterfacesFromSysfs() {
    printSubHeader("Network Interfaces");
    const auto entries = listDirectory("/sys/class/net");
    if (entries.empty()) {
        std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        return;
    }

    for (const auto &entry : entries) {
        const std::string name = entry.filename().string();
        const std::string state = readFirstLine((entry / "operstate").string()).value_or("N/A");
        const std::string mac = readFirstLine((entry / "address").string()).value_or("N/A");
        std::cout << "    " << name << " state=" << state << " mac=" << mac << "\n";
    }
}

void printImportantHealthSection() {
    printSectionHeader("OTHER IMPORTANT INFO");

    const DiskUsage disk = getRootDiskUsage();
    if (disk.valid) {
        printKeyValue("Root Disk Total", formatBytes(static_cast<long double>(disk.total)));
        printKeyValue("Root Disk Used", formatBytes(static_cast<long double>(disk.used)));
        printKeyValue("Root Disk Free", formatBytes(static_cast<long double>(disk.free)));
    } else {
        printKeyValue("Root Disk Usage", colorize("UNAVAILABLE", ansi::YELLOW));
    }

    printSubHeader("Disk Write Micro-Benchmark (128MB)");
    std::vector<double> benchmark_results;
    for (int i = 1; i <= 2; ++i) {
        const auto run = runSingleWriteBenchmark(128, i);
        if (run) {
            benchmark_results.push_back(*run);
            std::ostringstream label;
            label << "Run " << i;
            std::ostringstream value;
            value << std::fixed << std::setprecision(2) << *run << " MB/s";
            printKeyValue(label.str(), value.str());
        } else {
            std::ostringstream label;
            label << "Run " << i;
            printKeyValue(label.str(), colorize("UNAVAILABLE", ansi::YELLOW));
        }
    }

    if (!benchmark_results.empty()) {
        const double sum = std::accumulate(benchmark_results.begin(), benchmark_results.end(), 0.0);
        const double avg = sum / static_cast<double>(benchmark_results.size());
        std::ostringstream value;
        value << std::fixed << std::setprecision(2) << avg << " MB/s";
        printKeyValue("Average", value.str());
    } else {
        printKeyValue("Average", colorize("UNAVAILABLE", ansi::YELLOW));
    }

    printSubHeader("Thermal Zones");
    const auto thermals = collectThermals();
    if (thermals.empty()) {
        std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
    } else {
        for (const auto &thermal : thermals) {
            if (thermal.temp_c) {
                std::ostringstream temp;
                temp << std::fixed << std::setprecision(1) << *thermal.temp_c << " C";
                std::cout << "    " << thermal.zone << " (" << thermal.type << ") = " << temp.str()
                          << "\n";
            } else {
                std::cout << "    " << thermal.zone << " (" << thermal.type
                          << ") = " << colorize("N/A", ansi::YELLOW) << "\n";
            }
        }
    }

    printSubHeader("USB Devices");
    if (commandExists("lsusb")) {
        const auto usb = runCommand("lsusb 2>/dev/null");
        if (usb.exit_code == 0 && !trim(usb.output).empty()) {
            printBlockLines(usb.output);
        } else {
            std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        }
    } else {
        std::cout << "    " << colorize("UNAVAILABLE: lsusb command not found", ansi::YELLOW)
                  << "\n";
    }

    printSubHeader("Storage Devices");
    if (commandExists("lsblk")) {
        const auto blk = runCommand("lsblk -o NAME,TYPE,SIZE,MODEL,TRAN,MOUNTPOINT 2>/dev/null");
        if (blk.exit_code == 0 && !trim(blk.output).empty()) {
            printBlockLines(blk.output);
        } else {
            std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        }
    } else {
        std::cout << "    " << colorize("UNAVAILABLE: lsblk command not found", ansi::YELLOW)
                  << "\n";
    }

    printPowerSupplySummary();
    printNetworkInterfacesFromSysfs();
}

std::string getOsPrettyName() {
    std::ifstream input("/etc/os-release");
    if (!input) {
        return "N/A";
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!startsWith(line, "PRETTY_NAME=")) {
            continue;
        }
        std::string value = line.substr(std::strlen("PRETTY_NAME="));
        value = trim(value);
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return "N/A";
}

std::string getHostname() {
    char host[256] = {0};
    if (::gethostname(host, sizeof(host)) != 0) {
        return "N/A";
    }
    host[sizeof(host) - 1] = '\0';
    return host;
}

std::string getCurrentUser() {
    const char *env_user = std::getenv("USER");
    if (env_user != nullptr && std::strlen(env_user) > 0) {
        return env_user;
    }
    const passwd *pw = ::getpwuid(::getuid());
    if (pw == nullptr || pw->pw_name == nullptr) {
        return "N/A";
    }
    return pw->pw_name;
}

std::string getUserCount() {
    std::ifstream passwd_file("/etc/passwd");
    if (!passwd_file) {
        return colorize("N/A", ansi::YELLOW);
    }

    std::size_t count = 0;
    std::string line;
    while (std::getline(passwd_file, line)) {
        if (!trim(line).empty()) {
            ++count;
        }
    }
    return std::to_string(count);
}

std::string getLocalTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local_tm{};
    if (::localtime_r(&now, &local_tm) == nullptr) {
        return "N/A";
    }
    char buffer[128] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &local_tm) == 0) {
        return "N/A";
    }
    return buffer;
}

std::string detectVirtualization() {
    if (commandExists("systemd-detect-virt")) {
        const auto result = runCommand("systemd-detect-virt 2>/dev/null");
        const std::string value = trim(result.output);
        if (result.exit_code == 0 && !value.empty() && value != "none") {
            return value;
        }
    }

    const std::string cgroup = toLower(readFile("/proc/1/cgroup").value_or(""));
    if (cgroup.find("docker") != std::string::npos) {
        return "docker";
    }
    if (cgroup.find("lxc") != std::string::npos) {
        return "lxc";
    }

    if (fs::exists("/proc/xen")) {
        return "xen";
    }
    const auto hypervisor = readFirstLine("/sys/hypervisor/type");
    if (hypervisor && !hypervisor->empty()) {
        return *hypervisor;
    }

    const std::string vendor = toLower(readFirstLine("/sys/class/dmi/id/sys_vendor").value_or(""));
    const std::string product =
        toLower(readFirstLine("/sys/class/dmi/id/product_name").value_or(""));
    const std::string combined = vendor + " " + product;
    if (combined.find("kvm") != std::string::npos || combined.find("qemu") != std::string::npos) {
        return "kvm";
    }
    if (combined.find("vmware") != std::string::npos) {
        return "vmware";
    }
    if (combined.find("virtualbox") != std::string::npos) {
        return "virtualbox";
    }
    if (combined.find("hyper-v") != std::string::npos ||
        combined.find("microsoft") != std::string::npos) {
        return "hyper-v";
    }

    return "dedicated/none detected";
}

std::pair<std::string, std::string> getDefaultRouteAndInterface() {
    if (!commandExists("ip")) {
        return {"UNAVAILABLE", "UNAVAILABLE"};
    }

    const auto result = runCommand("ip route show default 2>/dev/null");
    const auto lines = splitLines(result.output);
    if (result.exit_code != 0 || lines.empty()) {
        return {"N/A", "N/A"};
    }

    const std::string route = lines.front();
    std::string iface = "N/A";
    const std::string token = " dev ";
    const auto pos = route.find(token);
    if (pos != std::string::npos) {
        const auto start = pos + token.size();
        auto end = route.find(' ', start);
        if (end == std::string::npos) {
            end = route.size();
        }
        iface = route.substr(start, end - start);
    }
    return {route, iface};
}

struct PackageInventory {
    std::string manager = "N/A";
    std::vector<std::string> packages;
};

PackageInventory collectInstalledPackages() {
    PackageInventory inventory;

    struct PackageCommand {
        std::string manager;
        std::string executable;
        std::string command;
    };

    const std::vector<PackageCommand> commands = {
        {"dpkg", "dpkg-query", R"(dpkg-query -W -f='${binary:Package}\n' 2>/dev/null)"},
        {"rpm", "rpm", "rpm -qa 2>/dev/null"},
        {"pacman", "pacman", "pacman -Qq 2>/dev/null"},
        {"apk", "apk", "apk info 2>/dev/null"},
    };

    for (const auto &candidate : commands) {
        if (!commandExists(candidate.executable)) {
            continue;
        }
        const auto result = runCommand(candidate.command);
        if (result.exit_code != 0 || trim(result.output).empty()) {
            continue;
        }

        inventory.manager = candidate.manager;
        const auto lines = splitLines(result.output);
        for (const auto &line : lines) {
            if (!line.empty()) {
                inventory.packages.push_back(line);
            }
        }
        if (!inventory.packages.empty()) {
            return inventory;
        }
    }

    return inventory;
}

void printMachineDumpSection() {
    printSectionHeader("INFO DUMP");

    printKeyValue("OS", getOsPrettyName());

    struct utsname uname_data{};
    if (::uname(&uname_data) == 0) {
        printKeyValue("Kernel", uname_data.release);
        printKeyValue("Architecture", uname_data.machine);
    } else {
        printKeyValue("Kernel", colorize("N/A", ansi::YELLOW));
        printKeyValue("Architecture", colorize("N/A", ansi::YELLOW));
    }

    printKeyValue("Hostname", getHostname());
    printKeyValue("Current User", getCurrentUser());
    printKeyValue("User Count", getUserCount());
    printKeyValue("Timestamp", getLocalTimestamp());
    printKeyValue("Virtualization", detectVirtualization());

    const auto [route, iface] = getDefaultRouteAndInterface();
    printKeyValue("Default Interface", iface);
    printKeyValue("Default Route", route);

    printSubHeader("Installed Packages");
    const auto packages = collectInstalledPackages();
    if (packages.manager == "N/A" || packages.packages.empty()) {
        printKeyValue("Package Inventory", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printKeyValue("Package Manager", packages.manager);
        printKeyValue("Package Count", std::to_string(packages.packages.size()));
    }
}

int main() {
    printBatterySection();
    printRuntimeSection();
    printInternetSection();
    printImportantHealthSection();
    printMachineDumpSection();

    std::cout << "\n" << colorize("Report complete.", ansi::GREEN) << "\n";
    return 0;
}
