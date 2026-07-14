#ifndef LCCL_ALLGATHER_NAIVE_H
#define LCCL_ALLGATHER_NAIVE_H

#include "collectives.h"
template<typename T>
class AllGatherNaive :public Collectives{
public:
    FORCE_INLINE_AICORE AllGatherNaive(
        int rank,
        int rankSize,
        uint32_t extraFlag)
        :Collectives(rank,rankSize,extraFlag){
        }
    FORCE_INLINE_AICORE void Init(KERNELS_ARGS_FUN()){
        Collectives::Init(KERNELS_ARGS_CALL());

        GetBlockSlice(len,blockIdx,blockNum,publishOffset,publishCount);
        inputGm.SetGlobalBuffer((__gm__ T*)input+publishOffset,publishCount);
        publishGm.SetGlobalBuffer((__gm__ T*)(shareAddrs[rank]+IPC_DATA_OFFSET)+publishOffset,publishCount);
        blocksPerRank=blockNum/rankSize;
        activeBlockCount=blocksPerRank*rankSize;
        isActiveBlock=(blockIdx<activeBlockCount);

        if(!isActiveBlock) return;

        sourceRank=blockIdx/blocksPerRank;
        const int64_t blockIdxWithinRank=blockIdx%blocksPerRank;

        GetBlockSlice(len,blockIdxWithinRank,blocksPerRank,sourceOffset,sourceCount);
        outputGm.SetGlobalBuffer((__gm__ T*)output+sourceRank*len+sourceOffset,sourceCount);

        sourceGm.SetGlobalBuffer((__gm__ T*)(shareAddrs[sourceRank]+IPC_DATA_OFFSET)+sourceOffset,sourceCount);

    }
    FORCE_INLINE_AICORE void Process()
    {
        if(publishCount>0){
            CpGM2GM<T>(
                       publishGm,
                       inputGm,
                       publishCount,
                       COPYONLY);
        }
        sync.SetInnerFlag(magic,PUBLISH_READY);

        if(!isActiveBlock) return;
        sync.WaitRankInnerFlag(magic,PUBLISH_READY,sourceRank);
        if(sourceCount>0){
        CpGM2GM<T>(
                outputGm,
                sourceGm,
                sourceCount,
                COPYONLY
               );
        }
    }

private:
    FORCE_INLINE_AICORE void GetBlockSlice(int64_t len,int64_t workerid,int64_t workercount, int64_t & offset,int64_t &count){
        count=CeilDiv(len,workercount);
        offset=count*workerid;

        if(offset>=len){
            offset=len;
            count=0;
        }
        else if(offset+count>len){
            count=len-offset;
        }
    }


private:
    static constexpr int32_t PUBLISH_READY=1;
    GlobalTensor<T> outputGm;
    GlobalTensor<T> inputGm;
    GlobalTensor<T> publishGm;

    GlobalTensor<T> sourceGm;

    int64_t blocksPerRank=0;
    int64_t activeBlockCount=0;
    bool isActiveBlock=false;

    int64_t publishOffset=0;
    int64_t publishCount=0;

    int sourceRank=0;
    int64_t sourceOffset=0;
    int64_t sourceCount=0;
};

#endif // LCCL_ALLGATHER_NAIVE_H
