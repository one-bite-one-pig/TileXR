#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "acl/acl.h"
#include "ep_urma_combine_host.h"
#include "ep_urma_combine_start_gate_window.h"
#include "tilexr_api.h"
#include "tilexr_ep.h"
#include "tilexr_ep_urma_combine_profile.h"
#include "tilexr_perf_trace.h"
#include "tilexr_types.h"
#include "tilexr_udma_types.h"

namespace {

constexpr int64_t kDefaultHidden = 512;
constexpr int64_t kDefaultTopK = 1;
constexpr int64_t kMaxHidden = 8192;
constexpr int64_t kMaxTopK = 16;
constexpr int64_t kMaxBatch = 1024 * 1024;
constexpr int64_t kAssistInts = 4;
constexpr int64_t kWorkspaceAlignment = 2 * 1024 * 1024;
constexpr std::size_t kProfileBufferAlignment = 32;
constexpr int64_t kDefaultRounds = 2;
constexpr uint32_t kProfileCoreCount = TileXREp::kEpUrmaCombineProfileCoreCount;
constexpr uint32_t kProfileStageCount = TileXREp::kEpUrmaCombinePerfStageCount;
constexpr uint32_t kProfileOpType = 313;
constexpr uint32_t kProfileDefaultCycleDivisor = 1000;
constexpr uint64_t kDefaultRouteSeed = 20260721;
constexpr int kDeviceLatencyRankSize = 64;
constexpr int64_t kDeviceLatencyBatchSize = 128;
constexpr int64_t kDeviceLatencyHidden = 7168;
constexpr int64_t kDeviceLatencyTopK = 8;
constexpr int64_t kDeviceLatencyWarmupRounds = 20;
constexpr int64_t kDeviceLatencyMeasuredRounds = 100;
constexpr uint32_t kDeviceLatencyPackCores = 48;
constexpr uint32_t kDeviceLatencySendCores = 16;
constexpr uint32_t kDeviceLatencyDoorbellBatch = 1;
constexpr uint32_t kDeviceLatencyRxScheduler = 1;
constexpr int kStrictKernelSingleHostRankSize = 8;
constexpr int64_t kStrictKernelSingleHostBatchSize = 32;
constexpr int64_t kStrictKernelSingleHostHidden = 5120;
constexpr int64_t kStrictKernelSingleHostTopK = 6;
constexpr int64_t kR141StrictBatchSize32 = 32;
constexpr int64_t kR141StrictBatchSize128 = 128;
constexpr int64_t kR141StrictWarmupRounds = 20;
constexpr int64_t kR141StrictMeasuredRounds = 10;
constexpr int64_t kR141StrictExtendedMeasuredRounds = 100;
constexpr uint64_t kR141StrictRouteSeed = 20260728;
constexpr uint32_t kR141StrictTotalCores = 64;
constexpr std::size_t kStrictKernelCycleCount = kProfileCoreCount;
constexpr std::size_t kStrictKernelCyclesBytes = kStrictKernelCycleCount * sizeof(uint64_t);
constexpr double kStrictKernelCyclesPerUs = 1000.0;

constexpr bool IsR141StrictSendCoreCount(uint32_t sendCores)
{
    return sendCores == 20 || sendCores == 22 || sendCores == 24 ||
        sendCores == 28 || sendCores == 32;
}

bool CheckAcl(aclError ret, const std::string &what)
{
    if (ret == ACL_SUCCESS) {
        return true;
    }
    std::cerr << what << " failed, acl ret=" << ret << std::endl;
    return false;
}

bool CheckTileXR(int ret, const std::string &what)
{
    if (ret == TileXR::TILEXR_SUCCESS) {
        return true;
    }
    std::cerr << what << " failed, TileXR ret=" << ret << std::endl;
    return false;
}

int GetEnvInt(const char *name, int fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? fallback : std::atoi(value);
}

int64_t GetEnvInt64(const char *name, int64_t fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? fallback : std::strtoll(value, nullptr, 10);
}

bool GetEnvPositiveInt64(const char *name, int64_t fallback, int64_t *out)
{
    if (out == nullptr || fallback <= 0) {
        return false;
    }
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        *out = fallback;
        return true;
    }
    errno = 0;
    char *end = nullptr;
    const long long parsed = std::strtoll(value, &end, 10);
    if (errno == ERANGE || end == value || end == nullptr || end[0] != '\0' || parsed <= 0) {
        return false;
    }
    *out = static_cast<int64_t>(parsed);
    return true;
}

uint64_t GetEnvUint64(const char *name, uint64_t fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? fallback : std::strtoull(value, nullptr, 10);
}

std::string GetEnvString(const char *name)
{
    const char *value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string EscapeJson(const std::string &value)
{
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

bool WriteProfileTrace(const std::string &profileRoot, int rank, int rankSize, std::size_t launchIndex,
    int64_t bs, int64_t h,
    int64_t topK, int64_t selfSendCnt, int64_t routeStride, uint64_t routeSeed,
    int64_t enqueueWindow, bool startGateExecuted, double hostRoundUs, double unprofiledMeanUs,
    const TileXR::TileXRPerfTraceHeader &header,
    const std::vector<TileXR::TileXRPerfCoreStageStats> &stats)
{
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path rankDir = fs::path(profileRoot) / ("rank" + std::to_string(rank));
    const fs::path launchDir = rankDir / ("launch" + std::to_string(launchIndex));
    fs::create_directories(launchDir, error);
    if (error) {
        std::cerr << "create profile directory failed: " << error.message() << std::endl;
        return false;
    }

    std::ofstream trace(launchDir / "trace.json");
    if (!trace) {
        return false;
    }
    trace << std::setprecision(12);
    trace << "{\n"
          << "  \"schema\": \"tilexr_perf_trace_report.v1\",\n"
          << "  \"op_type\": " << kProfileOpType << ",\n"
          << "  \"op_name\": \"TileXRMoeEpCombineUrma\",\n"
          << "  \"launch_id\": " << header.launchId << ",\n"
          << "  \"rank_size\": " << rankSize << ",\n"
          << "  \"max_core_count\": " << kProfileCoreCount << ",\n"
          << "  \"block_dim\": " << kProfileCoreCount << ",\n"
          << "  \"stage_count\": " << kProfileStageCount << ",\n"
          << "  \"cycle_to_us_divisor\": " << header.cycleToUsDivisor << ",\n"
          << "  \"kernel_timing_boundary\": \"pipe_all_bracketed_pre_flush\",\n"
          << "  \"profile_scope\": \""
          << (header.flags == 0 ? "kernel_total_only" :
              (header.flags == 1 ? "coarse_attribution" : "fine_attribution"))
          << "\",\n"
          << "  \"message_bytes\": " << header.messageBytes << ",\n"
          << "  \"host_round_us\": " << hostRoundUs << ",\n"
          << "  \"unprofiled_mean_us\": " << unprofiledMeanUs << ",\n"
          << "  \"profile_detail\": " << header.flags << ",\n"
          << "  \"config\": {\"bs\": " << bs << ", \"h\": " << h
           << ", \"top_k\": " << topK << ", \"self_send_count\": " << selfSendCnt
           << ", \"route_stride\": " << routeStride << ", \"route_seed\": " << routeSeed
           << ", \"enqueue_window\": " << enqueueWindow
           << ", \"qp_count\": " << TileXR::TILEXR_UDMA_QP_COUNT
           << ", \"doorbell_batch_size\": " << TileXREp::kEpUrmaCombineDoorbellBatchSize
           << ", \"tx_ready_batch_size\": " << TileXREp::kEpUrmaCombineTxReadyBatchSize
           << ", \"tx_ready_shared_flag\": "
           << (TileXREp::kEpUrmaCombineTxReadySharedFlag ? 1 : 0)
           << ", \"tx_ready_in_data\": "
           << (TileXREp::kEpUrmaCombineTxReadyInData ? "true" : "false")
           << ", \"tx_meta_prefetch_full\": "
           << (TileXREp::kEpUrmaCombineTxMetaPrefetchFull ? "true" : "false")
           << ", \"tx_ready_early_publish\": "
           << (TileXREp::kEpUrmaCombineTxReadyEarlyPublish ? "true" : "false")
           << ", \"rx_ready_sticky_mask\": "
           << (TileXREp::kEpUrmaCombineRxReadyStickyMask ? "true" : "false")
           << ", \"rx_ready_batch_mte2\": "
           << (TileXREp::kEpUrmaCombineRxReadyBatchMte2 ? "true" : "false")
           << ", \"rx_ready_batch_vector\": "
           << (TileXREp::kEpUrmaCombineRxReadyBatchVector ? "true" : "false")
           << ", \"send_route_balanced\": "
           << (TileXREp::kEpUrmaCombineBalancedSendRoutes ? "true" : "false")
           << ", \"parallel_round_publish\": "
           << (TileXREp::kEpUrmaCombineParallelRoundPublish ? "true" : "false")
           << ", \"deferred_round_credit\": "
           << (TileXREp::kEpUrmaCombineDeferredRoundCredit ? "true" : "false")
           << ", \"start_gate\": "
           << (TileXREp::kEpUrmaCombineStartGate ? "true" : "false")
           << ", \"start_gate_policy\": \""
           << (TileXREp::kEpUrmaCombineStartGate ?
               "first_after_stream_synchronize" : "disabled") << "\""
           << ", \"start_gate_executed\": "
           << (startGateExecuted ? "true" : "false")
           << ", \"qdc_version\": " << TileXREp::kEpUrmaCombineQdcVersion
           << ", \"rx_schedule\": \""
           << (TileXREp::kEpUrmaCombineRxStickyReady ? "token_round_robin_sticky" :
                TileXREp::kEpUrmaCombineRxScheduler == 1 ? "token_round_robin" : "sequential") << "\""
          << ", \"core_roles\": [{\"name\": \"pack_rx\", \"begin\": 0, \"count\": "
          << TileXREp::kEpUrmaCombineProfilePackReceiveCoreCount
           << "}, {\"name\": \"send\", \"begin\": "
           << TileXREp::kEpUrmaCombineProfilePackReceiveCoreCount << ", \"count\": "
           << TileXREp::kEpUrmaCombineProfileSendCoreCount << "}]},\n"
          << "  \"stats\": [\n";

    bool first = true;
    for (const auto &stat : stats) {
        if (stat.rank != static_cast<uint32_t>(rank) || stat.count == 0 || stat.stageId >= kProfileStageCount) {
            continue;
        }
        if (!first) {
            trace << ",\n";
        }
        first = false;
        trace << "    {\"rank\": " << stat.rank
              << ", \"core\": " << stat.core
              << ", \"stage\": \"" << TileXREp::kEpUrmaCombinePerfStageNames[stat.stageId] << "\""
              << ", \"stage_id\": " << stat.stageId
              << ", \"count\": " << stat.count
              << ", \"raw_cycles\": " << stat.sumCycles
              << ", \"min_cycles\": " << stat.minCycles
              << ", \"max_cycles\": " << stat.maxCycles
              << ", \"first_start_cycle\": " << stat.firstStartCycle
              << ", \"last_end_cycle\": " << stat.lastEndCycle
              << ", \"aux0\": " << stat.aux0
              << ", \"aux1\": " << stat.aux1
              << ", \"aux2\": " << stat.aux2
              << ", \"aux3\": " << stat.aux3
              << ", \"sum_us\": "
              << TileXR::PerfTraceCyclesToUs(stat.sumCycles, header.cycleToUsDivisor)
              << "}";
    }
    trace << "\n  ]\n}\n";
    trace.close();

    std::ofstream hostInfo(rankDir / "host_info.json");
    if (!hostInfo) {
        return false;
    }
    std::string host = GetEnvString("TILEXR_PROFILE_HOST");
    std::string hostIp = GetEnvString("TILEXR_PROFILE_HOST_IP");
    if (host.empty()) {
        host = "rank" + std::to_string(rank);
    }
    hostInfo << "{\n"
             << "  \"schema\": \"tilexr_collective_profile_host.v1\",\n"
             << "  \"rank\": " << rank << ",\n"
             << "  \"host\": \"" << EscapeJson(host) << "\",\n"
             << "  \"ip\": \"" << EscapeJson(hostIp) << "\",\n"
             << "  \"comm_mode\": \"udma-only\"\n"
             << "}\n";
    return true;
}

std::uintptr_t AlignAddress(std::uintptr_t value, std::size_t alignment)
{
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1U);
    return (value + mask) & ~mask;
}

std::size_t AlignSize(std::size_t value, std::size_t alignment)
{
    const std::size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

uint16_t RankValue(int rank)
{
    return static_cast<uint16_t>((15 + (rank % 4)) << 10);
}

int64_t TokenBegin(int64_t bs, int rank, int rankSize)
{
    return bs * rank / rankSize;
}

int TokenOwner(int64_t token, int64_t bs, int rankSize)
{
    return static_cast<int>(((token + 1) * rankSize - 1) / bs);
}

struct DemoResources {
    bool aclReady = false;
    bool deviceSet = false;
    int deviceId = 0;
    aclrtStream stream = nullptr;
    TileXRCommPtr comm = nullptr;
    bool workspaceRegistered = false;
    TileXRUDMAMemHandle workspaceHandle = 0;
    std::vector<void *> allocations;

    ~DemoResources()
    {
        if (stream != nullptr) {
            (void)aclrtSynchronizeStream(stream);
        }
        if (workspaceRegistered && comm != nullptr) {
            (void)TileXRUDMAUnregister(comm, workspaceHandle);
        }
        for (void *allocation : allocations) {
            if (allocation != nullptr) {
                (void)aclrtFree(allocation);
            }
        }
        if (comm != nullptr) {
            (void)TileXRCommDestroy(comm);
        }
        if (stream != nullptr) {
            (void)aclrtDestroyStream(stream);
        }
        if (deviceSet) {
            (void)aclrtResetDevice(deviceId);
        }
        if (aclReady) {
            (void)aclFinalize();
        }
    }
};

bool ValidateOutput(int rank, int rankSize, int64_t round, int64_t bs, int64_t h,
    const std::vector<uint16_t> &output)
{
    for (int64_t token = 0; token < bs; ++token) {
        const uint16_t expected = RankValue(TokenOwner(token, bs, rankSize));
        for (int64_t elem = 0; elem < h; ++elem) {
            const uint16_t actual = output[static_cast<std::size_t>(token) * h + elem];
            if (actual != expected) {
                std::cerr << "rank " << rank << " round " << round << " output[" << token << "][" << elem
                          << "] expected 0x" << std::hex << expected << " got 0x" << actual << std::dec
                          << std::endl;
                return false;
            }
        }
    }
    return true;
}

bool WriteExclusiveFile(const std::filesystem::path &path, const void *data, std::size_t bytes)
{
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) {
        std::cerr << "refusing to overwrite debug snapshot " << path << ": "
                  << std::strerror(errno) << std::endl;
        return false;
    }
    const auto *cursor = static_cast<const uint8_t *>(data);
    std::size_t remaining = bytes;
    while (remaining > 0) {
        const ssize_t written = ::write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            std::cerr << "write debug snapshot " << path << " failed: "
                      << std::strerror(errno) << std::endl;
            (void)::close(fd);
            return false;
        }
        cursor += static_cast<std::size_t>(written);
        remaining -= static_cast<std::size_t>(written);
    }
    if (::close(fd) != 0) {
        std::cerr << "close debug snapshot " << path << " failed: "
                  << std::strerror(errno) << std::endl;
        return false;
    }
    return true;
}

bool WriteWorkspaceDebugSnapshot(const std::string &root, int rank, int64_t round, int64_t magic,
    const TileXREp::EpUrmaCombineWorkspaceConfig &layout, void *workspaceDev, int64_t workspaceBytes,
    void *outputDev, std::size_t outputBytes)
{
    if (root.empty()) {
        return true;
    }
    if (workspaceDev == nullptr || outputDev == nullptr || outputBytes == 0 || workspaceBytes <= 0 ||
        static_cast<uint64_t>(workspaceBytes) > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid workspace debug snapshot parameters" << std::endl;
        return false;
    }

    namespace fs = std::filesystem;
    const fs::path snapshotDir = fs::path(root) / ("rank" + std::to_string(rank)) /
        ("round" + std::to_string(round));
    std::error_code ec;
    if (!fs::create_directories(snapshotDir, ec)) {
        std::cerr << "refusing to reuse debug snapshot directory " << snapshotDir;
        if (ec) {
            std::cerr << ": " << ec.message();
        }
        std::cerr << std::endl;
        return false;
    }

    const std::size_t snapshotBytes = static_cast<std::size_t>(workspaceBytes);
    std::vector<uint8_t> workspace(snapshotBytes);
    if (!CheckAcl(aclrtMemcpy(workspace.data(), snapshotBytes, workspaceDev, snapshotBytes,
            ACL_MEMCPY_DEVICE_TO_HOST), "copy workspace debug snapshot")) {
        return false;
    }
    if (!WriteExclusiveFile(snapshotDir / "workspace.bin", workspace.data(), workspace.size())) {
        return false;
    }
    std::vector<uint8_t> output(outputBytes);
    if (!CheckAcl(aclrtMemcpy(output.data(), output.size(), outputDev, output.size(),
            ACL_MEMCPY_DEVICE_TO_HOST), "copy output debug snapshot") ||
        !WriteExclusiveFile(snapshotDir / "output.bin", output.data(), output.size())) {
        return false;
    }

    std::ostringstream metadata;
    metadata << "{\n"
             << "  \"rank\": " << rank << ",\n"
             << "  \"round\": " << round << ",\n"
             << "  \"magic\": " << magic << ",\n"
             << "  \"workspace_bytes\": " << workspaceBytes << ",\n"
             << "  \"rank_size\": " << layout.rankSize << ",\n"
             << "  \"bs\": " << layout.bs << ",\n"
             << "  \"h\": " << layout.h << ",\n"
             << "  \"top_k\": " << layout.topK << ",\n"
             << "  \"self_send_cnt\": " << layout.selfSendCnt << ",\n"
             << "  \"comm_bytes\": " << layout.commBytes << ",\n"
             << "  \"block_count\": " << layout.blockCount << ",\n"
             << "  \"route_stride\": " << layout.routeStride << ",\n"
             << "  \"route_count\": " << layout.routeCount << ",\n"
             << "  \"rx_window_bytes\": " << layout.rxWindowBytes << ",\n"
             << "  \"rx_window_offset_0\": " << layout.rxWindowOffsets[0] << ",\n"
             << "  \"rx_window_offset_1\": " << layout.rxWindowOffsets[1] << ",\n"
             << "  \"round_done_offset_0\": " << layout.roundDoneOffsets[0] << ",\n"
             << "  \"round_done_offset_1\": " << layout.roundDoneOffsets[1] << ",\n"
             << "  \"rx_lane_done_offset\": " << layout.rxLaneDoneOffset << ",\n"
             << "  \"sender_done_offset\": " << layout.senderDoneOffset << ",\n"
             << "  \"round_publish_offset\": " << layout.roundPublishOffset << ",\n"
             << "  \"round_credit_offset\": " << layout.roundCreditOffset << ",\n"
             << "  \"start_gate_offset\": " << layout.startGateOffset << ",\n"
             << "  \"error_status_offset\": " << layout.errorStatusOffset << ",\n"
             << "  \"tx_ready_offset\": " << layout.txReadyOffset << ",\n"
             << "  \"tx_data_offset\": " << layout.txDataOffset << "\n"
             << "}\n";
    const std::string metadataText = metadata.str();
    if (!WriteExclusiveFile(snapshotDir / "layout.json", metadataText.data(), metadataText.size())) {
        return false;
    }
    std::cout << "WORKSPACE_DEBUG_SNAPSHOT rank=" << rank << " round=" << round
              << " magic=" << magic << " path=" << snapshotDir << std::endl;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    const int rankSize = argc > 1 ? std::atoi(argv[1]) : GetEnvInt("RANK_SIZE", 2);
    const int rank = argc > 2 ? std::atoi(argv[2]) : GetEnvInt("RANK", 0);
    const int npuCount = argc > 3 ? std::atoi(argv[3]) : GetEnvInt("TILEXR_DEMO_NPUS", rankSize);
    const int firstNpu = argc > 4 ? std::atoi(argv[4]) : GetEnvInt("TILEXR_DEMO_FIRST_NPU", 0);
    const int64_t bs = GetEnvInt64("TILEXR_DEMO_BS", rankSize);
    const int64_t h = GetEnvInt64("TILEXR_DEMO_H", kDefaultHidden);
    const int64_t topK = GetEnvInt64("TILEXR_DEMO_TOPK", kDefaultTopK);
    const int64_t rounds = GetEnvInt64("TILEXR_DEMO_ROUNDS", kDefaultRounds);
    const int64_t validateEvery = GetEnvInt64("TILEXR_DEMO_VALIDATE_EVERY", 1);
    const int64_t capacityMultiplier = GetEnvInt64("TILEXR_DEMO_CAPACITY_MULTIPLIER", 2);
    const int64_t warmupRounds = GetEnvInt64("TILEXR_DEMO_WARMUP_ROUNDS", 0);
    int64_t enqueueWindow = 1;
    if (!GetEnvPositiveInt64("TILEXR_DEMO_ENQUEUE_WINDOW", 1, &enqueueWindow)) {
        std::cerr << "TILEXR_DEMO_ENQUEUE_WINDOW must be a positive integer" << std::endl;
        return 2;
    }
    const bool deviceEventLatency = GetEnvInt("TILEXR_DEMO_DEVICE_EVENT_LATENCY", 0) != 0;
    const bool strictKernelLatency = GetEnvInt("TILEXR_DEMO_STRICT_KERNEL_LATENCY", 0) != 0;
    const std::string profileRoot = GetEnvString("TILEXR_DEMO_PROFILE_DIR");
    const std::string workspaceDebugRoot = GetEnvString("TILEXR_DEMO_WORKSPACE_DEBUG_DIR");
    const bool profileEnabled = !profileRoot.empty();
    const int64_t profileRound = GetEnvInt64("TILEXR_DEMO_PROFILE_ROUND", 0);
    const int64_t profileSamples = GetEnvInt64("TILEXR_DEMO_PROFILE_SAMPLES", 1);
    const int64_t profileCycleDivisor = GetEnvInt64(
        "TILEXR_DEMO_PROFILE_CYCLE_DIVISOR", kProfileDefaultCycleDivisor);
    const int64_t profileDetail = GetEnvInt64("TILEXR_DEMO_PROFILE_DETAIL", 2);
    const uint64_t routeSeed = GetEnvUint64("TILEXR_DEMO_ROUTE_SEED", kDefaultRouteSeed);
    if (rankSize < 1 || rank < 0 || rank >= rankSize || npuCount <= 0 || bs <= 0 || bs > kMaxBatch ||
        h <= 0 || h > kMaxHidden || topK <= 0 || topK > kMaxTopK || rounds <= 0 ||
        validateEvery <= 0 || capacityMultiplier <= 0 || capacityMultiplier > 16 ||
        warmupRounds < 0 || (profileEnabled && (profileRound < 0 || profileSamples <= 0 ||
            profileRound >= rounds || profileSamples > rounds - profileRound ||
            profileCycleDivisor <= 0 || profileCycleDivisor > std::numeric_limits<uint32_t>::max() ||
            profileDetail < 0 || profileDetail > 2)) ||
        bs > std::numeric_limits<int64_t>::max() / topK) {
        std::cerr << "invalid rank/device or stress configuration; require h <= "
                  << kMaxHidden << ", topK <= " << kMaxTopK << std::endl;
        return 2;
    }
    if (deviceEventLatency && strictKernelLatency) {
        std::cerr << "device-event and strict-kernel latency modes are mutually exclusive" << std::endl;
        return 2;
    }
    if (enqueueWindow > 1 && (deviceEventLatency || strictKernelLatency)) {
        std::cerr << "TILEXR_DEMO_ENQUEUE_WINDOW > 1 is supported only by normal/profile mode" << std::endl;
        return 2;
    }
    const bool full64LatencyShape = rankSize == kDeviceLatencyRankSize &&
        bs == kDeviceLatencyBatchSize && h == kDeviceLatencyHidden && topK == kDeviceLatencyTopK;
    const bool singleHost8StrictShape = rankSize == kStrictKernelSingleHostRankSize &&
        bs == kStrictKernelSingleHostBatchSize && h == kStrictKernelSingleHostHidden &&
        topK == kStrictKernelSingleHostTopK;
    const bool legacyLatencyContract = warmupRounds == kDeviceLatencyWarmupRounds &&
        rounds == kDeviceLatencyMeasuredRounds && routeSeed == kDefaultRouteSeed && !profileEnabled &&
        TileXREp::kEpUrmaCombineProfilePackReceiveCoreCount == kDeviceLatencyPackCores &&
        TileXREp::kEpUrmaCombineProfileSendCoreCount == kDeviceLatencySendCores &&
        TileXREp::kEpUrmaCombineDoorbellBatchSize == kDeviceLatencyDoorbellBatch &&
        TileXREp::kEpUrmaCombineRxScheduler == kDeviceLatencyRxScheduler &&
        TileXR::TILEXR_UDMA_QP_COUNT == kDeviceLatencySendCores &&
        !TileXREp::kEpUrmaCombineStartGate;
    const bool r141StrictShape = rankSize == kStrictKernelSingleHostRankSize &&
        (bs == kR141StrictBatchSize32 || bs == kR141StrictBatchSize128) &&
        h == kStrictKernelSingleHostHidden && topK == kStrictKernelSingleHostTopK;
    const bool r141StrictMeasuredRounds = rounds == kR141StrictMeasuredRounds ||
        rounds == kR141StrictExtendedMeasuredRounds;
    const uint32_t r141StrictPackCores = TileXREp::kEpUrmaCombineProfilePackReceiveCoreCount;
    const uint32_t r141StrictSendCores = TileXREp::kEpUrmaCombineProfileSendCoreCount;
    const bool r141StrictCoreLayout =
        r141StrictPackCores + r141StrictSendCores == kR141StrictTotalCores &&
        IsR141StrictSendCoreCount(r141StrictSendCores) &&
        TileXR::TILEXR_UDMA_QP_COUNT == r141StrictSendCores;
    const bool r141StrictContract = warmupRounds == kR141StrictWarmupRounds &&
        r141StrictMeasuredRounds && routeSeed == kR141StrictRouteSeed && !profileEnabled &&
        r141StrictCoreLayout &&
        TileXREp::kEpUrmaCombineDoorbellBatchSize == 1 &&
        TileXREp::kEpUrmaCombineTxReadyBatchSize == 1 &&
        !TileXREp::kEpUrmaCombineTxReadySharedFlag &&
        !TileXREp::kEpUrmaCombineTxReadyInData &&
        TileXREp::kEpUrmaCombineRxScheduler == 1 &&
        TileXREp::kEpUrmaCombineParallelRoundPublish &&
        TileXREp::kEpUrmaCombineStartGate &&
        TileXREp::kEpUrmaCombineQdcVersion == 3 &&
        !TileXREp::kEpUrmaCombineTxMetaPrefetchFull &&
        !TileXREp::kEpUrmaCombineTxReadyEarlyPublish &&
        TileXREp::kEpUrmaCombineRxReadyStickyMask &&
        !TileXREp::kEpUrmaCombineRxReadyBatchMte2 &&
        !TileXREp::kEpUrmaCombineRxReadyBatchVector;
    if ((deviceEventLatency && (!legacyLatencyContract || !full64LatencyShape)) ||
        (strictKernelLatency &&
            !((legacyLatencyContract && (full64LatencyShape || singleHost8StrictShape)) ||
              (r141StrictContract && r141StrictShape)))) {
        std::cerr << "latency contract mismatch: device-event requires 64/bs128/h7168/topK8; "
                  << "strict-kernel accepts the legacy S1 contract or the r141 sticky-only "
                  << "8-rank BS32/BS128, H5120, topK6, warmup20, measured10/100, seed20260728 "
                  << "64 total cores with Send in {20,22,24,28,32}, QP=Send, DB1, QDC-v3 contract"
                  << std::endl;
        return 2;
    }

    const int64_t tokenBegin = TokenBegin(bs, rank, rankSize);
    const int64_t tokenEnd = TokenBegin(bs, rank + 1, rankSize);
    const int64_t localTokenCount = tokenEnd - tokenBegin;
    if (localTokenCount > std::numeric_limits<int64_t>::max() / rankSize / topK) {
        std::cerr << "selfSendCnt overflow" << std::endl;
        return 2;
    }
    const int64_t selfSendCnt = localTokenCount * rankSize * topK;
    const int64_t routesPerDestination = bs * topK;
    if (routesPerDestination > std::numeric_limits<int64_t>::max() / rankSize) {
        std::cerr << "global route count overflow" << std::endl;
        return 2;
    }
    const int64_t globalRouteCount = routesPerDestination * rankSize;
    const int64_t globalRouteBegin = tokenBegin * rankSize * topK;
    if (selfSendCnt > std::numeric_limits<int64_t>::max() / capacityMultiplier) {
        std::cerr << "selfSendCapacity overflow" << std::endl;
        return 2;
    }
    const int64_t selfSendCapacity = selfSendCnt * capacityMultiplier;
    const int64_t quantDataBytes = (h + 31) / 32 * 32;
    const int64_t commBytes = 32 + quantDataBytes;
    const int64_t blockCount = (commBytes + 479) / 480;
    const int64_t routeStride = blockCount * 512;
    const std::size_t expertElements = static_cast<std::size_t>(selfSendCnt) * h;
    const std::size_t outputElements = static_cast<std::size_t>(bs) * h;
    const std::size_t expertBytes = expertElements * sizeof(uint16_t);
    const std::size_t assistBytes = static_cast<std::size_t>(selfSendCnt) * kAssistInts * sizeof(int32_t);
    const std::size_t weightBytes = static_cast<std::size_t>(bs) * topK * sizeof(float);
    const std::size_t outputBytes = outputElements * sizeof(uint16_t);

    DemoResources resources;
    resources.deviceId = firstNpu + rank % std::max(npuCount, 1);
    if (!CheckAcl(aclInit(nullptr), "aclInit")) {
        return 1;
    }
    resources.aclReady = true;
    if (!CheckAcl(aclrtSetDevice(resources.deviceId), "aclrtSetDevice")) {
        return 1;
    }
    resources.deviceSet = true;
    if (!CheckAcl(aclrtCreateStream(&resources.stream), "aclrtCreateStream") ||
        !CheckTileXR(TileXRCommInitRankLocal(rankSize, rank, &resources.comm), "TileXRCommInitRankLocal")) {
        return 1;
    }

    int64_t workspaceBytes = 0;
    if (!CheckTileXR(TileXRMoeEpCombineUrmaGetWorkspaceSize(
            rankSize, bs, h, topK, selfSendCapacity, &workspaceBytes),
            "TileXRMoeEpCombineUrmaGetWorkspaceSize")) {
        return 1;
    }
    std::cout << "rank " << rank << " stress config: rankSize=" << rankSize << " bs=" << bs << " h=" << h
              << " topK=" << topK << " tokenRange=[" << tokenBegin << "," << tokenEnd << ")"
              << " selfSendCnt=" << selfSendCnt << " rounds=" << rounds
              << " warmupRounds=" << warmupRounds << " validateEvery=" << validateEvery
              << " enqueueWindow=" << enqueueWindow
              << " workspaceBytes=" << workspaceBytes << " routeSeed=" << routeSeed
              << " profile=" << (profileEnabled ? "on" : "off")
              << " profileSamples=" << (profileEnabled ? profileSamples : 0)
              << " deviceEventLatency=" << (deviceEventLatency ? "on" : "off")
              << " strictKernelLatency=" << (strictKernelLatency ? "on" : "off")
              << " cores=" << TileXREp::kEpUrmaCombineProfilePackReceiveCoreCount << "+"
              << TileXREp::kEpUrmaCombineProfileSendCoreCount
              << " qps=" << TileXR::TILEXR_UDMA_QP_COUNT
              << " doorbellBatch=" << TileXREp::kEpUrmaCombineDoorbellBatchSize
              << " startGate=" << (TileXREp::kEpUrmaCombineStartGate ? "on" : "off")
              << " txReadyEarlyPublish=" << (TileXREp::kEpUrmaCombineTxReadyEarlyPublish ? 1 : 0)
              << " rxReadyStickyMask=" << (TileXREp::kEpUrmaCombineRxReadyStickyMask ? 1 : 0)
              << " rxReadyBatchMte2=" << (TileXREp::kEpUrmaCombineRxReadyBatchMte2 ? 1 : 0)
              << " rxReadyBatchVector=" << (TileXREp::kEpUrmaCombineRxReadyBatchVector ? 1 : 0)
              << " rxSchedule="
              << (TileXREp::kEpUrmaCombineRxStickyReady ? "token_round_robin_sticky" :
                  TileXREp::kEpUrmaCombineRxScheduler == 1 ? "token_round_robin" : "sequential")
              << std::endl;

    void *expertOutDev = nullptr;
    void *assistDev = nullptr;
    void *weightsDev = nullptr;
    void *outputDev = nullptr;
    void *rawWorkspaceDev = nullptr;
    void *strictKernelCyclesDev = nullptr;
    if (!CheckAcl(aclrtMalloc(&expertOutDev, expertBytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc expertOut") ||
        !CheckAcl(aclrtMalloc(&assistDev, assistBytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc assist") ||
        !CheckAcl(aclrtMalloc(&weightsDev, weightBytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc weights") ||
        !CheckAcl(aclrtMalloc(&outputDev, outputBytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc output") ||
        !CheckAcl(aclrtMalloc(&rawWorkspaceDev,
            static_cast<std::size_t>(workspaceBytes + kWorkspaceAlignment - 1), ACL_MEM_MALLOC_HUGE_FIRST),
            "aclrtMalloc workspace")) {
        resources.allocations = {expertOutDev, assistDev, weightsDev, outputDev, rawWorkspaceDev};
        return 1;
    }
    resources.allocations = {expertOutDev, assistDev, weightsDev, outputDev, rawWorkspaceDev};
    if (strictKernelLatency) {
        if (!CheckAcl(aclrtMalloc(&strictKernelCyclesDev, kStrictKernelCyclesBytes,
                ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc strict kernel cycles")) {
            return 1;
        }
        resources.allocations.push_back(strictKernelCyclesDev);
    }
    void *workspaceDev = reinterpret_cast<void *>(AlignAddress(
        reinterpret_cast<std::uintptr_t>(rawWorkspaceDev), static_cast<std::size_t>(kWorkspaceAlignment)));

    void *profileDev = nullptr;
    std::size_t profileStatsCount = 0;
    std::size_t profileBytes = 0;
    std::size_t profileStrideBytes = 0;
    std::size_t profileAllocationBytes = 0;
    const std::size_t profileSampleCount = profileEnabled ? static_cast<std::size_t>(profileSamples) : 0;
    std::vector<TileXR::TileXRPerfTraceHeader> profileHeaders;
    std::vector<std::vector<TileXR::TileXRPerfCoreStageStats>> profileStats;
    std::vector<bool> profileStartGateExecuted(profileSampleCount, false);
    if (profileEnabled) {
        int64_t requiredProfileBytes = 0;
        if (!CheckTileXR(TileXRMoeEpCombineUrmaGetProfileSize(rankSize, &requiredProfileBytes),
                "TileXRMoeEpCombineUrmaGetProfileSize") || requiredProfileBytes <=
                static_cast<int64_t>(TileXR::TILEXR_PERF_TRACE_STATS_OFFSET) ||
            static_cast<uint64_t>(requiredProfileBytes) > std::numeric_limits<std::size_t>::max()) {
            return 1;
        }
        profileBytes = static_cast<std::size_t>(requiredProfileBytes);
        profileStatsCount = (profileBytes - TileXR::TILEXR_PERF_TRACE_STATS_OFFSET) /
            sizeof(TileXR::TileXRPerfCoreStageStats);
        if (profileBytes > std::numeric_limits<std::size_t>::max() - (kProfileBufferAlignment - 1U)) {
            std::cerr << "profile buffer stride overflow" << std::endl;
            return 1;
        }
        profileStrideBytes = AlignSize(profileBytes, kProfileBufferAlignment);
        if (profileSampleCount > std::numeric_limits<std::size_t>::max() / profileStrideBytes) {
            std::cerr << "profile buffer allocation overflow" << std::endl;
            return 1;
        }
        profileAllocationBytes = profileStrideBytes * profileSampleCount;
        if (!CheckAcl(aclrtMalloc(&profileDev, profileAllocationBytes, ACL_MEM_MALLOC_HUGE_FIRST),
                "aclrtMalloc profile")) {
            return 1;
        }
        resources.allocations.push_back(profileDev);
        profileHeaders.assign(profileSampleCount, TileXR::TileXRPerfTraceHeader {});
        profileStats.assign(profileSampleCount,
            std::vector<TileXR::TileXRPerfCoreStageStats>(profileStatsCount));
        if (!CheckAcl(aclrtMemset(profileDev, profileAllocationBytes, 0, profileAllocationBytes),
                "clear profile buffers")) {
            return 1;
        }
        for (std::size_t launchIndex = 0; launchIndex < profileSampleCount; ++launchIndex) {
            auto &profileHeader = profileHeaders[launchIndex];
            profileHeader.rank = static_cast<uint32_t>(rank);
            profileHeader.rankSize = static_cast<uint32_t>(rankSize);
            profileHeader.blockDim = kProfileCoreCount;
            profileHeader.maxCoreCount = kProfileCoreCount;
            profileHeader.stageCount = kProfileStageCount;
            profileHeader.flags = static_cast<uint32_t>(profileDetail);
            profileHeader.cycleToUsDivisor = static_cast<uint32_t>(profileCycleDivisor);
            profileHeader.launchId = launchIndex;
            profileHeader.messageBytes = static_cast<uint64_t>(bs * topK * routeStride);
            profileHeader.opType = kProfileOpType;
            profileHeader.dataType = static_cast<uint32_t>(TileXR::TILEXR_DATA_TYPE_FP16);
            profileHeader.statsOffset = TileXR::TILEXR_PERF_TRACE_STATS_OFFSET;
            profileHeader.statsBytes = static_cast<uint64_t>(
                profileStatsCount * sizeof(TileXR::TileXRPerfCoreStageStats));
            void *profileSlice = static_cast<uint8_t *>(profileDev) + launchIndex * profileStrideBytes;
            if (!CheckAcl(aclrtMemcpy(profileSlice, sizeof(profileHeader), &profileHeader,
                    sizeof(profileHeader), ACL_MEMCPY_HOST_TO_DEVICE), "initialize profile header")) {
                return 1;
            }
        }
    }

    std::vector<int64_t> routePermutation(static_cast<std::size_t>(globalRouteCount));
    std::iota(routePermutation.begin(), routePermutation.end(), 0);
    std::mt19937_64 routeRng(routeSeed);
    std::shuffle(routePermutation.begin(), routePermutation.end(), routeRng);

    std::vector<uint16_t> expertOut(expertElements);
    std::vector<int32_t> assist(static_cast<std::size_t>(selfSendCnt) * kAssistInts, 0);
    std::vector<int64_t> routesPerPeer(static_cast<std::size_t>(rankSize), 0);
    int64_t selfRouteCount = 0;
    for (int64_t route = 0; route < selfSendCnt; ++route) {
        const int64_t globalRoute = routePermutation[static_cast<std::size_t>(globalRouteBegin + route)];
        const int32_t dstRank = static_cast<int32_t>(globalRoute / routesPerDestination);
        const int64_t destinationRoute = globalRoute % routesPerDestination;
        const int32_t token = static_cast<int32_t>(destinationRoute / topK);
        const int32_t topKId = static_cast<int32_t>(destinationRoute % topK);
        const std::size_t offset = static_cast<std::size_t>(route) * kAssistInts;
        assist[offset] = dstRank;
        assist[offset + 1] = token;
        assist[offset + 2] = topKId;
        assist[offset + 3] = 0;
        ++routesPerPeer[static_cast<std::size_t>(dstRank)];
        selfRouteCount += dstRank == rank ? 1 : 0;
        const uint16_t value = RankValue(TokenOwner(token, bs, rankSize));
        const auto begin = expertOut.begin() + static_cast<std::size_t>(route) * h;
        std::fill(begin, begin + h, value);
    }
    const auto peerRange = std::minmax_element(routesPerPeer.begin(), routesPerPeer.end());
    std::cout << "rank " << rank << " randomized routes: seed=" << routeSeed
              << " peerMin=" << *peerRange.first << " peerMax=" << *peerRange.second
              << " self=" << selfRouteCount << std::endl;
    std::vector<float> weights(static_cast<std::size_t>(bs) * topK, 1.0f / static_cast<float>(topK));
    if (!CheckAcl(aclrtMemcpy(expertOutDev, expertBytes, expertOut.data(), expertBytes,
            ACL_MEMCPY_HOST_TO_DEVICE), "copy expertOut") ||
        !CheckAcl(aclrtMemcpy(assistDev, assistBytes, assist.data(), assistBytes,
            ACL_MEMCPY_HOST_TO_DEVICE), "copy assist") ||
        !CheckAcl(aclrtMemcpy(weightsDev, weightBytes, weights.data(), weightBytes,
            ACL_MEMCPY_HOST_TO_DEVICE), "copy weights") ||
        !CheckAcl(aclrtMemset(workspaceDev, static_cast<std::size_t>(workspaceBytes), 0,
            static_cast<std::size_t>(workspaceBytes)), "memset workspace")) {
        return 1;
    }

    if (rankSize > 1) {
        if (!CheckTileXR(TileXRUDMARegister(resources.comm, static_cast<GM_ADDR>(workspaceDev),
                static_cast<std::size_t>(workspaceBytes), &resources.workspaceHandle),
                "TileXRUDMARegister workspace")) {
            return 1;
        }
        resources.workspaceRegistered = true;
    }

    TileXREp::EpUrmaCombineParams normalParams {};
    TileXREp::EpUrmaCombineLaunchContext normalContext {};
    if (!strictKernelLatency && !deviceEventLatency) {
        normalParams.expertOut = expertOutDev;
        normalParams.assistInfoForCombine = static_cast<int32_t *>(assistDev);
        normalParams.topKWeights = static_cast<float *>(weightsDev);
        normalParams.comm = resources.comm;
        normalParams.selfSendCnt = selfSendCnt;
        normalParams.bs = bs;
        normalParams.h = h;
        normalParams.topK = topK;
        normalParams.yOut = outputDev;
        normalParams.workspace = workspaceDev;
        normalParams.workspaceBytes = workspaceBytes;
        normalParams.perfTrace = profileEnabled ? profileDev : nullptr;
        normalParams.perfTraceBytes = profileEnabled ? static_cast<int64_t>(profileBytes) : 0;
        normalParams.dtype = TileXR::TILEXR_DATA_TYPE_FP16;
        normalParams.stream = resources.stream;
        if (!CheckTileXR(TileXREp::TileXREpValidateBasicUrmaCombineParams(normalParams),
                "TileXREpValidateBasicUrmaCombineParams normal") ||
            !CheckTileXR(TileXREp::TileXREpPrepareUrmaCombineLaunchContext(normalParams, &normalContext),
                "TileXREpPrepareUrmaCombineLaunchContext normal")) {
            return 1;
        }
    }

    TileXREp::EpUrmaCombineParams strictParams {};
    TileXREp::EpUrmaCombineLaunchContext strictContext {};
    if (strictKernelLatency) {
        strictParams.expertOut = expertOutDev;
        strictParams.assistInfoForCombine = static_cast<int32_t *>(assistDev);
        strictParams.topKWeights = static_cast<float *>(weightsDev);
        strictParams.comm = resources.comm;
        strictParams.selfSendCnt = selfSendCnt;
        strictParams.bs = bs;
        strictParams.h = h;
        strictParams.topK = topK;
        strictParams.yOut = outputDev;
        strictParams.workspace = workspaceDev;
        strictParams.workspaceBytes = workspaceBytes;
        strictParams.perfTrace = nullptr;
        strictParams.perfTraceBytes = 0;
        strictParams.strictKernelCycles = strictKernelCyclesDev;
        strictParams.strictKernelCyclesBytes = static_cast<int64_t>(kStrictKernelCyclesBytes);
        strictParams.dtype = TileXR::TILEXR_DATA_TYPE_FP16;
        strictParams.stream = resources.stream;
        if (!CheckTileXR(TileXREp::TileXREpValidateBasicUrmaCombineParams(strictParams),
                "TileXREpValidateBasicUrmaCombineParams strict") ||
            !CheckTileXR(TileXREp::TileXREpPrepareUrmaCombineLaunchContext(strictParams, &strictContext),
                "TileXREpPrepareUrmaCombineLaunchContext strict")) {
            return 1;
        }
    }

    std::vector<uint16_t> output(outputElements);
    std::vector<uint64_t> strictKernelCycles(kStrictKernelCycleCount, 0);
    auto prepareOutput = [&]() -> bool {
        return CheckAcl(aclrtMemset(outputDev, outputBytes, 0, outputBytes), "memset output");
    };
    auto enqueueCombine = [&](void *trace) -> bool {
        const int combineRet = trace == nullptr ?
            TileXRMoeEpCombineUrma(expertOutDev, static_cast<int32_t *>(assistDev),
                static_cast<float *>(weightsDev), resources.comm, selfSendCnt, bs, h, topK,
                outputDev, workspaceDev, workspaceBytes, TileXR::TILEXR_DATA_TYPE_FP16, resources.stream) :
            TileXRMoeEpCombineUrmaProfile(expertOutDev, static_cast<int32_t *>(assistDev),
                static_cast<float *>(weightsDev), resources.comm, selfSendCnt, bs, h, topK,
                outputDev, workspaceDev, workspaceBytes, trace, static_cast<int64_t>(profileBytes),
                TileXR::TILEXR_DATA_TYPE_FP16, resources.stream);
        return CheckTileXR(combineRet, trace == nullptr ?
            "TileXRMoeEpCombineUrma" : "TileXRMoeEpCombineUrmaProfile");
    };
    auto runCombine = [&](void *trace) -> bool {
        return prepareOutput() && enqueueCombine(trace) &&
            CheckAcl(aclrtSynchronizeStream(resources.stream), "aclrtSynchronizeStream");
    };
    TileXREp::EpUrmaCombineStartGateWindow startGateWindow;
    int64_t lastNormalMagic = 0;
    auto enqueuePreparedCombine = [&](void *trace, bool runStartGate) -> bool {
        int64_t magic = 0;
        if (!CheckTileXR(TileXRCommNextMagic(resources.comm, &magic), "TileXRCommNextMagic normal") ||
            !CheckAcl(aclrtMemsetAsync(outputDev, outputBytes, 0, outputBytes, resources.stream),
                "aclrtMemsetAsync output")) {
            return false;
        }
        normalParams.perfTrace = trace;
        normalParams.perfTraceBytes = trace == nullptr ? 0 : static_cast<int64_t>(profileBytes);
        const bool launched = CheckTileXR(TileXREp::TileXREpLaunchPreparedUrmaCombineKernel(
            normalParams, normalContext, magic, runStartGate),
            "TileXREpLaunchPreparedUrmaCombineKernel normal");
        if (launched) {
            lastNormalMagic = magic;
        }
        return launched;
    };
    auto measureCombineDevice = [&](double &elapsedUs) -> bool {
        aclrtEvent startEvent = nullptr;
        aclrtEvent endEvent = nullptr;
        auto destroyEvents = [&]() {
            if (startEvent != nullptr) {
                (void)aclrtDestroyEvent(startEvent);
            }
            if (endEvent != nullptr) {
                (void)aclrtDestroyEvent(endEvent);
            }
        };
        if (!CheckAcl(aclrtCreateEvent(&startEvent), "aclrtCreateEvent start") ||
            !CheckAcl(aclrtCreateEvent(&endEvent), "aclrtCreateEvent end") ||
            !prepareOutput() ||
            !CheckAcl(aclrtRecordEvent(startEvent, resources.stream), "aclrtRecordEvent start") ||
            !enqueueCombine(nullptr) ||
            !CheckAcl(aclrtRecordEvent(endEvent, resources.stream), "aclrtRecordEvent end") ||
            !CheckAcl(aclrtSynchronizeEvent(endEvent), "aclrtSynchronizeEvent end")) {
            destroyEvents();
            return false;
        }
        float elapsedMs = 0.0f;
        const bool elapsedOk = CheckAcl(
            aclrtEventElapsedTime(&elapsedMs, startEvent, endEvent), "aclrtEventElapsedTime");
        destroyEvents();
        if (!elapsedOk) {
            return false;
        }
        elapsedUs = static_cast<double>(elapsedMs) * 1000.0;
        return true;
    };
    auto runStrictKernelWarmup = [&]() -> bool {
        int64_t magic = 0;
        if (!CheckAcl(aclrtMemset(strictKernelCyclesDev, kStrictKernelCyclesBytes, 0,
                kStrictKernelCyclesBytes), "memset strict kernel cycles warmup") ||
            !prepareOutput() ||
            !CheckTileXR(TileXRCommNextMagic(resources.comm, &magic), "TileXRCommNextMagic strict warmup")) {
            return false;
        }
        if (!CheckTileXR(TileXREp::TileXREpLaunchPreparedUrmaCombineKernel(
                strictParams, strictContext, magic),
                "TileXREpLaunchPreparedUrmaCombineKernel strict warmup")) {
            return false;
        }
        return CheckAcl(aclrtSynchronizeStream(resources.stream), "aclrtSynchronizeStream strict warmup");
    };
    auto measureStrictKernel = [&](uint64_t &maxCycles, uint32_t &maxCore) -> bool {
        int64_t magic = 0;
        if (!CheckAcl(aclrtMemset(strictKernelCyclesDev, kStrictKernelCyclesBytes, 0,
                kStrictKernelCyclesBytes), "memset strict kernel cycles") ||
            !prepareOutput() ||
            !CheckTileXR(TileXRCommNextMagic(resources.comm, &magic), "TileXRCommNextMagic strict")) {
            return false;
        }
        if (!CheckTileXR(TileXREp::TileXREpLaunchPreparedUrmaCombineKernel(
                strictParams, strictContext, magic),
                "TileXREpLaunchPreparedUrmaCombineKernel strict") ||
            !CheckAcl(aclrtSynchronizeStream(resources.stream), "aclrtSynchronizeStream strict") ||
            !CheckAcl(aclrtMemcpy(strictKernelCycles.data(), kStrictKernelCyclesBytes,
                strictKernelCyclesDev, kStrictKernelCyclesBytes, ACL_MEMCPY_DEVICE_TO_HOST),
                "copy strict kernel cycles")) {
            return false;
        }
        maxCycles = 0;
        maxCore = 0;
        for (std::size_t core = 0; core < strictKernelCycles.size(); ++core) {
            const uint64_t cycles = strictKernelCycles[core];
            if (cycles == 0) {
                std::cerr << "rank " << rank << " strict kernel cycle sample missing for core "
                          << core << std::endl;
                return false;
            }
            if (cycles > maxCycles) {
                maxCycles = cycles;
                maxCore = static_cast<uint32_t>(core);
            }
        }
        return true;
    };

    if (strictKernelLatency || deviceEventLatency) {
        for (int64_t warmup = 0; warmup < warmupRounds; ++warmup) {
            const bool warmupOk = strictKernelLatency ? runStrictKernelWarmup() : runCombine(nullptr);
            if (!warmupOk) {
                return 1;
            }
        }
    } else {
        int64_t queuedWarmups = 0;
        for (int64_t warmup = 0; warmup < warmupRounds; ++warmup) {
            const bool firstAfterSynchronization = startGateWindow.BeginLaunch();
            const bool runStartGate = TileXREp::kEpUrmaCombineStartGate && firstAfterSynchronization;
            if (!enqueuePreparedCombine(nullptr, runStartGate)) {
                return 1;
            }
            ++queuedWarmups;
            if (queuedWarmups == enqueueWindow || warmup + 1 == warmupRounds) {
                if (!CheckAcl(aclrtSynchronizeStream(resources.stream),
                        "aclrtSynchronizeStream warmup window")) {
                    return 1;
                }
                startGateWindow.StreamSynchronized();
                queuedWarmups = 0;
            }
        }
    }

    std::vector<double> profileHostRoundUs(profileSampleCount, 0.0);
    double unprofiledRoundUs = 0.0;
    int64_t unprofiledRoundCount = 0;
    const auto start = std::chrono::steady_clock::now();
    int64_t windowBeginRound = 0;
    auto windowStart = start;
    auto isProfileRound = [&](int64_t round) {
        return profileEnabled && round >= profileRound && round - profileRound < profileSamples;
    };
    for (int64_t round = 0; round < rounds; ++round) {
        const bool profileThisRound = isProfileRound(round);
        const std::size_t profileSampleIndex = profileThisRound ?
            static_cast<std::size_t>(round - profileRound) : 0;
        void *profileSlice = profileThisRound ?
            static_cast<uint8_t *>(profileDev) + profileSampleIndex * profileStrideBytes : nullptr;
        const auto roundStart = std::chrono::steady_clock::now();
        const bool validate = round % validateEvery == 0 || round + 1 == rounds;
        if (!strictKernelLatency && !deviceEventLatency) {
            if (round == windowBeginRound) {
                windowStart = roundStart;
            }
            const bool firstAfterSynchronization = startGateWindow.BeginLaunch();
            const bool runStartGate = TileXREp::kEpUrmaCombineStartGate && firstAfterSynchronization;
            if (profileThisRound) {
                profileStartGateExecuted[profileSampleIndex] = runStartGate;
            }
            if (!enqueuePreparedCombine(profileSlice, runStartGate)) {
                return 1;
            }
            const bool windowFull = round - windowBeginRound + 1 >= enqueueWindow;
            const bool profileBoundary = round + 1 < rounds &&
                isProfileRound(round + 1) != profileThisRound;
            if (!windowFull && !validate && !profileBoundary) {
                continue;
            }
            if (!CheckAcl(aclrtSynchronizeStream(resources.stream),
                    "aclrtSynchronizeStream enqueue window")) {
                return 1;
            }
            startGateWindow.StreamSynchronized();
            if (!WriteWorkspaceDebugSnapshot(workspaceDebugRoot, rank, round, lastNormalMagic,
                    normalContext.workspace, workspaceDev, workspaceBytes, outputDev, outputBytes)) {
                return 1;
            }
            const int64_t windowRoundCount = round - windowBeginRound + 1;
            const double windowUs = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - windowStart).count();
            const double amortizedRoundUs = windowUs / static_cast<double>(windowRoundCount);
            for (int64_t accountedRound = windowBeginRound; accountedRound <= round; ++accountedRound) {
                if (isProfileRound(accountedRound)) {
                    const std::size_t sampleIndex = static_cast<std::size_t>(accountedRound - profileRound);
                    profileHostRoundUs[sampleIndex] = amortizedRoundUs;
                } else {
                    unprofiledRoundUs += amortizedRoundUs;
                    ++unprofiledRoundCount;
                }
            }
            if (validate && (!CheckAcl(aclrtMemcpy(output.data(), outputBytes, outputDev, outputBytes,
                    ACL_MEMCPY_DEVICE_TO_HOST), "copy output") ||
                    !ValidateOutput(rank, rankSize, round, bs, h, output))) {
                return 1;
            }
            if (validate) {
                std::cout << "rank " << rank << " round " << round
                          << " URMA combine validation PASS" << std::endl;
            }
            windowBeginRound = round + 1;
            continue;
        }
        double deviceElapsedUs = 0.0;
        uint64_t strictMaxCycles = 0;
        uint32_t strictMaxCore = 0;
        const bool roundOk = strictKernelLatency ? measureStrictKernel(strictMaxCycles, strictMaxCore) :
            (deviceEventLatency ? measureCombineDevice(deviceElapsedUs) :
                runCombine(profileSlice));
        if (!roundOk) {
            return 1;
        }
        if (deviceEventLatency) {
            std::ostringstream sample;
            sample << std::fixed << std::setprecision(6)
                   << "DEVICE_EVENT_LATENCY rank=" << rank << " round=" << round
                   << " elapsed_us=" << deviceElapsedUs;
            std::cout << sample.str() << std::endl;
        }
        if (strictKernelLatency) {
            deviceElapsedUs = static_cast<double>(strictMaxCycles) / kStrictKernelCyclesPerUs;
            std::ostringstream sample;
            sample << std::fixed << std::setprecision(6)
                   << "STRICT_KERNEL_LATENCY rank=" << rank << " round=" << round
                   << " max_core=" << strictMaxCore << " max_cycles=" << strictMaxCycles
                   << " elapsed_us=" << deviceElapsedUs;
            std::cout << sample.str() << std::endl;
        }
        const double roundUs = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - roundStart).count();
        if (profileThisRound) {
            profileHostRoundUs[profileSampleIndex] = roundUs;
        } else {
            unprofiledRoundUs += roundUs;
            ++unprofiledRoundCount;
        }
        if (validate && (!CheckAcl(aclrtMemcpy(output.data(), outputBytes, outputDev, outputBytes,
                ACL_MEMCPY_DEVICE_TO_HOST), "copy output") ||
                !ValidateOutput(rank, rankSize, round, bs, h, output))) {
            return 1;
        }
        if (validate) {
            std::cout << "rank " << rank << " round " << round << " URMA combine validation PASS" << std::endl;
        }
    }
    const double elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "rank " << rank << " completed " << rounds << " rounds in " << elapsedMs
              << " ms, average " << elapsedMs / static_cast<double>(rounds) << " ms/round" << std::endl;

    if (profileEnabled) {
        const double baselineUs = unprofiledRoundCount == 0 ? 0.0 :
            unprofiledRoundUs / static_cast<double>(unprofiledRoundCount);
        std::vector<uint8_t> profileHostBytes(profileAllocationBytes);
        if (!CheckAcl(aclrtMemcpy(profileHostBytes.data(), profileAllocationBytes,
                profileDev, profileAllocationBytes, ACL_MEMCPY_DEVICE_TO_HOST),
                "copy profile buffers")) {
            return 1;
        }
        for (std::size_t launchIndex = 0; launchIndex < profileSampleCount; ++launchIndex) {
            const uint8_t *profileSlice = profileHostBytes.data() + launchIndex * profileStrideBytes;
            TileXR::TileXRPerfTraceHeader copiedHeader {};
            std::memcpy(&copiedHeader, profileSlice, sizeof(copiedHeader));
            if (copiedHeader.launchId != launchIndex ||
                copiedHeader.statsOffset != TileXR::TILEXR_PERF_TRACE_STATS_OFFSET ||
                copiedHeader.statsBytes != profileHeaders[launchIndex].statsBytes) {
                std::cerr << "profile trace header mismatch for launch" << launchIndex << std::endl;
                return 1;
            }
            profileHeaders[launchIndex] = copiedHeader;
            std::memcpy(profileStats[launchIndex].data(), profileSlice + copiedHeader.statsOffset,
                static_cast<std::size_t>(copiedHeader.statsBytes));
            const bool hasKernelTotal = std::any_of(
                profileStats[launchIndex].begin(), profileStats[launchIndex].end(),
                [rank](const TileXR::TileXRPerfCoreStageStats &stat) {
                    return stat.rank == static_cast<uint32_t>(rank) && stat.stageId == 0 && stat.count > 0;
                });
            if (!hasKernelTotal) {
                std::cerr << "profile trace launch" << launchIndex
                          << " contains no kernel_total sample; rebuild with "
                          << "TILEXR_EP_ENABLE_PROFILING=ON and verify the trace header" << std::endl;
                return 1;
            }
            if (!WriteProfileTrace(profileRoot, rank, rankSize, launchIndex, bs, h, topK,
                    selfSendCnt, routeStride, routeSeed, enqueueWindow,
                    profileStartGateExecuted[launchIndex],
                    profileHostRoundUs[launchIndex], baselineUs,
                    profileHeaders[launchIndex], profileStats[launchIndex])) {
                return 1;
            }
            std::cout << "rank " << rank << " profile round " << profileRound + launchIndex
                      << " host=" << profileHostRoundUs[launchIndex]
                      << " us, unprofiled mean=" << baselineUs << " us, trace=" << profileRoot
                      << "/rank" << rank << "/launch" << launchIndex << "/trace.json" << std::endl;
        }
    }

    return 0;
}
