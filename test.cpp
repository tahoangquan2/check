#include <chrono>
#include <iostream>
#include <thread>

#include "command.hpp"
#include "cpu.hpp"
#include "dump.hpp"
#include "health.hpp"
#include "internet.hpp"
#include "ram.hpp"
#include "services.hpp"

struct TestState {
    int failures = 0;
};

inline void expect(TestState& state, bool condition, const std::string& name) {
    if (condition) return;
    ++state.failures;
    std::cerr << "FAIL: " << name << "\n";
}

inline std::size_t directoryEntryCount(const fs::path& path) {
    return listDirectory(path).entries.size();
}

inline void runParserTests(TestState& state) {
    expect(state, capacityPercentLevel(90.0) == PercentLevel::Good, "capacity high is healthy");
    expect(state, usagePercentLevel(90.0) == PercentLevel::Critical, "usage high is critical");
    expect(state, usagePercentLevel(60.0) == PercentLevel::Warning, "usage threshold is warning");

    expect(state, parseLongLongPrefix("123 KiB") == 123, "numeric prefix");
    expect(state, !parseIntPrefix("999999999999999"), "numeric overflow");
    expect(state, parseDoubleStrict("1.25") == 1.25, "strict double");
    expect(state, !parseDoubleStrict("1.25 ms"), "strict double rejects suffix");

    const CpuTimes cpu = parseProcStatCpuLine("cpu 1 2 3 4 5 6 7 8 90 100");
    expect(state, cpu.valid && cpu.idle_all == 9 && cpu.total == 36,
           "/proc/stat excludes guest fields");

    const auto memory = parseMemInfo("MemTotal: 1000 kB\nMemAvailable: 400 kB\nMalformed\n");
    expect(state, memory.size() == 2 && memory.at("MemTotal") == 1000, "/proc/meminfo fixture");

    const auto ping = parsePingAverage("rtt min/avg/max/mdev = 1.000/2.500/4.000/0.250 ms\n");
    expect(state, ping && *ping == "2.500 ms avg", "ping fixture");

    const auto service =
        parseSystemdRuntimeLine("demo.service loaded active running A service with spaces");
    expect(state, service && service->description == "A service with spaces",
           "systemd service fixture");
    expect(state,
           executablePathFromCommandLine("\"C:\\Program Files\\demo.exe\" --token secret") ==
               "C:\\Program Files\\demo.exe",
           "service arguments are omitted");

    const auto json = extractJsonField("{\"BackendState\":\"Run\\\"ning\\u0021\"}", "BackendState");
    expect(state, json && *json == "Run\"ning!", "JSON escaped string");
    const auto emoji = extractJsonField("{\"value\":\"\\uD83D\\uDE00\"}", "value");
    expect(state, emoji && *emoji == "\xF0\x9F\x98\x80", "JSON surrogate pair");
    expect(state, !extractJsonField("{\"BackendState\":\"bad\\q\"}", "BackendState"),
           "malformed JSON escape");

    expect(state,
           normalizeWindowsProductName("Windows 10 Home Single Language", "26200") ==
               "Windows 11 Home Single Language",
           "Windows 11 product name correction");
    expect(state, normalizeWindowsProductName("Windows 10 Pro", "19045") == "Windows 10 Pro",
           "Windows 10 product name preserved");

    const DiskUsage disk = diskUsageFromBlockCounts(1000, 800, 750, 4096);
    expect(state, disk.valid && disk.used == 200ULL * 4096ULL && disk.free == 750ULL * 4096ULL,
           "disk usage excludes reserved blocks from used space");

    const std::vector<std::string> block_lines = splitLines("first\n\nsecond\n");
    expect(state, countNonEmptyLines(block_lines) == 2, "blank block lines are not truncation");

    const std::vector<ThermalInfo> thermals = {{"generic", "ACPI Thermal Zone", 42.0},
                                               {"cpu", "CPU Package", 58.5}};
    const auto cpu_temperature = selectCpuTemperature(thermals);
#ifdef _WIN32
    expect(state, cpu_temperature && *cpu_temperature == 58.5,
           "specific CPU temperature replaces generic fallback");
#else
    expect(state, !cpu_temperature, "generic thermal zones are not reported as Linux CPU temp");
#endif
}

inline void runCommandTests(TestState& state, const std::string& executable) {
    const CommandResult child = runCommand({executable, "--child"});
    expect(state, child.ok() && trim(child.output) == "child ok", "argv subprocess");

    const CommandResult timeout =
        runCommand({executable, "--wait"}, {std::chrono::milliseconds(50), 1024});
    expect(state, timeout.failure == CommandFailure::TimedOut, "subprocess timeout");

    const CommandResult capped =
        runCommand({executable, "--spam"}, {std::chrono::milliseconds(2000), 1024});
    expect(state, capped.ok() && capped.output.size() == 1024 && capped.output_truncated,
           "subprocess output cap");

#ifndef _WIN32
    const auto descendant_start = std::chrono::steady_clock::now();
    const CommandResult descendant =
        runCommand({executable, "--descendant-pipe"}, {std::chrono::milliseconds(100), 1024});
    const double descendant_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - descendant_start).count();
    expect(state, descendant.failure == CommandFailure::TimedOut && descendant_seconds < 1.0,
           "subprocess timeout includes descendant-held pipes");
#endif
}

inline void runBenchmarkCleanupTest(TestState& state) {
    std::error_code error;
    const fs::path base = fs::temp_directory_path(error);
    if (error) {
        expect(state, false, "temporary directory lookup");
        return;
    }
#ifdef _WIN32
    const int process_id = _getpid();
#else
    const int process_id = static_cast<int>(::getpid());
#endif
    const fs::path directory =
        base / ("checktest." + std::to_string(process_id) + "." +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(directory, error) || error) {
        expect(state, false, "temporary benchmark directory creation");
        return;
    }

    const DiskBenchmarkResult benchmark = runDiskBenchmark(directory, 1);
    expect(state, benchmark.error.empty() && benchmark.write_mbps && benchmark.read_mbps,
           "bounded disk benchmark");
    expect(state, directoryEntryCount(directory) == 0, "benchmark RAII cleanup");
    fs::remove(directory, error);
    expect(state, !error, "temporary benchmark directory removal");
}

inline void runCliTests(TestState& state, const std::string& executable) {
    std::size_t expected_processors = 0;
#ifdef _WIN32
    expected_processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (::sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
        expected_processors = static_cast<std::size_t>(CPU_COUNT(&allowed));
    }
#endif
    expect(state, benchmarkCpuTargets().size() == expected_processors,
           "CPU benchmark covers every available processor without an eight-target cap");
    const CommandOptions budget{std::chrono::milliseconds(300000), 4 * 1024 * 1024};
    const CommandResult quick = runCommand({executable}, budget);
    expect(state,
           quick.failure == CommandFailure::None && (quick.exit_code == 0 || quick.exit_code == 2),
           "quick report completes");
    for (const std::string label : {"CPU", "RAM", "INTERNET", "SIMPLE INFO"}) {
        expect(state, quick.output.find(label) != std::string::npos, "quick section: " + label);
    }
    expect(state,
           quick.error.find("[bench]") == std::string::npos &&
               quick.error.find("[network]") == std::string::npos,
           "quick report stays passive");

    const CommandResult full = runCommand({executable, "--full"}, budget);
    expect(state,
           full.failure == CommandFailure::None && (full.exit_code == 0 || full.exit_code == 2) &&
               !full.output_truncated,
           "full report completes");
    for (const std::string label : {"CPU", "RAM", "INTERNET", "BATTERY HEALTH",
                                    "OTHER IMPORTANT INFO", "INFO DUMP", "SERVICES"}) {
        expect(state, full.output.find(label) != std::string::npos, "full section: " + label);
    }
    for (const std::string marker :
         {"[bench] cpu:", "[bench] ram:", "[bench] disk:", "[network] dns", "[network] http"}) {
        expect(state, full.error.find(marker) != std::string::npos, "full check: " + marker);
    }
    expect(state, full.error.find("[network] ping") == std::string::npos,
           "ping progress does not interrupt the table");
    const std::size_t ping_start = full.output.find("Ping (3 probes per host)");
    const std::size_t ping_end = full.output.find("DNS & HTTP", ping_start);
    const std::string ping_table = ping_start != std::string::npos
                                       ? full.output.substr(ping_start, ping_end - ping_start)
                                       : "";
    expect(state,
           ping_table.find("LATENCY / DETAIL") != std::string::npos &&
               ping_table.find("ELAPSED") != std::string::npos,
           "ping table labels latency and elapsed time separately");
    for (const std::string host : {"1.1.1.1", "8.8.8.8", "youtube.com", "codeforces.com",
                                   "github.com", "hquan.dev", "atcoder.jp"}) {
        expect(state, ping_table.find(host) != std::string::npos,
               "historical ping target: " + host);
    }
    for (const std::string label :
         {"CPU Benchmark (100M ops", "RAM R/W Benchmark (256MiB)", "Thermal Zones",
          "Installed Packages", "USB Devices", "Storage Devices", "Network Interfaces"}) {
        expect(state, full.output.find(label) != std::string::npos,
               "historical coverage: " + label);
    }
    for (int run = 1; run <= 3; ++run) {
        expect(state,
               full.output.find("Disk Write Run " + std::to_string(run)) != std::string::npos &&
                   full.output.find("Disk Read Run " + std::to_string(run)) != std::string::npos,
               "historical disk read/write run " + std::to_string(run));
    }
    if (commandExists("tailscale")) {
        for (const std::string label :
             {"Tailscale State", "Tailscale IPv4", "Tailscale IPv6", "Tailscale Netcheck"}) {
            expect(state, full.output.find(label) != std::string::npos,
                   "Tailscale check: " + label);
        }
        expect(state, full.error.find("[network] tailscale netcheck") != std::string::npos,
               "full report runs Tailscale netcheck");
    }
    expect(state,
           full.output.find(" more\n") == std::string::npos &&
               full.output.find(" more lines\n") == std::string::npos,
           "full inventories are not capped");
    expect(state, full.output.find("skipped (") == std::string::npos,
           "full report has no mode-disabled checks");
    for (const CpuTarget& target : benchmarkCpuTargets()) {
        expect(state,
               full.output.find("Logical Processor " + std::to_string(target.logical_id) + " ") !=
                   std::string::npos,
               "full benchmark includes logical processor " + std::to_string(target.logical_id));
    }

    for (const std::string flag : {"--section", "--network", "--network-host", "--network-url",
                                   "--no-color", "--version", "--help", "-h", "--unknown"}) {
        const CommandResult invalid = runCommand({executable, flag});
        expect(state,
               invalid.failure == CommandFailure::None && invalid.exit_code == 1 &&
                   invalid.output.find("[--full]") != std::string::npos,
               "reject unsupported flag: " + flag);
    }
    const CommandResult mixed = runCommand({executable, "--full", "--section", "cpu"});
    expect(state, mixed.exit_code == 1, "section selection cannot narrow full mode");
    const CommandResult duplicate = runCommand({executable, "--full", "--full"});
    expect(state, duplicate.exit_code == 1, "reject extra arguments");
}

inline void runFullListingTests(TestState& state) {
    std::ostringstream output;
    std::streambuf* previous = std::cout.rdbuf(output.rdbuf());
    std::vector<ServiceRuntimeUnit> rows;
    std::string block;
    for (int index = 0; index < 125; ++index) {
        const std::string name = "service" + std::to_string(index);
        rows.push_back({name, "loaded", "active", "running", name});
        block += name + "\n";
    }
    printServiceRuntimeTable(rows);
    const bool all_services = output.str().find("service124") != std::string::npos;
    output.str("");
    printBlockLines(block);
    const bool all_lines = output.str().find("service124") != std::string::npos;
    std::cout.rdbuf(previous);
    expect(state, all_services, "service table includes rows beyond the old 50-row cap");
    expect(state, all_lines, "device output includes lines beyond the old 100-line cap");
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--child") {
        std::cout << "child ok\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--wait") {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--spam") {
        for (int index = 0; index < 200000; ++index) std::cout << 'x';
        return 0;
    }
#ifndef _WIN32
    if (argc == 2 && std::string(argv[1]) == "--descendant-pipe") {
        const pid_t descendant = ::fork();
        if (descendant < 0) return 1;
        if (descendant == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            ::_exit(0);
        }
        return 0;
    }
#endif

    configureTerminal(true);
    TestState state;
    runParserTests(state);
    runFullListingTests(state);
    runCommandTests(state, argc > 0 && argv[0] != nullptr ? argv[0] : "test");
    runBenchmarkCleanupTest(state);
    runCliTests(state, argc > 1 ? argv[1]
                                : "./check"
#ifdef _WIN32
                                  ".exe"
#endif
    );
    if (state.failures != 0) {
        std::cerr << state.failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed.\n";
    return 0;
}
