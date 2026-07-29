# Usage

## Prerequisites

- Ubuntu 20.04 on Ascend 950/950PR;
- CANN 9.1-compatible toolkit and driver;
- TileXR checkout at commit `db4303e` with submodules initialized;
- root access where required for device and UDMA setup;
- eight genuinely idle devices for the supplied eight-rank workflow.

Never run into an occupied device set and never terminate unrelated jobs.

## Apply the source overlay

From the delivery root, inspect `SHA256SUMS`, then copy `code/` over a clean
TileXR checkout while preserving relative paths:

```bash
python3 scripts/verify_delivery.py
cp -a code/. /path/to/TileXR/
```

Review the resulting Git diff before building. Do not apply the overlay to a
checkout with overlapping uncommitted changes.

## Build S22

Production build:

```bash
bash scripts/build_s22_variant.sh /path/to/TileXR production my-s22-o2-prod
```

Profiling build for detailed traces:

```bash
bash scripts/build_s22_variant.sh /path/to/TileXR profile my-s22-o2-profile
```

The wrapper refuses to reuse an existing build/install identity. Set
`TILEXR_950A3_CANN_HOME` when CANN is not under the default path used by
`scripts/env_950a3.sh`.

## Smoke correctness

Use a unique communication endpoint and run ID:

```bash
export TILEXR_COMBINE_REMOTE_ROOT=/path/to/TileXR
export TILEXR_COMBINE_HOSTS=root@host
export TILEXR_COMBINE_DEVICE_MAPS=0,1,2,3,4,5,6,7
export TILEXR_COMBINE_BUILD_VARIANT=my-s22-o2-prod
export TILEXR_COMM_ID=host-ip:unique-port
export TILEXR_DEMO_RUN_ID=my-s22-smoke-bs32
export TILEXR_DEMO_MULTIHOST_LOG_ROOT=/new/evidence/root/bs32
export TILEXR_DEMO_BS=32
export TILEXR_DEMO_H=5120
export TILEXR_DEMO_TOPK=6
export TILEXR_DEMO_ROUTE_SEED=20260728
export TILEXR_DEMO_ENQUEUE_WINDOW=1
export TILEXR_DEMO_WARMUP_ROUNDS=20
export TILEXR_DEMO_ROUNDS=10
bash /path/to/TileXR/tests/ep/demo/run_tilexr_ep_urma_combine_multihost.sh 8
```

Repeat with BS128. Correctness requires all ranks to report validation PASS.

## Strict production timing

Add these settings to the production build run:

```bash
export TILEXR_DEMO_STRICT_KERNEL_LATENCY=1
export TILEXR_DEMO_WARMUP_ROUNDS=20
export TILEXR_DEMO_ROUNDS=100
export TILEXR_DEMO_VALIDATE_EVERY=100
```

Use `tilexr_ep_urma_combine_strict_kernel_latency_report.py` to aggregate the
generated summaries. Compare raw cycles first.

## Render a detailed profile

The first positional path is the fine trace root:

```bash
bash scripts/render_s22_profile.sh \
  /path/to/fine \
  /path/to/coarse \
  /path/to/output-copy \
  /path/to/total-only
```

The default report collapses start gate from proportional views. To inspect the
full launch preamble, run the Python reporter directly with
`--show-start-gate`.
