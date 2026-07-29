#include "ep_urma_combine_host.h"

#include <cstdint>
#include <limits>

#if defined(TILEXR_EP_URMA_OPERATOR_LAUNCH)
#include <dlfcn.h>

#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "acl/acl_rt.h"
#include "runtime/kernel.h"
#endif

#include "ep_urma_combine.h"
#include "tilexr_perf_trace.h"
#include "tilexr_udma_reg.h"

extern void launch_tilexr_ep_urma_combine_kernel(uint32_t blockDim, void *stream, GM_ADDR commArgs,
    GM_ADDR expertOut, GM_ADDR assistInfoForCombine, GM_ADDR topKWeights, GM_ADDR yOut, GM_ADDR workspace,
    int64_t selfSendCnt, int64_t bs, int64_t h, int64_t topK, int64_t workspaceBytes, int64_t magic,
    int64_t commBytes, int64_t blockCount, int64_t routeStride, int64_t rxWindowBytes, int64_t rxWindowOffset0,
    int64_t rxWindowOffset1, int64_t roundDoneOffset0, int64_t roundDoneOffset1, int64_t rxLaneDoneOffset,
    int64_t senderDoneOffset, int64_t roundPublishOffset, int64_t roundCreditOffset,
    int64_t startGateOffset, int64_t runStartGate,
    int64_t errorStatusOffset, int64_t txReadyOffset, int64_t txDataOffset, GM_ADDR perfTrace, int64_t perfTraceBytes,
    GM_ADDR strictKernelCycles);

namespace TileXREp {

#if defined(TILEXR_EP_URMA_OPERATOR_LAUNCH)

#if !defined(TILEXR_EP_URMA_OPERATOR_BINARY_NAME)
#error "TILEXR_EP_URMA_OPERATOR_BINARY_NAME is required for operator launch"
#endif

namespace {

constexpr const char *kUrmaCombineOperatorKernelName = "tilexr_ep_urma_combine_kernel";

struct UrmaCombineOperatorState {
    std::once_flag once;
    std::vector<uint8_t> binaryData;
    rtBinHandle binaryHandle = nullptr;
    aclrtFuncHandle functionHandle = nullptr;
    int status = TileXR::TILEXR_ERROR_INTERNAL;
};

void ReportOperatorError(const char *operation, int result)
{
    std::cerr << "TileXR URMA operator " << operation << " failed: " << result;
    const char *recentError = aclGetRecentErrMsg();
    if (recentError != nullptr) {
        std::cerr << " " << recentError;
    }
    std::cerr << std::endl;
}

bool ReadOperatorBinary(std::vector<uint8_t> &data, std::string &path)
{
    Dl_info moduleInfo {};
    if (dladdr(reinterpret_cast<const void *>(&TileXREpLaunchPreparedUrmaCombineKernel),
            &moduleInfo) == 0 || moduleInfo.dli_fname == nullptr) {
        return false;
    }
    path = moduleInfo.dli_fname;
    const std::size_t separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        return false;
    }
    path.resize(separator + 1U);
    path += TILEXR_EP_URMA_OPERATOR_BINARY_NAME;

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::streamsize size = input.tellg();
    if (size <= 0) {
        return false;
    }
    data.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    return static_cast<bool>(input.read(reinterpret_cast<char *>(data.data()), size));
}

void InitializeUrmaCombineOperator(UrmaCombineOperatorState &state)
{
    std::string binaryPath;
    if (!ReadOperatorBinary(state.binaryData, binaryPath)) {
        std::cerr << "TileXR URMA operator failed to read binary: " << binaryPath << std::endl;
        return;
    }

    rtDevBinary_t binary {};
    binary.magic = RT_DEV_BINARY_MAGIC_ELF_AIVEC;
    binary.version = 0U;
    binary.data = state.binaryData.data();
    binary.length = state.binaryData.size();
    rtBinHandle binaryHandle = nullptr;
    rtError_t rtResult = rtBinaryLoad(&binary, &binaryHandle);
    if (rtResult != RT_ERROR_NONE) {
        ReportOperatorError("rtBinaryLoad", rtResult);
        return;
    }

    rtFuncHandle rtFunction = nullptr;
    rtResult = rtBinaryGetFunctionByName(
        binaryHandle, kUrmaCombineOperatorKernelName, &rtFunction);
    if (rtResult != RT_ERROR_NONE) {
        ReportOperatorError("rtBinaryGetFunctionByName", rtResult);
        (void)rtBinaryUnLoad(binaryHandle);
        return;
    }

    aclrtFuncHandle functionHandle = reinterpret_cast<aclrtFuncHandle>(rtFunction);
    void *aicAddress = nullptr;
    void *aivAddress = nullptr;
    const aclError aclResult = aclrtGetFunctionAddr(functionHandle, &aicAddress, &aivAddress);
    if (aclResult != ACL_SUCCESS || aicAddress != nullptr || aivAddress == nullptr) {
        ReportOperatorError("AIV function validation", aclResult);
        (void)rtBinaryUnLoad(binaryHandle);
        return;
    }

    state.binaryHandle = binaryHandle;
    state.functionHandle = functionHandle;
    state.status = TileXR::TILEXR_SUCCESS;
    std::cerr << "TILEXR_URMA_OPERATOR_READY binary=" << binaryPath
              << " aiv_address=" << aivAddress << std::endl;
}

UrmaCombineOperatorState &GetUrmaCombineOperatorState()
{
    static UrmaCombineOperatorState state;
    std::call_once(state.once, [&state]() { InitializeUrmaCombineOperator(state); });
    return state;
}

template <typename T>
aclError AppendOperatorArg(aclrtArgsHandle argsHandle, T &value)
{
    aclrtParamHandle paramHandle = nullptr;
    return aclrtKernelArgsAppend(argsHandle, &value, sizeof(T), &paramHandle);
}

int LaunchUrmaCombineOperator(uint32_t blockDim, aclrtStream stream, GM_ADDR commArgs,
    GM_ADDR expertOut, GM_ADDR assistInfoForCombine, GM_ADDR topKWeights, GM_ADDR yOut,
    GM_ADDR workspace, int64_t selfSendCnt, int64_t bs, int64_t h, int64_t topK,
    int64_t workspaceBytes, int64_t magic, int64_t commBytes, int64_t blockCount,
    int64_t routeStride, int64_t rxWindowBytes, int64_t rxWindowOffset0,
    int64_t rxWindowOffset1, int64_t roundDoneOffset0, int64_t roundDoneOffset1,
    int64_t rxLaneDoneOffset, int64_t senderDoneOffset, int64_t roundPublishOffset,
    int64_t roundCreditOffset,
    int64_t startGateOffset, int64_t runStartGate, int64_t errorStatusOffset,
    int64_t txReadyOffset, int64_t txDataOffset, GM_ADDR perfTrace, int64_t perfTraceBytes,
    GM_ADDR strictKernelCycles)
{
    UrmaCombineOperatorState &state = GetUrmaCombineOperatorState();
    if (state.status != TileXR::TILEXR_SUCCESS) {
        return state.status;
    }

    aclrtArgsHandle argsHandle = nullptr;
    aclError result = aclrtKernelArgsInit(state.functionHandle, &argsHandle);
    if (result != ACL_SUCCESS) {
        ReportOperatorError("aclrtKernelArgsInit", result);
        return TileXR::TILEXR_ERROR_INTERNAL;
    }
#define TILEXR_APPEND_URMA_OPERATOR_ARG(value)                                                                       \
    do {                                                                                                              \
        result = AppendOperatorArg(argsHandle, value);                                                               \
        if (result != ACL_SUCCESS) {                                                                                  \
            ReportOperatorError("aclrtKernelArgsAppend", result);                                                   \
            (void)aclrtKernelArgsFinalize(argsHandle);                                                               \
            return TileXR::TILEXR_ERROR_INTERNAL;                                                                    \
        }                                                                                                             \
    } while (false)
    TILEXR_APPEND_URMA_OPERATOR_ARG(commArgs);
    TILEXR_APPEND_URMA_OPERATOR_ARG(expertOut);
    TILEXR_APPEND_URMA_OPERATOR_ARG(assistInfoForCombine);
    TILEXR_APPEND_URMA_OPERATOR_ARG(topKWeights);
    TILEXR_APPEND_URMA_OPERATOR_ARG(yOut);
    TILEXR_APPEND_URMA_OPERATOR_ARG(workspace);
    TILEXR_APPEND_URMA_OPERATOR_ARG(selfSendCnt);
    TILEXR_APPEND_URMA_OPERATOR_ARG(bs);
    TILEXR_APPEND_URMA_OPERATOR_ARG(h);
    TILEXR_APPEND_URMA_OPERATOR_ARG(topK);
    TILEXR_APPEND_URMA_OPERATOR_ARG(workspaceBytes);
    TILEXR_APPEND_URMA_OPERATOR_ARG(magic);
    TILEXR_APPEND_URMA_OPERATOR_ARG(commBytes);
    TILEXR_APPEND_URMA_OPERATOR_ARG(blockCount);
    TILEXR_APPEND_URMA_OPERATOR_ARG(routeStride);
    TILEXR_APPEND_URMA_OPERATOR_ARG(rxWindowBytes);
    TILEXR_APPEND_URMA_OPERATOR_ARG(rxWindowOffset0);
    TILEXR_APPEND_URMA_OPERATOR_ARG(rxWindowOffset1);
    TILEXR_APPEND_URMA_OPERATOR_ARG(roundDoneOffset0);
    TILEXR_APPEND_URMA_OPERATOR_ARG(roundDoneOffset1);
    TILEXR_APPEND_URMA_OPERATOR_ARG(rxLaneDoneOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(senderDoneOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(roundPublishOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(roundCreditOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(startGateOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(runStartGate);
    TILEXR_APPEND_URMA_OPERATOR_ARG(errorStatusOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(txReadyOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(txDataOffset);
    TILEXR_APPEND_URMA_OPERATOR_ARG(perfTrace);
    TILEXR_APPEND_URMA_OPERATOR_ARG(perfTraceBytes);
    TILEXR_APPEND_URMA_OPERATOR_ARG(strictKernelCycles);
#undef TILEXR_APPEND_URMA_OPERATOR_ARG

    result = aclrtKernelArgsFinalize(argsHandle);
    if (result != ACL_SUCCESS) {
        ReportOperatorError("aclrtKernelArgsFinalize", result);
        return TileXR::TILEXR_ERROR_INTERNAL;
    }
    aclrtLaunchKernelAttr launchAttr {};
    launchAttr.id = ACL_RT_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    launchAttr.value.engineType = ACL_RT_ENGINE_TYPE_AIV;
    aclrtLaunchKernelCfg launchConfig {&launchAttr, 1U};
    result = aclrtLaunchKernelWithConfig(
        state.functionHandle, blockDim, stream, &launchConfig, argsHandle, nullptr);
    if (result != ACL_SUCCESS) {
        ReportOperatorError("aclrtLaunchKernelWithConfig", result);
        return TileXR::TILEXR_ERROR_INTERNAL;
    }
    return TileXR::TILEXR_SUCCESS;
}

} // namespace

#endif

int TileXREpGetUrmaCombineProfileSize(int64_t rankSize, int64_t *profileBytes)
{
    if (profileBytes == nullptr || rankSize <= 0 || rankSize > TileXR::TILEXR_MAX_RANK_SIZE) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    constexpr int64_t kStatsPerRank = kEpUrmaCombineAivCount * kEpUrmaCombinePerfStageCount;
    constexpr int64_t kStatsBytesPerRank =
        kStatsPerRank * static_cast<int64_t>(sizeof(TileXR::TileXRPerfCoreStageStats));
    if (rankSize > (std::numeric_limits<int64_t>::max() -
        static_cast<int64_t>(TileXR::TILEXR_PERF_TRACE_STATS_OFFSET)) / kStatsBytesPerRank) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    *profileBytes = static_cast<int64_t>(TileXR::TILEXR_PERF_TRACE_STATS_OFFSET) +
        rankSize * kStatsBytesPerRank;
    return TileXR::TILEXR_SUCCESS;
}

int TileXREpValidateBasicUrmaCombineParams(const EpUrmaCombineParams &params)
{
    if ((params.selfSendCnt > 0 && (params.expertOut == nullptr || params.assistInfoForCombine == nullptr)) ||
        params.topKWeights == nullptr || params.comm == nullptr || params.yOut == nullptr ||
        params.workspace == nullptr || params.stream == nullptr || params.selfSendCnt < 0 || params.bs <= 0 ||
        params.h <= 0 || params.h > kEpUrmaCombineMaxHidden || params.topK <= 0 ||
        params.topK > kEpUrmaCombineMaxTopK || params.workspaceBytes <= 0 || params.perfTraceBytes < 0 ||
        ((params.perfTrace == nullptr) != (params.perfTraceBytes == 0)) ||
        params.strictKernelCyclesBytes < 0 ||
        ((params.strictKernelCycles == nullptr) != (params.strictKernelCyclesBytes == 0)) ||
        params.dtype != TileXR::TILEXR_DATA_TYPE_FP16) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    if ((reinterpret_cast<uintptr_t>(params.workspace) % kEpUrmaCombineWorkspaceAlignment) != 0) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    if (params.perfTrace != nullptr && (reinterpret_cast<uintptr_t>(params.perfTrace) % 32) != 0) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    constexpr int64_t kStrictKernelCyclesBytes =
        kEpUrmaCombineAivCount * static_cast<int64_t>(sizeof(uint64_t));
    if (params.strictKernelCycles != nullptr && (params.perfTrace != nullptr ||
        params.strictKernelCyclesBytes < kStrictKernelCyclesBytes ||
        (reinterpret_cast<uintptr_t>(params.strictKernelCycles) % 32) != 0)) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    return TileXR::TILEXR_SUCCESS;
}

int TileXREpPrepareUrmaCombineLaunchContext(
    const EpUrmaCombineParams &params, EpUrmaCombineLaunchContext *context)
{
    if (context == nullptr) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    *context = EpUrmaCombineLaunchContext {};

    int ret = TileXREpValidateBasicUrmaCombineParams(params);
    if (ret != TileXR::TILEXR_SUCCESS) {
        return ret;
    }
    ret = TileXRGetCommArgsHost(params.comm, context->hostArgs);
    if (ret != TileXR::TILEXR_SUCCESS || context->hostArgs == nullptr) {
        *context = EpUrmaCombineLaunchContext {};
        return ret == TileXR::TILEXR_SUCCESS ? TileXR::TILEXR_ERROR_NOT_INITIALIZED : ret;
    }
    ret = TileXRGetCommArgsDev(params.comm, context->devArgs);
    if (ret != TileXR::TILEXR_SUCCESS || context->devArgs == nullptr) {
        *context = EpUrmaCombineLaunchContext {};
        return ret == TileXR::TILEXR_SUCCESS ? TileXR::TILEXR_ERROR_NOT_INITIALIZED : ret;
    }

    const TileXR::CommArgs &args = *context->hostArgs;
    if (args.rankSize <= 0 || args.rankSize > TileXR::TILEXR_MAX_RANK_SIZE || args.rank < 0 ||
        args.rank >= args.rankSize || (args.rankSize > 1 &&
            ((args.extraFlag & TileXR::ExtraFlag::UDMA) == 0 || args.udmaInfoPtr == nullptr ||
                args.udmaRegistryPtr == nullptr))) {
        *context = EpUrmaCombineLaunchContext {};
        return TileXR::TILEXR_ERROR_NOT_INITIALIZED;
    }

    if (params.perfTrace != nullptr) {
        int64_t requiredProfileBytes = 0;
        ret = TileXREpGetUrmaCombineProfileSize(args.rankSize, &requiredProfileBytes);
        if (ret != TileXR::TILEXR_SUCCESS || params.perfTraceBytes < requiredProfileBytes) {
            *context = EpUrmaCombineLaunchContext {};
            return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
        }
    }

    ret = TileXREpBuildUrmaCombineWorkspaceConfig(
        args.rankSize, params.bs, params.h, params.topK, params.selfSendCnt, &context->workspace);
    if (ret != TileXR::TILEXR_SUCCESS || context->workspace.requiredBytes > params.workspaceBytes) {
        *context = EpUrmaCombineLaunchContext {};
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }

    if (args.rankSize > 1) {
        const TileXR::TileXRUDMARegistry *registry = nullptr;
        ret = TileXRGetUDMARegistryHost(params.comm, &registry);
        if (ret != TileXR::TILEXR_SUCCESS || !TileXR::UDMARegistryValid(registry, args.rankSize)) {
            *context = EpUrmaCombineLaunchContext {};
            return ret == TileXR::TILEXR_SUCCESS ? TileXR::TILEXR_ERROR_NOT_INITIALIZED : ret;
        }
        if (registry->regions[args.rank].base != static_cast<GM_ADDR>(params.workspace) ||
            !TileXR::UDMARegionContains(registry, args.rank, 0,
                static_cast<uint64_t>(context->workspace.requiredBytes))) {
            *context = EpUrmaCombineLaunchContext {};
            return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
        }
        for (int32_t peer = 0; peer < args.rankSize; ++peer) {
            if (!TileXR::UDMARegionContains(
                    registry, peer, 0, static_cast<uint64_t>(context->workspace.fixedBytes))) {
                *context = EpUrmaCombineLaunchContext {};
                return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
            }
        }
    }
    return TileXR::TILEXR_SUCCESS;
}

int TileXREpLaunchPreparedUrmaCombineKernel(
    const EpUrmaCombineParams &params, const EpUrmaCombineLaunchContext &context, int64_t magic,
    bool runStartGate)
{
#if defined(TILEXR_EP_URMA_OPERATOR_LAUNCH)
    return LaunchUrmaCombineOperator(static_cast<uint32_t>(kEpUrmaCombineAivCount), params.stream,
        context.devArgs, static_cast<GM_ADDR>(params.expertOut),
        reinterpret_cast<GM_ADDR>(params.assistInfoForCombine),
        reinterpret_cast<GM_ADDR>(params.topKWeights), static_cast<GM_ADDR>(params.yOut),
        static_cast<GM_ADDR>(params.workspace), params.selfSendCnt, params.bs, params.h, params.topK,
        params.workspaceBytes, magic, context.workspace.commBytes, context.workspace.blockCount,
        context.workspace.routeStride, context.workspace.rxWindowBytes,
        context.workspace.rxWindowOffsets[0], context.workspace.rxWindowOffsets[1],
        context.workspace.roundDoneOffsets[0], context.workspace.roundDoneOffsets[1],
        context.workspace.rxLaneDoneOffset, context.workspace.senderDoneOffset,
        context.workspace.roundPublishOffset, context.workspace.roundCreditOffset,
        context.workspace.startGateOffset,
        runStartGate ? 1 : 0, context.workspace.errorStatusOffset,
        context.workspace.txReadyOffset, context.workspace.txDataOffset,
        static_cast<GM_ADDR>(params.perfTrace), params.perfTraceBytes,
        static_cast<GM_ADDR>(params.strictKernelCycles));
#else
    launch_tilexr_ep_urma_combine_kernel(static_cast<uint32_t>(kEpUrmaCombineAivCount),
        params.stream, context.devArgs,
        static_cast<GM_ADDR>(params.expertOut), reinterpret_cast<GM_ADDR>(params.assistInfoForCombine),
        reinterpret_cast<GM_ADDR>(params.topKWeights), static_cast<GM_ADDR>(params.yOut),
        static_cast<GM_ADDR>(params.workspace), params.selfSendCnt, params.bs, params.h, params.topK,
        params.workspaceBytes, magic, context.workspace.commBytes, context.workspace.blockCount,
        context.workspace.routeStride, context.workspace.rxWindowBytes, context.workspace.rxWindowOffsets[0],
        context.workspace.rxWindowOffsets[1], context.workspace.roundDoneOffsets[0],
        context.workspace.roundDoneOffsets[1], context.workspace.rxLaneDoneOffset,
        context.workspace.senderDoneOffset, context.workspace.roundPublishOffset,
        context.workspace.roundCreditOffset,
        context.workspace.startGateOffset, runStartGate ? 1 : 0, context.workspace.errorStatusOffset,
        context.workspace.txReadyOffset, context.workspace.txDataOffset,
        static_cast<GM_ADDR>(params.perfTrace), params.perfTraceBytes,
        static_cast<GM_ADDR>(params.strictKernelCycles));
    return TileXR::TILEXR_SUCCESS;
#endif
}

int TileXREpLaunchUrmaCombineKernel(
    const EpUrmaCombineParams &params, const EpUrmaCombineLaunchContext &context)
{
    int64_t magic = 0;
    const int ret = TileXRCommNextMagic(params.comm, &magic);
    if (ret != TileXR::TILEXR_SUCCESS) {
        return ret;
    }
    return TileXREpLaunchPreparedUrmaCombineKernel(params, context, magic, true);
}

} // namespace TileXREp
