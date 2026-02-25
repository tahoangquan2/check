#pragma once

#include "utils.hpp"

inline SimpleCheck checkPing(const std::string& host, int probe_count = 2) {
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
    for (const auto& line : lines) {
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

inline SimpleCheck checkDns() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
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

inline SimpleCheck checkHttpLatency() {
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

inline void printTailscaleInternetInfo() {
    printSubHeader("Tailscale");

    if (fs::exists("/sys/class/net/tailscale0")) {
        const std::string state =
            readFirstLine("/sys/class/net/tailscale0/operstate").value_or("N/A");
        const std::string mac = readFirstLine("/sys/class/net/tailscale0/address").value_or("N/A");
        printKeyValue("  tailscale0 Interface", "present (state=" + state + ", mac=" + mac + ")");
    } else {
        printKeyValue("  tailscale0 Interface", colorize("not found", ansi::YELLOW));
    }

    if (!commandExists("tailscale")) {
        printKeyValue("  tailscale CLI", colorize("UNAVAILABLE", ansi::YELLOW));
        return;
    }

    printKeyValue("  tailscale CLI", colorize("available", ansi::GREEN));

    const auto ip4 = runCommand("tailscale ip -4 2>/dev/null");
    if (ip4.exit_code == 0 && !trim(ip4.output).empty()) {
        printKeyValue("  Tailscale IPv4", trim(ip4.output));
    } else {
        printKeyValue("  Tailscale IPv4", colorize("N/A", ansi::YELLOW));
    }

    const auto ip6 = runCommand("tailscale ip -6 2>/dev/null");
    if (ip6.exit_code == 0 && !trim(ip6.output).empty()) {
        printKeyValue("  Tailscale IPv6", trim(ip6.output));
    } else {
        printKeyValue("  Tailscale IPv6", colorize("N/A", ansi::YELLOW));
    }

    const auto netcheck = runCommand("tailscale netcheck 2>/dev/null | head -n 12");
    if (netcheck.exit_code == 0 && !trim(netcheck.output).empty()) {
        printSubHeader("Tailscale Netcheck");
        printBlockLines(netcheck.output);
    } else {
        printKeyValue("Tailscale Netcheck", colorize("UNAVAILABLE", ansi::YELLOW));
    }
}

inline void printInternetSection(const std::vector<ProcessUsage>& top_net) {
    printSectionHeader("INTERNET");

    printSubHeader("Ping Stress Check (2 probes each)");
    const std::vector<std::string> ping_hosts = {"1.1.1.1",        "8.8.8.8",    "youtube.com",
                                                 "codeforces.com", "github.com", "hquan.dev",
                                                 "atcoder.jp"};
    for (const auto& host : ping_hosts) {
        const auto ping = checkPing(host, 3);
        printKeyValue("  Ping " + host, stateLabel(ping.state) + " - " + ping.detail);
    }

    printSubHeader("DNS & HTTP");
    const SimpleCheck dns = checkDns();
    printKeyValue("  DNS", stateLabel(dns.state) + " - " + dns.detail);

    const SimpleCheck http = checkHttpLatency();
    printKeyValue("  HTTP", stateLabel(http.state) + " - " + http.detail);

    printTailscaleInternetInfo();

    if (top_net.empty()) {
        printKeyValue("Top Network Processes", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printSubHeader("Top 10 Processes (by Network Sockets)");
        printTopProcessTable(top_net);
    }
}
