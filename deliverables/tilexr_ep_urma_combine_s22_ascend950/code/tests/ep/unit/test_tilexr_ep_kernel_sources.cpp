#include <fstream>
#include <cstdint>
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

void TestKernelUsesTileXRPeerMemory()
{
    const std::string path = "src/ep/kernels/tilexr_ep_dispatch_kernel.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "extern \"C\" __global__ __aicore__ void tilexr_ep_dispatch_kernel");
    CheckContains(path, contents, "launch_tilexr_ep_dispatch_kernel");
    CheckContains(path, contents, "CommArgs");
    CheckContains(path, contents, "peerMems");
    CheckContains(path, contents, "GlobalTensor<GM_ADDR> peerMems");
    CheckContains(path, contents, "peerMems.GetValue(peer)");
    CheckContains(path, contents, "IPC_DATA_OFFSET");
    CheckContains(path, contents, "SyncCollectives");
    CheckContains(path, contents, "DataCopyPad");
    CheckContains(path, contents, "kEpStepWindowCleared");
    CheckContains(path, contents, "kEpStepDispatchReady");
    CheckContains(path, contents, "kEpStepDispatchDrained");
    CheckContains(path, contents, "LoadInt32FromGm");
    CheckContains(path, contents, "LoadAssistTupleFromGm");
    CheckContains(path, contents, "StoreWindowHeader");
    CheckContains(path, contents, "StoreSlotHeader");
    CheckContains(path, contents, "StoreAssistTuple");
    CheckContains(path, contents, "TileXREpFlushDispatchSlotHeaders");
    CheckContains(path, contents, "sourceWindow = TileXREpWindowBase");
    CheckNotContains(path, contents, "expertIds[");
    CheckNotContains(path, contents, "assistBase[item]");
    CheckNotContains(path, contents, "args->peerMems[peer]");
    CheckNotContains(path, contents, "slot->count");
    CheckNotContains(path, contents, "assist[index]");
}

void TestCrossNodeDispatchUsesUDMARegistry()
{
    const std::string path = "src/ep/kernels/tilexr_ep_dispatch_kernel.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "tilexr_udma.h");
    CheckContains(path, contents, "TileXR::UDMARegistryEnabled(args)");
    CheckContains(path, contents, "TileXREpUsesUdmaWindow");
    CheckContains(path, contents, "TileXR::UDMAPutNbi<uint8_t>");
    CheckContains(path, contents, "TileXR::UDMAQuiet(args, dstRank)");
    CheckContains(path, contents, "TileXREpFlushDispatchSlotHeaders");
    CheckContains(path, contents, "sourceWindow = TileXREpWindowBase");
}

void TestCrossNodeDispatchPullsRemoteSlots()
{
    const std::string path = "src/ep/kernels/tilexr_ep_dispatch_kernel.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "tilexr_ep_dispatch_cross_node_kernel");
    CheckContains(path, contents, "launch_tilexr_ep_dispatch_cross_node_kernel");
    CheckContains(path, contents, "TileXREpPullUdmaSlots");
    CheckContains(path, contents, "TileXR::UDMAGetNbi<uint8_t>");
    CheckContains(path, contents, "TileXREpNotifyUdmaReady");
    CheckContains(path, contents, "TileXREpWaitUdmaReady");
    CheckContains(path, contents, "TileXREpNotifyAllUdmaReady");
    CheckContains(path, contents, "TileXREpWaitAllUdmaReady");
    CheckContains(path, contents, "TileXR::UDMAPutSignalNbi<uint8_t>");
}

void TestCrossNodeDispatchSeparatesLocalAndRemotePeers()
{
    const std::string path = "src/ep/kernels/tilexr_ep_dispatch_kernel.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "TileXREpIsSameNodePeer");
    CheckContains(path, contents, "TileXREpUsesUdmaPeer");
    CheckContains(path, contents, "if (localRankSize <= 1)");
    CheckContains(path, contents, "TileXREpPublishLocalUdmaSlot");
    CheckContains(path, contents, "TileXREpDispatchWriteWindow");
    CheckContains(path, contents, "args->localRankSize");
    CheckContains(path, contents, "if (localRankSize > 1)");
    CheckContains(path, contents, "dstRank != rank && TileXREpIsSameNodePeer(rank, dstRank, localRankSize)");
    CheckContains(path, contents, "srcRank != rank &&");
    CheckContains(path, contents, "TileXREpIsSameNodePeer(rank, srcRank, localRankSize)");
    CheckContains(path, contents, "sameNodeSource ? rank : srcRank");
    CheckContains(path, contents, "!TileXREpIsSameNodePeer(rank, peer, localRankSize)");
}

void TestHostDispatchSplitsCrossNodeKernel()
{
    const std::string path = "src/ep/host/ep_kernel_launch.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "launch_tilexr_ep_dispatch_cross_node_kernel");
    CheckContains(path, contents, "TileXREpUsesCrossNodeKernel");
}

void TestDispatchHelpersLiveInDispatchHelperFile()
{
    const std::string path = "src/ep/kernels/tilexr_ep_dispatch_helpers.h";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "TileXREpRouteToDstRank");
    CheckContains(path, contents, "TileXREpCopyRoutePayload");
    CheckContains(path, contents, "TileXREpStoreDispatchSlotHeader");
    CheckContains(path, contents, "TileXREpStoreAssistTuple");
    CheckContains(path, contents, "localWindow + PayloadOffset(dstRank, slotBytes)");
}

void TestCombineHelpersLiveInCombineHelperFile()
{
    const std::string path = "src/ep/kernels/tilexr_ep_combine_helpers.h";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "TileXREpDrainSourceWindow");
    CheckContains(path, contents, "TileXREpWaitDispatchSlotReady");
    CheckContains(path, contents, "slotMagic");
    CheckContains(path, contents, "TileXREpLoadAssistTuple");
    CheckContains(path, contents, "TileXREpGetCombineTokenId");
    CheckContains(path, contents, "TileXREpGetCombineTopKId");
    CheckContains(path, contents, "sourceWindow + SlotOffset(slotRank, slotBytes)");
}

void TestCombineKernelUsesTileXRPeerMemory()
{
    const std::string path = "src/ep/kernels/tilexr_ep_combine_kernel.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "extern \"C\" __global__ __aicore__ void tilexr_ep_combine_kernel");
    CheckContains(path, contents, "launch_tilexr_ep_combine_kernel");
    CheckContains(path, contents, "kEpStepCombineWindowCleared");
    CheckContains(path, contents, "kEpStepCombineReady");
    CheckContains(path, contents, "ScatterCombineRows");
    CheckContains(path, contents, "DrainCombineRows");
    CheckContains(path, contents, "AccumulateRow");
    CheckContains(path, contents, "tilexr_ep_combine_cross_node_kernel");
    CheckContains(path, contents, "tilexr_ep_combine_cross_node_drain_kernel");
    CheckContains(path, contents, "TileXREpNotifyRemoteUdmaReadySeparate");
    CheckContains(path, contents, "TileXREpWaitRemoteUdmaReady");
    CheckNotContains(path, contents, "tilexr_ep_dispatch_kernel");

    std::string hostLaunch;
    if (ReadFile("src/ep/host/ep_kernel_launch.cpp", &hostLaunch)) {
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "TileXREpLaunchCombineKernel");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "launch_tilexr_ep_combine_kernel");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch,
            "launch_tilexr_ep_combine_cross_node_kernel");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "aclrtSynchronizeStream");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "TILEXR_ERROR_TIMEOUT");
    }
}

void TestKernelCommonHasCombineHelpers()
{
    const std::string path = "src/ep/kernels/tilexr_ep_kernel_common.h";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "UDMASecondOperationOffset");
    CheckContains(path, contents, "TileXREpNotifyRemoteUdmaReadySeparate");
    CheckContains(path, contents, "TileXREpWaitRemoteUdmaReady");
    CheckContains(path, contents, "TileXREpStoreStatusValue");
    CheckContains(path, contents, "TileXREpFlushUdmaSourceWindow");
    CheckContains(path, contents, "IsValidShape");

    std::string combine;
    if (ReadFile("src/ep/kernels/tilexr_ep_combine_kernel.cpp", &combine)) {
        CheckContains("src/ep/kernels/tilexr_ep_combine_kernel.cpp", combine, "kEpStatusRemoteReadyTimeout");
    }
}

void TestDispatchDemoRunsCombine()
{
    const std::string path = "tests/ep/demo/tilexr_ep_dispatch_demo.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "TileXRMoeEpCombine");
    CheckContains(path, contents, "ValidateCombineOutputs");
    CheckContains(path, contents, "combine validation");
}

void TestKernelForwardsActiveMask()
{
    std::string hostLaunch;
    if (ReadFile("src/ep/host/ep_kernel_launch.cpp", &hostLaunch)) {
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "xActiveMask");
    }

    std::string kernel;
    if (ReadFile("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", &kernel)) {
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "xActiveMaskGM");
    }

    std::string dispatchHelpers;
    if (ReadFile("src/ep/kernels/tilexr_ep_dispatch_helpers.h", &dispatchHelpers)) {
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_helpers.h", dispatchHelpers, "TileXREpIsTokenActive");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_helpers.h", dispatchHelpers, "xActiveMaskGM == nullptr");
    }
}

void TestKernelForwardsExpertTokenNumsType()
{
    std::string hostLaunch;
    if (ReadFile("src/ep/host/ep_kernel_launch.cpp", &hostLaunch)) {
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "expertTokenNumsType");
    }

    std::string kernel;
    if (ReadFile("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", &kernel)) {
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "expertTokenNumsType");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpClearExpertTokenNums");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpFinalizeExpertTokenNums");
    }

    std::string combineHelpers;
    if (ReadFile("src/ep/kernels/tilexr_ep_combine_helpers.h", &combineHelpers)) {
        CheckContains("src/ep/kernels/tilexr_ep_combine_helpers.h", combineHelpers,
            "TileXREpIncrementExpertTokenNum");
        CheckContains("src/ep/kernels/tilexr_ep_combine_helpers.h", combineHelpers,
            "running += expertTokenNumsOut[localExpert]");
    }
}

void TestKernelForwardsTpRecvCountsOut()
{
    std::string hostLaunch;
    if (ReadFile("src/ep/host/ep_kernel_launch.cpp", &hostLaunch)) {
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "tpRecvCountsOut");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "tpWorldSize");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "tpRankId");
    }

    std::string kernel;
    if (ReadFile("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", &kernel)) {
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "tpRecvCountsOutGM");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpAppendTpGroupRows");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpTpGroupStartRank");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "tpWorldSize");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "tpRankId");
    }

    const std::string demoPath = "tests/ep/demo/tilexr_ep_dispatch_demo.cpp";
    std::string demo;
    if (ReadFile(demoPath, &demo)) {
        CheckContains(demoPath, demo, "TILEXR_EP_DEMO_TP_RECV_COUNTS");
        CheckContains(demoPath, demo, "TILEXR_EP_DEMO_TP_WORLD_SIZE");
        CheckContains(demoPath, demo, "TILEXR_EP_DEMO_TP_RANK_ID");
        CheckContains(demoPath, demo, "BuildExpectedTpRoutes");
        CheckContains(demoPath, demo, "ValidateTpRecvCounts");
    }
}

void TestDispatchDemoExercisesV2OptionalInputs()
{
    const std::string path = "tests/ep/demo/tilexr_ep_dispatch_demo.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckContains(path, contents, "TILEXR_EP_DEMO_ACTIVE_MASK");
    CheckContains(path, contents, "TILEXR_EP_DEMO_EXPERT_TOKEN_NUMS_TYPE");
    CheckContains(path, contents, "xActiveMaskDev");
    CheckContains(path, contents, "expertTokenNumsType");
}

void TestKernelForwardsSharedExpertConfig()
{
    std::string hostLaunch;
    if (ReadFile("src/ep/host/ep_kernel_launch.cpp", &hostLaunch)) {
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "sharedExpertNum");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "sharedExpertRankNum");
    }

    std::string kernel;
    if (ReadFile("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", &kernel)) {
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpRouteToDstRank");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "sharedExpertRankNum");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpLocalExpertCount");
    }

    std::string demo;
    if (ReadFile("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", &demo)) {
        CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "TILEXR_EP_DEMO_SHARED_EXPERT_NUM");
        CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "TILEXR_EP_DEMO_SHARED_EXPERT_RANK_NUM");
    }
}

void TestKernelForwardsStaticQuantConfig()
{
    std::string hostLaunch;
    if (ReadFile("src/ep/host/ep_kernel_launch.cpp", &hostLaunch)) {
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "scales");
        CheckContains("src/ep/host/ep_kernel_launch.cpp", hostLaunch, "quantMode");
    }

    std::string kernel;
    if (ReadFile("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", &kernel)) {
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "scalesGM");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "quantMode");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpCopyStaticQuantRoutePayload");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "TileXREpClampInt8");
    }

    std::string demo;
    if (ReadFile("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", &demo)) {
        CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "TILEXR_EP_DEMO_QUANT_MODE");
        CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "TILEXR_EP_DEMO_STATIC_QUANT_SCALE");
    }
}

void TestKernelForwardsPerTokenDynamicQuantConfig()
{
    std::string kernel;
    if (ReadFile("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", &kernel)) {
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "kEpQuantModePerTokenDynamic");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel,
            "TileXREpCopyPerTokenDynamicQuantRoutePayload");
        CheckContains("src/ep/kernels/tilexr_ep_dispatch_kernel.cpp", kernel, "dynamicScalesOutGM");
    }

    std::string demo;
    if (ReadFile("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", &demo)) {
        CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "usePerTokenDynamicQuant");
        CheckContains("tests/ep/demo/tilexr_ep_dispatch_demo.cpp", demo, "DynamicScaleForXValue");
    }
}

void TestClearLocalWindowDoesNotPreclearSlotHeaders()
{
    const std::string path = "src/ep/kernels/tilexr_ep_dispatch_kernel.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }

    CheckNotContains(path, contents,
        "for (int32_t srcRank = 0; srcRank < rankSize; ++srcRank) {\n"
        "        StoreSlotHeader(localWindow + SlotOffset(srcRank, slotBytes), 0, srcRank, 0, 0, tBuf);\n"
        "    }");
}

void TestNoForbiddenDependencies()
{
    const std::vector<std::string> paths = {
        "src/ep/kernels/tilexr_ep_dispatch_kernel.cpp",
        "src/ep/kernels/tilexr_ep_combine_kernel.cpp",
        "src/ep/kernels/tilexr_ep_kernel_common.h",
        "src/ep/kernels/tilexr_ep_dispatch_helpers.h",
        "src/ep/kernels/tilexr_ep_combine_helpers.h",
        "src/ep/host/ep_kernel_launch.cpp",
        "src/ep/CMakeLists.txt",
    };
    const std::vector<std::string> forbidden = {
        "src/mc2",
        "3rdparty/ops-transformer",
        "GetHcclContext",
        "TileXRUDMARegister",
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

void TestUrmaCombineUsesDataAsFlagAndRegisteredUdma()
{
    const std::string path = "src/ep/kernels/tilexr_ep_urma_combine_kernel.cpp";
    std::string contents;
    if (!ReadFile(path, &contents)) {
        return;
    }
    CheckContains(path, contents, "tilexr_data_as_flag.h");
    CheckContains(path, contents, "tilexr_ep_urma_combine_kernel");
    CheckContains(path, contents, "TxRouteReady");
    CheckContains(path, contents, "cursorBuf");
    CheckContains(path, contents, "UDMAPutNbi<uint8_t>");
    CheckContains(path, contents, "UDMAPutNbiDoorbellBatched<uint8_t>");
    CheckContains(path, contents, "UDMAFlushDoorbellBatchGroup(&doorbellBatch)");
    CheckContains(path, contents, "UDMAQuiet");
    CheckContains(path, contents, "static_cast<uint32_t>(senderId)");
    CheckContains(path, contents, "TILEXR_UDMA_QP_COUNT");
    CheckContains(path, contents, "CheckRouteReady");
    CheckContains(path, contents, "StartClearRouteFlags");
    CheckContains(path, contents, "CleanTokenRouteFlags");
    CheckContains(path, contents, "clearFlagBuf");
    CheckContains(path, contents, "TILEXR_EP_URMA_CACHELESS");
    CheckContains(path, contents, "#if !defined(TILEXR_EP_URMA_CACHELESS)");
    CheckContains(path, contents, "ReadGmByPassDCache");
    CheckContains(path, contents, "ReadGmByPassDCache(src + 2)");
    CheckContains(path, contents, "tuple.expertId = 0");
    CheckContains(path, contents, "kRouteMetaPrefetchCapacityBytes = 16 * 1024");
    CheckContains(path, contents, "sizeof(TileXREp::EpAssistTuple) == 16");
    CheckContains(path, contents, "StartRouteMetaPrefetch");
    CheckContains(path, contents, "LoadRouteMetaBypass");
    CheckContains(path, contents,
        "src.SetL2CacheHint<AscendC::CacheRwMode::READ>");
    CheckContains(path, contents, "LoadPrefetchedRouteMeta");
    CheckContains(path, contents, "routeMetaPrefetchPending");
    CheckContains(path, contents, "PerfStage::TX_META_SCAN, 3");
    CheckContains(path, contents, "kMaxSendUbBytes <= TileXR::TILEXR_PERF_TRACE_LOCAL_STATS_UB_OFFSET");
    CheckContains(path, contents, "WriteGmByPassDCache");
    CheckContains(path, contents, "CacheMode::CACHE_MODE_DISABLE");
    CheckContains(path, contents, "CacheRwMode::READ");
    CheckContains(path, contents, "CacheRwMode::WRITE");
    CheckContains(path, contents, "DataSyncBarrier<AscendC::MemDsbT::ALL>");
    CheckContains(path, contents, "kEpUrmaCombineDataBlockBytes - sizeof(float)");
    CheckContains(path, contents, "block * TileXR::DATA_AS_FLAG_FLAG_FLOATS");
    CheckContains(path, contents, "DataCopyExtParams payloadIn");
    CheckContains(path, contents, "DataCopyExtParams payloadOut");
    CheckContains(path, contents, "DataCopyExtParams flagOut");
    CheckContains(path, contents, "selfCopyPayloadBytes");
    CheckContains(path, contents, "selfCopyReady");
    CheckContains(path, contents, "UDMACleanCacheLines");
    CheckContains(path, contents, "static_cast<uint32_t>(magic) & 1U");
    CheckNotContains(path, contents, "combineEpoch");
    CheckContains(path, contents, "kEpUrmaCombineQuantModeInt8PerRoute");
    CheckContains(path, contents,
        "TILEXR_EP_URMA_QDC_VERSION == 1 || TILEXR_EP_URMA_QDC_VERSION == 2");
    CheckContains(path, contents,
        "TILEXR_EP_URMA_QDC_VERSION == 0 || TILEXR_EP_URMA_QDC_VERSION == 2");
    CheckContains(path, contents, "TILEXR_EP_URMA_QDC_VERSION == 3");
    CheckContains(path, contents, "TILEXR_EP_URMA_QDC_VERSION != 3");
    CheckContains(path, contents, "kMaxFiniteHalf = 65504.0f");
    CheckContains(path, contents, "halfScaleSafe");
    CheckContains(path, contents, "maxAbs >= 0.0f && maxAbs <= kMaxFiniteHalf");
    CheckContains(path, contents, "inverseScale > 0.0f && inverseScale <= kMaxFiniteHalf");
    CheckContains(path, contents, "Muls<half>");
    CheckContains(path, contents, "Abs<half>");
    CheckContains(path, contents, "ReduceMax<half>");
    CheckContains(path, contents, "Abs<float>");
    CheckContains(path, contents, "ReduceMax<float>");
    CheckContains(path, contents, "Cast<int8_t, half>");
    CheckContains(path, contents, "Cast<half, int8_t>");
    CheckContains(path, contents, "LoadTopKWeights");
    CheckContains(path, contents, "#if !TILEXR_EP_URMA_CACHELESS");
    CheckContains(path, contents, "Axpy<float, float>");
    CheckContains(path, contents, "kPipelineBufferCount = 2");
    CheckContains(path, contents,
        "using PackInputQueue = AscendC::TQue<AscendC::QuePosition::VECIN, 2>");
    CheckContains(path, contents, "pipe.InitBuffer(inputQueue");
    CheckContains(path, contents, "inputQueue.AllocTensor<half>()");
    CheckContains(path, contents, "inputQueue.EnQue(input)");
    CheckContains(path, contents, "inputQueue.DeQue<half>()");
    CheckContains(path, contents, "inputQueue.FreeTensor<half>(input)");
    CheckContains(path, contents, "kPipelineBufferCount * routeStride");
    CheckContains(path, contents, "kPipelineBufferCount * logicalBytes");
    CheckContains(path, contents, "kMaxPackUbBytes");
    CheckContains(path, contents, "kMaxReceiveUbBytes");
    CheckContains(path, contents, "EnqueuePackInput");
    CheckNotContains(path, contents, "StartPackInputCopy");
    CheckContains(path, contents, "StartPackPublish");
    CheckContains(path, contents, "FinishPackCopy");
    CheckContains(path, contents, "PublishTxReadyBatch");
    CheckContains(path, contents, "PublishTxReadyInData");
    CheckContains(path, contents, "LoadTxReadyInData");
    CheckContains(path, contents, "TILEXR_EP_URMA_TX_READY_IN_DATA");
    CheckContains(path, contents, "kEpUrmaCombineTxReadyHeaderOffset");
    CheckContains(path, contents, "txRouteAddr + TileXREp::kEpUrmaCombineTxReadyHeaderOffset");
    CheckContains(path, contents,
        "readyBatchCount == static_cast<int64_t>(TileXREp::kEpUrmaCombineTxReadyBatchSize)");
    CheckContains(path, contents,
        "TILEXR_EP_URMA_TX_READY_SHARED_FLAG && TILEXR_EP_URMA_TX_READY_BATCH_SIZE > 1");
    CheckContains(path, contents, "const int64_t publishedRouteCount = 1;");
    CheckContains(path, contents, "TxReadyPollRoute");
    CheckContains(path, contents, "laneBegin + ((route - laneBegin) /");
    CheckContains(path, contents, "const int64_t readyRoute = TxReadyPollRoute(route, begin);");
    CheckContains(path, contents, "readyRoute * TileXREp::kEpUrmaCombineCacheLineBytes");
    CheckContains(path, contents, "StartUnpackRoute");
    CheckNotContains(path, contents, "Cast<float, int8_t>");
    CheckNotContains(path, contents, "AscendC::Dequantize");
    CheckNotContains(path, contents, "AscendC::AscendDequant");
    CheckContains(path, contents, "tilexr_perf_trace_local.h");
    CheckContains(path, contents, "TileXRPerfLocalStatsInit");
    CheckContains(path, contents, "TileXRPerfLocalStatsFlush");
    CheckContains(path, contents, "ProfileKernelTimingBegin");
    CheckContains(path, contents,
        "if (TileXR::TileXRPerfTraceEnabled(perfTrace))");
    CheckContains(path, contents,
        "PerfStageId(PerfStage::KERNEL_TOTAL), kernelStart, kernelEnd");
    CheckContains(path, contents, "ProfileBufferValid");
    CheckContains(path, contents, "header->flags <= 2");
    CheckContains(path, contents, "GM_ADDR kernelPerfTrace = perfTrace;");
    CheckContains(path, contents, "if (profileDetail == 0)");
    CheckContains(path, contents, "perfTraceBytes");
    CheckContains(path, contents, "TILEXR_PERF_TRACE_MIN_UB_BYTES");
    CheckContains(path, contents, "PerfStage::PACK_TOTAL");
    CheckContains(path, contents, "PerfStage::PACK_INPUT_WAIT");
    CheckContains(path, contents, "PerfStage::RX_UNPACK_WAIT");
    CheckContains(path, contents, "PerfStage::DCCI_TOTAL");
    CheckContains(path, contents, "DcciProfileCategory::TX_DATA");
    CheckContains(path, contents, "DcciProfileCategory::RX_FLAG_POLL");
    CheckContains(path, contents, "DcciProfileCategory::RX_DATA");
    CheckContains(path, contents, "DcciProfileCategory::CONTROL_OTHER");
    CheckContains(path, contents, "PerfStage::UDMA_POST");
    CheckContains(path, contents, "PerfStage::GLOBAL_ROUND_WAIT");
    CheckContains(path, contents, "PerfStage::START_GATE");
    CheckContains(path, contents, "#if TILEXR_EP_URMA_START_GATE");
    CheckContains(path, contents, "WaitForSynchronizedStart");
    CheckContains(path, contents, "kEpUrmaCombineStartLocalReady");
    CheckContains(path, contents, "kEpUrmaCombineStartRankReady");
    CheckContains(path, contents, "kEpUrmaCombineStartPublishDone");
    CheckContains(path, contents, "kEpUrmaCombineStartRun");
    CheckContains(path, contents, "startGateOffset");
    CheckContains(path, contents, "int64_t runStartGate");
    CheckContains(path, contents, "if (runStartGate != 0)");
    CheckContains(path, contents, "(runStartGate != 0 && runStartGate != 1)");
    CheckContains(path, contents, "const uint32_t qpIdx = static_cast<uint32_t>(senderId)");
    CheckContains(path, contents, "#if TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH");
    CheckContains(path, contents, "StartParallelRoundPublish");
    CheckContains(path, contents, "PublishRoundShard");
    CheckContains(path, contents, "FinishParallelRoundPublish");
    CheckContains(path, contents,
        "peer += static_cast<int32_t>(TileXREp::kEpUrmaCombineSendLaneCount)");
    CheckContains(path, contents,
        "static_cast<uint32_t>(TileXREp::kEpUrmaCombineCacheLineBytes), qpIdx");
    CheckContains(path, contents, "TileXR::UDMAQuiet(args, peer, qpIdx)");
    CheckContains(path, contents,
        "rank * TileXREp::kEpUrmaCombineCacheLineBytes");
    CheckContains(path, contents, "kEpUrmaCombinePublishDone");
    CheckContains(path, contents, "WaitLocalLines(publishAddr, 1, roundValue");
    CheckContains(path, contents,
        "magic, TileXREp::kEpUrmaCombineRxBufferReleased);");
    CheckContains(path, contents,
        "LoadControlValue(lineAddr, finePerfTrace, perfStats) == expected");
    CheckNotContains(path, contents, "ControlGeneration");
    CheckContains(path, contents, "RecordRoundPublishCounters");
    CheckContains(path, contents, "PerfStage::SELF_COPY");
    CheckContains(path, contents, "if (tuple.srcRank == rank)");
    CheckContains(path, contents, "CopySelfRoute(txRouteAddr, workspaceGM + rxRouteOffset");
    CheckContains(path, contents, "udmaInfo->qpNum <");
    CheckNotContains(path, contents, "SelfCopyRoutes");
    CheckNotContains(path, contents, "selfCopyDoneOffset");
    CheckNotContains(path, contents, "kEpUrmaCombineSelfCopyCoreIndex");
    CheckNotContains(path, contents, "PerfStage::SELF_COPY_TOTAL");
    CheckNotContains(path, contents, "PerfStage::LOCAL_SELF_COPY_WAIT");
    CheckNotContains(path, contents, "pipelineEnabled");
    CheckNotContains(path, contents, "!TileXR::TileXRPerfTraceEnabled(perfTrace)");
    CheckContains(path, contents, "const bool reverse = (lane & 1) != 0;");
    CheckContains(path, contents, "const int64_t remainder = laneLength %");
    CheckContains(path, contents, "const int64_t signedDelta = reverse ? rotation - senderId : senderId - rotation;");
    CheckNotContains(path, contents, "senderId - begin % TileXREp::kEpUrmaCombineSendLaneCount");
    CheckContains(path, contents, "route + TileXREp::kEpUrmaCombineSendLaneCount");
    CheckContains(path, contents, "kRxTokenScheduleWindow = 3");
    CheckContains(path, contents, "tokenActive[candidate]");
    CheckContains(path, contents, "activeToken[selectedSlot] = nextToken++");
    CheckNotContains(path, contents, "QuantizeInt8");
    CheckNotContains(path, contents, "for (int64_t elem");
    CheckNotContains(path, contents, "peerMems");
    CheckNotContains(path, contents, "shmem");

    const std::size_t packFunction = contents.find("void PackRoutes(");
    const std::size_t queueInit = contents.find("pipe.InitBuffer(inputQueue", packFunction);
    const std::size_t logicalInit = contents.find("Duplicate<int8_t>(logical", queueInit);
    const std::size_t packed0Init = contents.find("Duplicate<float>(packed0", logicalInit);
    const std::size_t packed1Init = contents.find("Duplicate<float>(packed1", packed0Init);
    const std::size_t packLoop = contents.find("for (int64_t route = begin; route < end; ++route)", packed1Init);
    const std::size_t enqueueInput = contents.find("EnqueuePackInput(expertOut", packLoop);
    const std::size_t dequeueInput = contents.find("inputQueue.DeQue<half>()", enqueueInput);
    const std::size_t quantizeStart = contents.find("StartPackQuantization(input", dequeueInput);
    const std::size_t quantizeFinish = contents.find("FinishPackQuantization(input", quantizeStart);
    const std::size_t freeInput = contents.find("inputQueue.FreeTensor<half>(input)", quantizeFinish);
    const std::size_t publishStart = contents.find("StartPackPublish(txRouteAddr", freeInput);
    if (packFunction == std::string::npos || queueInit == std::string::npos ||
        logicalInit == std::string::npos || packed0Init == std::string::npos ||
        packed1Init == std::string::npos || packLoop == std::string::npos ||
        enqueueInput == std::string::npos || dequeueInput == std::string::npos ||
        quantizeStart == std::string::npos || quantizeFinish == std::string::npos ||
        freeInput == std::string::npos || publishStart == std::string::npos ||
        queueInit >= logicalInit || logicalInit >= packed0Init || packed0Init >= packed1Init ||
        packed1Init >= packLoop || packLoop >= enqueueInput || enqueueInput >= dequeueInput ||
        dequeueInput >= quantizeStart || quantizeStart >= quantizeFinish ||
        quantizeFinish >= freeInput || freeInput >= publishStart) {
        std::cerr << path
                  << " must keep Pack input ownership in TQue until quantization finishes and before MTE3 publish"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t pendingCopyFinish = contents.find(
        "FinishPackCopy(workspaceGM + txDataOffset + pendingRoute", packLoop);
    const std::size_t fullBatchReady = contents.find(
        "readyBatchCount == static_cast<int64_t>(TileXREp::kEpUrmaCombineTxReadyBatchSize)",
        pendingCopyFinish);
    const std::size_t fullBatchPublish = contents.find(
        "PublishTxReadyBatch(workspaceGM", fullBatchReady);
    const std::size_t tailBatch = contents.find("if (route + 1 == end)", fullBatchPublish);
    const std::size_t tailCopyFinish = contents.find("FinishPackCopy(txRouteAddr", tailBatch);
    const std::size_t tailBatchPublish = contents.find(
        "PublishTxReadyBatch(workspaceGM", tailCopyFinish);
    if (pendingCopyFinish == std::string::npos || fullBatchReady == std::string::npos ||
        fullBatchPublish == std::string::npos || tailBatch == std::string::npos ||
        tailCopyFinish == std::string::npos || tailBatchPublish == std::string::npos ||
        pendingCopyFinish >= fullBatchReady || fullBatchReady >= fullBatchPublish ||
        tailBatch >= tailCopyFinish || tailCopyFinish >= tailBatchPublish) {
        std::cerr << path
                  << " must finish every full or tail TX-ready batch copy before publishing its flag"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t mainQuantizeStart = contents.find(
        "StartPackQuantization(input", freeInput + 1);
    const std::size_t earlyGuard = contents.find(
        "#if TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH", mainQuantizeStart);
    const std::size_t earlyFinish = contents.find(
        "FinishAndPublishPackBatch(workspaceGM", earlyGuard);
    const std::size_t mainQuantizeFinish = contents.find(
        "FinishPackQuantization(input", earlyFinish);
    const std::size_t currentSubmit = contents.find(
        "StartPackPublish(txRouteAddr", mainQuantizeFinish);
    const std::size_t baselineGuard = contents.find(
        "#if !TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH", currentSubmit);
    const std::size_t baselineFinish = contents.find(
        "FinishPackCopy(workspaceGM + txDataOffset + pendingRoute", baselineGuard);
    const std::size_t earlyTailGuard = contents.find(
        "#if TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH", baselineFinish);
    const std::size_t earlyTailFinish = contents.find(
        "FinishAndPublishPackBatch(workspaceGM", earlyTailGuard);
    if (mainQuantizeStart == std::string::npos || earlyGuard == std::string::npos ||
        earlyFinish == std::string::npos || mainQuantizeFinish == std::string::npos ||
        currentSubmit == std::string::npos || baselineGuard == std::string::npos ||
        baselineFinish == std::string::npos || earlyTailGuard == std::string::npos ||
        earlyTailFinish == std::string::npos || mainQuantizeStart >= earlyFinish ||
        earlyFinish >= mainQuantizeFinish || mainQuantizeFinish >= currentSubmit ||
        currentSubmit >= baselineFinish || baselineFinish >= earlyTailFinish) {
        std::cerr << path
                  << " must publish the previous TX route between V_S set/wait only in the early path, "
                     "retain the submit-then-finish baseline path, and drain the final route"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t finishHelper = contents.find(
        "__aicore__ TILEXR_EP_LOCAL_FUNCTION void FinishAndPublishPackBatch");
    const std::size_t helperMteWait = contents.find("FinishPackCopy(txRouteAddr", finishHelper);
    const std::size_t helperReadyPublish = contents.find("PublishTxReadyBatch(workspaceGM", helperMteWait);
    const std::size_t helperFirstReady = contents.find(
        "PerfStage::PACK_FIRST_TX_READY", helperReadyPublish);
    if (finishHelper == std::string::npos || helperMteWait == std::string::npos ||
        helperReadyPublish == std::string::npos || helperFirstReady == std::string::npos ||
        helperMteWait >= helperReadyPublish || helperReadyPublish >= helperFirstReady) {
        std::cerr << path
                  << " must wait for route data before ready publication and record first TX-ready afterwards"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t batchReadyHelper = contents.find(
        "__aicore__ TILEXR_EP_LOCAL_FUNCTION uint32_t CheckRoutesReadyBatch");
    const std::size_t batchCopyLoop = contents.find("for (int64_t topKId = 0; topKId < topK", batchReadyHelper);
    const std::size_t batchEvent = contents.find(
        "SetFlag<AscendC::HardEvent::MTE2_S>(EVENT_ID0);", batchCopyLoop);
    const std::size_t batchWait = contents.find(
        "WaitFlag<AscendC::HardEvent::MTE2_S>(EVENT_ID0);", batchEvent);
    const std::size_t vectorDelta = contents.find("AscendC::Adds<float>(routeDelta", batchWait);
    const std::size_t vectorReduce = contents.find("AscendC::ReduceMax<float>", vectorDelta);
    const std::size_t vectorWait = contents.find(
        "WaitFlag<AscendC::HardEvent::V_S>(EVENT_ID0);", vectorReduce);
    if (batchReadyHelper == std::string::npos || batchCopyLoop == std::string::npos ||
        batchEvent == std::string::npos || batchWait == std::string::npos ||
        vectorDelta == std::string::npos || vectorReduce == std::string::npos ||
        vectorWait == std::string::npos || batchCopyLoop >= batchEvent ||
        batchEvent >= batchWait || batchWait >= vectorDelta || vectorDelta >= vectorReduce ||
        vectorReduce >= vectorWait) {
        std::cerr << path
                  << " must queue pending ready reads before one completion wait and keep Vector reduction behind MTE2"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t allReadyDecision = contents.find(
        "candidateReady = readyMask == allRoutesReadyMask");
    const std::size_t acquireBeforeUnpack = contents.find("CachelessAcquireBarrier();", allReadyDecision);
    const std::size_t firstPayloadUnpack = contents.find("StartUnpackRoute(firstRouteAddr", acquireBeforeUnpack);
    if (allReadyDecision == std::string::npos || acquireBeforeUnpack == std::string::npos ||
        firstPayloadUnpack == std::string::npos || allReadyDecision >= acquireBeforeUnpack ||
        acquireBeforeUnpack >= firstPayloadUnpack) {
        std::cerr << path
                  << " must keep full ready-mask decision before acquire and payload unpack"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t clearSubmit = contents.find("StartClearRouteFlags(routeAddr");
    const std::size_t legacyClearFence = contents.find(
        "FinishTokenRouteFlagClears(firstRouteAddr", clearSubmit);
    const std::size_t outputStart = contents.find(
        "const uint64_t outputStart = ProfileBegin(perfTrace);", clearSubmit);
    const std::size_t outputFence = contents.find(
        "WaitFlag<AscendC::HardEvent::MTE3_S>(EVENT_ID0);", outputStart);
    const std::size_t clearClean = contents.find("CleanTokenRouteFlags(firstRouteAddr", outputFence);
    if (clearSubmit == std::string::npos || legacyClearFence == std::string::npos ||
        outputStart == std::string::npos ||
        outputFence == std::string::npos || clearClean == std::string::npos ||
        clearSubmit >= legacyClearFence || legacyClearFence >= outputStart ||
        outputStart >= outputFence || outputFence >= clearClean) {
        std::cerr << path << " must keep the v0/v2/v3 dedicated clear fence before output and v1 clear drain after output"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t gateCall = contents.find(
        "WaitForSynchronizedStart(args, workspaceGM, magic");
    const std::size_t kernelTiming = contents.find(
        "const uint64_t kernelStart = ProfileKernelTimingBegin(kernelPerfTrace);", gateCall);
    const std::size_t strictTiming = contents.find(
        "const uint64_t strictKernelStart = StrictKernelTimingBegin(strictKernelCycles);", gateCall);
    if (gateCall == std::string::npos || kernelTiming == std::string::npos ||
        strictTiming == std::string::npos || gateCall >= kernelTiming || gateCall >= strictTiming) {
        std::cerr << path << " must release the start gate before kernel timing begins" << std::endl;
        ++g_failures;
    }

    const std::size_t profileTimingBegin = contents.find(
        "uint64_t ProfileKernelTimingBegin(GM_ADDR perfTrace)");
    const std::size_t profileBeginBarrier = contents.find(
        "AscendC::PipeBarrier<PIPE_ALL>();", profileTimingBegin);
    const std::size_t profileFinish = contents.find("void ProfileFinish(");
    const std::size_t profileFinishBarrier = contents.find(
        "AscendC::PipeBarrier<PIPE_ALL>();", profileFinish);
    const std::size_t profileKernelRecord = contents.find(
        "PerfStageId(PerfStage::KERNEL_TOTAL), kernelStart, kernelEnd", profileFinish);
    const std::size_t profileFlush = contents.find(
        "TileXR::TileXRPerfLocalStatsFlush", profileFinish);
    if (profileTimingBegin == std::string::npos || profileBeginBarrier == std::string::npos ||
        profileFinish == std::string::npos || profileFinishBarrier == std::string::npos ||
        profileKernelRecord == std::string::npos || profileFlush == std::string::npos ||
        profileTimingBegin >= profileBeginBarrier || profileFinish >= profileFinishBarrier ||
        profileFinishBarrier >= profileKernelRecord || profileKernelRecord >= profileFlush) {
        std::cerr << path
                  << " must PIPE_ALL-bracket profile kernel_total and exclude trace flush"
                  << std::endl;
        ++g_failures;
    }

    const std::size_t metaSubmit = contents.find(
        "StartRouteMetaPrefetch(assistInfoGM, selfSendCnt, meta, EVENT_ID1)");
    const std::size_t selfReadyInit = contents.find(
        "Duplicate<float>(selfCopyReady", metaSubmit);
    const std::size_t cursorInit = contents.find(
        "for (int64_t lane = 0; lane < TileXREp::kEpUrmaCombinePackLaneCount", selfReadyInit);
    const std::size_t metaScanStart = contents.find(
        "const uint64_t metaScanStart = ProfileBegin(perfTrace);", cursorInit);
    const std::size_t firstMetaWait = contents.find(
        "WaitFlag<AscendC::HardEvent::MTE2_S>(EVENT_ID1);", metaScanStart);
    const std::size_t prefetchedMetaLoad = contents.find(
        "LoadPrefetchedRouteMeta(meta, route)", firstMetaWait);
    const std::size_t metaDrainStart = contents.find(
        "const uint64_t metaDrainStart = ProfileBegin(perfTrace);", prefetchedMetaLoad);
    const std::size_t finalMetaWait = contents.find(
        "WaitFlag<AscendC::HardEvent::MTE2_S>(EVENT_ID1);", metaDrainStart);
    const std::size_t metaDrainProfile = contents.find(
        "ProfileEnd(perfTrace, perfStats, PerfStage::TX_META_SCAN, metaDrainStart);",
        finalMetaWait);
    if (metaSubmit == std::string::npos || selfReadyInit == std::string::npos ||
        cursorInit == std::string::npos || metaScanStart == std::string::npos ||
        firstMetaWait == std::string::npos || prefetchedMetaLoad == std::string::npos ||
        metaDrainStart == std::string::npos || finalMetaWait == std::string::npos ||
        metaDrainProfile == std::string::npos || metaSubmit >= selfReadyInit ||
        selfReadyInit >= cursorInit || cursorInit >= metaScanStart ||
        metaScanStart >= firstMetaWait || firstMetaWait >= prefetchedMetaLoad ||
        prefetchedMetaLoad >= metaDrainStart || metaDrainStart >= finalMetaWait ||
        finalMetaWait >= metaDrainProfile) {
        std::cerr << path
                  << " must submit one full metadata prefetch before ready work, wait before UB decode, and profile the final drain"
                  << std::endl;
        ++g_failures;
    }

    std::string common;
    const std::string commonPath = "src/ep/common/ep_urma_combine.h";
    if (ReadFile(commonPath, &common)) {
        CheckContains(commonPath, common, "kEpUrmaCombineAivCount = kEpUrmaCombineProfileCoreCount");
        CheckContains(commonPath, common,
            "kEpUrmaCombinePackLaneCount = kEpUrmaCombineProfilePackReceiveCoreCount");
        CheckContains(commonPath, common,
            "kEpUrmaCombineSendLaneCount = kEpUrmaCombineProfileSendCoreCount");
        CheckContains(commonPath, common, "kEpUrmaCombinePublishDone = 5");
        CheckContains(commonPath, common, "kEpUrmaCombineStartLocalReady = 6");
        CheckContains(commonPath, common, "kEpUrmaCombineStartRankReady = 7");
        CheckContains(commonPath, common, "kEpUrmaCombineStartPublishDone = 8");
        CheckContains(commonPath, common, "kEpUrmaCombineStartRun = 9");
        CheckContains(commonPath, common, "kEpUrmaCombineRequiredQpCount");
        CheckNotContains(commonPath, common, "kEpUrmaCombineSelfCopyLaneCount");
    }

    std::string profileSchema;
    const std::string profileSchemaPath = "src/include/tilexr_ep_urma_combine_profile.h";
    if (ReadFile(profileSchemaPath, &profileSchema)) {
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineProfileCoreCount = 64");
        CheckContains(profileSchemaPath, profileSchema,
            "kEpUrmaCombineProfilePackReceiveCoreCount =");
        CheckContains(profileSchemaPath, profileSchema,
            "kEpUrmaCombineProfileSendCoreCount = TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT 10");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineBalancedSendRoutes = true");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineDoorbellBatchSize");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_TX_READY_BATCH_SIZE 1");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineTxReadyBatchSize");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_TX_READY_SHARED_FLAG 0");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineTxReadySharedFlag");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_TX_READY_IN_DATA 0");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineTxReadyInData");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_TX_META_PREFETCH_FULL 0");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineTxMetaPrefetchFull");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineRxRoundRobin");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineRxStickyReady");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineParallelRoundPublish");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_QDC_VERSION 0");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombineQdcVersion");
        CheckNotContains(profileSchemaPath, profileSchema, "kEpUrmaCombineProfileSelfCopyCoreCount");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH 0");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_RX_READY_STICKY_MASK 0");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_RX_READY_BATCH_MTE2 0");
        CheckContains(profileSchemaPath, profileSchema, "TILEXR_EP_URMA_RX_READY_BATCH_VECTOR 0");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombinePerfStageCount = 27");
        CheckContains(profileSchemaPath, profileSchema, "PACK_INPUT_WAIT = 2");
        CheckContains(profileSchemaPath, profileSchema, "RX_UNPACK_WAIT = 7");
        CheckContains(profileSchemaPath, profileSchema, "DCCI_TOTAL = 20");
        CheckContains(profileSchemaPath, profileSchema, "PACK_TX_DATA_SUBMIT = 22");
        CheckContains(profileSchemaPath, profileSchema, "PACK_FIRST_TX_READY = 23");
        CheckContains(profileSchemaPath, profileSchema, "PACK_MTE3_EXPOSED_WAIT = 24");
        CheckContains(profileSchemaPath, profileSchema, "RX_READY_MTE2_WAIT = 25");
        CheckContains(profileSchemaPath, profileSchema, "RX_READY_VECTOR = 26");
        CheckContains(profileSchemaPath, profileSchema, "\"pack_input_wait\"");
        CheckContains(profileSchemaPath, profileSchema, "\"rx_unpack_wait\"");
        CheckContains(profileSchemaPath, profileSchema, "\"dcci_total\"");
        CheckContains(profileSchemaPath, profileSchema, "\"start_gate\"");
        CheckContains(profileSchemaPath, profileSchema, "kEpUrmaCombinePerfStageNames");
    }

    std::string localProfile;
    const std::string localProfilePath = "src/include/tilexr_perf_trace_local.h";
    if (ReadFile(localProfilePath, &localProfile)) {
        CheckContains(localProfilePath, localProfile,
            "TILEXR_PERF_TRACE_LOCAL_MAX_STAGE_COUNT = 27");
    }

    const std::size_t start = contents.find("StartParallelRoundPublish");
    const std::size_t ownDone = contents.find(
        "StoreControlValue(workspaceGM + roundDoneOffset", start);
    const std::size_t publishReady = contents.find(
        "StoreControlValue(workspaceGM + roundPublishOffset", start);
    if (start == std::string::npos || ownDone == std::string::npos || publishReady == std::string::npos ||
        ownDone >= publishReady) {
        std::cerr << path << " must store own roundDone before releasing roundPublish" << std::endl;
        ++g_failures;
    }
}

void TestUrmaCombineSharedTxReadyMapping()
{
    constexpr int64_t coreLayouts[][2] = {{48, 16}, {44, 20}};
    constexpr int64_t batchSizes[] = {1, 2, 4};
    for (const auto &layout : coreLayouts) {
        const int64_t packLaneCount = layout[0];
        const int64_t sendLaneCount = layout[1];
        for (int64_t selfSendCnt = 0; selfSendCnt <= 1024; ++selfSendCnt) {
            for (int64_t lane = 0; lane < packLaneCount; ++lane) {
                const int64_t begin = selfSendCnt * lane / packLaneCount;
                const int64_t end = selfSendCnt * (lane + 1) / packLaneCount;
                const int64_t laneLength = end - begin;
                const int64_t remainder = laneLength % sendLaneCount;
                const bool reverse = (lane & 1) != 0;
                int64_t rotation = lane % sendLaneCount;
                if (remainder != 0) {
                    rotation = begin % sendLaneCount;
                    if (reverse) {
                        rotation = (rotation + remainder - 1) % sendLaneCount;
                    }
                }
                for (int64_t batchSize : batchSizes) {
                    for (int64_t senderId = 0; senderId < sendLaneCount; ++senderId) {
                        const int64_t signedDelta = reverse ? rotation - senderId : senderId - rotation;
                        const int64_t delta = (signedDelta + sendLaneCount) % sendLaneCount;
                        for (int64_t route = begin + delta; route < end; route += sendLaneCount) {
                            const int64_t readyRoute =
                                begin + ((route - begin) / batchSize) * batchSize;
                            const bool valid = readyRoute >= begin && readyRoute <= route &&
                                readyRoute < end && route - readyRoute < batchSize &&
                                (readyRoute - begin) % batchSize == 0 &&
                                (batchSize != 1 || readyRoute == route);
                            if (!valid) {
                                std::cerr << "shared TX-ready mapping escaped its Pack lane: selfSendCnt="
                                          << selfSendCnt << " lane=" << lane << " sender=" << senderId
                                          << " route=" << route << " readyRoute=" << readyRoute
                                          << " batchSize=" << batchSize << std::endl;
                                ++g_failures;
                                return;
                            }
                        }
                    }
                }
            }
        }
    }
}

void TestUrmaCombineEarlyPublishStateMachine()
{
    for (int routeCount = 0; routeCount <= 8; ++routeCount) {
        std::vector<int> submitted(routeCount, 0);
        std::vector<int> finished(routeCount, 0);
        std::vector<int> published(routeCount, 0);
        int pending = -1;
        for (int route = 0; route < routeCount; ++route) {
            if (pending >= 0) {
                ++finished[pending];
                ++published[pending];
                pending = -1;
            }
            ++submitted[route];
            pending = route;
        }
        if (pending >= 0) {
            ++finished[pending];
            ++published[pending];
        }
        for (int route = 0; route < routeCount; ++route) {
            if (submitted[route] != 1 || finished[route] != 1 || published[route] != 1) {
                std::cerr << "early TX-ready state machine did not submit/finish/publish route "
                          << route << " exactly once for routeCount=" << routeCount << std::endl;
                ++g_failures;
                return;
            }
        }
    }
}

void TestUrmaCombineStickyReadyModel()
{
    constexpr uint32_t allRoutes = (1U << 6U) - 1U;
    const bool firstPassReady[6] = {false, true, true, true, true, true};
    uint32_t readyMask = 0;
    for (uint32_t route = 0; route < 6; ++route) {
        if (firstPassReady[route]) {
            readyMask |= 1U << route;
        }
    }
    if (readyMask != (allRoutes & ~1U) || (allRoutes & ~readyMask) != 1U) {
        std::cerr << "sticky ready mask must retain non-prefix ready routes" << std::endl;
        ++g_failures;
    }

    readyMask = 0;
    if (readyMask != 0) {
        std::cerr << "a reused RX scheduler slot must not inherit the previous token mask" << std::endl;
        ++g_failures;
    }

    constexpr uint32_t blockCount = 15;
    bool routeBlocks[6][blockCount] = {};
    for (uint32_t route = 0; route < 6; ++route) {
        for (uint32_t block = 0; block < blockCount; ++block) {
            routeBlocks[route][block] = true;
        }
    }
    routeBlocks[4][blockCount - 1] = false;
    uint32_t completeMask = 0;
    for (uint32_t route = 0; route < 6; ++route) {
        bool complete = true;
        for (uint32_t block = 0; block < blockCount; ++block) {
            complete = complete && routeBlocks[route][block];
        }
        if (complete) {
            completeMask |= 1U << route;
        }
    }
    if ((completeMask & (1U << 4U)) != 0 || completeMask == allRoutes) {
        std::cerr << "RX ready batching must reject a route whose final block is not ready" << std::endl;
        ++g_failures;
    }
}

} // namespace

int main()
{
    TestKernelUsesTileXRPeerMemory();
    TestCrossNodeDispatchUsesUDMARegistry();
    TestCrossNodeDispatchPullsRemoteSlots();
    TestCrossNodeDispatchSeparatesLocalAndRemotePeers();
    TestHostDispatchSplitsCrossNodeKernel();
    TestDispatchHelpersLiveInDispatchHelperFile();
    TestCombineHelpersLiveInCombineHelperFile();
    TestCombineKernelUsesTileXRPeerMemory();
    TestKernelCommonHasCombineHelpers();
    TestDispatchDemoRunsCombine();
    TestKernelForwardsActiveMask();
    TestKernelForwardsExpertTokenNumsType();
    TestKernelForwardsTpRecvCountsOut();
    TestDispatchDemoExercisesV2OptionalInputs();
    TestKernelForwardsSharedExpertConfig();
    TestKernelForwardsStaticQuantConfig();
    TestKernelForwardsPerTokenDynamicQuantConfig();
    TestClearLocalWindowDoesNotPreclearSlotHeaders();
    TestNoForbiddenDependencies();
    TestUrmaCombineUsesDataAsFlagAndRegisteredUdma();
    TestUrmaCombineSharedTxReadyMapping();
    TestUrmaCombineEarlyPublishStateMachine();
    TestUrmaCombineStickyReadyModel();
    if (g_failures != 0) {
        std::cerr << g_failures << " TileXR EP kernel source checks failed" << std::endl;
        return 1;
    }
    std::cout << "TileXR EP kernel source checks passed" << std::endl;
    return 0;
}
