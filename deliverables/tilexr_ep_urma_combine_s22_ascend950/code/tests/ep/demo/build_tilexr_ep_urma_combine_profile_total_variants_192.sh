#!/usr/bin/env bash
set -euo pipefail

ROOT="${TILEXR_PROFILE_TOTAL_ROOT:-/root/TileXR-inline-debug-20260728}"
BUILDER="${ROOT}/tests/ep/demo/build_tilexr_ep_urma_combine_variant.sh"
S22=r141-o2-inline-p42-s22-qp22-deferred-credit-profile-total-192-20260729-v8
S28=r141-o2-inline-p36-s28-qp28-deferred-credit-profile-total-192-20260729-v8

if [[ "$#" -ne 1 || ! "$1" =~ ^(plan|build)$ ]]; then
    echo "Usage: $0 <plan|build>" >&2
    exit 2
fi

for variant in "${S22}" "${S28}"; do
    for path in \
        "${ROOT}/build_variants/${variant}" \
        "${ROOT}/install_variants/${variant}" \
        "${ROOT}/tests/ep/build_variants/${variant}" \
        "${ROOT}/tests/ep/install_variants/${variant}"; do
        if [[ -e "${path}" ]]; then
            echo "Refusing to overwrite variant path: ${path}" >&2
            exit 3
        fi
    done
done
echo "PROFILE_TOTAL_BUILD_PLAN variant=${S22} pack=42 send=22 qp=22"
echo "PROFILE_TOTAL_BUILD_PLAN variant=${S28} pack=36 send=28 qp=28"
if [[ "$1" == plan ]]; then
    exit 0
fi

export TILEXR_950A3_CANN_HOME=/home/pkg/b061/cann-9.1.T560
source "${ROOT}/scripts/common_env.sh"
export TILEXR_BUILD_JOBS="${TILEXR_BUILD_JOBS:-8}"
export TILEXR_EP_ENABLE_PROFILING_BUILD=ON
export TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD=ON
export TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT_BUILD=ON
export TILEXR_EP_URMA_START_GATE_BUILD=ON
export TILEXR_EP_URMA_QDC_VERSION_BUILD=3
export TILEXR_EP_URMA_TX_READY_BATCH_SIZE_BUILD=1
export TILEXR_EP_URMA_TX_READY_SHARED_FLAG_BUILD=OFF
export TILEXR_EP_URMA_TX_READY_IN_DATA_BUILD=OFF
export TILEXR_EP_URMA_TX_META_PREFETCH_FULL_BUILD=OFF
export TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH_BUILD=OFF
export TILEXR_EP_URMA_RX_READY_STICKY_MASK_BUILD=ON
export TILEXR_EP_URMA_RX_READY_BATCH_MTE2_BUILD=OFF
export TILEXR_EP_URMA_RX_READY_BATCH_VECTOR_BUILD=OFF
export TILEXR_EP_URMA_OPERATOR_LAUNCH_BUILD=OFF
export TILEXR_EP_KERNEL_OPT_LEVEL_BUILD=O2
export TILEXR_EP_URMA_KERNEL_DIAGNOSTIC_MODE_BUILD=NONE
export TILEXR_CMAKE_BUILD_TYPE_BUILD=

TILEXR_EP_URMA_QP_COUNT_BUILD=22 bash "${BUILDER}" "${S22}" 22 1 1
TILEXR_EP_URMA_QP_COUNT_BUILD=28 bash "${BUILDER}" "${S28}" 28 1 1

for test_name in \
    test_tilexr_ep_api_sources \
    test_tilexr_ep_kernel_sources \
    test_tilexr_ep_host_validation \
    test_tilexr_ep_layout \
    test_tilexr_ep_start_gate_window; do
    "${ROOT}/tests/ep/install_variants/${S22}/bin/${test_name}"
done

echo "PROFILE_TOTAL_BUILD_COMPLETE variants=2 source_guards=5"
