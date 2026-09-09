# Machine Health CLI

`check` prints a quick passive machine-health report. `check --full` runs all diagnostics,
including CPU, RAM, and disk benchmarks and active network checks.

## Build

The recommended build command on both Windows and Linux is:

```bash
make
```

The Makefile detects Windows and adds the required system libraries automatically.

For a manual Windows build from PowerShell with Clang/MinGW, use this complete command:

```powershell
clang++ -std=c++17 -O3 -fno-exceptions -fno-rtti -Wall -Wextra -pedantic check.cpp -pthread -lws2_32 -liphlpapi -lpsapi -ladvapi32 -o check.exe
```

The four `-l...` arguments are required on Windows. The shorter command below is Linux-only:

```bash
clang++ -std=c++17 -O3 -fno-exceptions -fno-rtti -Wall -Wextra -pedantic check.cpp -pthread -o check
```

The Windows build is verified with Clang/MinGW. The Linux build and runtime paths are verified with
GCC 12 and Clang 14.

For a binary that will stay on the build machine, `-march=native -mtune=native` can be added as
an optional local-only profile. The normal build deliberately omits native tuning so the binary is
portable.

## Test

The lowercase `makefile` also exposes test and smoke targets:

```bash
make test
make smoke
```

Both targets run the regression suite, including real quick and full reports. The full-report
test runs benchmarks and contacts the network endpoints listed below.

## Run

The default report samples CPU, RAM, process/socket usage, and passive machine identity data. It
does not send network traffic or run CPU, memory, or disk benchmarks.

```bash
./check
```

`--full` prints the CPU, RAM, internet, battery, health, detailed machine information, and services
sections. The detailed machine information includes the quick report's machine identity data.
It runs 100 million benchmark operations on every available logical processor and a 256 MiB RAM
benchmark. It sends three ping probes each to `1.1.1.1`, `8.8.8.8`, `youtube.com`, `codeforces.com`,
`github.com`, `hquan.dev`, and `atcoder.jp`, with a 10-second budget per host. It also runs a DNS
lookup of `example.com` (3-second budget) and an HTTP request to `https://example.com` (6-second
budget). When Tailscale is installed, it reports backend state, IPv4/IPv6 addresses, and runs
`tailscale netcheck` (15-second budget).

Full mode prints all service rows and executable paths, installed packages, and USB/storage
device rows without display-count caps. Linux also includes power-supply devices and interface IP
addresses. The top-process tables retain their historical top-10 presentation.

```bash
./check --full
```

These are the only two supported invocations. Color is detected automatically; redirected output
is plain text.

The disk benchmark automatically uses the current directory and falls back to the system temporary
directory only when the current directory cannot be resolved. It performs three 256 MiB write/read
runs and reports each result plus the write and cached-read averages. It preflights free space,
creates each file atomically, never drops the operating system page cache, verifies the full read
byte count, and removes each file on every exit path. Checks whose tools or hardware data are
unavailable report that limitation. Exit codes are 0 for no recorded failures, 1 for invalid
arguments, and 2 for recorded diagnostic failures.

## Install as a command

```bash
mkdir -p ~/.local/bin
cp ./check ~/.local/bin/check
chmod +x ~/.local/bin/check
```

Add `~/.local/bin` to `PATH` if needed:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```
