#include "battery.hpp"
#include "cpu.hpp"
#include "dump.hpp"
#include "health.hpp"
#include "internet.hpp"
#include "process.hpp"
#include "ram.hpp"
#include "services.hpp"

inline void printUsage(const char* program_name) {
    const std::string program =
        program_name != nullptr && std::strlen(program_name) > 0 ? program_name : "check";
    std::cout << "Usage: " << program << " [--full]\n"
              << "  check         Quick passive report.\n"
              << "  check --full  All diagnostics, CPU/RAM/disk benchmarks, and network checks.\n";
}

int main(int argc, char** argv) {
    const bool full = argc == 2 && argv[1] != nullptr && std::string(argv[1]) == "--full";
    if (argc > 1 && !full) {
        std::cerr << "Expected no arguments or --full.\n";
        printUsage(argc > 0 ? argv[0] : "check");
        return 1;
    }
    configureTerminal(false);

    const ProcessTelemetry processes = collectProcessTelemetry();
    const auto top_cpu = topProcesses(processes, ProcessSort::Cpu, 10);
    const auto top_ram = topProcesses(processes, ProcessSort::Memory, 10);
    const auto top_net = topProcesses(processes, ProcessSort::Network, 10);
    const ThermalSnapshot thermals = full ? collectThermalSnapshot() : ThermalSnapshot{};

    printCpuSection(top_cpu, full, full, thermals);
    printRamSection(top_ram, full);
    printInternetSection(top_net, full);
    if (full) {
        printBatterySection();
        printImportantHealthSection(true, automaticBenchmarkDirectory(), thermals);
        printMachineDumpSection();
        printServicesSection();
    } else {
        printMachineSummarySection();
    }

    return reportCounts().failed == 0 ? 0 : 2;
}
