#include <set>

#include "battery.hpp"
#include "cpu.hpp"
#include "dump.hpp"
#include "health.hpp"
#include "internet.hpp"
#include "process.hpp"
#include "ram.hpp"
#include "services.hpp"

constexpr const char* CHECK_VERSION = "2.0.0";

struct CliOptions {
    bool full = false;
    bool network = false;
    bool no_color = false;
    bool custom_sections = false;
    std::string network_host = "example.com";
    std::string network_url = "https://example.com";
    std::set<std::string> sections;
};

inline void printUsage(const char* program_name) {
    const std::string program =
        program_name != nullptr && std::strlen(program_name) > 0 ? program_name : "check";
    std::cout
        << "Usage: " << program << " [options]\n"
        << "  --full                  Complete report with CPU, RAM, and disk benchmarks.\n"
        << "  --section NAME          Select a repeatable section: cpu, ram, internet,\n"
        << "                          summary, battery, health, info, services, or all.\n"
        << "  --network               Run active network checks (3s ping/DNS, 6s HTTP).\n"
        << "  --network-host HOST     Host used for the one ping and DNS check.\n"
        << "  --network-url URL       HTTP(S) URL used for the one latency check.\n"
        << "  --no-color              Disable ANSI color even on a terminal.\n"
        << "  --version               Show version.\n"
        << "  --help, -h              Show this help.\n";
}

inline bool validSection(const std::string& section) {
    return section == "cpu" || section == "ram" || section == "internet" || section == "summary" ||
           section == "battery" || section == "health" || section == "info" ||
           section == "services" || section == "all";
}

inline bool safeCliText(const std::string& value, std::size_t limit) {
    if (value.empty() || value.size() > limit) return false;
    for (const unsigned char byte : value) {
        if (byte < 0x20U || byte == 0x7fU) return false;
    }
    return true;
}

inline bool parseCli(int argc, char** argv, CliOptions& options, std::string& error, bool& stop) {
    stop = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        if (argument == "--help" || argument == "-h") {
            printUsage(argc > 0 ? argv[0] : "check");
            stop = true;
            return true;
        }
        if (argument == "--version") {
            std::cout << "check " << CHECK_VERSION << "\n";
            stop = true;
            return true;
        }
        if (argument == "--full")
            options.full = true;
        else if (argument == "--network")
            options.network = true;
        else if (argument == "--no-color")
            options.no_color = true;
        else if (argument == "--section" || argument == "--network-host" ||
                 argument == "--network-url") {
            if (++index >= argc || argv[index] == nullptr) {
                error = argument + " requires a value";
                return false;
            }
            const std::string value = argv[index];
            if (argument == "--section") {
                if (!validSection(value)) {
                    error = "unknown section: " + value;
                    return false;
                }
                options.custom_sections = true;
                options.sections.insert(value);
            } else if (argument == "--network-host") {
                if (!safeCliText(value, 253)) {
                    error = "invalid network host";
                    return false;
                }
                options.network_host = value;
            } else if (argument == "--network-url") {
                if (!safeCliText(value, 2048) ||
                    (!startsWith(value, "https://") && !startsWith(value, "http://"))) {
                    error = "network URL must be a valid HTTP(S) URL";
                    return false;
                }
                options.network_url = value;
            }
        } else {
            error = "unknown option: " + argument;
            return false;
        }
    }
    return true;
}

inline void addAllSections(std::set<std::string>& sections) {
    sections.insert("cpu");
    sections.insert("ram");
    sections.insert("internet");
    sections.insert("summary");
    sections.insert("battery");
    sections.insert("health");
    sections.insert("info");
    sections.insert("services");
}

inline bool sectionEnabled(const CliOptions& options, const std::string& section) {
    return options.sections.find(section) != options.sections.end();
}

int main(int argc, char** argv) {
    CliOptions options;
    std::string error;
    bool stop = false;
    if (!parseCli(argc, argv, options, error, stop)) {
        std::cerr << error << "\n";
        printUsage(argc > 0 ? argv[0] : "check");
        return 1;
    }
    if (stop) return 0;
    configureTerminal(options.no_color);
    const std::optional<fs::path> benchmark_directory =
        options.full ? automaticBenchmarkDirectory() : std::nullopt;

    if (options.custom_sections) {
        if (options.sections.erase("all") != 0) addAllSections(options.sections);
    } else {
        options.sections = {"cpu", "ram", "internet", "summary"};
        if (options.full) addAllSections(options.sections);
        if (options.network) options.sections.insert("internet");
    }

    ProcessTelemetry processes;
    if (sectionEnabled(options, "cpu") || sectionEnabled(options, "ram") ||
        sectionEnabled(options, "internet")) {
        processes = collectProcessTelemetry();
    }
    const std::vector<ProcessUsage> top_cpu = topProcesses(processes, ProcessSort::Cpu, 10);
    const std::vector<ProcessUsage> top_ram = topProcesses(processes, ProcessSort::Memory, 10);
    const std::vector<ProcessUsage> top_net = topProcesses(processes, ProcessSort::Network, 10);
    ThermalSnapshot thermals;
    if ((sectionEnabled(options, "cpu") && options.full) || sectionEnabled(options, "health")) {
        thermals = collectThermalSnapshot();
    }

    if (sectionEnabled(options, "cpu")) {
        printCpuSection(top_cpu, options.full, options.full, thermals);
    }
    if (sectionEnabled(options, "ram")) printRamSection(top_ram, options.full);
    if (sectionEnabled(options, "internet")) {
        printInternetSection(top_net, options.network, options.network_host, options.network_url);
    }
    if (sectionEnabled(options, "summary") && !sectionEnabled(options, "info")) {
        printMachineSummarySection();
    }
    if (sectionEnabled(options, "battery")) printBatterySection();
    if (sectionEnabled(options, "health")) {
        printImportantHealthSection(options.full, benchmark_directory, thermals);
    }
    if (sectionEnabled(options, "info")) printMachineDumpSection();
    if (sectionEnabled(options, "services")) printServicesSection();

    return reportCounts().failed == 0 ? 0 : 2;
}
