#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "Usage: $0 <fine-dir> <coarse-dir> <output-dir> [total-dir]" >&2
    exit 2
fi

delivery_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fine="$(cd "$1" && pwd)"
coarse="$(cd "$2" && pwd)"
output="$3"
total="${4:-}"

if [[ -e "${output}" ]]; then
    echo "Refusing to overwrite output directory: ${output}" >&2
    exit 3
fi
mkdir -p "${output}"
cp -a "${fine}/." "${output}/"

args=(
    "${output}"
    --coarse-dir "${coarse}"
    --design-label "S22 P42/S22/QP22 O2"
    --skip-generic-report
)
if [[ -n "${total}" ]]; then
    total="$(cd "${total}" && pwd)"
    args+=(--total-dir "${total}")
fi
python3 "${delivery_root}/code/tests/ep/demo/tilexr_ep_urma_combine_profile_report.py" "${args[@]}"
