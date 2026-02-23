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

inline std::string getOsPrettyName() {
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

inline std::string getHostname() {
    char host[256] = {0};
    if (::gethostname(host, sizeof(host)) != 0) {
        return "N/A";
    }
    host[sizeof(host) - 1] = '\0';
    return host;
}

inline std::string getCurrentUser() {
    const char* env_user = std::getenv("USER");
    if (env_user != nullptr && std::strlen(env_user) > 0) {
        return env_user;
    }
    const passwd* pw = ::getpwuid(::getuid());
    if (pw == nullptr || pw->pw_name == nullptr) {
        return "N/A";
    }
    return pw->pw_name;
}

inline std::string getUserCount() {
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

inline std::string getLocalTimestamp() {
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

inline std::string detectVirtualization() {
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

inline std::pair<std::string, std::string> getDefaultRouteAndInterface() {
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

inline PackageInventory collectInstalledPackages() {
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

    for (const auto& candidate : commands) {
        if (!commandExists(candidate.executable)) {
            continue;
        }
        const auto result = runCommand(candidate.command);
        if (result.exit_code != 0 || trim(result.output).empty()) {
            continue;
        }

        inventory.manager = candidate.manager;
        const auto lines = splitLines(result.output);
        for (const auto& line : lines) {
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

inline void printMachineDumpSection() {
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

    const auto uptime = readUptimeSeconds();
    printKeyValue("Uptime", uptime ? formatUptime(*uptime) : colorize("N/A", ansi::YELLOW));

    const auto [processes, threads] = countProcessesAndThreads();
    printKeyValue("Process Count", std::to_string(processes));
    printKeyValue("Thread Count", std::to_string(threads));

    printKeyValue("Virtualization", detectVirtualization());

    const auto [route, iface] = getDefaultRouteAndInterface();
    printKeyValue("Default Interface", iface);
    printKeyValue("Default Route", route);

    const auto packages = collectInstalledPackages();
    if (packages.manager == "N/A" || packages.packages.empty()) {
        printKeyValue("Package Inventory", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printKeyValue("Package Manager", packages.manager);
        printKeyValue("Package Count", std::to_string(packages.packages.size()));
    }
}
