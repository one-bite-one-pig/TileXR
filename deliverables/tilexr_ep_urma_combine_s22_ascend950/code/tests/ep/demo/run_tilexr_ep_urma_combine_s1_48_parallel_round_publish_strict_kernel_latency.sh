#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run_tilexr_ep_urma_combine_multihost.sh"
EXPECTED_VARIANT="s1-p48-s16-db1-prp16-strict-cycle-v1"

if [[ "$#" -ne 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

variant="${TILEXR_COMBINE_BUILD_VARIANT:-${EXPECTED_VARIANT}}"
if [[ "${variant}" != "${EXPECTED_VARIANT}" ]]; then
    echo "parallel-publish strict mode requires TILEXR_COMBINE_BUILD_VARIANT=${EXPECTED_VARIANT}" >&2
    exit 2
fi
if [[ -n "${TILEXR_DEMO_PROFILE_DIR:-}" && "${TILEXR_DEMO_PROFILE_DIR:-}" != "-" ]]; then
    echo "parallel-publish strict mode does not allow TILEXR_DEMO_PROFILE_DIR" >&2
    exit 2
fi

all_devices='0,1,2,3,4,5,6,7;0,1,2,3,4,5,6,7;0,1,2,3,4,5,6,7;0,1,2,3,4,5,6,7;0,1,2,3,4,5,6,7;0,1,2,3,4,5,6,7;0,1,2,3,4,5,6,7;0,1,2,3,4,5,6,7'

export TILEXR_COMBINE_BUILD_VARIANT="${EXPECTED_VARIANT}"
export TILEXR_COMBINE_DEVICE_MAPS="${all_devices}"
export TILEXR_DEMO_DEVICE_EVENT_LATENCY=0
export TILEXR_DEMO_STRICT_KERNEL_LATENCY=1
export TILEXR_DEMO_BS=128
export TILEXR_DEMO_TOPK=8
export TILEXR_DEMO_H=7168
export TILEXR_DEMO_ROUTE_SEED=20260721
export TILEXR_DEMO_WARMUP_ROUNDS=20
export TILEXR_DEMO_ROUNDS=100
export TILEXR_DEMO_VALIDATE_EVERY=100
export TILEXR_DEMO_TIMEOUT_SEC="${TILEXR_DEMO_TIMEOUT_SEC:-2400}"
export TILEXR_DEMO_RUN_ID="${TILEXR_DEMO_RUN_ID:-ep-combine-64rank-bs128-k8-h7168-s1-p48-s16-db1-prp16-strict-cycle-v1-$(date +%Y%m%d-%H%M%S)}"
unset TILEXR_DEMO_PROFILE_DIR

exec bash "${RUNNER}" 8
