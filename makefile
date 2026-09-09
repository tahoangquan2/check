CXX = clang++
CXXFLAGS = -std=c++17 -O3 -fno-exceptions -fno-rtti -Wall -Wextra -pedantic -pthread
HEADERS := $(wildcard *.hpp)
TESTBIN = checktest$(EXE)

ifeq ($(OS),Windows_NT)
EXE := .exe
LDLIBS := -lws2_32 -liphlpapi -lpsapi -ladvapi32
endif

.PHONY: all test smoke

all: check$(EXE)

check$(EXE): check.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) check.cpp $(LDLIBS) -o $@

$(TESTBIN): test.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) test.cpp $(LDLIBS) -o $@

test: check$(EXE) $(TESTBIN)
	./$(TESTBIN) ./check$(EXE)

smoke: test
