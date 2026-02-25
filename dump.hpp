#pragma once

#include <array>
#include <vector>

#ifdef _WIN32
#include <intrin.h>
#include <winreg.h>
#endif

#include "utils.hpp"

inline std::optional<double> readUptimeSeconds() {
#ifdef _WIN32
    return GetTickCount64() / 1000.0;
#else
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
#endif
}

inline std::pair<int, long long> countProcessesAndThreads() {
    int process_count = 0;
    long long thread_count = 0;
#ifdef _WIN32
    HANDLE process_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (process_snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Process32First(process_snap, &entry)) {
            do {
                ++process_count;
            } while (Process32Next(process_snap, &entry));
        }
        CloseHandle(process_snap);
    }

    HANDLE thread_snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (thread_snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(thread_snap, &entry)) {
            do {
                ++thread_count;
            } while (Thread32Next(thread_snap, &entry));
        }
        CloseHandle(thread_snap);
    }
#else
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
#endif

    return {process_count, thread_count};
}

inline std::string getOsPrettyName() {
#ifdef _WIN32
    const auto result = runCommand(
        "powershell -NoProfile -Command "
        "\"$os=Get-CimInstance Win32_OperatingSystem; if($os){$os.Caption + ' ' + $os.Version}\" "
        "2>nul");
    if (result.exit_code == 0) {
        const std::string value = trim(result.output);
        if (!value.empty()) {
            return value;
        }
    }
    return "Windows";
#else
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
#endif
}

inline std::string getHostname() {
    char host[256] = {0};
#ifdef _WIN32
    DWORD size = sizeof(host);
    if (GetComputerNameA(host, &size)) {
        return host;
    }
#else
    if (::gethostname(host, sizeof(host)) == 0) {
        host[sizeof(host) - 1] = '\0';
        return host;
    }
#endif
    return "N/A";
}

inline std::string getCurrentUser() {
#ifdef _WIN32
    char user[256] = {0};
    DWORD size = sizeof(user);
    if (GetUserNameA(user, &size)) {
        return user;
    }
    return "N/A";
#else
    const char* env_user = std::getenv("USER");
    if (env_user != nullptr && std::strlen(env_user) > 0) {
        return env_user;
    }
    const passwd* pw = ::getpwuid(::getuid());
    if (pw == nullptr || pw->pw_name == nullptr) {
        return "N/A";
    }
    return pw->pw_name;
#endif
}

inline std::string getUserCount() {
#ifdef _WIN32
    return "1";
#else
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
#endif
}

inline std::string getLocalTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local_tm{};
#ifdef _WIN32
    std::tm* tm_ptr = std::localtime(&now);
    if (tm_ptr == nullptr) {
        return "N/A";
    }
    local_tm = *tm_ptr;
#else
    if (::localtime_r(&now, &local_tm) == nullptr) {
        return "N/A";
    }
#endif
    char buffer[128] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &local_tm) == 0) {
        return "N/A";
    }
    return buffer;
}

#ifdef _WIN32
inline std::string getWindowsKernelVersion() {
    using RtlGetVersionPtr = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == nullptr) {
        return "Windows kernel info not exposed";
    }

    FARPROC proc = GetProcAddress(ntdll, "RtlGetVersion");
    if (proc == nullptr) {
        return "Windows kernel info not exposed";
    }

    RtlGetVersionPtr rtl_get_version = nullptr;
    static_assert(sizeof(proc) == sizeof(rtl_get_version),
                  "Function pointer size mismatch on this platform");
    std::memcpy(&rtl_get_version, &proc, sizeof(proc));

    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtl_get_version(&info) != 0) {
        return "Windows kernel info not exposed";
    }

    std::ostringstream out;
    out << info.dwMajorVersion << "." << info.dwMinorVersion << "." << info.dwBuildNumber;
    return out.str();
}

inline std::string getWindowsArchitecture() {
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return "x64";
        case PROCESSOR_ARCHITECTURE_INTEL:
            return "x86";
        case PROCESSOR_ARCHITECTURE_ARM64:
            return "ARM64";
        case PROCESSOR_ARCHITECTURE_ARM:
            return "ARM";
        default:
            return "Unknown architecture";
    }
}
#endif

inline std::string detectVirtualization() {
#ifdef _WIN32
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 1);
    const bool has_hypervisor = (regs[2] & (1 << 31)) != 0;
    if (!has_hypervisor) {
        return "none detected";
    }

    int hv_regs[4] = {0, 0, 0, 0};
    __cpuid(hv_regs, 0x40000000);
    char hv_vendor[13] = {0};
    std::memcpy(hv_vendor + 0, &hv_regs[1], 4);
    std::memcpy(hv_vendor + 4, &hv_regs[2], 4);
    std::memcpy(hv_vendor + 8, &hv_regs[3], 4);
    const std::string vendor = toLower(std::string(hv_vendor));

    if (vendor.find("microsoft") != std::string::npos ||
        vendor.find("hyper-v") != std::string::npos ||
        vendor.find("hvvendor") != std::string::npos) {
        return "hyper-v";
    }
    if (vendor.find("kvm") != std::string::npos) {
        return "kvm";
    }
    if (vendor.find("vmware") != std::string::npos) {
        return "vmware";
    }
    if (vendor.find("vbox") != std::string::npos) {
        return "virtualbox";
    }
    if (vendor.find("xen") != std::string::npos) {
        return "xen";
    }

    const auto fallback = runCommand(
        "powershell -NoProfile -Command "
        "\"$cs=Get-CimInstance Win32_ComputerSystem; if($cs){$cs.HypervisorPresent}\" 2>nul");
    const std::string value = toLower(trim(fallback.output));
    if (value == "true") {
        return "present (vendor=" + std::string(hv_vendor) + ")";
    }

    return "present (vendor unknown)";
#else
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
#endif
}

inline std::pair<std::string, std::string> getDefaultRouteAndInterface() {
#ifdef _WIN32
    ULONG table_size = 0;
    DWORD rc = GetIpForwardTable(nullptr, &table_size, FALSE);
    if (rc != ERROR_INSUFFICIENT_BUFFER) {
        return {"default route lookup failed", "interface lookup failed"};
    }

    std::vector<unsigned char> buffer(table_size);
    auto* table = reinterpret_cast<MIB_IPFORWARDTABLE*>(buffer.data());
    rc = GetIpForwardTable(table, &table_size, FALSE);
    if (rc != NO_ERROR || table->dwNumEntries == 0) {
        return {"default route not found", "interface not found"};
    }

    const MIB_IPFORWARDROW* best = nullptr;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (row.dwForwardDest != 0 || row.dwForwardMask != 0) {
            continue;
        }
        if (best == nullptr || row.dwForwardMetric1 < best->dwForwardMetric1) {
            best = &row;
        }
    }

    if (best == nullptr) {
        return {"default route not found", "interface not found"};
    }

    in_addr gateway{};
    gateway.S_un.S_addr = best->dwForwardNextHop;
    char gateway_text[INET_ADDRSTRLEN] = {0};
    if (InetNtopA(AF_INET, &gateway, gateway_text, sizeof(gateway_text)) == nullptr) {
        std::strncpy(gateway_text, "unknown", sizeof(gateway_text) - 1);
    }

    std::string iface = "ifIndex=" + std::to_string(best->dwForwardIfIndex);
    MIB_IFROW if_row{};
    if_row.dwIndex = best->dwForwardIfIndex;
    if (GetIfEntry(&if_row) == NO_ERROR) {
        if (if_row.dwDescrLen > 0) {
            iface.assign(reinterpret_cast<const char*>(if_row.bDescr),
                         reinterpret_cast<const char*>(if_row.bDescr) + if_row.dwDescrLen);
            while (!iface.empty() && iface.back() == '\0') {
                iface.pop_back();
            }
        } else {
            std::wstring wide_name(if_row.wszName);
            const std::string parsed = wideToUtf8(wide_name);
            if (!parsed.empty()) {
                iface = parsed;
            }
        }
    }

    std::ostringstream route;
    route << "0.0.0.0/0 via " << gateway_text << " metric " << best->dwForwardMetric1;
    return {route.str(), iface};
#else
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
#endif
}

struct PackageInventory {
    std::string manager = "N/A";
    std::vector<std::string> packages;
};

inline PackageInventory collectInstalledPackages() {
    PackageInventory inventory;
#ifdef _WIN32
    inventory.manager = "registry";

    std::vector<std::pair<HKEY, std::string>> roots = {
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_LOCAL_MACHINE,
         "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
    };

    std::unordered_map<std::string, bool> dedup;
    for (const auto& [root, path] : roots) {
        HKEY key = nullptr;
        if (RegOpenKeyExA(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
            continue;
        }

        DWORD index = 0;
        while (true) {
            char subkey_name[256] = {0};
            DWORD subkey_size = static_cast<DWORD>(sizeof(subkey_name));
            const LONG enum_rc = RegEnumKeyExA(key, index, subkey_name, &subkey_size, nullptr,
                                               nullptr, nullptr, nullptr);
            if (enum_rc == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (enum_rc != ERROR_SUCCESS) {
                ++index;
                continue;
            }

            HKEY app_key = nullptr;
            if (RegOpenKeyExA(key, subkey_name, 0, KEY_READ, &app_key) == ERROR_SUCCESS) {
                char display_name[2048] = {0};
                DWORD type = 0;
                DWORD size = static_cast<DWORD>(sizeof(display_name));
                if (RegQueryValueExA(app_key, "DisplayName", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(display_name),
                                     &size) == ERROR_SUCCESS &&
                    (type == REG_SZ || type == REG_EXPAND_SZ) && size > 1) {
                    const std::string app = trim(display_name);
                    if (!app.empty() && dedup.find(app) == dedup.end()) {
                        dedup[app] = true;
                        inventory.packages.push_back(app);
                    }
                }
                RegCloseKey(app_key);
            }
            ++index;
        }

        RegCloseKey(key);
    }

    std::sort(inventory.packages.begin(), inventory.packages.end());
    return inventory;
#else
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
#endif
}

inline void printNetworkInterfacesFromSysfs() {
    printSubHeader("Network Interfaces");
#ifdef _WIN32
    ULONG size = 0;
    DWORD rc = GetAdaptersInfo(nullptr, &size);
    if (rc != ERROR_BUFFER_OVERFLOW) {
        std::cout << "    " << colorize("adapter list query failed", ansi::YELLOW) << "\n";
        return;
    }

    std::vector<unsigned char> buffer(size);
    auto* addrs = reinterpret_cast<IP_ADAPTER_INFO*>(buffer.data());
    rc = GetAdaptersInfo(addrs, &size);
    if (rc != NO_ERROR) {
        std::cout << "    " << colorize("adapter list query failed", ansi::YELLOW) << "\n";
        return;
    }

    bool printed = false;
    for (auto* adapter = addrs; adapter != nullptr; adapter = adapter->Next) {
        std::string name = adapter->Description;
        if (name.empty()) {
            name = adapter->AdapterName;
        }
        if (name.empty()) {
            name = "Unnamed Adapter";
        }

        std::ostringstream mac;
        for (ULONG i = 0; i < adapter->AddressLength; ++i) {
            if (i > 0) {
                mac << ":";
            }
            mac << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(adapter->Address[i]) << std::dec;
        }
        const std::string mac_text = mac.str().empty() ? "virtual" : mac.str();

        std::vector<std::string> ips;
        for (IP_ADDR_STRING* ip = &adapter->IpAddressList; ip != nullptr; ip = ip->Next) {
            const std::string addr = trim(ip->IpAddress.String);
            if (!addr.empty() && addr != "0.0.0.0") {
                ips.push_back(addr);
            }
        }

        std::ostringstream ip_text;
        for (std::size_t i = 0; i < ips.size(); ++i) {
            if (i > 0) {
                ip_text << ",";
            }
            ip_text << ips[i];
        }
        if (ips.empty()) {
            ip_text << "no-ip";
        }

        const std::string state = ips.empty() ? "down/unknown" : "up";
        std::cout << "    " << name << " state=" << state << " mac=" << mac_text
                  << " ip=" << ip_text.str() << "\n";
        printed = true;
    }

    if (!printed) {
        std::cout << "    " << colorize("no adapters reported", ansi::YELLOW) << "\n";
    }
#else
    const auto entries = listDirectory("/sys/class/net");
    if (entries.empty()) {
        std::cout << "    " << colorize("UNAVAILABLE", ansi::YELLOW) << "\n";
        return;
    }

    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        const std::string state = readFirstLine((entry / "operstate").string()).value_or("N/A");
        const std::string mac = readFirstLine((entry / "address").string()).value_or("N/A");
        std::cout << "    " << name << " state=" << state << " mac=" << mac << "\n";
    }
#endif
}

#ifdef _WIN32
inline std::vector<std::string> runPowerShellList(const std::string& script) {
    const auto result = runCommand("powershell -NoProfile -Command \"" + script + "\" 2>nul");
    if (result.exit_code != 0) {
        return {};
    }
    std::vector<std::string> lines;
    for (const auto& line : splitLines(result.output)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}
#endif

inline void printMachineDumpSection() {
    printSectionHeader("INFO DUMP");

    printKeyValue("OS", getOsPrettyName());

#ifdef _WIN32
    printKeyValue("Kernel", getWindowsKernelVersion());
    printKeyValue("Architecture", getWindowsArchitecture());
#else
    struct utsname uname_data{};
    if (::uname(&uname_data) == 0) {
        printKeyValue("Kernel", uname_data.release);
        printKeyValue("Architecture", uname_data.machine);
    } else {
        printKeyValue("Kernel", colorize("N/A", ansi::YELLOW));
        printKeyValue("Architecture", colorize("N/A", ansi::YELLOW));
    }
#endif

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
#ifdef _WIN32
    if (packages.packages.empty()) {
        printKeyValue("Package Inventory", colorize("registry list is empty", ansi::YELLOW));
    } else {
        printKeyValue("Package Inventory", "registry");
        printKeyValue("Package Count", std::to_string(packages.packages.size()));
    }
#else
    if (packages.manager == "N/A" || packages.packages.empty()) {
        printKeyValue("Package Inventory", colorize("UNAVAILABLE", ansi::YELLOW));
    } else {
        printKeyValue("Package Manager", packages.manager);
        printKeyValue("Package Count", std::to_string(packages.packages.size()));
    }
#endif

    printSubHeader("USB Devices");
#ifdef _WIN32
    {
        const auto usb_rows = runPowerShellList(
            "$rows=Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPClass -eq 'USB' } | "
            "Select-Object -First 24 Name,Status; "
            "if($rows){$rows | ForEach-Object { $_.Name + ' [' + $_.Status + ']' }}");
        if (usb_rows.empty()) {
            std::cout << "    " << colorize("No USB devices reported", ansi::YELLOW) << "\n";
        } else {
            for (const auto& row : usb_rows) {
                std::cout << "    " << row << "\n";
            }
        }
    }
#else
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
#endif

    printSubHeader("Storage Devices");
#ifdef _WIN32
    {
        const auto storage_rows = runPowerShellList(
            "$rows=Get-CimInstance Win32_DiskDrive | Select-Object Model,Size,InterfaceType; "
            "if($rows){$rows | ForEach-Object { $_.Model + ' | ' + $_.InterfaceType + ' | ' + "
            "$_.Size }}");
        if (storage_rows.empty()) {
            std::cout << "    " << colorize("No storage devices reported", ansi::YELLOW) << "\n";
        } else {
            for (const auto& row : storage_rows) {
                std::cout << "    " << row << "\n";
            }
        }
    }
#else
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
#endif

    printNetworkInterfacesFromSysfs();
}
