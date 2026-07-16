#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#include "local_launcher.h"

namespace {

int gFailures = 0;

struct EnvironmentSnapshot {
    bool present = false;
    std::string value;
};

EnvironmentSnapshot CaptureEnvironment(const char *name)
{
    const char *value = std::getenv(name);
    return {value != nullptr, value == nullptr ? std::string() : std::string(value)};
}

void RestoreEnvironment(const char *name, const EnvironmentSnapshot &snapshot)
{
    if (snapshot.present) {
        setenv(name, snapshot.value.c_str(), 1);
    } else {
        unsetenv(name);
    }
}

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

bool HasArgument(int argc, char **argv, const std::string &expected)
{
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && expected == argv[index]) {
            return true;
        }
    }
    return false;
}

int ParseRank(int argc, char **argv)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] != nullptr && std::string(argv[index]) == "--rank") {
            return std::atoi(argv[index + 1]);
        }
    }
    return -1;
}

int RunFakeWorker(int argc, char **argv)
{
    const int rank = ParseRank(argc, argv);
    const char *modeValue = std::getenv("TILEXR_LAUNCHER_TEST_MODE");
    const std::string mode = modeValue == nullptr ? "success" : modeValue;
    std::cout << "fake worker rank " << rank << '\n';
    if (mode == "failure" && rank == 1) {
        return 7;
    }
    if (mode == "timeout") {
        sleep(3);
    }
    return 0;
}

std::string MakeTempDirectory()
{
    std::vector<char> pathTemplate;
    const std::string pattern = "/tmp/tilexr-perf-launcher-test.XXXXXX";
    pathTemplate.assign(pattern.begin(), pattern.end());
    pathTemplate.push_back('\0');
    char *path = mkdtemp(pathTemplate.data());
    return path == nullptr ? std::string() : std::string(path);
}

bool FileExists(const std::string &path)
{
    std::ifstream input(path);
    return input.good();
}

void RemoveScenarioLogs(const std::string &directory, int rankSize)
{
    for (int rank = 0; rank < rankSize; ++rank) {
        const std::string path = directory + "/collective_perf_rank" + std::to_string(rank) + ".log";
        unlink(path.c_str());
    }
    rmdir(directory.c_str());
}

void TestWorkerArgumentRewrite()
{
    char program[] = "tool";
    char op[] = "--op";
    char allgather[] = "allgather";
    char worker[] = "--worker";
    char rankOption[] = "--rank";
    char oldRank[] = "9";
    char *arguments[] = {program, op, allgather, worker, rankOption, oldRank, nullptr};

    const auto rewritten = TileXRCollectivesTool::BuildWorkerArguments(
        6, arguments, 3, "/tmp/tilexr_collective_perf_tool");
    const std::vector<std::string> expected = {
        "/tmp/tilexr_collective_perf_tool",
        "--op",
        "allgather",
        "--worker",
        "--rank",
        "3",
    };
    Expect(rewritten == expected, "worker arguments must replace the existing worker/rank selection");
}

void TestLauncherScenarios(int argc, char **argv)
{
    const std::string root = MakeTempDirectory();
    Expect(!root.empty(), "temporary launcher directory must be created");
    if (root.empty()) {
        return;
    }

    TileXRCollectivesTool::LocalLauncherConfig config;
    config.rankSize = 2;
    config.timeoutSec = 5;

    const EnvironmentSnapshot availableNpus = CaptureEnvironment("TILEXR_AVAILABLE_NPUS");
    const EnvironmentSnapshot skipInsufficient =
        CaptureEnvironment("TILEXR_SKIP_IF_INSUFFICIENT_NPUS");
    setenv("TILEXR_AVAILABLE_NPUS", "8", 1);
    unsetenv("TILEXR_SKIP_IF_INSUFFICIENT_NPUS");

    config.logDir = root + "/success";
    unsetenv("TILEXR_LAUNCHER_TEST_MODE");
    Expect(TileXRCollectivesTool::RunLocalLauncher(config, argc, argv) == 0,
        "all successful workers must produce a successful launch");
    Expect(FileExists(config.logDir + "/collective_perf_rank0.log"), "rank 0 success log must exist");
    Expect(FileExists(config.logDir + "/collective_perf_rank1.log"), "rank 1 success log must exist");

    config.logDir = root + "/failure";
    setenv("TILEXR_LAUNCHER_TEST_MODE", "failure", 1);
    Expect(TileXRCollectivesTool::RunLocalLauncher(config, argc, argv) == 1,
        "a failed rank must fail the whole launch");

    config.logDir = root + "/timeout";
    config.timeoutSec = 1;
    setenv("TILEXR_LAUNCHER_TEST_MODE", "timeout", 1);
    Expect(TileXRCollectivesTool::RunLocalLauncher(config, argc, argv) == 124,
        "the launcher must return 124 when the deadline expires");
    unsetenv("TILEXR_LAUNCHER_TEST_MODE");

    setenv("TILEXR_AVAILABLE_NPUS", "1", 1);
    Expect(TileXRCollectivesTool::RunLocalLauncher(config, argc, argv) == 1,
        "insufficient NPUs must fail by default");
    setenv("TILEXR_SKIP_IF_INSUFFICIENT_NPUS", "1", 1);
    Expect(TileXRCollectivesTool::RunLocalLauncher(config, argc, argv) == 0,
        "insufficient NPUs must cleanly skip when requested");

    RestoreEnvironment("TILEXR_AVAILABLE_NPUS", availableNpus);
    RestoreEnvironment("TILEXR_SKIP_IF_INSUFFICIENT_NPUS", skipInsufficient);

    RemoveScenarioLogs(root + "/success", config.rankSize);
    RemoveScenarioLogs(root + "/failure", config.rankSize);
    RemoveScenarioLogs(root + "/timeout", config.rankSize);
    rmdir(root.c_str());
}

} // namespace

int main(int argc, char **argv)
{
    if (HasArgument(argc, argv, "--worker")) {
        return RunFakeWorker(argc, argv);
    }

    TestWorkerArgumentRewrite();
    TestLauncherScenarios(argc, argv);
    if (gFailures != 0) {
        std::cerr << gFailures << " launcher tests failed\n";
        return 1;
    }
    std::cout << "TileXR collective perf tool launcher tests passed\n";
    return 0;
}
