#pragma once
#include "utils.hpp"

struct BatteryInfo {
    std::string name;
    std::string status = "N/A";
    std::optional<long long> capacity;
    std::optional<long long> cycle_count;
    std::optional<long long> estimated_runtime_min;
    std::optional<long long> charge_full;
    std::optional<long long> charge_full_design;
    std::optional<long long> energy_full;
    std::optional<long long> energy_full_design;
    std::optional<long long> voltage_now;
    std::optional<double> health_percent;
};

inline std::vector<BatteryInfo> collectBatteries() {
    std::vector<BatteryInfo> batteries;
#ifdef _WIN32
    std::optional<long long> design_voltage_mv;
    std::optional<long long> estimated_runtime_min;
    const auto cim = runCommand(
        "powershell -NoProfile -Command "
        "\"$b=Get-CimInstance Win32_Battery | Select-Object -First 1; "
        "if($b){"
        "Write-Output ('DesignVoltage=' + $b.DesignVoltage);"
        "Write-Output ('EstimatedRunTime=' + $b.EstimatedRunTime);"
        "Write-Output ('BatteryLifeTime=' + $b.BatteryLifeTime)"
        "}\" 2>nul");
    if (cim.exit_code == 0) {
        for (const auto& line : splitLines(cim.output)) {
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const std::string key = trim(line.substr(0, eq));
            const std::string value = trim(line.substr(eq + 1));
            if (key == "DesignVoltage") {
                const auto parsed = parseLongLongPrefix(value);
                if (parsed && *parsed > 0) {
                    design_voltage_mv = *parsed;
                }
            } else if (key == "EstimatedRunTime" || key == "BatteryLifeTime") {
                const auto parsed = parseLongLongPrefix(value);
                if (parsed && *parsed > 0 && *parsed != 71582788) {
                    estimated_runtime_min = *parsed;
                }
            }
        }
    }

    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        if (status.BatteryFlag != 128 && status.BatteryFlag != 255) {
            BatteryInfo info;
            info.name = "Internal";
            if (status.BatteryFlag & 8)
                info.status = "Charging";
            else if (status.ACLineStatus == 1)
                info.status = "Not charging";
            else
                info.status = "Discharging";

            if (status.BatteryLifePercent != 255) {
                info.capacity = status.BatteryLifePercent;
                info.health_percent = status.BatteryLifePercent;
            }

            if (design_voltage_mv) {
                info.voltage_now = *design_voltage_mv * 1000;
            }
            if (estimated_runtime_min) {
                info.estimated_runtime_min = estimated_runtime_min;
            } else if (status.BatteryLifeTime != static_cast<DWORD>(-1)) {
                info.estimated_runtime_min = static_cast<long long>(status.BatteryLifeTime) / 60;
            }
            batteries.push_back(info);
        }
    }
#else
    const auto entries = listDirectory("/sys/class/power_supply");
    for (const auto& entry : entries) {
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
#endif
    return batteries;
}

inline std::vector<std::pair<std::string, std::optional<long long>>> collectAcAdapters() {
    std::vector<std::pair<std::string, std::optional<long long>>> adapters;
#ifdef _WIN32
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        if (status.ACLineStatus == 0)
            adapters.push_back({"AC", 0});
        else if (status.ACLineStatus == 1)
            adapters.push_back({"AC", 1});
    }
#else
    const auto entries = listDirectory("/sys/class/power_supply");
    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        const std::string type = readFirstLine((entry / "type").string()).value_or("");
        if (!startsWith(name, "AC") && type != "Mains") {
            continue;
        }
        adapters.push_back({name, readLongFromFile((entry / "online").string())});
    }
#endif
    return adapters;
}

inline void printBatterySection() {
    printSectionHeader("BATTERY HEALTH");
    const auto batteries = collectBatteries();
    if (batteries.empty()) {
        printKeyValue("Battery", colorize("No battery detected", ansi::YELLOW));
    } else {
        for (const auto& battery : batteries) {
            printSubHeader("Battery " + battery.name);
            printKeyValue("  Status", battery.status);
#ifdef _WIN32
            printKeyValue("  Capacity",
                          battery.capacity ? colorByPercent(static_cast<double>(*battery.capacity))
                                           : colorize("reported by driver only", ansi::YELLOW));
            if (battery.estimated_runtime_min) {
                std::ostringstream runtime;
                runtime << *battery.estimated_runtime_min << " min";
                printKeyValue("  Estimated Runtime", runtime.str());
            } else {
                printKeyValue("  Estimated Runtime",
                              colorize("not reported (likely on AC)", ansi::YELLOW));
            }
            printKeyValue("  Health", battery.health_percent
                                          ? colorByPercent(*battery.health_percent)
                                          : colorize("health telemetry not exposed", ansi::YELLOW));
#else
            printKeyValue("  Capacity", battery.capacity
                                            ? colorByPercent(static_cast<double>(*battery.capacity))
                                            : colorize("N/A", ansi::YELLOW));
            printKeyValue("  Cycle Count", battery.cycle_count
                                               ? std::to_string(*battery.cycle_count)
                                               : colorize("N/A", ansi::YELLOW));
            printKeyValue("  Health", battery.health_percent
                                          ? colorByPercent(*battery.health_percent)
                                          : colorize("N/A", ansi::YELLOW));
#endif
            if (battery.voltage_now) {
                std::ostringstream voltage;
                voltage << std::fixed << std::setprecision(2)
                        << (static_cast<double>(*battery.voltage_now) / 1000000.0) << " V";
                printKeyValue(
#ifdef _WIN32
                    "  Battery Voltage",
#else
                    "  Voltage",
#endif
                    voltage.str());
            } else {
                printKeyValue(
#ifdef _WIN32
                    "  Battery Voltage",
#else
                    "  Voltage",
#endif
                    colorize(
#ifdef _WIN32
                        "not exposed by firmware",
#else
                        "N/A",
#endif
                        ansi::YELLOW));
            }
        }
    }

    const auto adapters = collectAcAdapters();
    if (adapters.empty()) {
        printKeyValue(
            "AC Adapter",
#ifdef _WIN32
            colorize("state not exposed", ansi::YELLOW)
#else
            colorize("N/A", ansi::YELLOW)
#endif
        );
    } else {
        for (const auto& adapter : adapters) {
            const std::string state = adapter.second
                                          ? (*adapter.second == 1 ? colorize("Online", ansi::GREEN)
                                                                  : colorize("Offline", ansi::RED))
                                          :
#ifdef _WIN32
                                          colorize("state not exposed", ansi::YELLOW);
#else
                                          colorize("N/A", ansi::YELLOW);
#endif
            printKeyValue("  AC Adapter", state);
        }
    }
}
