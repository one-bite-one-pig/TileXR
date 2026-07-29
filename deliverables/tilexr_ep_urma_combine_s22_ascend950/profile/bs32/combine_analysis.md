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
- Kernel headline source: total-only strict-equivalent profile
- Headline max-core -> max-rank -> launch median: 41.126 us
- Coarse attribution kernel median: 40.653 us
- Fine attribution kernel median: 44.639 us
- Representative detailed launch: launch0 (nearest the headline median)
- Start Gate executed launches: 10/10; executed-only medians: latency=58.758 us, WQEs=56.000, doorbells=56.000, quiets=56.000
- Cross-rank median unprofiled host round: 162.495 us
- Cross-rank median fine-run unprofiled host round: 175.230 us
- Cross-rank median coarse-profile host round: 134.710 us (-17.10% difference; treat a negative value as run-to-run noise)
- Cross-rank median fine-profile host round: 204.705 us (+16.82% intrusion)
- Representative-launch cross-rank median device critical path: 39.572 us
- Cross-rank median critical Pack phase: 5.222 us
- Cross-rank median critical Receive phase: 26.539 us
- Cross-rank median critical Send phase: 25.517 us
- Cross-rank median coarse Send-core OLS slope: -0.033 us/core
- Cross-rank median coarse Send-core spread: 5.972 us
- Cross-rank median coarse Send-core spread / mean: 25.240%
- Cross-rank median WQEs per doorbell: 1.000
- Cross-rank median Round Publish control WQEs: 7.000
- Cross-rank median Round Publish control doorbells: 7.000
- Cross-rank median Round Publish completion waits: 7.000
- Cross-rank median RX flag checks: 1347.000
- Cross-rank median RX flag miss ratio: 85.745%
- Cross-rank median RX bypassed tokens: 0.000
- Cross-rank median RX out-of-order completions: 0.000
- Cross-rank median active SelfCopy substage max: 3.185 us
- Cross-rank median sender0 tail: 17.215 us
- Cross-rank median max per-core explicit DCCI span: 0.000 us
- Cross-rank median aggregate explicit DCCI core-time: 0.000 us (tx_data=0.000, rx_flag=0.000, rx_data=0.000, control_other=0.000)
- Cross-rank median explicit DCCI share of all AIV kernel core-time: 0.00%
- Cross-rank median explicit DCCI share on the fine-profile critical core: 0.00% (dcci=0.000 us, kernel=43.439 us)
- Cross-rank median explicit DCCI share of Pack+Receive core-time: 0.00%
- Cross-rank median metadata scan amplification: 1.000x
- Cross-rank median Send metadata scan amplification: 1.000x
- Cross-rank median TxReady attempt miss ratio: 3.030%

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
