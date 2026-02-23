#pragma once
#include "utils.hpp"

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

inline std::vector<BatteryInfo> collectBatteries() {
    std::vector<BatteryInfo> batteries;
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
    return batteries;
}

inline std::vector<std::pair<std::string, std::optional<long long>>> collectAcAdapters() {
    std::vector<std::pair<std::string, std::optional<long long>>> adapters;
    const auto entries = listDirectory("/sys/class/power_supply");
    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        const std::string type = readFirstLine((entry / "type").string()).value_or("");
        if (!startsWith(name, "AC") && type != "Mains") {
            continue;
        }
        adapters.push_back({name, readLongFromFile((entry / "online").string())});
    }
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
            printKeyValue("  Capacity", battery.capacity
                                            ? colorByPercent(static_cast<double>(*battery.capacity))
                                            : colorize("N/A", ansi::YELLOW));
            printKeyValue("  Cycle Count", battery.cycle_count
                                               ? std::to_string(*battery.cycle_count)
                                               : colorize("N/A", ansi::YELLOW));
            printKeyValue("  Health", battery.health_percent
                                          ? colorByPercent(*battery.health_percent)
                                          : colorize("N/A", ansi::YELLOW));
            if (battery.voltage_now) {
                std::ostringstream voltage;
                voltage << std::fixed << std::setprecision(2)
                        << (static_cast<double>(*battery.voltage_now) / 1000000.0) << " V";
                printKeyValue("  Voltage", voltage.str());
            } else {
                printKeyValue("  Voltage", colorize("N/A", ansi::YELLOW));
            }
        }
    }

    const auto adapters = collectAcAdapters();
    if (adapters.empty()) {
        printKeyValue("AC Adapter", colorize("N/A", ansi::YELLOW));
    } else {
        for (const auto& adapter : adapters) {
            const std::string state = adapter.second
                                          ? (*adapter.second == 1 ? colorize("Online", ansi::GREEN)
                                                                  : colorize("Offline", ansi::RED))
                                          : colorize("N/A", ansi::YELLOW);
            printKeyValue("  AC Adapter", state);
        }
    }
}
