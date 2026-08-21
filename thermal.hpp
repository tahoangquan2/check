#pragma once

#include "command.hpp"
#include "utils.hpp"

struct ThermalInfo {
    std::string zone;
    std::string type;
    std::optional<double> temp_c;
};

struct ThermalSnapshot {
    std::vector<ThermalInfo> values;
    std::string error;
};

inline std::vector<ThermalInfo> collectThermals(std::string* collection_error = nullptr) {
    std::vector<ThermalInfo> values;
#ifdef _WIN32
    const CommandResult result = runPowerShell(
        "$rows=Get-CimInstance Win32_PerfFormattedData_Counters_ThermalZoneInformation "
        "-ErrorAction SilentlyContinue; if($rows){$rows | ForEach-Object { "
        "[Console]::Out.WriteLine(($_.Name -replace '[|\\r\\n]',' ') + '|' + "
        "$_.Temperature) }}");
    if (!result.ok()) {
        if (collection_error != nullptr) *collection_error = "thermal sensor query failed";
        return values;
    }

    for (const std::string& line : splitLines(result.output)) {
        const std::size_t separator = line.find('|');
        if (separator == std::string::npos) continue;
        ThermalInfo info;
        info.zone = trim(line.substr(0, separator));
        info.type = "ACPI Thermal Zone";
        const auto parsed = parseDoubleStrict(trim(line.substr(separator + 1)));
        if (parsed) {
            double celsius = *parsed > 200.0 ? *parsed - 273.15 : *parsed;
            if (celsius > -80.0 && celsius < 200.0) info.temp_c = celsius;
        }
        values.push_back(info);
    }
#else
    const DirectoryListing thermal_zones = listDirectory("/sys/class/thermal");
    if (thermal_zones.error && collection_error != nullptr) {
        *collection_error = "/sys/class/thermal: " + thermal_zones.error.message();
    }
    for (const fs::path& path : thermal_zones.entries) {
        const std::string name = path.filename().string();
        if (!startsWith(name, "thermal_zone")) continue;
        ThermalInfo info;
        info.zone = name;
        info.type = readFirstLine((path / "type").string()).value_or("N/A");
        const auto temperature = readLongFromFile((path / "temp").string());
        if (temperature) {
            const double value = static_cast<double>(*temperature);
            info.temp_c = std::abs(value) >= 1000.0 ? value / 1000.0 : value;
        }
        values.push_back(info);
    }

    const DirectoryListing monitors = listDirectory("/sys/class/hwmon");
    if (monitors.error && collection_error != nullptr) {
        if (!collection_error->empty()) *collection_error += "; ";
        *collection_error += "/sys/class/hwmon: " + monitors.error.message();
    }
    for (const fs::path& monitor : monitors.entries) {
        const std::string monitor_name =
            readFirstLine((monitor / "name").string()).value_or("hwmon");
        const DirectoryListing files = listDirectory(monitor);
        if (files.error && collection_error != nullptr) {
            if (!collection_error->empty()) *collection_error += "; ";
            *collection_error += monitor.string() + ": " + files.error.message();
        }
        for (const fs::path& path : files.entries) {
            const std::string filename = path.filename().string();
            if (!startsWith(filename, "temp") || filename.find("_input") == std::string::npos) {
                continue;
            }
            const std::string prefix = filename.substr(0, filename.find("_input"));
            ThermalInfo info;
            info.zone = monitor.filename().string() + "/" + prefix;
            info.type = readFirstLine((monitor / (prefix + "_label")).string())
                            .value_or(monitor_name + " " + prefix);
            const auto temperature = readLongFromFile(path.string());
            if (temperature) {
                const double value = static_cast<double>(*temperature);
                info.temp_c = std::abs(value) >= 1000.0 ? value / 1000.0 : value;
            }
            values.push_back(info);
        }
    }
#endif
    return values;
}

inline ThermalSnapshot collectThermalSnapshot() {
    ThermalSnapshot snapshot;
    snapshot.values = collectThermals(&snapshot.error);
    return snapshot;
}
