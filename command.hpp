#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

enum class CommandFailure { None, StartFailed, TimedOut, WaitFailed };

struct CommandOptions {
    std::chrono::milliseconds timeout{5000};
    std::size_t output_limit = 1024 * 1024;
};

struct CommandResult {
    int exit_code = -1;
    std::string output;
    std::string error;
    CommandFailure failure = CommandFailure::None;
    bool output_truncated = false;

    bool ok() const { return failure == CommandFailure::None && exit_code == 0; }
};

inline void appendCommandOutput(std::string& target, const char* data, std::size_t size,
                                std::size_t limit, bool& truncated) {
    const std::size_t room = target.size() < limit ? limit - target.size() : 0;
    const std::size_t copied = std::min(room, size);
    target.append(data, copied);
    truncated = truncated || copied != size;
}

#ifdef _WIN32
inline std::wstring commandUtf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int count =
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0],
                            count) <= 0) {
        return {};
    }
    return result;
}

inline std::wstring quoteWindowsArgument(const std::wstring& value) {
    if (!value.empty() && value.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return value;
    }

    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t value_char : value) {
        if (value_char == L'\\') {
            ++slashes;
            continue;
        }
        if (value_char == L'\"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            slashes = 0;
            continue;
        }
        quoted.append(slashes, L'\\');
        slashes = 0;
        quoted.push_back(value_char);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

inline std::wstring buildWindowsCommandLine(const std::vector<std::string>& argv) {
    std::wstring command_line;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) {
            command_line.push_back(L' ');
        }
        command_line += quoteWindowsArgument(commandUtf8ToWide(argv[i]));
    }
    return command_line;
}

struct WindowsPipeReader {
    HANDLE pipe = nullptr;
    std::string* target = nullptr;
    std::size_t limit = 0;
    bool truncated = false;
};

inline void readWindowsCommandPipe(WindowsPipeReader* reader) {
    char buffer[4096];
    DWORD read_count = 0;
    while (ReadFile(reader->pipe, buffer, sizeof(buffer), &read_count, nullptr) &&
           read_count != 0) {
        appendCommandOutput(*reader->target, buffer, static_cast<std::size_t>(read_count),
                            reader->limit, reader->truncated);
    }
    CloseHandle(reader->pipe);
    reader->pipe = nullptr;
}

inline void closeWindowsCommandPipes(HANDLE out_read, HANDLE out_write, HANDLE err_read,
                                     HANDLE err_write) {
    if (out_read != nullptr) CloseHandle(out_read);
    if (out_write != nullptr) CloseHandle(out_write);
    if (err_read != nullptr) CloseHandle(err_read);
    if (err_write != nullptr) CloseHandle(err_write);
}

inline CommandResult runCommand(const std::vector<std::string>& argv,
                                const CommandOptions& options = {}) {
    CommandResult result;
    if (argv.empty() || argv.front().empty()) {
        result.failure = CommandFailure::StartFailed;
        result.error = "empty command";
        return result;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    HANDLE err_read = nullptr;
    HANDLE err_write = nullptr;
    if (!CreatePipe(&out_read, &out_write, &security, 0) ||
        !CreatePipe(&err_read, &err_write, &security, 0) ||
        !SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0)) {
        closeWindowsCommandPipes(out_read, out_write, err_read, err_write);
        result.failure = CommandFailure::StartFailed;
        result.error = "pipe creation failed";
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;

    PROCESS_INFORMATION process{};
    std::wstring command_line = buildWindowsCommandLine(argv);
    std::vector<wchar_t> mutable_line(command_line.begin(), command_line.end());
    mutable_line.push_back(L'\0');

    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (!CreateProcessW(nullptr, mutable_line.data(), nullptr, nullptr, TRUE, flags, nullptr,
                        nullptr, &startup, &process)) {
        closeWindowsCommandPipes(out_read, out_write, err_read, err_write);
        result.failure = CommandFailure::StartFailed;
        result.error = "process creation failed (error " + std::to_string(GetLastError()) + ")";
        return result;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                     sizeof(limits)) ||
            !AssignProcessToJobObject(job, process.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }

    CloseHandle(out_write);
    CloseHandle(err_write);
    out_write = nullptr;
    err_write = nullptr;

    WindowsPipeReader stdout_reader{out_read, &result.output, options.output_limit, false};
    WindowsPipeReader stderr_reader{err_read, &result.error, options.output_limit, false};
    std::thread stdout_thread(readWindowsCommandPipe, &stdout_reader);
    std::thread stderr_thread(readWindowsCommandPipe, &stderr_reader);

    ResumeThread(process.hThread);
    const long long timeout_count = std::max<long long>(0, options.timeout.count());
    const DWORD timeout = static_cast<DWORD>(std::min<long long>(timeout_count, MAXDWORD - 1));
    const DWORD wait = WaitForSingleObject(process.hProcess, timeout);
    if (wait == WAIT_TIMEOUT) {
        result.failure = CommandFailure::TimedOut;
        if (job != nullptr) {
            TerminateJobObject(job, 1);
        } else {
            TerminateProcess(process.hProcess, 1);
        }
        WaitForSingleObject(process.hProcess, 1000);
    } else if (wait != WAIT_OBJECT_0) {
        result.failure = CommandFailure::WaitFailed;
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }

    DWORD exit_code = static_cast<DWORD>(-1);
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
        result.exit_code = static_cast<int>(exit_code);
    }

    if (job != nullptr) CloseHandle(job);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    stdout_thread.join();
    stderr_thread.join();
    result.output_truncated = stdout_reader.truncated || stderr_reader.truncated;
    return result;
}

inline CommandResult runPowerShell(const std::string& fixed_script,
                                   const CommandOptions& options = {}) {
    return runCommand(
        {"powershell", "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", fixed_script},
        options);
}
#else
inline void setCommandPipeNonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

inline bool drainCommandPipe(int fd, std::string& target, std::size_t limit, bool& truncated) {
    char buffer[4096];
    while (true) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            appendCommandOutput(target, buffer, static_cast<std::size_t>(count), limit, truncated);
            continue;
        }
        if (count == 0) {
            return false;
        }
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
}

inline CommandResult runCommand(const std::vector<std::string>& argv,
                                const CommandOptions& options = {}) {
    CommandResult result;
    if (argv.empty() || argv.front().empty()) {
        result.failure = CommandFailure::StartFailed;
        result.error = "empty command";
        return result;
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        if (out_pipe[0] >= 0) ::close(out_pipe[0]);
        if (out_pipe[1] >= 0) ::close(out_pipe[1]);
        if (err_pipe[0] >= 0) ::close(err_pipe[0]);
        if (err_pipe[1] >= 0) ::close(err_pipe[1]);
        result.failure = CommandFailure::StartFailed;
        result.error = "pipe creation failed";
        return result;
    }

    std::vector<char*> child_argv;
    child_argv.reserve(argv.size() + 1);
    for (const std::string& arg : argv) {
        child_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    child_argv.push_back(nullptr);

    const pid_t child = ::fork();
    if (child == 0) {
        ::setpgid(0, 0);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        ::execvp(child_argv[0], child_argv.data());
        ::_exit(127);
    }
    if (child < 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        result.failure = CommandFailure::StartFailed;
        result.error = "fork failed";
        return result;
    }

    ::setpgid(child, child);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    setCommandPipeNonblocking(out_pipe[0]);
    setCommandPipeNonblocking(err_pipe[0]);

    bool out_open = true;
    bool err_open = true;
    bool out_truncated = false;
    bool err_truncated = false;
    bool child_done = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;

    while (!child_done || out_open || err_open) {
        if (std::chrono::steady_clock::now() >= deadline) {
            result.failure = CommandFailure::TimedOut;
            if (::kill(-child, SIGKILL) != 0) ::kill(child, SIGKILL);
            while (!child_done) {
                const pid_t waited = ::waitpid(child, &status, 0);
                if (waited == child || (waited < 0 && errno == ECHILD)) {
                    child_done = true;
                } else if (waited < 0 && errno != EINTR) {
                    result.failure = CommandFailure::WaitFailed;
                    child_done = true;
                }
            }
            if (out_open) {
                out_open = drainCommandPipe(out_pipe[0], result.output, options.output_limit,
                                            out_truncated);
            }
            if (err_open) {
                err_open = drainCommandPipe(err_pipe[0], result.error, options.output_limit,
                                            err_truncated);
            }
            out_open = false;
            err_open = false;
            child_done = true;
            break;
        }

        pollfd descriptors[2] = {{out_pipe[0], static_cast<short>(out_open ? POLLIN : 0), 0},
                                 {err_pipe[0], static_cast<short>(err_open ? POLLIN : 0), 0}};
        ::poll(descriptors, 2, 20);
        if (out_open && (descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            out_open =
                drainCommandPipe(out_pipe[0], result.output, options.output_limit, out_truncated);
        }
        if (err_open && (descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            err_open =
                drainCommandPipe(err_pipe[0], result.error, options.output_limit, err_truncated);
        }

        if (!child_done) {
            const pid_t waited = ::waitpid(child, &status, WNOHANG);
            if (waited == child) {
                child_done = true;
            } else if (waited < 0 && errno != EINTR) {
                result.failure = CommandFailure::WaitFailed;
                child_done = true;
            }
        }
    }

    ::close(out_pipe[0]);
    ::close(err_pipe[0]);
    result.output_truncated = out_truncated || err_truncated;
    if (result.failure == CommandFailure::None) {
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exit_code = 128 + WTERMSIG(status);
        }
    }
    return result;
}
#endif
