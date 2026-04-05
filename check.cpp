#include "battery.hpp"
#include "cpu.hpp"
#include "dump.hpp"
#include "health.hpp"
#include "internet.hpp"
#include "ram.hpp"
#include "services.hpp"

inline void printUsage(const char* program_name) {
    const std::string program =
        (program_name != nullptr && std::strlen(program_name) > 0) ? program_name : "check";
    std::cout << "Usage: " << program << " [--full] [--help]\n";
    std::cout << "  --full  Show full report (battery, health, info dump, services).\n";
    std::cout << "  --help  Show this help message.\n";
}

int main(int argc, char** argv) {
    bool full_report = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg == "--full") {
            full_report = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            printUsage(argc > 0 ? argv[0] : "check");
            return 0;
        }

        std::cerr << colorize("Unknown option: " + arg, ansi::RED) << "\n";
        printUsage(argc > 0 ? argv[0] : "check");
        return 1;
    }

    const auto top_cpu = getTopProcesses("%cpu", 10);
    const auto top_ram = getTopProcesses("%mem", 10);
    const auto top_net = getTopProcesses("net", 10);

    printCpuSection(top_cpu);
    printRamSection(top_ram);
    printInternetSection(top_net);

    if (full_report) {
        printBatterySection();
        printImportantHealthSection();
        printMachineDumpSection();
        printServicesSection();
    } else {
        printMachineSummarySection();
    }

    std::cout << "\n" << colorize("Report complete.", ansi::GREEN) << "\n";
    return 0;
}
