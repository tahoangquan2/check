#pragma once

#include "command.hpp"
#include "process.hpp"
#include "utils.hpp"

inline bool appendJsonCodepoint(std::string& output, unsigned int codepoint) {
    if (codepoint <= 0x7fU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

inline int jsonHexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

inline bool parseJsonString(const std::string& json, std::size_t& position, std::string& output) {
    if (position >= json.size() || json[position] != '"') return false;
    ++position;
    while (position < json.size()) {
        const unsigned char value = static_cast<unsigned char>(json[position++]);
        if (value == '"') return true;
        if (value < 0x20U) return false;
        if (value != '\\') {
            output.push_back(static_cast<char>(value));
            continue;
        }
        if (position >= json.size()) return false;
        const char escape = json[position++];
        if (escape == '"' || escape == '\\' || escape == '/')
            output.push_back(escape);
        else if (escape == 'b')
            output.push_back('\b');
        else if (escape == 'f')
            output.push_back('\f');
        else if (escape == 'n')
            output.push_back('\n');
        else if (escape == 'r')
            output.push_back('\r');
        else if (escape == 't')
            output.push_back('\t');
        else if (escape == 'u') {
            if (position + 4 > json.size()) return false;
            unsigned int codepoint = 0;
            for (int index = 0; index < 4; ++index) {
                const int digit = jsonHexDigit(json[position++]);
                if (digit < 0) return false;
                codepoint = codepoint * 16U + static_cast<unsigned int>(digit);
            }
            if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                if (position + 6 > json.size() || json[position] != '\\' ||
                    json[position + 1] != 'u') {
                    return false;
                }
                position += 2;
                unsigned int low = 0;
                for (int index = 0; index < 4; ++index) {
                    const int digit = jsonHexDigit(json[position++]);
                    if (digit < 0) return false;
                    low = low * 16U + static_cast<unsigned int>(digit);
                }
                if (low < 0xdc00U || low > 0xdfffU) return false;
                codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
            } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                return false;
            }
            if (!appendJsonCodepoint(output, codepoint)) return false;
        } else {
            return false;
        }
    }
    return false;
}

inline std::optional<std::string> extractJsonField(const std::string& json,
                                                   const std::string& key) {
    std::size_t position = 0;
    while (position < json.size()) {
        if (json[position] != '"') {
            ++position;
            continue;
        }
        std::string candidate;
        if (!parseJsonString(json, position, candidate)) return std::nullopt;
        std::size_t separator = position;
        while (separator < json.size() &&
               std::isspace(static_cast<unsigned char>(json[separator])) != 0) {
            ++separator;
        }
        if (candidate != key || separator >= json.size() || json[separator] != ':') continue;
        position = separator + 1;
        while (position < json.size() &&
               std::isspace(static_cast<unsigned char>(json[position])) != 0) {
            ++position;
        }
        std::string value;
        if (!parseJsonString(json, position, value)) return std::nullopt;
        return value;
    }
    return std::nullopt;
}

#ifdef _WIN32
inline std::string getWindowsTailscaleInterfaceSummary() {
    ULONG size = 0;
    DWORD rc = GetAdaptersInfo(nullptr, &size);
    if (rc != ERROR_BUFFER_OVERFLOW) {
        return colorize("adapter lookup failed", ansi::YELLOW);
    }

    std::vector<unsigned char> buffer(size);
    auto* addresses = reinterpret_cast<IP_ADAPTER_INFO*>(buffer.data());
    rc = GetAdaptersInfo(addresses, &size);
    if (rc != NO_ERROR) {
        return colorize("adapter lookup failed", ansi::YELLOW);
    }

    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        std::string desc = adapter->Description;
        std::string lowered = toLower(desc + " " + adapter->AdapterName);
        if (lowered.find("tailscale") == std::string::npos) {
            continue;
        }

        std::ostringstream mac;
        for (ULONG i = 0; i < adapter->AddressLength; ++i) {
            if (i > 0) {
                mac << ":";
            }
            mac << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(adapter->Address[i]) << std::dec;
        }
        std::string mac_text = mac.str().empty() ? "virtual adapter" : mac.str();
        return "present (name=" + (desc.empty() ? adapter->AdapterName : desc) +
               ", mac=" + mac_text + ")";
    }
    return colorize("not found", ansi::YELLOW);
}
#endif

inline std::optional<std::string> parsePingAverage(const std::string& output) {
    const std::vector<std::string> lines = splitLines(output);
    for (const std::string& line : lines) {
        const std::size_t average = line.find("Average =");
        if (average != std::string::npos) return trim(line.substr(average + 9)) + " avg";
        if (line.find("min/avg/max") == std::string::npos) continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string values = trim(line.substr(equals + 1));
        const std::size_t space = values.find(' ');
        if (space != std::string::npos) values.resize(space);
        std::istringstream parser(values);
        std::string part;
        if (!std::getline(parser, part, '/')) continue;
        if (std::getline(parser, part, '/')) return part + " ms avg";
    }
    return std::nullopt;
}

inline SimpleCheck checkPing(const std::string& host, int probe_count = 1) {
    if (!commandExists("ping")) {
        return {CheckState::Unavailable, "ping command not found"};
    }

    const int probes = std::max(1, probe_count);
#ifdef _WIN32
    const CommandResult result =
        runCommand({"ping", "-n", std::to_string(probes), "-w", "2000", host},
                   {std::chrono::milliseconds(3000), 64 * 1024});
#else
    const CommandResult result = runCommand({"ping", "-c", std::to_string(probes), "-W", "2", host},
                                            {std::chrono::milliseconds(3000), 64 * 1024});
#endif
    if (result.failure == CommandFailure::TimedOut) return {CheckState::Fail, "timed out (3s)"};
    if (!result.ok()) return {CheckState::Fail, "host unreachable"};
    return {CheckState::Pass, parsePingAverage(result.output).value_or("reachable")};
}

inline SimpleCheck checkDns(const std::string& host) {
#ifdef _WIN32
    if (!commandExists("nslookup")) return {CheckState::Unavailable, "nslookup not found"};
    const CommandResult result =
        runCommand({"nslookup", host}, {std::chrono::milliseconds(3000), 64 * 1024});
#else
    if (!commandExists("getent")) return {CheckState::Unavailable, "getent not found"};
    const CommandResult result =
        runCommand({"getent", "ahosts", host}, {std::chrono::milliseconds(3000), 64 * 1024});
#endif
    if (result.failure == CommandFailure::TimedOut) return {CheckState::Fail, "timed out (3s)"};
    return result.ok() && !trim(result.output).empty()
               ? SimpleCheck{CheckState::Pass, "resolved"}
               : SimpleCheck{CheckState::Fail, "resolution failed"};
}

inline SimpleCheck checkHttpLatency(const std::string& url) {
    if (!commandExists("curl")) {
        return {CheckState::Unavailable, "curl command not found"};
    }

    const CommandResult result =
        runCommand({"curl", "--silent", "--output",
#ifdef _WIN32
                    "NUL",
#else
                    "/dev/null",
#endif
                    "--write-out", "%{time_total}", "--max-time", "5", url},
                   {std::chrono::milliseconds(6000), 64 * 1024});
    if (!result.ok()) {
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

inline void printTailscaleInternetInfo() {
    printSubHeader("Tailscale");

#ifdef _WIN32
    printKeyValue("  Tailscale Adapter", getWindowsTailscaleInterfaceSummary());
#else
    if (pathExists("/sys/class/net/tailscale0")) {
        const std::string state =
            readFirstLine("/sys/class/net/tailscale0/operstate").value_or("N/A");
        const std::string mac = readFirstLine("/sys/class/net/tailscale0/address").value_or("N/A");
        printKeyValue("  tailscale0 Interface", "present (state=" + state + ", mac=" + mac + ")");
    } else {
        printKeyValue("  tailscale0 Interface", colorize("not found", ansi::YELLOW));
    }
#endif

    if (!commandExists("tailscale")) {
        printKeyValue("  Tailscale CLI", colorize("not installed", ansi::YELLOW));
        return;
    }

    printKeyValue("  Tailscale CLI", colorize("available", ansi::GREEN));
}

inline void printInternetSection(const std::vector<ProcessUsage>& top_net, bool run_network,
                                 const std::string& host, const std::string& url) {
    printSectionHeader("INTERNET");

    if (run_network) {
        printSubHeader("Active Network Checks (one endpoint, bounded)");
        std::cerr << "[network] ping " << sanitizeTerminalText(host) << " (3s budget)...\n";
        auto started = std::chrono::steady_clock::now();
        const SimpleCheck ping = checkPing(host);
        double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::ostringstream ping_detail;
        ping_detail << stateLabel(ping.state) << " - " << ping.detail << " (" << std::fixed
                    << std::setprecision(2) << elapsed << " s)";
        printKeyValue("  Ping " + host, ping_detail.str());

        std::cerr << "[network] dns " << sanitizeTerminalText(host) << " (3s budget)...\n";
        started = std::chrono::steady_clock::now();
        const SimpleCheck dns = checkDns(host);
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::ostringstream dns_detail;
        dns_detail << stateLabel(dns.state) << " - " << dns.detail << " (" << std::fixed
                   << std::setprecision(2) << elapsed << " s)";
        printKeyValue("  DNS", dns_detail.str());

        std::cerr << "[network] http " << sanitizeTerminalText(url) << " (6s budget)...\n";
        started = std::chrono::steady_clock::now();
        const SimpleCheck http = checkHttpLatency(url);
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::ostringstream http_detail;
        http_detail << stateLabel(http.state) << " - " << http.detail << " (" << std::fixed
                    << std::setprecision(2) << elapsed << " s)";
        printKeyValue("  HTTP", http_detail.str());

    } else {
        printKeyValue("Active Network Checks", "skipped (use --network)");
    }

    printTailscaleInternetInfo();

    if (top_net.empty()) {
        printKeyValue("Top Network Processes",
                      colorize("process telemetry not exposed", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by Network Sockets)");
        printTopProcessTable(top_net);
    }
}
