# TileXR EP URMA Combine Profile

- Ranks captured: 8
- Core roles: 42 pack_rx + 22 send
- Shared ready flag: off
- TX ready in data: off
- TX metadata full prefetch: off
- TX early ready publish: off
- RX sticky ready mask: on
- RX batched MTE2 ready reads: off
- RX Vector ready reduction: off
- Statistic scope: 10 paired distributed launches; per launch take max core per rank, then max rank; headline is the median across launches
- Kernel timing boundary: pipe_all_bracketed_pre_flush
- Kernel headline source: coarse attribution profile
- Headline max-core -> max-rank -> launch median: 89.431 us
- Coarse attribution kernel median: 89.431 us
- Fine attribution kernel median: 97.513 us
- Representative detailed launch: launch8 (nearest the headline median)
- Start Gate executed launches: 10/10; executed-only medians: latency=259.991 us, WQEs=56.000, doorbells=56.000, quiets=56.000
- Cross-rank median unprofiled host round: 479.811 us
- Cross-rank median fine-run unprofiled host round: 421.475 us
- Cross-rank median coarse-profile host round: 482.455 us (+0.55% difference; treat a negative value as run-to-run noise)
- Cross-rank median fine-profile host round: 415.325 us (-1.46% intrusion)
- Representative-launch cross-rank median device critical path: 87.061 us
- Cross-rank median critical Pack phase: 18.638 us
- Cross-rank median critical Receive phase: 58.795 us
- Cross-rank median critical Send phase: 74.870 us
- Cross-rank median coarse Send-core OLS slope: 0.039 us/core
- Cross-rank median coarse Send-core spread: 6.310 us
- Cross-rank median coarse Send-core spread / mean: 8.810%
- Cross-rank median WQEs per doorbell: 1.000
- Cross-rank median Round Publish control WQEs: 7.000
- Cross-rank median Round Publish control doorbells: 7.000
- Cross-rank median Round Publish completion waits: 7.000
- Cross-rank median RX flag checks: 3648.000
- Cross-rank median RX flag miss ratio: 78.947%
- Cross-rank median RX bypassed tokens: 64.500
- Cross-rank median RX out-of-order completions: 47.000
- Cross-rank median active SelfCopy substage max: 6.717 us
- Cross-rank median sender0 tail: 16.334 us
- Cross-rank median max per-core explicit DCCI span: 0.000 us
- Cross-rank median aggregate explicit DCCI core-time: 0.000 us (tx_data=0.000, rx_flag=0.000, rx_data=0.000, control_other=0.000)
- Cross-rank median explicit DCCI share of all AIV kernel core-time: 0.00%
- Cross-rank median explicit DCCI share on the fine-profile critical core: 0.00% (dcci=0.000 us, kernel=96.299 us)
- Cross-rank median explicit DCCI share of Pack+Receive core-time: 0.00%
- Cross-rank median metadata scan amplification: 1.000x
- Cross-rank median Send metadata scan amplification: 1.000x
- Cross-rank median TxReady attempt miss ratio: 21.512%

The HTML coarse timeline uses first/last activity envelopes. Its receive_total composition comes from
same-core fine exposed wall-time while the double-buffer pipeline remains enabled; the colored parts are not
a chronological sequence and must not be added to infer hardware-engine occupancy.
The dequant+clear interval also contains next-route unpack submission bookkeeping, but not its asynchronous
in-flight transfer time.
Repeated fine stages are compared by sum_us in the heatmap, and their envelope must not be interpreted as
continuous execution.
Explicit DCCI spans cover combine-kernel UDMACleanCacheLines calls but exclude surrounding barriers and the
SQ/CQ cache maintenance inside UDMA post/quiet. Category totals are accumulated core-time.
The all-AIV ratio describes aggregate kernel core-time; the critical-core ratio is the latency-oriented
view. Neither ratio is a direct prediction of speedup because AIVs execute concurrently and synchronize.
Device clocks are normalized independently per rank, so cross-rank absolute event ordering is intentionally
not inferred. Fine-stage durations include material instrumentation overhead.
