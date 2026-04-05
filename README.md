# Machine Health CLI

## Compile

```bash
clang++ -std=c++17 -Ofast -march=native -mtune=native -funroll-loops -fno-exceptions -fno-rtti -Wall -Wextra -pedantic check.cpp -pthread -o check
```

In Windows, add `-lws2_32 -liphlpapi -lpsapi` to the command above.

## Run

```bash
./check
```

Use `--full` to include the complete report:

```bash
./check --full
```

## Install as a command

```bash
mkdir -p ~/.local/bin
cp ./check ~/.local/bin/check
chmod +x ~/.local/bin/check
```

## Add `~/.local/bin` to PATH

For bash:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

## Verify command works everywhere in terminal sessions

```bash
which check
check
```
