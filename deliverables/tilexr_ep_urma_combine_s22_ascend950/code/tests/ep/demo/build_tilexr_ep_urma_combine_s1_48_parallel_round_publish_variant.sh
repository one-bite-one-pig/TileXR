#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDER="${SCRIPT_DIR}/build_tilexr_ep_urma_combine_variant.sh"
EXPECTED_VARIANT="s1-p48-s16-db1-prp16-strict-cycle-v1"

if [[ "$#" -ne 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

export TILEXR_EP_ENABLE_PROFILING_BUILD=OFF
export TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD=ON
export TILEXR_EP_URMA_QP_COUNT_BUILD=16
exec bash "${BUILDER}" "${EXPECTED_VARIANT}" 16 1 1
