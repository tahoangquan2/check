#include "battery.hpp"
#include "cpu.hpp"
#include "dump.hpp"
#include "health.hpp"
#include "internet.hpp"
#include "ram.hpp"

int main() {
    const auto top_cpu = getTopProcesses("%cpu", 10);
    const auto top_ram = getTopProcesses("%mem", 10);

    printBatterySection();
    printCpuSection(top_cpu);
    printRamSection(top_ram);
    printInternetSection();
    printImportantHealthSection();
    printMachineDumpSection();

    std::cout << "\n" << colorize("Report complete.", ansi::GREEN) << "\n";
    return 0;
}
