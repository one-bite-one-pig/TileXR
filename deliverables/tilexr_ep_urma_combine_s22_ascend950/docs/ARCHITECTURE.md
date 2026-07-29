# Architecture

## Data path

The operator uses 64 logical AIV blocks on Ascend 950:

- cores 0-41: Pack local expert output, publish TX readiness, poll RX readiness,
  dequantize, and write output;
- cores 42-63: scan assigned remote routes and submit UDMA sends through 22
  QPs.

The fixed routing seed used by the evidence is `20260728`; the tested shape is
H=5120, top-k=6, rank-size=8, enqueue-window=1.

## Protocol choices

S22 retains the protocol operations required for safe buffer reuse:

- per-route TX ready publication;
- RX sticky ready masks under the round-robin scheduler;
- parallel global round publication;
- deferred credit wait before reusing the same ping-pong parity;
- a start gate on the first launch after stream synchronization.

Ping-pong buffering does not remove ownership publication. It allows the next
round to proceed without an unconditional end-of-round global barrier, while
the deferred credit check protects the next reuse of the same parity.

## Measurement contracts

- Production winner: profiling-OFF `strictKernelCycles`.
- Boundary: `PIPE_ALL`, `GetSystemCycle`, kernel body, `PIPE_ALL`.
- Aggregation: max core per rank, max rank per launch, median across launches.
- Display conversion: raw cycles divided by 1000. Raw cycles remain canonical.
- Total-only profile: records only `kernel_total` with strict-equivalent
  boundaries.
- Coarse/fine profile: attribution only; instrumentation changes absolute time.

Host launch, stream synchronization, start-gate host coordination, and trace
flush are not part of `strictKernelCycles`.
