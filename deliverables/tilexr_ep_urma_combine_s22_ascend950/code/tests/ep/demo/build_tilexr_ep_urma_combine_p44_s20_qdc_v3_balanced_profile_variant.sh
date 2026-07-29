#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDER="${SCRIPT_DIR}/build_tilexr_ep_urma_combine_variant.sh"
EXPECTED_VARIANT="s1-p44-s20-db1-prp20-sgwin-qdc-v3-txb1-srb-profile-v1"

if [[ "$#" -ne 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

export TILEXR_EP_ENABLE_PROFILING_BUILD=ON
export TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD=ON
export TILEXR_EP_URMA_START_GATE_BUILD=ON
export TILEXR_EP_URMA_QDC_VERSION_BUILD=3
export TILEXR_EP_URMA_TX_READY_BATCH_SIZE_BUILD=1
export TILEXR_EP_URMA_TX_READY_SHARED_FLAG_BUILD=OFF
export TILEXR_EP_URMA_TX_READY_IN_DATA_BUILD=OFF
export TILEXR_EP_URMA_QP_COUNT_BUILD=20
exec bash "${BUILDER}" "${EXPECTED_VARIANT}" 20 1 1
