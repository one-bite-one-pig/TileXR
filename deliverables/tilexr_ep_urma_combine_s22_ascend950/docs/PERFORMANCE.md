# Performance Result

## Production winner

All values below are raw cycles from valid 100-round strict captures, using
max-core -> max-rank -> median100.

| Layout | BS32 median | BS128 median |
|---|---:|---:|
| P42/S22 | 43,130 | 94,889 |
| P36/S28 | 46,716 | 115,397 |

S22 improves over S28 by 7.68% at BS32 and 17.77% at BS128. The corresponding
display values are 43.130 us and 94.889 us under the established `/1000`
conversion.

## Profile timing convergence

For S22 BS32:

- strict100 median: 43,130 cycles;
- matched strict10 median: 41,703.5 cycles;
- total-only profile10 median: 41,126 cycles.

Total-only differs from the matched strict window by -1.38%. This demonstrates
that the old large strict/profile gap came from intrusive stage instrumentation
and mismatched sampling windows, not a production kernel cost.

## Attribution highlights

The delivered reports show these cross-rank medians on their representative
attribution launches:

| BS | Pack | Receive | Send | Sender spread |
|---|---:|---:|---:|---:|
| 32 | 5.222 us | 26.539 us | 25.517 us | 5.972 us |
| 128 | 18.638 us | 58.795 us | 74.870 us | 6.310 us |

These stage maxima may come from different cores and overlap. They must not be
summed as a critical path or used to choose the production layout.

## Start-gate presentation

Start gate is a first-launch coordination preamble. In the captured profiling
runs its executed-only median is 58.758 us for BS32 and 259.991 us for BS128.
Showing it proportionally compresses the actual Pack/Receive/Send region into a
small portion of the chart.

The delivery report therefore:

1. retains gate execution count, latency, WQEs, and doorbells in the header;
2. removes `start_gate` from phase bars and the fine heatmap by default;
3. rebases the detailed timeline to the first visible steady-state stage;
4. supports `--show-start-gate` for audit views.

No raw timing value is altered.
