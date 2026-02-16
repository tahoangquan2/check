# check

Single-file Linux machine health CLI written in C++ (`check.cpp`).

## Compile with clang

```bash
clang++ -std=c++17 -Ofast -march=native -mtune=native -funroll-loops -fno-exceptions -fno-rtti -Wall -Wextra -pedantic check.cpp -o check
```

## Run

```bash
./check
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
