#pragma once

#include <map>

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
};

inline std::vector<ServiceUnitFile> listSystemdUnitFiles() {
    std::vector<ServiceUnitFile> rows;
    if (!commandExists("systemctl")) {
        return rows;
    }

    const auto result = runCommand(
        "systemctl list-unit-files --type=service --all --no-legend --no-pager 2>/dev/null");
    if (result.exit_code != 0) {
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

    const std::string cmd = "systemctl list-units --type=service --state=" + state +
                            " --all --no-legend --no-pager 2>/dev/null";
    const auto result = runCommand(cmd);
    if (result.exit_code != 0) {
        return rows;
    }

    const auto lines = splitLines(result.output);
    for (const auto& line : lines) {
        if (line.empty() || line.find("loaded units listed") != std::string::npos) {
            continue;
        }

        std::istringstream parser(line);
        ServiceRuntimeUnit row;
        if (!(parser >> row.unit >> row.load >> row.active >> row.sub)) {
            continue;
        }

        std::string description;
        std::getline(parser, description);
        row.description = trim(description);
        rows.push_back(row);
    }

    return rows;
}

inline void printServiceRuntimeTable(const std::vector<ServiceRuntimeUnit>& rows) {
    if (rows.empty()) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
        return;
    }

    std::cout << "    " << std::left << std::setw(44) << "UNIT" << std::setw(9) << "LOAD"
              << std::setw(9) << "ACTIVE" << std::setw(12) << "SUB"
              << "DESCRIPTION"
              << "\n";
    for (const auto& row : rows) {
        std::cout << "    " << std::left << std::setw(44) << row.unit << std::setw(9) << row.load
                  << std::setw(9) << row.active << std::setw(12) << row.sub << row.description
                  << "\n";
    }
    std::cout << std::left;
}

inline std::string parseWindowsServiceState(const std::string& line) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return "UNKNOWN";
    }

    std::istringstream parser(trim(line.substr(colon + 1)));
    std::string state_code;
    std::string state_name;
    parser >> state_code >> state_name;
    if (state_name.empty()) {
        return "UNKNOWN";
    }
    return state_name;
}

inline std::vector<WindowsServiceRow> listWindowsServices() {
    std::vector<WindowsServiceRow> rows;
    if (!commandExists("sc")) {
        return rows;
    }

    const auto result = runCommand("sc.exe query type= service state= all 2>nul");
    if (result.exit_code != 0) {
        return rows;
    }

    std::string current_name;
    std::string current_state;

    const auto lines = splitLines(result.output);
    for (const auto& line : lines) {
        if (startsWith(line, "SERVICE_NAME:")) {
            if (!current_name.empty()) {
                WindowsServiceRow row;
                row.name = current_name;
                row.state = current_state.empty() ? "UNKNOWN" : current_state;
                rows.push_back(row);
            }

            current_name = trim(line.substr(std::string("SERVICE_NAME:").size()));
            current_state.clear();
            continue;
        }

        if (startsWith(line, "STATE")) {
            current_state = parseWindowsServiceState(line);
        }
    }

    if (!current_name.empty()) {
        WindowsServiceRow row;
        row.name = current_name;
        row.state = current_state.empty() ? "UNKNOWN" : current_state;
        rows.push_back(row);
    }

    return rows;
}

inline void printWindowsServiceTable(const std::vector<WindowsServiceRow>& rows,
                                     std::size_t limit) {
    if (rows.empty()) {
        std::cout << "    " << colorize("N/A", ansi::YELLOW) << "\n";
        return;
    }

    const std::size_t count = std::min(limit, rows.size());
    std::cout << "    " << std::left << std::setw(40) << "SERVICE" << "STATE"
              << "\n";
    for (std::size_t i = 0; i < count; ++i) {
        std::cout << "    " << std::left << std::setw(40) << rows[i].name << rows[i].state << "\n";
    }
    if (rows.size() > count) {
        std::cout << "    "
                  << colorize("... " + std::to_string(rows.size() - count) + " more", ansi::YELLOW)
                  << "\n";
    }
    std::cout << std::left;
}

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

    printSubHeader("Running Services (all)");
    printWindowsServiceTable(running_services, running_services.size());

    printSubHeader("Stopped Services (first 20)");
    printWindowsServiceTable(stopped_services, 20);
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
        for (const auto& unit : unit_files) {
            std::cout << "    " << std::left << std::setw(44) << unit.unit << std::setw(18)
                      << unit.state << unit.preset << "\n";
        }
        std::cout << std::left;
    }

    printSubHeader("Running Services");
    printServiceRuntimeTable(running_units);

    printSubHeader("Failed Services");
    printServiceRuntimeTable(failed_units);
}
