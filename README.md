# Machine Health CLI

`check` prints a passive, bounded machine-health report by default. Network traffic requires
`--network`, while CPU, RAM, and disk benchmarks run as part of `--full`.

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

## Run

The default report samples CPU, RAM, process/socket usage, and passive machine identity data. It
does not send network traffic or run CPU, memory, or disk benchmarks.

```bash
./check
```

`--full` prints every report section and runs the bounded CPU, RAM, and disk benchmarks. The disk
benchmark automatically uses the current directory and falls back to the system temporary directory
only when the current directory cannot be resolved.

```bash
./check --full
```

Active network checks remain optional and can be combined with the full report:

```bash
# One ping and DNS lookup with 3-second budgets, plus one HTTP request with a 6-second budget.
./check --network

./check --full --network
```

The disk benchmark performs three 256 MiB write/read runs and reports each result plus the write and
cached-read averages. It preflights free space, creates each file atomically, never drops the
operating system page cache, verifies the full read byte count, labels reads as cached, and removes
each file on every exit path.

The network endpoint set is deliberately minimal and configurable:

```bash
./check --network --network-host example.com --network-url https://example.com
```

Select one or more report sections with repeated `--section` flags:

```bash
./check --section cpu --section ram
./check --section all
```

Available sections are `cpu`, `ram`, `internet`, `summary`, `battery`, `health`, `info`, and
`services`. Use `--no-color` for plain terminal/log output and `--version` for build provenance.

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
