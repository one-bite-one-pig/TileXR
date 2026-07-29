#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "Usage: $0 <tilexr-root> [production|profile] [variant-name]" >&2
    exit 2
fi

root="$(cd "$1" && pwd)"
mode="${2:-production}"
if [[ ! "${mode}" =~ ^(production|profile)$ ]]; then
    echo "Mode must be production or profile" >&2
    exit 2
fi

variant="${3:-tilexr-combine-s22-o2-${mode}}"
if [[ ! "${variant}" =~ ^[a-z0-9-]+$ ]]; then
    echo "Variant name must match [a-z0-9-]+" >&2
    exit 2
fi

builder="${root}/tests/ep/demo/build_tilexr_ep_urma_combine_variant.sh"
if [[ ! -x "${builder}" ]]; then
    echo "Missing executable variant builder: ${builder}" >&2
    exit 3
fi

for path in \
    "${root}/build_ep_variants/${variant}" \
    "${root}/install_variants/${variant}" \
    "${root}/tests/ep/build_variants/${variant}" \
    "${root}/tests/ep/install_variants/${variant}"; do
    if [[ -e "${path}" ]]; then
        echo "Refusing to overwrite variant path: ${path}" >&2
        exit 3
    fi
done

export TILEXR_EP_ENABLE_PROFILING_BUILD=OFF
if [[ "${mode}" == profile ]]; then
    export TILEXR_EP_ENABLE_PROFILING_BUILD=ON
fi
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
export TILEXR_EP_URMA_QP_COUNT_BUILD=22
export TILEXR_CMAKE_BUILD_TYPE_BUILD=

echo "Building ${variant}: P42/S22/QP22/DB1/O2 profiling=${TILEXR_EP_ENABLE_PROFILING_BUILD}"
bash "${builder}" "${variant}" 22 1 1
