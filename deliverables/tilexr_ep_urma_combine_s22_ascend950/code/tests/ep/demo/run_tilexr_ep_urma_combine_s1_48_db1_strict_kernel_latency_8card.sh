#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run_tilexr_ep_urma_combine_multihost.sh"
EXPECTED_VARIANT="s1-p48-s16-db1-strict-cycle-v2-8card"

if [[ "$#" -ne 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

variant="${TILEXR_COMBINE_BUILD_VARIANT:-${EXPECTED_VARIANT}}"
if [[ "${variant}" != "${EXPECTED_VARIANT}" ]]; then
    echo "8-card strict-kernel latency mode requires TILEXR_COMBINE_BUILD_VARIANT=${EXPECTED_VARIANT}" >&2
    exit 2
fi
if [[ -n "${TILEXR_DEMO_PROFILE_DIR:-}" && "${TILEXR_DEMO_PROFILE_DIR:-}" != "-" ]]; then
    echo "8-card strict-kernel latency mode does not allow TILEXR_DEMO_PROFILE_DIR" >&2
    exit 2
fi

export TILEXR_COMBINE_BUILD_VARIANT="${EXPECTED_VARIANT}"
export TILEXR_COMBINE_HOSTS='root@141.61.52.35'
export TILEXR_COMBINE_DEVICE_MAPS='0,1,2,3,4,5,6,7'
export TILEXR_DEMO_DEVICE_EVENT_LATENCY=0
export TILEXR_DEMO_STRICT_KERNEL_LATENCY=1
export TILEXR_DEMO_BS=32
export TILEXR_DEMO_TOPK=6
export TILEXR_DEMO_H=5120
export TILEXR_DEMO_ROUTE_SEED=20260721
export TILEXR_DEMO_WARMUP_ROUNDS=20
export TILEXR_DEMO_ROUNDS=100
export TILEXR_DEMO_VALIDATE_EVERY=100
export TILEXR_DEMO_TIMEOUT_SEC="${TILEXR_DEMO_TIMEOUT_SEC:-1200}"
export TILEXR_DEMO_RUN_ID="${TILEXR_DEMO_RUN_ID:-ep-combine-8rank-bs32-k6-h5120-s1-p48-s16-db1-strict-cycle-v2-8card-$(date +%Y%m%d-%H%M%S)}"
unset TILEXR_DEMO_PROFILE_DIR

exec bash "${RUNNER}" 8
