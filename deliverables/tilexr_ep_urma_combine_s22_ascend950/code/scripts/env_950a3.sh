#!/usr/bin/env bash

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "Source this script instead of executing it:" >&2
    echo "  source scripts/env_950a3.sh" >&2
    exit 2
fi

_tilexr_950a3_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

export ASCEND_HOME_PATH="${TILEXR_950A3_CANN_HOME:-/home/pkg/b100/cann-9.1.0}"
if [[ ! -r "${ASCEND_HOME_PATH}/set_env.sh" ]]; then
    echo "TileXR 950A3 CANN environment not found: ${ASCEND_HOME_PATH}/set_env.sh" >&2
    unset _tilexr_950a3_script_dir
    return 1
fi

source "${ASCEND_HOME_PATH}/set_env.sh"
source "${_tilexr_950a3_script_dir}/common_env.sh"

if [[ "${TILEXR_SOC_NAME}" != "ascend950" ]]; then
    echo "Unexpected TileXR SOC detection: ${TILEXR_SOC_NAME} (expected ascend950)" >&2
    unset _tilexr_950a3_script_dir
    return 1
fi

export SOC_VERSION="${SOC_VERSION:-Ascend950}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "TileXR environment loaded, but project-local CMake is not installed." >&2
    echo "Expected path: ${TILEXR_UTIL_HOME}/cmake/bin/cmake" >&2
fi

unset _tilexr_950a3_script_dir
