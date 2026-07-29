#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#ifdef TILEXR_SOURCE_ROOT
const char *kSourceRoot = TILEXR_SOURCE_ROOT;
#else
const char *kSourceRoot = ".";
#endif

std::string JoinPath(const std::string &base, const std::string &path)
{
    if (base.empty() || base[base.size() - 1] == '/') {
        return base + path;
    }
    return base + "/" + path;
}

bool ReadFile(const std::string &relativePath, std::string *contents)
{
    const std::string fullPath = JoinPath(kSourceRoot, relativePath);
    std::ifstream stream(fullPath.c_str());
    if (!stream.is_open()) {
        std::cerr << "missing file: " << relativePath << std::endl;
        ++g_failures;
        return false;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    *contents = buffer.str();
    return true;
}

void CheckContains(const std::string &label, const std::string &contents, const std::string &needle)
{
    if (contents.find(needle) == std::string::npos) {
        std::cerr << label << " missing: " << needle << std::endl;
        ++g_failures;
    }
}

void CheckNotContains(const std::string &label, const std::string &contents, const std::string &needle)
{
    if (contents.find(needle) != std::string::npos) {
        std::cerr << label << " contains forbidden string: " << needle << std::endl;
        ++g_failures;
    }
}

void TestPublicHeader()
{
    std::string contents;
    if (!ReadFile("src/include/tilexr_ep.h", &contents)) {
        return;
    }

    CheckContains("src/include/tilexr_ep.h", contents, "#ifdef __cplusplus");
    CheckContains("src/include/tilexr_ep.h", contents, "extern \"C\"");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpDispatch(");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpDispatchV2(");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpCombine(");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpCombineV2(");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpCombineUrma(");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpCombineUrmaProfile(");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpCombineUrmaGetWorkspaceSize(");
    CheckContains("src/include/tilexr_ep.h", contents, "int TileXRMoeEpCombineUrmaGetProfileSize(");
    CheckContains("src/include/tilexr_ep.h", contents, "int64_t perfTraceBytes");
    CheckContains("src/include/tilexr_ep.h", contents, "TileXRCommPtr comm");
    CheckContains("src/include/tilexr_ep.h", contents, "TileXR::TileXRDataType dtype");
    CheckContains("src/include/tilexr_ep.h", contents, "aclrtStream stream");
    CheckContains("src/include/tilexr_ep.h", contents, "xActiveMask");
    CheckContains("src/include/tilexr_ep.h", contents, "dynamicScalesOut");
    CheckContains("src/include/tilexr_ep.h", contents, "tpRecvCountsOut");
    CheckContains("src/include/tilexr_ep.h", contents, "expandScalesOut");
    CheckContains("src/include/tilexr_ep.h", contents, "workspace");
    CheckContains("src/include/tilexr_ep.h", contents, "quantMode");
    CheckContains("src/include/tilexr_ep.h", contents, "tpWorldSize");
    CheckContains("src/include/tilexr_ep.h", contents, "sharedExpertNum");

    std::string apiContents;
    if (ReadFile("src/include/tilexr_api.h", &apiContents)) {
        CheckContains("src/include/tilexr_api.h", apiContents, "TileXRGetUDMARegistryHost");
    }
}

void TestBuildPlacement()
{
    std::string rootCmake;
    if (ReadFile("CMakeLists.txt", &rootCmake)) {
        CheckContains("CMakeLists.txt", rootCmake,
            "option(TILEXR_BUILD_EP \"Build TileXR EP communication library\" OFF)");
        CheckContains("CMakeLists.txt", rootCmake, "TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT");
        CheckContains("CMakeLists.txt", rootCmake, "TILEXR_EP_URMA_DOORBELL_BATCH_SIZE");
        CheckContains("CMakeLists.txt", rootCmake,
            "TILEXR_EP_URMA_DOORBELL_BATCH_SIZE must be 1, 2, 4, or 8");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_TX_READY_BATCH_SIZE 1 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "TILEXR_EP_URMA_TX_READY_BATCH_SIZE must be 1, 2, or 4");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_TX_READY_SHARED_FLAG 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "TILEXR_EP_URMA_TX_READY_SHARED_FLAG must be 0 or 1");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_TX_READY_IN_DATA 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "TILEXR_EP_URMA_TX_READY_IN_DATA must be 0 or 1");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_TX_META_PREFETCH_FULL 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "TILEXR_EP_URMA_TX_META_PREFETCH_FULL must be 0 or 1");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_RX_READY_STICKY_MASK 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_RX_READY_BATCH_MTE2 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_RX_READY_BATCH_VECTOR 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake, "TILEXR_EP_URMA_RX_SCHEDULER");
        CheckContains("CMakeLists.txt", rootCmake, "TILEXR_EP_URMA_START_GATE");
        CheckContains("CMakeLists.txt", rootCmake,
            "set(TILEXR_EP_URMA_QDC_VERSION 0 CACHE STRING");
        CheckContains("CMakeLists.txt", rootCmake,
            "TILEXR_EP_URMA_QDC_VERSION must be 0, 1, 2, or 3");
        CheckContains("CMakeLists.txt", rootCmake, "TILEXR_UDMA_QP_COUNT");
        CheckContains("CMakeLists.txt", rootCmake,
            "TILEXR_UDMA_QP_COUNT LESS TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT");
        CheckContains("CMakeLists.txt", rootCmake, "add_subdirectory(src/ep)");
    }

    std::string epCmake;
    if (ReadFile("src/ep/CMakeLists.txt", &epCmake)) {
        CheckContains("src/ep/CMakeLists.txt", epCmake, "add_library(tilexr-ep SHARED");
        CheckContains("src/ep/CMakeLists.txt", epCmake, "tile-comm");
        CheckContains("src/ep/CMakeLists.txt", epCmake, "libtilexr_ep_combine_kernel.so");
        CheckContains("src/ep/CMakeLists.txt", epCmake, "libtilexr_ep_urma_combine_kernel.so");
        CheckContains("src/ep/CMakeLists.txt", epCmake, "TILEXR_EP_ENABLE_PROFILING");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_DOORBELL_BATCH_SIZE=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_TX_READY_BATCH_SIZE=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_TX_READY_SHARED_FLAG=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_TX_READY_IN_DATA=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_TX_META_PREFETCH_FULL=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_TX_READY_EARLY_PUBLISH=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_RX_READY_STICKY_MASK=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_RX_READY_BATCH_MTE2=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_RX_READY_BATCH_VECTOR=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "TILEXR_EP_URMA_TX_META_PREFETCH_FULL requires an Ascend950-class target");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_START_GATE=");
        CheckContains("src/ep/CMakeLists.txt", epCmake,
            "-DTILEXR_EP_URMA_QDC_VERSION=");
        CheckContains("src/ep/CMakeLists.txt", epCmake, "tilexr_ep.h");
        CheckContains("src/ep/CMakeLists.txt", epCmake, "install(TARGETS tilexr-ep");
    }

    std::string variantBuilder;
    const std::string variantBuilderPath = "tests/ep/demo/build_tilexr_ep_urma_combine_variant.sh";
    if (ReadFile(variantBuilderPath, &variantBuilder)) {
        CheckContains(variantBuilderPath, variantBuilder,
            "qdc_version=\"${TILEXR_EP_URMA_QDC_VERSION_BUILD:-0}\"");
        CheckContains(variantBuilderPath, variantBuilder, "^(0|1|2|3)$");
        CheckContains(variantBuilderPath, variantBuilder, "TILEXR_VARIANT_QDC_VERSION=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_TX_READY_BATCH_SIZE_BUILD");
        CheckContains(variantBuilderPath, variantBuilder, "TILEXR_VARIANT_TX_READY_BATCH=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_TX_READY_SHARED_FLAG_BUILD");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_VARIANT_TX_READY_SHARED_FLAG=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_TX_READY_IN_DATA_BUILD");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_VARIANT_TX_READY_IN_DATA=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_TX_META_PREFETCH_FULL_BUILD");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_VARIANT_TX_META_PREFETCH_FULL=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH_BUILD");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_VARIANT_TX_READY_EARLY_PUBLISH=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_RX_READY_STICKY_MASK_BUILD");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_VARIANT_RX_READY_STICKY_MASK=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_RX_READY_BATCH_MTE2_BUILD");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_VARIANT_RX_READY_BATCH_MTE2=");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_EP_URMA_RX_READY_BATCH_VECTOR_BUILD");
        CheckContains(variantBuilderPath, variantBuilder,
            "TILEXR_VARIANT_RX_READY_BATCH_VECTOR=");
        CheckContains(variantBuilderPath, variantBuilder, "TILEXR_VARIANT_SOURCE_SHA256=");
    }

    std::string epDemoCmake;
    const std::string epDemoCmakePath = "tests/ep/CMakeLists.txt";
    if (ReadFile(epDemoCmakePath, &epDemoCmake)) {
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_QDC_VERSION 0 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "TILEXR_EP_URMA_QDC_VERSION must be 0, 1, 2, or 3");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_TX_READY_BATCH_SIZE 1 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_TX_READY_SHARED_FLAG 0 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_TX_READY_IN_DATA 0 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_TX_META_PREFETCH_FULL 0 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH 0 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_RX_READY_STICKY_MASK 0 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_RX_READY_BATCH_MTE2 0 CACHE STRING");
        CheckContains(epDemoCmakePath, epDemoCmake,
            "set(TILEXR_EP_URMA_RX_READY_BATCH_VECTOR 0 CACHE STRING");
    }
}

void TestEpHostChecksRegisteredWorkspace()
{
    std::string launchContext;
    if (!ReadFile("src/ep/host/ep_launch_context.cpp", &launchContext)) {
        return;
    }

    CheckContains("src/ep/host/ep_launch_context.cpp", launchContext, "ValidateRegisteredWorkspace");
    CheckContains("src/ep/host/ep_launch_context.cpp", launchContext, "TileXRGetUDMARegistryHost");
    CheckContains("src/ep/host/ep_launch_context.cpp", launchContext, "UDMARegionContains");
    CheckContains("src/ep/host/ep_launch_context.cpp", launchContext, "TileXREpUdmaRequiredWorkspaceBytes");
}

void TestEpSocDefaultFollowsEnvironment()
{
    std::string epCmake;
    if (!ReadFile("src/ep/CMakeLists.txt", &epCmake)) {
        return;
    }

    CheckContains("src/ep/CMakeLists.txt", epCmake, "$ENV{TILEXR_SOC_NAME}");
    CheckContains("src/ep/CMakeLists.txt", epCmake, "string(TOLOWER");
    CheckContains("src/ep/CMakeLists.txt", epCmake, "ascend910b");
    CheckContains("src/ep/CMakeLists.txt", epCmake, "dav-c220-vec");
    CheckContains("src/ep/CMakeLists.txt", epCmake, "Ascend910B");
}

void TestChipMapRecognizesAscend950Dt9582()
{
    std::string internal;
    if (!ReadFile("src/comm/tilexr_internal.cpp", &internal)) {
        return;
    }

    CheckContains("src/comm/tilexr_internal.cpp", internal, "\"Ascend950DT_9582\", ChipName::CHIP_950");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "\"Ascend950PR_9599\", ChipName::CHIP_950PR");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "bool UseLegacyIpcPid(ChipName chipName)");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "TILEXR_IPC_MODE");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "std::strcmp(mode, \"legacy\")");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "std::strcmp(mode, \"superpod\")");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "bool UseUdmaOnlyIpc()");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "std::strcmp(mode, \"udma-only\")");
    CheckContains("src/comm/tilexr_internal.cpp", internal, "chipName == ChipName::CHIP_950PR");

    std::string comm;
    if (ReadFile("src/comm/tilexr_comm.cpp", &comm)) {
        CheckContains("src/comm/tilexr_comm.cpp", comm, "if (UseUdmaOnlyIpc())");
        CheckContains("src/comm/tilexr_comm.cpp", comm, "peer IPC setup skipped");
        CheckContains("src/comm/tilexr_comm.cpp", comm, "const int localRankBegin = rank_ - localRank_");
        CheckContains("src/comm/tilexr_comm.cpp", comm, "std::set<int> enabledDevices");
    }
}

void TestEpKernelUsesCceArchFlags()
{
    std::string epCmake;
    if (!ReadFile("src/ep/CMakeLists.txt", &epCmake)) {
        return;
    }

    CheckContains("src/ep/CMakeLists.txt", epCmake, "-xcce");
    CheckContains("src/ep/CMakeLists.txt", epCmake, "${TILEXR_EP_AICORE_ARCH}");
    CheckContains("src/ep/CMakeLists.txt", epCmake, "set(TILEXR_EP_KERNEL_LINK_OPTIONS ${TILEXR_EP_AICORE_ARCH})");
    CheckNotContains("src/ep/CMakeLists.txt", epCmake, "-xasc");
    CheckNotContains("src/ep/CMakeLists.txt", epCmake, "--npu-arch=");
    CheckNotContains("src/ep/CMakeLists.txt", epCmake, "--cce-auto-infer-kernel-type=false");
    CheckNotContains("src/ep/CMakeLists.txt", epCmake, "--cce-fatobj-link");
}

void TestSyncFlagBuffersUseFullFlagUnit()
{
    std::string syncHeader;
    if (!ReadFile("src/include/tilexr_sync.h", &syncHeader)) {
        return;
    }

    CheckContains("src/include/tilexr_sync.h", syncHeader,
        "LocalTensor<int64_t> localSet = tBuf.GetWithOffset<int64_t>(FLAG_UNIT_INT_NUM, 0);");
    CheckContains("src/include/tilexr_sync.h", syncHeader,
        "LocalTensor<int64_t> localWait = tBuf.GetWithOffset<int64_t>(FLAG_UNIT_INT_NUM, 0);");
}

const char *kRemoteDeployScript = "tests/ep/demo/deploy_and_run_remote.sh";

void TestRemoteDeployScriptCleansRemoteCheckout()
{
    std::string deployScript;
    if (!ReadFile(kRemoteDeployScript, &deployScript)) {
        return;
    }

    CheckContains(kRemoteDeployScript, deployScript, "case \"\\${remote_repo}\" in");
    CheckContains(kRemoteDeployScript, deployScript, "Refusing to clean unexpected remote repo");
    CheckContains(kRemoteDeployScript, deployScript, "rm -rf -- \"\\${remote_repo}\"");
    CheckContains(kRemoteDeployScript, deployScript, "mkdir -p -- \"\\${remote_repo}\"");
}

void TestRemoteDeployScriptInitializesEpSubmodulesOnly()
{
    std::string deployScript;
    if (!ReadFile(kRemoteDeployScript, &deployScript)) {
        return;
    }

    CheckContains(kRemoteDeployScript, deployScript,
        "submodule update --init 3rdparty/hcomm 3rdparty/ops-transformer");
    CheckNotContains(kRemoteDeployScript, deployScript, "submodule update --init --recursive");
    CheckNotContains(kRemoteDeployScript, deployScript, "3rdparty/shmem");
    CheckNotContains(kRemoteDeployScript, deployScript, "3rdparty/spdlog");
}

void TestRemoteDeployScriptDoesNotExposePrivateRemoteDefaults()
{
    std::string deployScript;
    if (!ReadFile(kRemoteDeployScript, &deployScript)) {
        return;
    }

    CheckContains(kRemoteDeployScript, deployScript, "TILEXR_EP_REMOTE:?set TILEXR_EP_REMOTE");
    CheckContains(kRemoteDeployScript, deployScript, "TILEXR_EP_REMOTE_BASE:?set TILEXR_EP_REMOTE_BASE");
    CheckNotContains(kRemoteDeployScript, deployScript, "TILEXR_EP_REMOTE:-");
    CheckNotContains(kRemoteDeployScript, deployScript, "TILEXR_EP_REMOTE_BASE:-");
    CheckNotContains(kRemoteDeployScript, deployScript, "REMOTE_BASE=/");
}

void TestDemoRunnerUsesLibAndLib64Paths()
{
    std::string runner;
    if (!ReadFile("tests/ep/demo/run_tilexr_ep_dispatch_demo.sh", &runner)) {
        return;
    }

    CheckContains("tests/ep/demo/run_tilexr_ep_dispatch_demo.sh", runner, "${TILEXR_ROOT}/install/lib64");
    CheckContains("tests/ep/demo/run_tilexr_ep_dispatch_demo.sh", runner, "${TILEXR_ROOT}/install/lib");
    CheckContains("tests/ep/demo/run_tilexr_ep_dispatch_demo.sh", runner, "${INSTALL_DIR}/lib64");
    CheckContains("tests/ep/demo/run_tilexr_ep_dispatch_demo.sh", runner, "${INSTALL_DIR}/lib");
}

void TestDispatchDemoRegistersAlignedUdmaWorkspace()
{
    std::string demo;
    if (!ReadFile("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", &demo)) {
        return;
    }

    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "AlignAddress");
    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "rawWorkspaceDev");
    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo,
        "workspaceDev = reinterpret_cast<void *>(AlignAddress(");
    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo,
        "TileXRUDMARegister(comm, static_cast<GM_ADDR>(workspaceDev), workspaceBytes");
    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "EpRequiredWorkspaceBytes");
}

void TestUrmaCombineDemoReusesRegisteredWorkspace()
{
    std::string demo;
    const std::string path = "tests/ep/demo/tilexr_ep_urma_combine_demo.cpp";
    if (!ReadFile(path, &demo)) {
        return;
    }

    CheckContains(path, demo, "TileXRMoeEpCombineUrmaGetWorkspaceSize");
    CheckContains(path, demo, "TileXRMoeEpCombineUrmaGetProfileSize");
    CheckContains(path, demo, "AlignAddress");
    CheckContains(path, demo, "TileXRUDMARegister");
    CheckContains(path, demo, "if (rankSize > 1)");
    CheckContains(path, demo, "TILEXR_DEMO_BS");
    CheckContains(path, demo, "TILEXR_DEMO_H");
    CheckContains(path, demo, "TILEXR_DEMO_TOPK");
    CheckContains(path, demo, "TILEXR_DEMO_ROUNDS");
    CheckContains(path, demo, "TILEXR_DEMO_VALIDATE_EVERY");
    CheckContains(path, demo, "TILEXR_DEMO_ENQUEUE_WINDOW");
    CheckContains(path, demo, "TILEXR_DEMO_PROFILE_DIR");
    CheckContains(path, demo, "TILEXR_DEMO_PROFILE_SAMPLES");
    CheckContains(path, demo, "TILEXR_DEMO_WARMUP_ROUNDS");
    CheckContains(path, demo, "rankSize < 1");
    CheckNotContains(path, demo, "rankSize < 2");
    CheckContains(path, demo, "TokenBegin");
    CheckContains(path, demo, "TILEXR_DEMO_ROUTE_SEED");
    CheckContains(path, demo, "std::shuffle");
    CheckContains(path, demo, "randomized routes");
    CheckContains(path, demo, "TokenOwner");
    CheckContains(path, demo, "localTokenCount * rankSize * topK");
    CheckNotContains(path, demo, "bs % rankSize");
    CheckContains(path, demo, "for (int64_t round = 0; round < rounds; ++round)");
    CheckContains(path, demo, "TileXRMoeEpCombineUrma(");
    CheckContains(path, demo, "TileXRMoeEpCombineUrmaProfile(");
    CheckContains(path, demo, "round - profileRound < profileSamples");
    CheckContains(path, demo, "aclrtMemsetAsync(outputDev, outputBytes, 0, outputBytes, resources.stream)");
    CheckContains(path, demo, "TileXREpPrepareUrmaCombineLaunchContext(normalParams, &normalContext)");
    CheckContains(path, demo, "TileXRCommNextMagic(resources.comm, &magic)");
    CheckContains(path, demo, "EpUrmaCombineStartGateWindow startGateWindow");
    CheckContains(path, demo, "startGateWindow.BeginLaunch()");
    CheckContains(path, demo, "startGateWindow.StreamSynchronized()");
    CheckContains(path, demo, "normalParams, normalContext, magic, runStartGate");
    CheckContains(path, demo, "queuedWarmups == enqueueWindow");
    CheckContains(path, demo, "!windowFull && !validate && !profileBoundary");
    CheckContains(path, demo, "windowBeginRound = round + 1");
    CheckContains(path, demo, "(void)aclrtSynchronizeStream(stream);");
    CheckContains(path, demo, "enqueueWindow > 1 && (deviceEventLatency || strictKernelLatency)");
    CheckContains(path, demo, "profileStrideBytes = AlignSize(profileBytes, kProfileBufferAlignment)");
    CheckContains(path, demo, "profileAllocationBytes = profileStrideBytes * profileSampleCount");
    CheckContains(path, demo, "copy profile buffers");
    CheckContains(path, demo, "(\"launch\" + std::to_string(launchIndex))");
    CheckContains(path, demo, "\\\"start_gate\\\": ");
    CheckContains(path, demo, "\\\"start_gate_policy\\\": \\\"");
    CheckContains(path, demo, "first_after_stream_synchronize");
    CheckContains(path, demo, "\\\"start_gate_executed\\\": ");
    CheckContains(path, demo, "\\\"qdc_version\\\": ");
    CheckContains(path, demo, "\\\"tx_ready_batch_size\\\": ");
    CheckContains(path, demo, "\\\"tx_ready_shared_flag\\\": ");
    CheckContains(path, demo, "\\\"tx_ready_in_data\\\": ");
    CheckContains(path, demo, "\\\"tx_meta_prefetch_full\\\": ");
    CheckContains(path, demo, "\\\"enqueue_window\\\": ");
    CheckContains(path, demo,
        "\\\"kernel_timing_boundary\\\": \\\"pipe_all_bracketed_pre_flush\\\"");
    CheckContains(path, demo, "\\\"profile_scope\\\": \\\"");
    CheckContains(path, demo, "kernel_total_only");
    CheckContains(path, demo, "profileDetail < 0 || profileDetail > 2");
    CheckContains(path, demo, "hasKernelTotal");
    CheckContains(path, demo, "tilexr_ep_urma_combine_profile.h");
    CheckContains(path, demo, "kDefaultHidden = 512");

    const std::size_t cleanupSync = demo.find("(void)aclrtSynchronizeStream(stream);");
    const std::size_t cleanupUnregister = demo.find("(void)TileXRUDMAUnregister(comm, workspaceHandle);");
    if (cleanupSync == std::string::npos || cleanupUnregister == std::string::npos ||
        cleanupSync >= cleanupUnregister) {
        std::cerr << path << " must drain the stream before unregistering the workspace" << std::endl;
        ++g_failures;
    }
}

void TestUrmaCombineProfileApiIsSafe()
{
    const std::string path = "src/ep/host/tilexr_ep_urma_combine.cpp";
    std::string contents;
    if (ReadFile(path, &contents)) {
        CheckContains(path, contents, "TileXREpGetUrmaCombineProfileSize");
        CheckContains(path, contents, "perfTraceBytes");
        CheckContains(path, contents, "TILEXR_EP_ENABLE_PROFILING");
        CheckContains(path, contents, "TILEXR_ERROR_NOT_SUPPORT");
    }

    const std::string hostPath = "src/ep/host/ep_urma_combine_host.cpp";
    std::string host;
    if (ReadFile(hostPath, &host)) {
        CheckContains(hostPath, host, "params.perfTraceBytes < requiredProfileBytes");
        CheckContains(hostPath, host, "params.perfTrace) % 32");
        CheckContains(hostPath, host, "context.workspace.startGateOffset");
        CheckContains(hostPath, host, "runStartGate ? 1 : 0");
    }
}

void TestDispatchDemoUsesHostBarrierBeforeValidation()
{
    std::string demo;
    if (!ReadFile("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", &demo)) {
        return;
    }

    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "DemoBarrierAll");
    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "TILEXR_DEMO_BARRIER_ADDR");
    CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "dispatch synchronized");
}

void TestNoForbiddenDependencies()
{
    const std::vector<std::string> paths = {
        "src/include/tilexr_ep.h",
        "src/ep/CMakeLists.txt",
        "src/ep/host/ep_layout.h",
        "src/ep/host/ep_layout.cpp",
    };
    const std::vector<std::string> forbidden = {
        "src/mc2",
        "3rdparty/ops-transformer",
        "GetHcclContext",
        "TileXRUDMARegister",
        "UDMAPut",
        "shmem",
    };

    for (std::vector<std::string>::const_iterator path = paths.begin(); path != paths.end(); ++path) {
        std::string contents;
        if (!ReadFile(*path, &contents)) {
            continue;
        }
        for (std::vector<std::string>::const_iterator needle = forbidden.begin(); needle != forbidden.end(); ++needle) {
            CheckNotContains(*path, contents, *needle);
        }
    }
}

} // namespace

int main()
{
    TestPublicHeader();
    TestBuildPlacement();
    TestEpHostChecksRegisteredWorkspace();
    TestEpSocDefaultFollowsEnvironment();
    TestChipMapRecognizesAscend950Dt9582();
    TestEpKernelUsesCceArchFlags();
    TestSyncFlagBuffersUseFullFlagUnit();
    TestRemoteDeployScriptCleansRemoteCheckout();
    TestRemoteDeployScriptInitializesEpSubmodulesOnly();
    TestRemoteDeployScriptDoesNotExposePrivateRemoteDefaults();
    TestDemoRunnerUsesLibAndLib64Paths();
    TestDispatchDemoRegistersAlignedUdmaWorkspace();
    TestUrmaCombineDemoReusesRegisteredWorkspace();
    TestUrmaCombineProfileApiIsSafe();
    TestDispatchDemoUsesHostBarrierBeforeValidation();
    TestNoForbiddenDependencies();
    return g_failures == 0 ? 0 : 1;
}
