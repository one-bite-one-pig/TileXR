#include "local_launcher.h"

#include <cerrno>
#include <cctype>
#include <climits>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char **environ;

namespace TileXRCollectivesTool {
namespace {

volatile sig_atomic_t gCaughtSignal = 0;

void HandleTerminationSignal(int signalNumber)
{
    gCaughtSignal = signalNumber;
}

class SignalHandlerGuard {
public:
    SignalHandlerGuard() = default;

    ~SignalHandlerGuard()
    {
        Restore();
    }

    bool Install()
    {
        gCaughtSignal = 0;

        struct sigaction action;
        std::memset(&action, 0, sizeof(action));
        action.sa_handler = HandleTerminationSignal;
        sigemptyset(&action.sa_mask);

        if (sigaction(SIGINT, &action, &oldSigint_) != 0) {
            return false;
        }
        sigintInstalled_ = true;

        if (sigaction(SIGTERM, &action, &oldSigterm_) != 0) {
            Restore();
            return false;
        }
        sigtermInstalled_ = true;
        return true;
    }

private:
    void Restore()
    {
        if (sigtermInstalled_) {
            sigaction(SIGTERM, &oldSigterm_, nullptr);
            sigtermInstalled_ = false;
        }
        if (sigintInstalled_) {
            sigaction(SIGINT, &oldSigint_, nullptr);
            sigintInstalled_ = false;
        }
    }

    bool sigintInstalled_ = false;
    bool sigtermInstalled_ = false;
    struct sigaction oldSigint_ {};
    struct sigaction oldSigterm_ {};
};

struct ChildProcess {
    pid_t pid = -1;
    int rank = -1;
    bool reaped = false;
};

std::string JoinPath(const std::string &directory, const std::string &name)
{
    if (directory.empty() || directory == ".") {
        return directory.empty() ? name : directory + "/" + name;
    }
    if (directory.back() == '/') {
        return directory + name;
    }
    return directory + "/" + name;
}

std::string DirName(const std::string &path)
{
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

bool IsDirectory(const std::string &path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool EnsureDirectory(const std::string &path)
{
    if (path.empty() || path == ".") {
        return true;
    }
    if (IsDirectory(path)) {
        return true;
    }

    std::string current = path.front() == '/' ? "/" : "";
    std::string::size_type begin = path.front() == '/' ? 1 : 0;
    while (begin < path.size()) {
        while (begin < path.size() && path[begin] == '/') {
            ++begin;
        }
        if (begin == path.size()) {
            break;
        }

        const std::string::size_type end = path.find('/', begin);
        const std::string component = path.substr(
            begin,
            end == std::string::npos ? std::string::npos : end - begin);
        if (!component.empty() && component != ".") {
            if (!current.empty() && current.back() != '/') {
                current += '/';
            }
            current += component;

            if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "ERROR: cannot create log directory " << current
                          << ": " << std::strerror(errno) << '\n';
                return false;
            }
            if (!IsDirectory(current)) {
                std::cerr << "ERROR: log path is not a directory: " << current
                          << '\n';
                return false;
            }
        }

        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return true;
}

bool ParseNonNegativeInt(const char *text, int *value)
{
    if (text == nullptr || text[0] == '\0' || value == nullptr) {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    while (end != nullptr && *end != '\0' &&
           std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (errno != 0 || end == text || end == nullptr || *end != '\0' ||
        parsed < 0 || parsed > INT_MAX) {
        return false;
    }

    *value = static_cast<int>(parsed);
    return true;
}

int DetectAvailableNpus()
{
    const char *availableOverride = std::getenv("TILEXR_AVAILABLE_NPUS");
    if (availableOverride != nullptr && availableOverride[0] != '\0') {
        int available = -1;
        if (ParseNonNegativeInt(availableOverride, &available)) {
            return available;
        }
        std::cerr << "WARNING: TILEXR_AVAILABLE_NPUS is not a non-negative integer; "
                  << "falling back to npu-smi\n";
    }

    FILE *pipe = popen("npu-smi info -l 2>/dev/null", "r");
    if (pipe == nullptr) {
        return -1;
    }

    int available = -1;
    char line[512];
    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        const std::string outputLine(line);
        const std::string marker = "Total Count";
        const std::string::size_type markerPosition = outputLine.find(marker);
        if (markerPosition == std::string::npos) {
            continue;
        }
        const std::string::size_type colonPosition =
            outputLine.find(':', markerPosition + marker.size());
        if (colonPosition != std::string::npos &&
            ParseNonNegativeInt(outputLine.c_str() + colonPosition + 1, &available)) {
            break;
        }
    }
    pclose(pipe);
    return available;
}

bool SkipIfInsufficientNpus()
{
    const char *value = std::getenv("TILEXR_SKIP_IF_INSUFFICIENT_NPUS");
    return value != nullptr &&
        (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
         std::strcmp(value, "yes") == 0);
}

int CheckNpuCapacity(const LocalLauncherConfig &config)
{
    const int available = DetectAvailableNpus();
    if (available < 0) {
        return -1;
    }

    const int64_t required =
        static_cast<int64_t>(config.firstNpu) + config.rankSize;
    if (required <= available) {
        return -1;
    }

    const std::string message =
        "insufficient NPUs: required " + std::to_string(required) +
        ", available " + std::to_string(available);
    if (SkipIfInsufficientNpus()) {
        std::cout << "SKIP: " << message << '\n';
        return 0;
    }
    std::cerr << "ERROR: " << message << '\n';
    return 1;
}

std::string CanonicalizeIfPossible(const std::string &path)
{
    char *resolved = realpath(path.c_str(), nullptr);
    if (resolved == nullptr) {
        return path;
    }
    std::string result(resolved);
    std::free(resolved);
    return result;
}

std::string FindOnPath(const std::string &name)
{
    const char *pathValue = std::getenv("PATH");
    const std::string searchPath =
        pathValue == nullptr ? "/usr/local/bin:/usr/bin:/bin" : pathValue;

    std::string::size_type begin = 0;
    while (begin <= searchPath.size()) {
        const std::string::size_type end = searchPath.find(':', begin);
        const std::string directory =
            end == std::string::npos ? searchPath.substr(begin)
                                     : searchPath.substr(begin, end - begin);
        const std::string candidate = JoinPath(
            directory.empty() ? "." : directory,
            name);
        if (access(candidate.c_str(), X_OK) == 0) {
            return CanonicalizeIfPossible(candidate);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return name;
}

std::string ResolveSelfExecutable(const char *argv0)
{
    std::vector<char> buffer(4096);
    while (buffer.size() <= 1024 * 1024) {
        const ssize_t length =
            readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (length < 0) {
            break;
        }
        if (static_cast<std::size_t>(length) < buffer.size() - 1) {
            buffer[static_cast<std::size_t>(length)] = '\0';
            return std::string(buffer.data());
        }
        buffer.resize(buffer.size() * 2);
    }

    if (argv0 == nullptr || argv0[0] == '\0') {
        return "";
    }
    const std::string fallback(argv0);
    if (fallback.find('/') != std::string::npos) {
        return CanonicalizeIfPossible(fallback);
    }
    return FindOnPath(fallback);
}

std::vector<char *> MakeMutableArgv(std::vector<std::string> &arguments)
{
    std::vector<char *> result;
    result.reserve(arguments.size() + 1);
    for (std::string &argument : arguments) {
        result.push_back(const_cast<char *>(argument.c_str()));
    }
    result.push_back(nullptr);
    return result;
}

std::string RankLogPath(const LocalLauncherConfig &config, int rank)
{
    return JoinPath(
        config.logDir.empty() ? "." : config.logDir,
        "collective_perf_rank" + std::to_string(rank) + ".log");
}

int EnsureDescriptorAboveStandardStreams(int descriptor)
{
    if (descriptor > STDERR_FILENO) {
        return descriptor;
    }

    const int duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    const int savedErrno = errno;
    close(descriptor);
    errno = savedErrno;
    return duplicate;
}

int SpawnWorker(
    const LocalLauncherConfig &config,
    int argc,
    char **argv,
    int rank,
    const std::string &selfExecutable,
    pid_t *pid)
{
    const std::string logPath = RankLogPath(config, rank);
    int logDescriptor = open(
        logPath.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0644);
    if (logDescriptor < 0) {
        std::cerr << "ERROR: cannot open " << logPath << ": "
                  << std::strerror(errno) << '\n';
        return errno;
    }

    logDescriptor = EnsureDescriptorAboveStandardStreams(logDescriptor);
    if (logDescriptor < 0) {
        const int error = errno;
        std::cerr << "ERROR: cannot prepare " << logPath << ": "
                  << std::strerror(error) << '\n';
        return error;
    }

    posix_spawn_file_actions_t fileActions;
    int result = posix_spawn_file_actions_init(&fileActions);
    const bool fileActionsInitialized = result == 0;
    if (result == 0) {
        result = posix_spawn_file_actions_adddup2(
            &fileActions,
            logDescriptor,
            STDOUT_FILENO);
    }
    if (result == 0) {
        result = posix_spawn_file_actions_adddup2(
            &fileActions,
            logDescriptor,
            STDERR_FILENO);
    }
    if (result == 0) {
        result = posix_spawn_file_actions_addclose(
            &fileActions,
            logDescriptor);
    }

    if (result != 0) {
        if (fileActionsInitialized) {
            posix_spawn_file_actions_destroy(&fileActions);
        }
        close(logDescriptor);
        std::cerr << "ERROR: cannot configure worker log redirection for rank "
                  << rank << ": " << std::strerror(result) << '\n';
        return result;
    }

    std::vector<std::string> arguments = BuildWorkerArguments(
        argc,
        argv,
        rank,
        selfExecutable);
    std::vector<char *> mutableArguments = MakeMutableArgv(arguments);
    result = posix_spawn(
        pid,
        selfExecutable.c_str(),
        &fileActions,
        nullptr,
        mutableArguments.data(),
        environ);

    posix_spawn_file_actions_destroy(&fileActions);
    close(logDescriptor);

    if (result != 0) {
        std::cerr << "ERROR: cannot start worker rank " << rank << ": "
                  << std::strerror(result) << '\n';
    }
    return result;
}

void SleepMilliseconds(long milliseconds)
{
    struct timespec remaining;
    remaining.tv_sec = milliseconds / 1000;
    remaining.tv_nsec = (milliseconds % 1000) * 1000 * 1000;
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

bool HasLiveChildren(const std::vector<ChildProcess> &children)
{
    for (const ChildProcess &child : children) {
        if (!child.reaped) {
            return true;
        }
    }
    return false;
}

void PollChildrenForCleanup(std::vector<ChildProcess> *children)
{
    for (ChildProcess &child : *children) {
        if (child.reaped) {
            continue;
        }

        int status = 0;
        const pid_t result = waitpid(child.pid, &status, WNOHANG);
        if (result == child.pid || (result < 0 && errno == ECHILD)) {
            child.reaped = true;
        }
    }
}

void TerminateAndReapChildren(std::vector<ChildProcess> *children)
{
    for (const ChildProcess &child : *children) {
        if (!child.reaped) {
            kill(child.pid, SIGTERM);
        }
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(1);

    while (HasLiveChildren(*children)) {
        PollChildrenForCleanup(children);
        if (!HasLiveChildren(*children)) {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        SleepMilliseconds(10);
    }

    for (const ChildProcess &child : *children) {
        if (!child.reaped) {
            kill(child.pid, SIGKILL);
        }
    }

    for (ChildProcess &child : *children) {
        if (child.reaped) {
            continue;
        }
        int status = 0;
        pid_t result;
        do {
            result = waitpid(child.pid, &status, 0);
        } while (result < 0 && errno == EINTR);
        if (result == child.pid || (result < 0 && errno == ECHILD)) {
            child.reaped = true;
        } else if (result < 0) {
            std::cerr << "ERROR: cannot reap worker rank " << child.rank
                      << ": " << std::strerror(errno) << '\n';
        }
    }
}

void TailFile(const std::string &path, std::size_t lineCount)
{
    std::ifstream input(path);
    if (!input) {
        return;
    }

    std::deque<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (lines.size() == lineCount) {
            lines.pop_front();
        }
        lines.push_back(std::move(line));
    }

    std::cerr << "===== " << path << " =====\n";
    for (const std::string &tailLine : lines) {
        std::cerr << tailLine << '\n';
    }
}

void TailRankLogs(const LocalLauncherConfig &config)
{
    for (int rank = 0; rank < config.rankSize; ++rank) {
        TailFile(RankLogPath(config, rank), 80);
    }
}

bool PrintRankZeroLog(const LocalLauncherConfig &config)
{
    const std::string path = RankLogPath(config, 0);
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        std::cerr << "ERROR: cannot read rank 0 log: " << path << '\n';
        return false;
    }
    std::cout << input.rdbuf();
    std::cout.flush();
    return input.good() || input.eof();
}

int WaitForProfileReport(pid_t pid)
{
    while (true) {
        if (gCaughtSignal != 0) {
            std::vector<ChildProcess> helperProcess = {{pid, -1, false}};
            TerminateAndReapChildren(&helperProcess);
            return 128 + gCaughtSignal;
        }

        int status = 0;
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return 0;
            }
            if (WIFEXITED(status)) {
                std::cerr << "ERROR: aggregate profile report exited with status "
                          << WEXITSTATUS(status) << '\n';
            } else if (WIFSIGNALED(status)) {
                std::cerr << "ERROR: aggregate profile report was terminated by signal "
                          << WTERMSIG(status) << '\n';
            }
            return 1;
        }
        if (result < 0 && errno != EINTR) {
            std::cerr << "ERROR: cannot wait for aggregate profile report: "
                      << std::strerror(errno) << '\n';
            if (errno != ECHILD) {
                std::vector<ChildProcess> helperProcess = {{pid, -1, false}};
                TerminateAndReapChildren(&helperProcess);
            }
            return 1;
        }
        SleepMilliseconds(10);
    }
}

int RunProfileReport(
    const LocalLauncherConfig &config,
    const std::string &selfExecutable)
{
    if (!config.profileEnabled) {
        return 0;
    }

    const std::string helper = JoinPath(
        DirName(selfExecutable),
        "tilexr_collective_profile_report.py");
    if (access(helper.c_str(), R_OK) != 0) {
        std::cerr << "WARNING: " << helper
                  << " not found; skipping aggregate profile report\n";
        return 0;
    }

    const std::string profileRoot = config.profileDir.empty()
        ? "run/prof/collectives"
        : config.profileDir;
    const char *pythonCandidates[] = {"python3", "python"};
    for (const char *python : pythonCandidates) {
        std::vector<std::string> arguments = {
            python,
            helper,
            profileRoot,
            "--warmup-iters",
            std::to_string(config.warmupIters),
            "--iters",
            std::to_string(config.measuredIters),
            "--profile-sample-every",
            std::to_string(config.profileSampleEvery),
        };
        if (config.profileAiPrompt) {
            arguments.emplace_back("--emit-ai-prompt");
        }

        std::vector<char *> mutableArguments = MakeMutableArgv(arguments);
        pid_t pid = -1;
        const int result = posix_spawnp(
            &pid,
            python,
            nullptr,
            nullptr,
            mutableArguments.data(),
            environ);
        if (result == ENOENT) {
            continue;
        }
        if (result != 0) {
            std::cerr << "ERROR: cannot start aggregate profile report with "
                      << python << ": " << std::strerror(result) << '\n';
            return 1;
        }
        return WaitForProfileReport(pid);
    }

    std::cerr << "WARNING: python3/python not found; skipping aggregate profile report\n";
    return 0;
}

std::string DescribeChildStatus(int status)
{
    if (WIFEXITED(status)) {
        return "exited with status " + std::to_string(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return "was terminated by signal " + std::to_string(WTERMSIG(status));
    }
    return "ended unexpectedly";
}

} // namespace

std::vector<std::string> BuildWorkerArguments(
    int argc,
    char **argv,
    int rank,
    const std::string &selfExecutable)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc : 0) + 4);
    arguments.push_back(selfExecutable);

    for (int index = 1; index < argc; ++index) {
        if (argv == nullptr || argv[index] == nullptr) {
            continue;
        }

        const std::string argument(argv[index]);
        if (argument == "--worker" || argument.compare(0, 9, "--worker=") == 0) {
            continue;
        }
        if (argument == "--rank") {
            if (index + 1 < argc) {
                ++index;
            }
            continue;
        }
        if (argument.compare(0, 7, "--rank=") == 0) {
            continue;
        }
        arguments.push_back(argument);
    }

    arguments.emplace_back("--worker");
    arguments.emplace_back("--rank");
    arguments.push_back(std::to_string(rank));
    return arguments;
}

int RunLocalLauncher(
    const LocalLauncherConfig &config,
    int argc,
    char **argv)
{
    if (config.rankSize <= 0) {
        std::cerr << "ERROR: rank size must be a positive integer\n";
        return 1;
    }
    if (config.firstNpu < 0) {
        std::cerr << "ERROR: first NPU must be a non-negative integer\n";
        return 1;
    }
    if (config.timeoutSec <= 0) {
        std::cerr << "ERROR: timeout must be a positive integer\n";
        return 1;
    }
    const int capacityStatus = CheckNpuCapacity(config);
    if (capacityStatus >= 0) {
        return capacityStatus;
    }
    if (!EnsureDirectory(config.logDir.empty() ? "." : config.logDir)) {
        return 1;
    }

    const char *argv0 = argc > 0 && argv != nullptr ? argv[0] : nullptr;
    const std::string selfExecutable = ResolveSelfExecutable(argv0);
    if (selfExecutable.empty() || access(selfExecutable.c_str(), X_OK) != 0) {
        std::cerr << "ERROR: cannot resolve the tilexr_collective_perf_tool executable\n";
        return 1;
    }

    SignalHandlerGuard signalHandlers;
    if (!signalHandlers.Install()) {
        std::cerr << "ERROR: cannot install launcher signal handlers: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    std::vector<ChildProcess> children;
    children.reserve(static_cast<std::size_t>(config.rankSize));
    for (int rank = 0; rank < config.rankSize; ++rank) {
        if (gCaughtSignal != 0) {
            std::cerr << "ERROR: launcher interrupted by signal "
                      << gCaughtSignal << '\n';
            TerminateAndReapChildren(&children);
            TailRankLogs(config);
            return 128 + gCaughtSignal;
        }

        pid_t pid = -1;
        if (SpawnWorker(
                config,
                argc,
                argv,
                rank,
                selfExecutable,
                &pid) != 0) {
            TerminateAndReapChildren(&children);
            TailRankLogs(config);
            return 1;
        }
        children.push_back({pid, rank, false});
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(config.timeoutSec);

    std::size_t completed = 0;
    while (completed < children.size()) {
        if (gCaughtSignal != 0) {
            std::cerr << "ERROR: launcher interrupted by signal "
                      << gCaughtSignal << "; killing remaining ranks\n";
            TerminateAndReapChildren(&children);
            TailRankLogs(config);
            return 128 + gCaughtSignal;
        }

        bool madeProgress = false;
        for (ChildProcess &child : children) {
            if (child.reaped) {
                continue;
            }

            int status = 0;
            const pid_t result = waitpid(child.pid, &status, WNOHANG);
            if (result == 0 || (result < 0 && errno == EINTR)) {
                continue;
            }
            if (result < 0) {
                std::cerr << "ERROR: waitpid failed for rank " << child.rank
                          << ": " << std::strerror(errno) << '\n';
                TerminateAndReapChildren(&children);
                TailRankLogs(config);
                return 1;
            }

            child.reaped = true;
            ++completed;
            madeProgress = true;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                std::cerr << "ERROR: rank " << child.rank << ' '
                          << DescribeChildStatus(status)
                          << "; killing remaining ranks\n";
                TerminateAndReapChildren(&children);
                TailRankLogs(config);
                return 1;
            }
        }

        if (completed == children.size()) {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "ERROR: timed out after " << config.timeoutSec
                      << "s; killing remaining ranks\n";
            TerminateAndReapChildren(&children);
            TailRankLogs(config);
            return 124;
        }

        if (!madeProgress) {
            SleepMilliseconds(10);
        }
    }

    if (gCaughtSignal != 0) {
        TailRankLogs(config);
        return 128 + gCaughtSignal;
    }
    if (!PrintRankZeroLog(config)) {
        TailRankLogs(config);
        return 1;
    }

    const int reportStatus = RunProfileReport(config, selfExecutable);
    if (reportStatus != 0) {
        TailRankLogs(config);
        return reportStatus;
    }
    if (gCaughtSignal != 0) {
        return 128 + gCaughtSignal;
    }
    return 0;
}

} // namespace TileXRCollectivesTool
