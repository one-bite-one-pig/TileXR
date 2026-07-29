# TileXR EP URMA Combine S22 Delivery

This directory is a self-contained delivery snapshot of the validated Ascend 950
TileXR EP URMA Combine implementation and its S22 configuration.

## Selected configuration

`S22` means 42 Pack/Receive AIV cores and 22 Send AIV cores:

- P42 / S22 / QP22 / doorbell batch 1
- QDC v3 and balanced Send-route ownership
- parallel round publish with deferred round credit
- RX sticky ready mask enabled
- TX early-ready publish disabled
- RX batched MTE2 and Vector-ready reduction disabled
- cacheless mode enabled
- BiSheng kernel optimization `-O2`
- production build: profiling disabled

The 100-round strict-kernel medians are 43,130 cycles for BS32 and 94,889
cycles for BS128. S22 beats P36/S28 by 7.68% and 17.77%, respectively.

## Directory map

- `code/`: repository-relative source overlay, build integration, demos, report
  tools, and focused tests.
- `docs/`: architecture, integration, usage, and performance notes.
- `profile/`: generated S22 BS32/BS128 reports and strict100 JSON summaries.
- `manifests/`: validated profiling-build manifest, expanded BiSheng command,
  compiler-driver evidence, and audit binary.
- `latex/`: Chinese LaTeX report summarizing the Ascend 950 practice.
- `scripts/`: delivery-level build, profile rendering, and integrity helpers.
- `SHA256SUMS`: integrity manifest for every delivered file except itself.

## Start here

1. Read [Delivery Scope](docs/DELIVERY_SCOPE.md).
2. Apply `code/` to a clean TileXR checkout at base commit `db4303e`.
3. Follow [Usage](docs/USAGE.md) to build a new, collision-free production
   variant.
4. Open [BS32 profile](profile/bs32/combine_report.html) or
   [BS128 profile](profile/bs128/combine_report.html).
5. Read [Performance](docs/PERFORMANCE.md) before interpreting profile bars.

The HTML report keeps start-gate evidence in its header but collapses it from
the proportional timeline and rebases the axis to steady-state work. Pass
`--show-start-gate` to the delivered report tool when the full launch preamble
is explicitly needed.
