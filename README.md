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

## Install as a command (user-local, no sudo)

```bash
mkdir -p ~/.local/bin
cp ./check ~/.local/bin/check
chmod +x ~/.local/bin/check
```

## Add `~/.local/bin` to PATH (Linux)

For bash:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

For zsh:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

## Verify command works everywhere in terminal sessions

```bash
which check
check
```
