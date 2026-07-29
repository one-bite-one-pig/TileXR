/*
 * Copyright (c) 2024-2026 TileXR Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef TILEXR_UDMA_H
#define TILEXR_UDMA_H

#include "kernel_operator.h"
#include "comm_args.h"
#include "tilexr_udma_reg.h"
#include "tilexr_udma_types.h"

namespace TileXR {

/**
 * @file tilexr_udma.h
 * @brief Device-side UDMA wrapper for TileXR-registered memory.
 *
 * Host side registers ordinary device memory with TileXRUDMARegister. Device
 * kernels then use byte offsets into that registered region for PUT/GET/SIGNAL.
 * This header is self-contained for the device path and intentionally avoids
 * symmetric-memory APIs.
 */

#if (defined(__NPU_ARCH__) && (__NPU_ARCH__ == 3510)) || \
    (defined(CATLASS_ARCH) && (CATLASS_ARCH == 3510)) || defined(TILEXR_UDMA_FORCE_ENABLE)
constexpr bool TILEXR_UDMA_ARCH_SUPPORTED = true;
#else
constexpr bool TILEXR_UDMA_ARCH_SUPPORTED = false;
#endif

struct UDMASignalParams {
    __gm__ uint64_t* sigAddr;
    uint64_t signal;
};

struct UDMADoorbellBatchState {
    uint64_t queueId;
    uint64_t dbAddr;
    uint32_t latestHead;
    uint32_t pendingWqes;
    uint32_t doorbellCount;
    uint32_t queueSwitchCount;
};

constexpr uint32_t TILEXR_UDMA_MAX_DOORBELL_BATCH_QUEUES = 16;

struct UDMADoorbellBatchGroup {
    UDMADoorbellBatchState queues[TILEXR_UDMA_MAX_DOORBELL_BATCH_QUEUES];
    uint32_t activeQueueCount;
    uint32_t fallbackDoorbellCount;
    uint32_t lastQueueIndex;
};

#if defined(TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT) && \
    TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT
struct UDMACQPollDiagnostic {
    uint32_t cqeWord0;
    uint32_t expectedOwner;
    uint32_t curTail;
    uint32_t targetIndex;
    uint32_t retries;
    uint32_t sqeWord0;
    uint32_t sqeWord1;
    uint32_t sqeWord2;
    uint32_t sqeWord3;
    uint64_t cqeAddress;
    uint64_t wqeAddress;
};
#endif

#if (defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_HELPERS_NOINLINE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_UDMA_HELPERS_NOINLINE) || \
    (defined(TILEXR_EP_URMA_DIAGNOSTIC_TILEXR_NOINLINE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_TILEXR_NOINLINE)
#define TILEXR_UDMA_HELPER_FUNCTION __attribute__((noinline))
#else
#define TILEXR_UDMA_HELPER_FUNCTION inline
#endif

#if (defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_POST_NOINLINE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_UDMA_POST_NOINLINE) || \
    (defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_HELPERS_NOINLINE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_UDMA_HELPERS_NOINLINE) || \
    (defined(TILEXR_EP_URMA_DIAGNOSTIC_TILEXR_NOINLINE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_TILEXR_NOINLINE)
#define TILEXR_UDMA_POST_FUNCTION __attribute__((noinline))
#else
#define TILEXR_UDMA_POST_FUNCTION inline
#endif

__aicore__ TILEXR_UDMA_HELPER_FUNCTION bool UDMAEnabled(const __gm__ CommArgs* args)
{
    return args != nullptr && ((args->extraFlag & ExtraFlag::UDMA) != 0) && args->udmaInfoPtr != nullptr;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION bool UDMARegistryEnabled(const __gm__ CommArgs* args)
{
    return UDMAEnabled(args) && args->udmaRegistryPtr != nullptr;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION __gm__ UDMAInfo* GetUDMAInfo(const __gm__ CommArgs* args)
{
    return reinterpret_cast<__gm__ UDMAInfo*>(args->udmaInfoPtr);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION __gm__ TileXRUDMARegistry* GetUDMARegistry(const __gm__ CommArgs* args)
{
    return reinterpret_cast<__gm__ TileXRUDMARegistry*>(args->udmaRegistryPtr);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION bool UDMARegisteredRangeValid(
    const __gm__ TileXRUDMARegistry* registry, int targetRank, uint64_t byteOffset, uint64_t byteCount)
{
    if (registry == nullptr || registry->magic != TILEXR_UDMA_REGISTRY_MAGIC ||
        registry->version != TILEXR_UDMA_REGISTRY_VERSION || registry->regionCount == 0 ||
        registry->rankSize > TILEXR_MAX_RANK_SIZE || targetRank < 0 ||
        static_cast<uint32_t>(targetRank) >= registry->rankSize) {
        return false;
    }
    const auto& region = registry->regions[targetRank];
    if (region.base == nullptr || byteOffset > region.bytes) {
        return false;
    }
    return byteCount <= region.bytes - byteOffset;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION __gm__ uint8_t* UDMARegisteredRemoteAddr(
    const __gm__ TileXRUDMARegistry* registry, int targetRank, uint64_t byteOffset)
{
    return reinterpret_cast<__gm__ uint8_t*>(registry->regions[targetRank].base + byteOffset);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMACleanCacheLines(__gm__ uint8_t* addr, uint64_t length)
{
    if (addr == nullptr || length == 0) {
        return;
    }
    __gm__ uint8_t* start = reinterpret_cast<__gm__ uint8_t*>(
        reinterpret_cast<uint64_t>(addr) / TILEXR_UDMA_CACHE_LINE_SIZE * TILEXR_UDMA_CACHE_LINE_SIZE);
    __gm__ uint8_t* end = reinterpret_cast<__gm__ uint8_t*>(
        (reinterpret_cast<uint64_t>(addr) + length - 1) / TILEXR_UDMA_CACHE_LINE_SIZE *
        TILEXR_UDMA_CACHE_LINE_SIZE);
    AscendC::GlobalTensor<uint8_t> global;
    global.SetGlobalBuffer(start);
    for (uint64_t i = 0; i <= static_cast<uint64_t>(end - start); i += TILEXR_UDMA_CACHE_LINE_SIZE) {
        __asm__ __volatile__("" ::: "memory");
        AscendC::DataCacheCleanAndInvalid<uint8_t,
            AscendC::CacheLine::SINGLE_CACHE_LINE, AscendC::DcciDst::CACHELINE_OUT>(global[i]);
        __asm__ __volatile__("" ::: "memory");
    }
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION __gm__ UDMAWQCtx* UDMAGetWQCtx(
    __gm__ UDMAInfo* udmaInfo, uint32_t pe, uint32_t qpIdx)
{
    uint32_t qpNum = udmaInfo->qpNum;
    return reinterpret_cast<__gm__ UDMAWQCtx*>(udmaInfo->sqPtr + (pe * qpNum + qpIdx) * sizeof(UDMAWQCtx));
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION __gm__ UDMACQCtx* UDMAGetSCQCtx(
    __gm__ UDMAInfo* udmaInfo, uint32_t pe, uint32_t qpIdx)
{
    uint32_t qpNum = udmaInfo->qpNum;
    return reinterpret_cast<__gm__ UDMACQCtx*>(udmaInfo->scqPtr + (pe * qpNum + qpIdx) * sizeof(UDMACQCtx));
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION __gm__ UDMAMemInfo* UDMAGetRemoteMemInfo(
    __gm__ UDMAInfo* udmaInfo, uint32_t pe, uint32_t qpIdx)
{
    return reinterpret_cast<__gm__ UDMAMemInfo*>(
        udmaInfo->memPtr + sizeof(UDMAMemInfo) * (pe * udmaInfo->qpNum + qpIdx));
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPollCQUpdateInfo(
    uint32_t curTail, __gm__ UDMACQCtx* cqCtxEntry, __gm__ UDMAWQCtx* wqCtxEntry)
{
    st_dev(static_cast<uint32_t>(curTail & 0xFFFFFF), reinterpret_cast<__gm__ uint32_t*>(cqCtxEntry->dbAddr), 0);
    st_dev(curTail, reinterpret_cast<__gm__ uint32_t*>(wqCtxEntry->tailAddr), 0);
}

#if defined(TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT) && \
    TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMACaptureCQPollDiagnostic(
    UDMACQPollDiagnostic* diagnostic, uint32_t cqeWord0, uint32_t expectedOwner,
    uint32_t curTail, uint32_t targetIndex, uint32_t retries,
    __gm__ UDMACqeCtx* cqeAddr, __gm__ UDMAWQCtx* wqCtxEntry)
{
    if (diagnostic == nullptr) {
        return;
    }
    const uint32_t wqeSize = 1U << wqCtxEntry->baseBkShift;
    __gm__ uint8_t* wqeAddr = reinterpret_cast<__gm__ uint8_t*>(
        wqCtxEntry->bufAddr + wqeSize * (curTail % TILEXR_UDMA_SQ_BB_COUNT));
    UDMACleanCacheLines(wqeAddr, 4U * sizeof(uint32_t));
    __asm__ __volatile__("" ::: "memory");
    __gm__ uint32_t* sqeWords = reinterpret_cast<__gm__ uint32_t*>(wqeAddr);
    diagnostic->cqeWord0 = cqeWord0;
    diagnostic->expectedOwner = expectedOwner;
    diagnostic->curTail = curTail;
    diagnostic->targetIndex = targetIndex;
    diagnostic->retries = retries;
    diagnostic->sqeWord0 = sqeWords[0];
    diagnostic->sqeWord1 = sqeWords[1];
    diagnostic->sqeWord2 = sqeWords[2];
    diagnostic->sqeWord3 = sqeWords[3];
    diagnostic->cqeAddress = reinterpret_cast<uint64_t>(cqeAddr);
    diagnostic->wqeAddress = reinterpret_cast<uint64_t>(wqeAddr);
    __asm__ __volatile__("" ::: "memory");
}
#endif

__aicore__ TILEXR_UDMA_HELPER_FUNCTION uint32_t UDMAPollCQ(
    __gm__ UDMAInfo* udmaInfo, uint32_t pe, uint32_t qpIdx, uint32_t idx
#if defined(TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT) && \
    TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT
    , UDMACQPollDiagnostic* diagnostic = nullptr
#endif
    )
{
    if (idx == 0) {
        return 0;
    }
    __gm__ UDMACQCtx* cqCtxEntry = UDMAGetSCQCtx(udmaInfo, pe, qpIdx);
    __gm__ UDMAWQCtx* wqCtxEntry = UDMAGetWQCtx(udmaInfo, pe, qpIdx);
    uint64_t cqBaseAddr = cqCtxEntry->bufAddr;
    uint32_t cqeSize = 1U << cqCtxEntry->baseBkShift;
    uint32_t curTail = ld_dev(reinterpret_cast<__gm__ uint32_t*>(cqCtxEntry->tailAddr), 0);
    while (curTail != idx) {
        __gm__ UDMACqeCtx* cqeAddr = reinterpret_cast<__gm__ UDMACqeCtx*>(
            cqBaseAddr + cqeSize * (curTail & (TILEXR_UDMA_CQ_DEPTH - 1)));
        bool validOwner = ((curTail / TILEXR_UDMA_CQ_DEPTH) & 1) != 0;
        uint32_t times = 0;
        uint32_t cqeWord0 = 0;
        while (times < TILEXR_UDMA_MAX_RETRY_TIMES) {
            UDMACleanCacheLines(reinterpret_cast<__gm__ uint8_t*>(cqeAddr), sizeof(UDMACqeCtx));
            __asm__ __volatile__("" ::: "memory");
            cqeWord0 = reinterpret_cast<__gm__ uint32_t*>(cqeAddr)[0];
            __asm__ __volatile__("" ::: "memory");
            const bool owner = (cqeWord0 & (1U << 2U)) != 0;
            if ((validOwner ^ owner) != 0) {
                break;
            }
            ++times;
        }
        if (times >= TILEXR_UDMA_MAX_RETRY_TIMES) {
#if defined(TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT) && \
    TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT
            UDMACaptureCQPollDiagnostic(diagnostic, cqeWord0,
                validOwner ? 0U : 1U, curTail, idx, times, cqeAddr, wqCtxEntry);
#endif
            return 0xFF;
        }
        uint8_t status = static_cast<uint8_t>((cqeWord0 >> 24U) & 0xFFU);
        uint8_t subStatus = static_cast<uint8_t>((cqeWord0 >> 16U) & 0xFFU);
        if (status != 0 || subStatus != 0) {
#if defined(TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT) && \
    TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT
            UDMACaptureCQPollDiagnostic(diagnostic, cqeWord0,
                validOwner ? 0U : 1U, curTail, idx, times, cqeAddr, wqCtxEntry);
#endif
            return (static_cast<uint32_t>(status) << 8) | subStatus;
        }
        ++curTail;
    }
    st_dev(curTail, reinterpret_cast<__gm__ uint32_t*>(cqCtxEntry->tailAddr), 0);
    UDMAPollCQUpdateInfo(curTail, cqCtxEntry, wqCtxEntry);
    return 0;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION uint32_t UDMAWqeBBCnt(UDMAOpcode opcode)
{
    return opcode == UDMAOpcode::WRITE_WITH_NOTIFY ? 2U : 1U;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION __gm__ uint8_t* UDMAGetSgeCtxAddr(
    __gm__ uint8_t* wqeAddr, UDMAOpcode opcode)
{
    if (opcode == UDMAOpcode::WRITE_WITH_NOTIFY) {
        return wqeAddr + sizeof(UDMASqeCtx) + sizeof(UDMANotifyCtx);
    }
    return wqeAddr + sizeof(UDMASqeCtx);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAFillNotifyData(
    __gm__ UDMASqeCtx* sqeCtx, uint32_t tid, uint32_t tokenValue, const UDMASignalParams* params)
{
    if (params == nullptr) {
        return;
    }
    __gm__ UDMANotifyCtx* notifyCtx =
        reinterpret_cast<__gm__ UDMANotifyCtx*>(reinterpret_cast<__gm__ uint8_t*>(sqeCtx) + sizeof(UDMASqeCtx));
    __gm__ uint32_t* notifyWords = reinterpret_cast<__gm__ uint32_t*>(notifyCtx);
    const uint64_t notifyAddr = reinterpret_cast<uint64_t>(params->sigAddr);
    notifyWords[0] = tid & 0xFFFFFU;
    notifyWords[1] = tokenValue;
    notifyWords[2] = static_cast<uint32_t>(notifyAddr & 0xFFFFFFFFU);
    notifyWords[3] = static_cast<uint32_t>((notifyAddr >> 32) & 0xFFFFFFFFU);
    notifyWords[4] = static_cast<uint32_t>(params->signal & 0xFFFFFFFFU);
    notifyWords[5] = static_cast<uint32_t>((params->signal >> 32) & 0xFFFFFFFFU);
    notifyWords[6] = 0;
    notifyWords[7] = 0;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAFillSqeCtx(
    __gm__ UDMASqeCtx* sqeCtx, __gm__ uint8_t* remoteAddr, __gm__ UDMAMemInfo* remoteMemInfo,
    uint32_t curHead, uint32_t depth, UDMAOpcode opcode, const UDMASignalParams* signalParams)
{
    constexpr uint32_t sqeFlag = 0b00100010U;
    const uint32_t sqeBbIdx = curHead % depth;
    const uint32_t tokenEn = remoteMemInfo->tokenValueValid ? 1U : 0U;
    const uint32_t rmtJettyType = remoteMemInfo->rmtJettyType & 0x3U;
    const uint32_t owner = (curHead & depth) == 0U ? 1U : 0U;
    __gm__ uint32_t* sqeWords = reinterpret_cast<__gm__ uint32_t*>(sqeCtx);

    // Store complete ABI words so optimized code never reads stale reserved bits from a reused SQ slot.
    sqeWords[0] = (sqeBbIdx & 0xFFFFU) | (sqeFlag << 16) |
        (tokenEn << 28) | (rmtJettyType << 29) | (owner << 31);
    sqeWords[1] = (static_cast<uint32_t>(remoteMemInfo->targetHint) & 0xFFU) |
        ((static_cast<uint32_t>(opcode) & 0xFFU) << 8);
    sqeWords[2] = (remoteMemInfo->tpn & 0xFFFFFFU) | (1U << 24);
    sqeWords[3] = remoteMemInfo->tid & 0xFFFFFU;

    __gm__ uint32_t* rmtEidWords = reinterpret_cast<__gm__ uint32_t*>(remoteMemInfo->eidAddr);
    sqeWords[4] = rmtEidWords[0];
    sqeWords[5] = rmtEidWords[1];
    sqeWords[6] = rmtEidWords[2];
    sqeWords[7] = rmtEidWords[3];
    sqeWords[8] = remoteMemInfo->rmtTokenValue;
    sqeWords[9] = 0;

    const uint64_t remoteAddrValue = reinterpret_cast<uint64_t>(remoteAddr);
    sqeWords[10] = static_cast<uint32_t>(remoteAddrValue & 0xFFFFFFFFU);
    sqeWords[11] = static_cast<uint32_t>((remoteAddrValue >> 32) & 0xFFFFFFFFU);
    UDMAFillNotifyData(sqeCtx, remoteMemInfo->tid, remoteMemInfo->rmtTokenValue, signalParams);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAFillSgeCtx(
    __gm__ UDMASgeCtx* sgeCtx, uint64_t messageLen, __gm__ uint8_t* localAddr)
{
    __gm__ uint32_t* sgeWords = reinterpret_cast<__gm__ uint32_t*>(sgeCtx);
    const uint64_t localAddrValue = reinterpret_cast<uint64_t>(localAddr);
    sgeWords[0] = static_cast<uint32_t>(messageLen);
    sgeWords[1] = 0;
    sgeWords[2] = static_cast<uint32_t>(localAddrValue & 0xFFFFFFFFU);
    sgeWords[3] = static_cast<uint32_t>((localAddrValue >> 32) & 0xFFFFFFFFU);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPublishWqe(
    __gm__ uint8_t* wqeAddr, uint64_t length)
{
#if (defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_COMPILER_FENCE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_COMPILER_FENCE) || \
    (defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE)
    __asm__ __volatile__("" ::: "memory");
#endif
    AscendC::PipeBarrier<PIPE_ALL>();
#if defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE) && \
    TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE
    AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();
#endif
    UDMACleanCacheLines(wqeAddr, length);
#if (defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_COMPILER_FENCE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_COMPILER_FENCE) || \
    (defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE) && \
     TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE)
    __asm__ __volatile__("" ::: "memory");
#endif
#if defined(TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE) && \
    TILEXR_EP_URMA_DIAGNOSTIC_UDMA_WQE_RELEASE_FENCE
    AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();
#endif
    AscendC::PipeBarrier<PIPE_ALL>();
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPollCQWhenSQOverflow(
    __gm__ UDMAInfo* udmaInfo, __gm__ UDMAWQCtx* qpCtxEntry, uint32_t wqeCnt, uint32_t pe, uint32_t qpIdx)
{
    constexpr uint32_t pollCQThreshold = 10;
    uint32_t curTail = ld_dev(reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->tailAddr), 0);
    if ((wqeCnt + pollCQThreshold) % TILEXR_UDMA_SQ_BB_COUNT == curTail % TILEXR_UDMA_SQ_BB_COUNT) {
        uint32_t idx = (curTail + TILEXR_UDMA_NUM_CQE_PER_POLL) > wqeCnt ?
            wqeCnt : curTail + TILEXR_UDMA_NUM_CQE_PER_POLL;
        (void)UDMAPollCQ(udmaInfo, pe, qpIdx, idx);
    }
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPostSendUpdateInfo(
    uint32_t curHead, __gm__ UDMAWQCtx* qpCtxEntry)
{
    st_dev(curHead, reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->dbAddr), 0);
    st_dev(curHead, reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->headAddr), 0);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAFlushDoorbellBatch(
    UDMADoorbellBatchState* state)
{
    if (state == nullptr || state->pendingWqes == 0) {
        return;
    }
    AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();
    AscendC::PipeBarrier<PIPE_ALL>();
    st_dev(state->latestHead, reinterpret_cast<__gm__ uint32_t*>(state->dbAddr), 0);
    state->pendingWqes = 0;
    ++state->doorbellCount;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION UDMADoorbellBatchState* UDMAGetDoorbellBatchState(
    const __gm__ CommArgs* args, uint32_t pe, uint32_t qpIdx, UDMADoorbellBatchGroup* group)
{
    if (!UDMAEnabled(args) || group == nullptr) {
        return nullptr;
    }
    __gm__ UDMAInfo* udmaInfo = GetUDMAInfo(args);
    if (qpIdx >= udmaInfo->qpNum) {
        return nullptr;
    }
    __gm__ UDMAWQCtx* qpCtxEntry = UDMAGetWQCtx(udmaInfo, pe, qpIdx);
    const uint64_t queueId = qpCtxEntry->wqeCntAddr;
    if (group->activeQueueCount > 0 && group->lastQueueIndex < group->activeQueueCount &&
        group->queues[group->lastQueueIndex].queueId == queueId) {
        return &group->queues[group->lastQueueIndex];
    }
    for (uint32_t index = 0; index < group->activeQueueCount; ++index) {
        if (group->queues[index].queueId == queueId) {
            group->lastQueueIndex = index;
            return &group->queues[index];
        }
    }
    if (group->activeQueueCount >= TILEXR_UDMA_MAX_DOORBELL_BATCH_QUEUES) {
        return nullptr;
    }
    group->lastQueueIndex = group->activeQueueCount;
    UDMADoorbellBatchState* state = &group->queues[group->activeQueueCount++];
    state->queueId = queueId;
    state->dbAddr = qpCtxEntry->dbAddr;
    return state;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAFlushDoorbellBatchGroup(
    UDMADoorbellBatchGroup* group)
{
    if (group == nullptr) {
        return;
    }
    for (uint32_t index = 0; index < group->activeQueueCount; ++index) {
        UDMAFlushDoorbellBatch(&group->queues[index]);
    }
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION uint32_t UDMADoorbellBatchGroupCommitCount(
    const UDMADoorbellBatchGroup* group)
{
    if (group == nullptr) {
        return 0;
    }
    uint32_t count = group->fallbackDoorbellCount;
    for (uint32_t index = 0; index < group->activeQueueCount; ++index) {
        count += group->queues[index].doorbellCount;
    }
    return count;
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMASelectDoorbellBatchQueue(
    UDMADoorbellBatchState* state, __gm__ UDMAWQCtx* qpCtxEntry)
{
    const uint64_t queueId = qpCtxEntry->wqeCntAddr;
    if (state->queueId == queueId) {
        return;
    }
    UDMAFlushDoorbellBatch(state);
    if (state->queueId != 0) {
        ++state->queueSwitchCount;
    }
    state->queueId = queueId;
    state->dbAddr = qpCtxEntry->dbAddr;
}

__aicore__ TILEXR_UDMA_POST_FUNCTION void UDMAPostSend(
    __gm__ UDMAInfo* udmaInfo, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr,
    uint32_t pe, uint32_t qpIdx, uint64_t messageLen, UDMAOpcode opcode, const UDMASignalParams* signalParams)
{
    __gm__ UDMAWQCtx* qpCtxEntry = UDMAGetWQCtx(udmaInfo, pe, qpIdx);
    uint32_t wqeSize = 1U << qpCtxEntry->baseBkShift;
    uint32_t curHead = ld_dev(reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->headAddr), 0);
    uint32_t wqeCnt = ld_dev(reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->wqeCntAddr), 0);
    UDMAPollCQWhenSQOverflow(udmaInfo, qpCtxEntry, wqeCnt, pe, qpIdx);

    __gm__ UDMAMemInfo* remoteMemInfo = UDMAGetRemoteMemInfo(udmaInfo, pe, qpIdx);
    __gm__ uint8_t* wqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(qpCtxEntry->bufAddr + wqeSize * (curHead % TILEXR_UDMA_SQ_BB_COUNT));
    __gm__ UDMASqeCtx* sqeCtx = reinterpret_cast<__gm__ UDMASqeCtx*>(wqeAddr);
    UDMAFillSqeCtx(sqeCtx, remoteAddr, remoteMemInfo, curHead, qpCtxEntry->depth, opcode, signalParams);

    __gm__ UDMASgeCtx* sgeCtx = reinterpret_cast<__gm__ UDMASgeCtx*>(UDMAGetSgeCtxAddr(wqeAddr, opcode));
    UDMAFillSgeCtx(sgeCtx, messageLen, localAddr);
    uint32_t wqeBbCnt = UDMAWqeBBCnt(opcode);
    UDMAPublishWqe(wqeAddr, wqeSize * wqeBbCnt);
    curHead += wqeBbCnt;
    UDMAPostSendUpdateInfo(curHead, qpCtxEntry);
    ++wqeCnt;
    st_dev(wqeCnt, reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->wqeCntAddr), 0);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPostSendDoorbellBatched(
    __gm__ UDMAInfo* udmaInfo, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr,
    uint32_t pe, uint32_t qpIdx, uint64_t messageLen, UDMAOpcode opcode,
    const UDMASignalParams* signalParams, uint32_t batchSize, UDMADoorbellBatchState* state)
{
    if (batchSize <= 1 || state == nullptr) {
        UDMAPostSend(udmaInfo, remoteAddr, localAddr, pe, qpIdx, messageLen, opcode, signalParams);
        return;
    }

    __gm__ UDMAWQCtx* qpCtxEntry = UDMAGetWQCtx(udmaInfo, pe, qpIdx);
    UDMASelectDoorbellBatchQueue(state, qpCtxEntry);
    uint32_t wqeSize = 1U << qpCtxEntry->baseBkShift;
    uint32_t curHead = ld_dev(reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->headAddr), 0);
    uint32_t wqeCnt = ld_dev(reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->wqeCntAddr), 0);

    constexpr uint32_t pollCQThreshold = 10;
    uint32_t curTail = ld_dev(reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->tailAddr), 0);
    if ((wqeCnt + pollCQThreshold) % TILEXR_UDMA_SQ_BB_COUNT ==
        curTail % TILEXR_UDMA_SQ_BB_COUNT) {
        // CQ polling must never wait for a WQE that is still hidden behind a deferred doorbell.
        UDMAFlushDoorbellBatch(state);
        uint32_t idx = (curTail + TILEXR_UDMA_NUM_CQE_PER_POLL) > wqeCnt ?
            wqeCnt : curTail + TILEXR_UDMA_NUM_CQE_PER_POLL;
        (void)UDMAPollCQ(udmaInfo, pe, qpIdx, idx);
    }

    __gm__ UDMAMemInfo* remoteMemInfo = UDMAGetRemoteMemInfo(udmaInfo, pe, qpIdx);
    __gm__ uint8_t* wqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(qpCtxEntry->bufAddr + wqeSize *
            (curHead % TILEXR_UDMA_SQ_BB_COUNT));
    __gm__ UDMASqeCtx* sqeCtx = reinterpret_cast<__gm__ UDMASqeCtx*>(wqeAddr);
    UDMAFillSqeCtx(sqeCtx, remoteAddr, remoteMemInfo, curHead, qpCtxEntry->depth, opcode, signalParams);

    __gm__ UDMASgeCtx* sgeCtx = reinterpret_cast<__gm__ UDMASgeCtx*>(
        UDMAGetSgeCtxAddr(wqeAddr, opcode));
    UDMAFillSgeCtx(sgeCtx, messageLen, localAddr);
    uint32_t wqeBbCnt = UDMAWqeBBCnt(opcode);
    UDMAPublishWqe(wqeAddr, wqeSize * wqeBbCnt);
    curHead += wqeBbCnt;
    st_dev(curHead, reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->headAddr), 0);
    ++wqeCnt;
    st_dev(wqeCnt, reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->wqeCntAddr), 0);

    state->latestHead = curHead;
    ++state->pendingWqes;
    if (state->pendingWqes >= batchSize) {
        UDMAFlushDoorbellBatch(state);
    }
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAWrite(
    const __gm__ CommArgs* args, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr,
    uint32_t pe, uint32_t qpIdx, uint64_t messageLen)
{
    if constexpr (TILEXR_UDMA_ARCH_SUPPORTED) {
        UDMAPostSend(GetUDMAInfo(args), remoteAddr, localAddr, pe, qpIdx, messageLen, UDMAOpcode::WRITE, nullptr);
    }
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAWriteDoorbellBatched(
    const __gm__ CommArgs* args, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr,
    uint32_t pe, uint32_t qpIdx, uint64_t messageLen, uint32_t batchSize,
    UDMADoorbellBatchState* state)
{
    if constexpr (TILEXR_UDMA_ARCH_SUPPORTED) {
        UDMAPostSendDoorbellBatched(GetUDMAInfo(args), remoteAddr, localAddr, pe, qpIdx,
            messageLen, UDMAOpcode::WRITE, nullptr, batchSize, state);
    }
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMARead(
    const __gm__ CommArgs* args, __gm__ uint8_t* localAddr, __gm__ uint8_t* remoteAddr,
    uint32_t pe, uint32_t qpIdx, uint64_t messageLen)
{
    if constexpr (TILEXR_UDMA_ARCH_SUPPORTED) {
        UDMAPostSend(GetUDMAInfo(args), remoteAddr, localAddr, pe, qpIdx, messageLen, UDMAOpcode::READ, nullptr);
    }
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAWriteNotify(
    const __gm__ CommArgs* args, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr,
    uint32_t pe, uint32_t qpIdx, uint64_t messageLen, const UDMASignalParams* signalParams)
{
    if constexpr (TILEXR_UDMA_ARCH_SUPPORTED) {
        UDMAPostSend(GetUDMAInfo(args), remoteAddr, localAddr, pe, qpIdx, messageLen,
                     UDMAOpcode::WRITE_WITH_NOTIFY, signalParams);
    }
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPutNbi(
    const __gm__ CommArgs* args, int targetRank, const __gm__ T* localSrc, uint64_t byteOffset,
    uint32_t byteCount, uint32_t qpIdx)
{
    if (!UDMARegistryEnabled(args)) return;

    auto udmaInfo = GetUDMAInfo(args);
    if (qpIdx >= udmaInfo->qpNum) return;

    auto registry = GetUDMARegistry(args);
    if (!UDMARegisteredRangeValid(registry, targetRank, byteOffset, byteCount)) return;

    auto remoteAddr = UDMARegisteredRemoteAddr(registry, targetRank, byteOffset);
    UDMAWrite(args, remoteAddr, reinterpret_cast<__gm__ uint8_t*>(const_cast<__gm__ T*>(localSrc)),
              targetRank, qpIdx, byteCount);
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPutNbiDoorbellBatched(
    const __gm__ CommArgs* args, int targetRank, const __gm__ T* localSrc, uint64_t byteOffset,
    uint32_t byteCount, uint32_t qpIdx, uint32_t batchSize, UDMADoorbellBatchState* state)
{
    if (!UDMARegistryEnabled(args)) return;

    auto udmaInfo = GetUDMAInfo(args);
    if (qpIdx >= udmaInfo->qpNum) return;

    auto registry = GetUDMARegistry(args);
    if (!UDMARegisteredRangeValid(registry, targetRank, byteOffset, byteCount)) return;

    auto remoteAddr = UDMARegisteredRemoteAddr(registry, targetRank, byteOffset);
    UDMAWriteDoorbellBatched(args, remoteAddr,
        reinterpret_cast<__gm__ uint8_t*>(const_cast<__gm__ T*>(localSrc)), targetRank, qpIdx,
        byteCount, batchSize, state);
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPutNbi(
    const __gm__ CommArgs* args, int targetRank, const __gm__ T* localSrc, uint64_t byteOffset, uint32_t byteCount)
{
    UDMAPutNbi<T>(args, targetRank, localSrc, byteOffset, byteCount, 0);
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPutRegisteredNbi(
    const __gm__ CommArgs* args, int targetRank, const __gm__ T* localSrc, uint64_t byteOffset, uint32_t byteCount)
{
    UDMAPutNbi<T>(args, targetRank, localSrc, byteOffset, byteCount);
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAGetNbi(
    const __gm__ CommArgs* args, int sourceRank, __gm__ T* localDst, uint64_t byteOffset, uint32_t byteCount)
{
    if (!UDMARegistryEnabled(args)) return;

    auto registry = GetUDMARegistry(args);
    if (!UDMARegisteredRangeValid(registry, sourceRank, byteOffset, byteCount)) return;

    auto remoteAddr = UDMARegisteredRemoteAddr(registry, sourceRank, byteOffset);
    UDMARead(args, reinterpret_cast<__gm__ uint8_t*>(localDst), remoteAddr, sourceRank, 0, byteCount);
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAGetRegisteredNbi(
    const __gm__ CommArgs* args, int sourceRank, __gm__ T* localDst, uint64_t byteOffset, uint32_t byteCount)
{
    UDMAGetNbi<T>(args, sourceRank, localDst, byteOffset, byteCount);
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPutSignalNbi(
    const __gm__ CommArgs* args, int targetRank, const __gm__ T* localSrc, uint64_t byteOffset,
    uint32_t byteCount, uint64_t signalByteOffset, uint64_t signal)
{
    if (!UDMARegistryEnabled(args)) return;

    auto registry = GetUDMARegistry(args);
    if (!UDMARegisteredRangeValid(registry, targetRank, byteOffset, byteCount) ||
        !UDMARegisteredRangeValid(registry, targetRank, signalByteOffset, sizeof(uint64_t))) {
        return;
    }

    UDMASignalParams signalParams = {};
    signalParams.sigAddr = reinterpret_cast<__gm__ uint64_t*>(
        UDMARegisteredRemoteAddr(registry, targetRank, signalByteOffset));
    signalParams.signal = signal;
    auto remoteAddr = UDMARegisteredRemoteAddr(registry, targetRank, byteOffset);
    UDMAWriteNotify(args, remoteAddr, reinterpret_cast<__gm__ uint8_t*>(const_cast<__gm__ T*>(localSrc)),
                    targetRank, 0, byteCount, &signalParams);
}

template <typename T>
__aicore__ TILEXR_UDMA_HELPER_FUNCTION void UDMAPutRegisteredSignalNbi(
    const __gm__ CommArgs* args, int targetRank, const __gm__ T* localSrc, uint64_t byteOffset,
    uint32_t byteCount, uint64_t signalByteOffset, uint64_t signal)
{
    UDMAPutSignalNbi<T>(args, targetRank, localSrc, byteOffset, byteCount, signalByteOffset, signal);
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION uint32_t UDMAQuiet(
    const __gm__ CommArgs* args, int targetRank, uint32_t qpIdx
#if defined(TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT) && \
    TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT
    , UDMACQPollDiagnostic* diagnostic = nullptr
#endif
    )
{
    if (!UDMAEnabled(args)) return 0;
    __gm__ UDMAInfo* udmaInfo = GetUDMAInfo(args);
    if (qpIdx >= udmaInfo->qpNum) return 0;
    __gm__ UDMAWQCtx* qpCtxEntry = UDMAGetWQCtx(udmaInfo, targetRank, qpIdx);
    uint32_t wqeCnt = ld_dev(reinterpret_cast<__gm__ uint32_t*>(qpCtxEntry->wqeCntAddr), 0);
    return UDMAPollCQ(udmaInfo, targetRank, qpIdx, wqeCnt
#if defined(TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT) && \
    TILEXR_EP_URMA_DIAGNOSTIC_PROGRESS_TIMEOUT
        , diagnostic
#endif
        );
}

__aicore__ TILEXR_UDMA_HELPER_FUNCTION uint32_t UDMAQuiet(
    const __gm__ CommArgs* args, int targetRank)
{
    return UDMAQuiet(args, targetRank, 0);
}

#undef TILEXR_UDMA_POST_FUNCTION
#undef TILEXR_UDMA_HELPER_FUNCTION

} // namespace TileXR

#endif // TILEXR_UDMA_H
