#ifndef TILEXR_TOOLS_COLLECTIVES_LOCAL_LAUNCHER_H_
#define TILEXR_TOOLS_COLLECTIVES_LOCAL_LAUNCHER_H_

#include <string>
#include <vector>

namespace TileXRCollectivesTool {

struct LocalLauncherConfig {
    int rankSize = 1;
    int firstNpu = 0;
    int timeoutSec = 600;
    std::string logDir = ".";
    bool profileEnabled = false;
    std::string profileDir;
    bool profileAiPrompt = false;
    int warmupIters = 5;
    int measuredIters = 20;
    int profileSampleEvery = 1;
};

std::vector<std::string> BuildWorkerArguments(
    int argc,
    char **argv,
    int rank,
    const std::string &selfExecutable);

int RunLocalLauncher(
    const LocalLauncherConfig &config,
    int argc,
    char **argv);

} // namespace TileXRCollectivesTool

#endif // TILEXR_TOOLS_COLLECTIVES_LOCAL_LAUNCHER_H_
