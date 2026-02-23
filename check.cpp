#include "battery.hpp"
#include "cpu.hpp"
#include "dump.hpp"
#include "health.hpp"
#include "internet.hpp"
#include "ram.hpp"

int main() {
    printBatterySection();
    printCpuSection();
    printRamSection();
    printInternetSection();
    printImportantHealthSection();
    printMachineDumpSection();

    std::cout << "\n" << colorize("Report complete.", ansi::GREEN) << "\n";
    return 0;
}
