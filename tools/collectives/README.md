# TileXR Collectives Tools

`tilexr_collective_perf_tool` is an incremental single-host collective correctness/performance command.
The existing `tests/collectives/run_collective_perf.sh` and `tilexr_collective_perf` target remain unchanged.
It launches one worker process per local rank, keeps ACL and communicator state isolated per process,
and preserves the existing device-event timing path.

Build from the repository root:

```bash
source scripts/common_env.sh
cmake -S . -B build \
  -DTILEXR_BUILD_COLLECTIVES=ON \
  -DTILEXR_BUILD_COLLECTIVES_TOOLS=ON \
  -DTILEXR_BUILD_TESTS=OFF \
  -DBUILD_TESTING=OFF
cmake --build build --target tilexr_collective_perf_tool -j"$(nproc)"
```

`BUILD_TESTING` is a standard CTest option and defaults to `ON`; disable it for a tools-only build.

Run directly from the build tree:

```bash
./build/tools/collectives/tilexr_collective_perf_tool \
  --rank-size 4 --first-npu 0 \
  --op allgather --min-bytes 4096 --max-bytes 16777216 \
  --warmup-iters 5 --iters 20 --datatype int32 --check 1
```

The parent process writes `collective_perf_rank<N>.log` under `--log-dir` (the current directory by
default), stops all remaining workers if one rank fails, and returns `124` on timeout. Set the timeout
with `--timeout-sec N` or `TILEXR_COLLECTIVES_RUN_TIMEOUT_SEC=N`.

Before launching, the tool checks `TILEXR_AVAILABLE_NPUS` or falls back to `npu-smi info -l`. If the
requested device range is unavailable, set `TILEXR_SKIP_IF_INSUFFICIENT_NPUS=1` to return success with
a `SKIP` message instead of failing.

`--worker --rank R` runs exactly one rank. It is an internal/compatibility mode used by the parent
launcher and can also be driven by a separate multi-host launcher; normal single-host users should not
pass it. The existing `tests/collectives/run_collective_perf_multihost.sh` continues to target the old
`tilexr_collective_perf` executable and is not changed by this tool.

When profiling is enabled, the C++ workers write per-rank/per-launch traces and the parent runs the
tool-owned `tilexr_collective_profile_report.py` helper to create the aggregate HTML, Markdown, and
Perfetto reports. The helper is copied beside the build-tree executable and installed into the same
binary directory, so `tools/collectives` has no source dependency on `tests/collectives`.
