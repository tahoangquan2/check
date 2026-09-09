#pragma once

#include <map>

#include "command.hpp"
#include "utils.hpp"

struct ServiceUnitFile {
    std::string unit;
    std::string state;
    std::string preset;
};

struct ServiceRuntimeUnit {
    std::string unit;
    std::string load;
    std::string active;
    std::string sub;
    std::string description;
};

struct WindowsServiceRow {
    std::string name;
    std::string state;
    std::string exe_path;
};

inline std::optional<ServiceRuntimeUnit> parseSystemdRuntimeLine(const std::string& line) {
    std::istringstream parser(line);
    ServiceRuntimeUnit row;
    if (!(parser >> row.unit >> row.load >> row.active >> row.sub)) return std::nullopt;
    std::getline(parser, row.description);
    row.description = trim(row.description);
    return row;
}

inline std::string executablePathFromCommandLine(const std::string& raw) {
    const std::string command_line = trim(raw);
    if (command_line.empty()) return "N/A";
    if (command_line.front() == '"') {
        const std::size_t quote = command_line.find('"', 1);
        if (quote != std::string::npos) return command_line.substr(1, quote - 1);
    }
    const std::string lowered = toLower(command_line);
    const std::size_t extension = lowered.find(".exe");
    if (extension != std::string::npos) return command_line.substr(0, extension + 4);
    const std::size_t space = command_line.find(' ');
    return command_line.substr(0, space);
}

inline std::vector<ServiceUnitFile> listSystemdUnitFiles() {
    std::vector<ServiceUnitFile> rows;
    if (!commandExists("systemctl")) {
        return rows;
    }

    const CommandResult result = runCommand(
        {"systemctl", "list-unit-files", "--type=service", "--all", "--no-legend", "--no-pager"});
    if (!result.ok()) {
        return rows;
    }

    const auto lines = splitLines(result.output);
    for (const auto& line : lines) {
        if (line.empty() || line.find("unit files listed") != std::string::npos) {
            continue;
        }

        std::istringstream parser(line);
        ServiceUnitFile row;
        if (!(parser >> row.unit >> row.state)) {
            continue;
        }
        if (!(parser >> row.preset)) {
            row.preset = "N/A";
        }
        rows.push_back(row);
    }

    return rows;
}

inline std::vector<ServiceRuntimeUnit> listSystemdUnitsByState(const std::string& state) {
    std::vector<ServiceRuntimeUnit> rows;
    if (!commandExists("systemctl")) {
        return rows;
    }

    if (state != "running" && state != "failed") return rows;
    const CommandResult result =
        runCommand({"systemctl", "list-units", "--type=service", "--state=" + state, "--all",
                    "--no-legend", "--no-pager"});
    if (!result.ok()) {
        return rows;
    }

    const auto lines = splitLines(result.output);
    for (const auto& line : lines) {
        if (line.empty() || line.find("loaded units listed") != std::string::npos) {
            continue;
        }

        const auto row = parseSystemdRuntimeLine(line);
        if (row) rows.push_back(*row);
    }

    return rows;
}

inline void printServiceRuntimeTable(const std::vector<ServiceRuntimeUnit>& rows,
                                     std::size_t limit = std::numeric_limits<std::size_t>::max()) {
    if (rows.empty()) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
        return;
    }

    std::cout << "    " << std::left << std::setw(44) << "UNIT" << std::setw(9) << "LOAD"
              << std::setw(9) << "ACTIVE" << std::setw(12) << "SUB"
              << "DESCRIPTION"
              << "\n";
    const std::size_t count = std::min(limit, rows.size());
    for (std::size_t index = 0; index < count; ++index) {
        const ServiceRuntimeUnit& row = rows[index];
        std::cout << "    " << std::left << std::setw(44) << sanitizeTerminalText(row.unit)
                  << std::setw(9) << sanitizeTerminalText(row.load) << std::setw(9)
                  << sanitizeTerminalText(row.active) << std::setw(12)
                  << sanitizeTerminalText(row.sub) << sanitizeTerminalText(row.description) << "\n";
    }
    if (rows.size() > count) {
        std::cout << "    ... " << rows.size() - count << " more\n";
    }
    std::cout << std::left;
}

#ifdef _WIN32
inline std::string windowsServiceState(DWORD state) {
    if (state == SERVICE_RUNNING) return "RUNNING";
    if (state == SERVICE_STOPPED) return "STOPPED";
    if (state == SERVICE_PAUSED) return "PAUSED";
    if (state == SERVICE_START_PENDING) return "START_PENDING";
    if (state == SERVICE_STOP_PENDING) return "STOP_PENDING";
    if (state == SERVICE_PAUSE_PENDING) return "PAUSE_PENDING";
    if (state == SERVICE_CONTINUE_PENDING) return "CONTINUE_PENDING";
    return "UNKNOWN";
}

inline std::vector<WindowsServiceRow> listWindowsServices() {
    std::vector<WindowsServiceRow> rows;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (manager == nullptr) return rows;
    DWORD bytes = 0;
    DWORD count = 0;
    DWORD resume = 0;
    EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr,
                          0, &bytes, &count, &resume, nullptr);
    if (GetLastError() != ERROR_MORE_DATA || bytes == 0) {
        CloseServiceHandle(manager);
        return rows;
    }
    std::vector<unsigned char> buffer(bytes);
    if (EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                              buffer.data(), bytes, &bytes, &count, &resume, nullptr)) {
        const ENUM_SERVICE_STATUS_PROCESSW* services =
            reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
        for (DWORD index = 0; index < count; ++index) {
            WindowsServiceRow row;
            row.name = wideToUtf8(services[index].lpServiceName);
            row.state = windowsServiceState(services[index].ServiceStatusProcess.dwCurrentState);
            rows.push_back(row);
        }
    }
    CloseServiceHandle(manager);
    return rows;
}

inline std::string queryWindowsServiceExePath(const std::string& service_name) {
    if (service_name.empty()) {
        return "N/A";
    }

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) return "N/A";
    const std::wstring wide_name = commandUtf8ToWide(service_name);
    SC_HANDLE service = OpenServiceW(manager, wide_name.c_str(), SERVICE_QUERY_CONFIG);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        return "N/A";
    }
    DWORD bytes = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytes);
    std::vector<unsigned char> buffer(bytes);
    std::string path = "N/A";
    if (bytes != 0 &&
        QueryServiceConfigW(service, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data()), bytes,
                            &bytes)) {
        const QUERY_SERVICE_CONFIGW* config =
            reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (config->lpBinaryPathName != nullptr) {
            path = executablePathFromCommandLine(wideToUtf8(config->lpBinaryPathName));
        }
    }
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return path;
}

inline void populateWindowsServiceExePaths(std::vector<WindowsServiceRow>& rows,
                                           std::size_t limit) {
    const std::size_t count = std::min(limit, rows.size());
    for (std::size_t i = 0; i < count; ++i) {
        rows[i].exe_path = queryWindowsServiceExePath(rows[i].name);
    }
}

inline void printWindowsServiceTable(const std::vector<WindowsServiceRow>& rows,
                                     std::size_t limit) {
    if (rows.empty()) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
        return;
    }

    const std::size_t count = std::min(limit, rows.size());
    std::cout << "    " << std::left << std::setw(40) << "SERVICE" << "EXECUTABLE"
              << "\n";
    for (std::size_t i = 0; i < count; ++i) {
        std::cout << "    " << std::left << std::setw(40) << sanitizeTerminalText(rows[i].name)
                  << sanitizeTerminalText(rows[i].exe_path.empty() ? "N/A" : rows[i].exe_path)
                  << "\n";
    }
    if (rows.size() > count) {
        std::cout << "    "
                  << colorize("... " + std::to_string(rows.size() - count) + " more", ansi::YELLOW)
                  << "\n";
    }
    std::cout << std::left;
}
#endif

inline void printServicesSection() {
    printSectionHeader("SERVICES");

#ifdef _WIN32
    const auto services = listWindowsServices();
    if (services.empty()) {
        printKeyValue("sc.exe", colorize("UNAVAILABLE OR NO ACCESS", ansi::YELLOW));
        return;
    }

    std::map<std::string, int> state_counts;
    std::vector<WindowsServiceRow> running_services;
    std::vector<WindowsServiceRow> stopped_services;
    for (const auto& service : services) {
        ++state_counts[service.state];
        if (service.state == "RUNNING") {
            running_services.push_back(service);
        }
        if (service.state == "STOPPED") {
            stopped_services.push_back(service);
        }
    }

    printKeyValue("Service Control", colorize("available (sc.exe)", ansi::GREEN));
    printKeyValue("Total Services", std::to_string(services.size()));
    printKeyValue("Running Services", std::to_string(running_services.size()));
    printKeyValue("Stopped Services", std::to_string(stopped_services.size()));

    printSubHeader("Service States");
    for (const auto& [state, count] : state_counts) {
        printKeyValue("  " + state, std::to_string(count));
    }

    populateWindowsServiceExePaths(running_services, running_services.size());
    populateWindowsServiceExePaths(stopped_services, stopped_services.size());

    printSubHeader("Running Services (all)");
    printWindowsServiceTable(running_services, running_services.size());

    printSubHeader("Stopped Services (all)");
    printWindowsServiceTable(stopped_services, stopped_services.size());
    return;
#endif

    if (!commandExists("systemctl")) {
        printKeyValue("systemctl", colorize("UNAVAILABLE", ansi::YELLOW));
        return;
    }

    const auto unit_files = listSystemdUnitFiles();
    const auto running_units = listSystemdUnitsByState("running");
    const auto failed_units = listSystemdUnitsByState("failed");

    if (unit_files.empty() && running_units.empty() && failed_units.empty()) {
        printKeyValue("systemd", colorize("UNAVAILABLE OR NO ACCESS", ansi::YELLOW));
        return;
    }

    printKeyValue("Unit Files", std::to_string(unit_files.size()));
    printKeyValue("Running Services", std::to_string(running_units.size()));
    printKeyValue("Failed Services", std::to_string(failed_units.size()));

    if (!unit_files.empty()) {
        std::map<std::string, int> state_counts;
        for (const auto& unit : unit_files) {
            ++state_counts[unit.state];
        }

        printSubHeader("Service Unit File States");
        for (const auto& [state, count] : state_counts) {
            printKeyValue("  " + state, std::to_string(count));
        }

        printSubHeader("All Service Unit Files");
        std::cout << "    " << std::left << std::setw(44) << "UNIT FILE" << std::setw(18) << "STATE"
                  << "PRESET"
                  << "\n";
        const std::size_t count = unit_files.size();
        for (std::size_t index = 0; index < count; ++index) {
            const ServiceUnitFile& unit = unit_files[index];
            std::cout << "    " << std::left << std::setw(44) << sanitizeTerminalText(unit.unit)
                      << std::setw(18) << sanitizeTerminalText(unit.state)
                      << sanitizeTerminalText(unit.preset) << "\n";
        }
        if (unit_files.size() > count) {
            std::cout << "    ... " << unit_files.size() - count << " more\n";
        }
        std::cout << std::left;
    }

    printSubHeader("Running Services");
    printServiceRuntimeTable(running_units);

    printSubHeader("Failed Services");
    printServiceRuntimeTable(failed_units);
}
