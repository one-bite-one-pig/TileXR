#!/usr/bin/env bash
set -euo pipefail

ROOT="${TILEXR_PROFILE_TOTAL_ROOT:-/root/TileXR-inline-debug-20260728}"
RUNNER="${ROOT}/tests/ep/demo/run_tilexr_ep_urma_combine_multihost.sh"
CAMPAIGN="${TILEXR_PROFILE_TOTAL_CAMPAIGN:-tilexr-r141-profile-total-s22-s28-192-20260729-v8}"
CAMPAIGN_ROOT="/tmp/${CAMPAIGN}"
PORT_BASE="${TILEXR_PROFILE_TOTAL_PORT_BASE:-17500}"
RUN_SUFFIX="${TILEXR_PROFILE_TOTAL_RUN_SUFFIX:-v8}"
STAGE_BEGIN="${TILEXR_PROFILE_TOTAL_STAGE_BEGIN:-0}"
STAGE_END="${TILEXR_PROFILE_TOTAL_STAGE_END:-19}"
TEARDOWN_SEC="${TILEXR_PROFILE_TOTAL_TEARDOWN_SEC:-30}"
HOST="root@141.61.49.192"
DEVICES="0,1,2,3,4,5,6,7"

if [[ "$#" -ne 1 || ! "$1" =~ ^(plan|run)$ ]]; then
    echo "Usage: $0 <plan|run>" >&2
    exit 2
fi
if [[ ! "${PORT_BASE}" =~ ^[1-9][0-9]*$ ]]; then
    echo "TILEXR_PROFILE_TOTAL_PORT_BASE must be a positive integer" >&2
    exit 2
fi
if [[ ! "${RUN_SUFFIX}" =~ ^[a-zA-Z0-9._-]+$ ]]; then
    echo "TILEXR_PROFILE_TOTAL_RUN_SUFFIX contains invalid characters" >&2
    exit 2
fi
for value_name in STAGE_BEGIN STAGE_END TEARDOWN_SEC; do
    value="${!value_name}"
    if [[ ! "${value}" =~ ^[0-9]+$ ]]; then
        echo "${value_name} must be a non-negative integer" >&2
        exit 2
    fi
done
if ((STAGE_BEGIN > STAGE_END || STAGE_END >= 20 || TEARDOWN_SEC == 0)); then
    echo "invalid stage range or teardown interval" >&2
    exit 2
fi

stages=(
    p36-s28:128:strict
    p42-s22:32:coarse
    p36-s28:128:total
    p42-s22:128:fine
    p42-s22:32:strict
    p36-s28:32:total
    p42-s22:128:coarse
    p36-s28:32:fine
    p42-s22:32:matched
    p42-s22:128:strict
    p36-s28:128:coarse
    p42-s22:32:total
    p36-s28:32:strict
    p42-s22:32:fine
    p36-s28:128:matched
    p36-s28:128:fine
    p42-s22:128:total
    p36-s28:32:coarse
    p42-s22:128:matched
    p36-s28:32:matched
)

variant_for_layout() {
    case "$1" in
        p42-s22)
            echo r141-o2-inline-p42-s22-qp22-deferred-credit-profile-total-192-20260729-v8
            ;;
        p36-s28)
            echo r141-o2-inline-p36-s28-qp28-deferred-credit-profile-total-192-20260729-v8
            ;;
        *) return 2 ;;
    esac
}

if [[ -e "${CAMPAIGN_ROOT}" ]]; then
    echo "Refusing to overwrite campaign root: ${CAMPAIGN_ROOT}" >&2
    exit 3
fi
for stage in "${!stages[@]}"; do
    if ((stage < STAGE_BEGIN || stage > STAGE_END)); then
        continue
    fi
    port=$((PORT_BASE + stage))
    if ss -H -lntup | grep -Eq ":${port}([[:space:]]|$)"; then
        echo "Experiment port is already in use: ${port}" >&2
        exit 3
    fi
    IFS=: read -r layout bs mode <<<"${stages[$stage]}"
    variant="$(variant_for_layout "${layout}")"
    manifest="${ROOT}/install_variants/${variant}/share/tilexr/ep_urma_combine_variant.env"
    if [[ ! -f "${manifest}" ]]; then
        echo "Missing variant manifest: ${manifest}" >&2
        exit 3
    fi
    echo "PROFILE_TOTAL_PLAN stage=$((stage + 1))/20 layout=${layout} bs=${bs} mode=${mode} port=${port} variant=${variant}"
done
if [[ "$1" == plan ]]; then
    exit 0
fi

mkdir "${CAMPAIGN_ROOT}"
mkdir "${CAMPAIGN_ROOT}/launchers"

for stage in "${!stages[@]}"; do
    if ((stage < STAGE_BEGIN || stage > STAGE_END)); then
        continue
    fi
    IFS=: read -r layout bs mode <<<"${stages[$stage]}"
    variant="$(variant_for_layout "${layout}")"
    port=$((PORT_BASE + stage))
    launcher="${CAMPAIGN_ROOT}/launchers/${layout}-bs${bs}-${mode}.log"
    run_id="ep-combine-8rank-h5120-k6-r141-${layout}-profile-total-bs${bs}-${mode}-ew1-20260729-${RUN_SUFFIX}"
    log_root="${CAMPAIGN_ROOT}/runs/${layout}/bs${bs}/${mode}"
    echo "PROFILE_TOTAL_STAGE_START stage=$((stage + 1))/20 layout=${layout} bs=${bs} mode=${mode} port=${port}"

    common_env=(
        TILEXR_950A3_CANN_HOME=/home/pkg/b061/cann-9.1.T560
        TILEXR_COMBINE_REMOTE_ROOT="${ROOT}"
        TILEXR_COMBINE_HOSTS="${HOST}"
        TILEXR_COMBINE_DEVICE_MAPS="${DEVICES}"
        TILEXR_COMBINE_BUILD_VARIANT="${variant}"
        TILEXR_COMM_ID="172.27.21.192:${port}"
        TILEXR_DEMO_RUN_ID="${run_id}"
        TILEXR_DEMO_MULTIHOST_LOG_ROOT="${log_root}"
        TILEXR_DEMO_BS="${bs}"
        TILEXR_DEMO_H=5120
        TILEXR_DEMO_TOPK=6
        TILEXR_DEMO_ROUTE_SEED=20260728
        TILEXR_DEMO_ENQUEUE_WINDOW=1
        TILEXR_DEMO_DEVICE_EVENT_LATENCY=0
        TILEXR_DEMO_TIMEOUT_SEC=1800
    )

    if [[ "${mode}" == strict || "${mode}" == matched ]]; then
        warmup_rounds=20
        measured_rounds=100
        if [[ "${mode}" == matched ]]; then
            measured_rounds=10
        fi
        env "${common_env[@]}" \
            TILEXR_DEMO_WARMUP_ROUNDS="${warmup_rounds}" \
            TILEXR_DEMO_ROUNDS="${measured_rounds}" \
            TILEXR_DEMO_VALIDATE_EVERY="${measured_rounds}" \
            TILEXR_DEMO_STRICT_KERNEL_LATENCY=1 \
            bash "${RUNNER}" 8 >"${launcher}" 2>&1
        sample_count="$(grep -c '"round":' "${log_root}/strict_kernel_latency_summary.json")"
        pass_count="$(grep -R -h -c 'validation PASS' "${log_root}"/*/rank_*.log | awk '{sum += $1} END {print sum + 0}')"
        if [[ "${sample_count}" -ne "${measured_rounds}" || "${pass_count}" -ne 16 ]]; then
            echo "PROFILE_TOTAL_STAGE_INVALID stage=$((stage + 1))/20 samples=${sample_count} passes=${pass_count}" >&2
            exit 5
        fi
        echo "PROFILE_TOTAL_STAGE_COMPLETE stage=$((stage + 1))/20 layout=${layout} bs=${bs} mode=${mode} samples=${sample_count} passes=${pass_count}"
    else
        case "${mode}" in
            total) profile_detail=0; profile_scope=kernel_total_only ;;
            coarse) profile_detail=1; profile_scope=coarse_attribution ;;
            fine) profile_detail=2; profile_scope=fine_attribution ;;
            *) exit 2 ;;
        esac
        profile_root="${CAMPAIGN_ROOT}/profiles/${layout}/bs${bs}/${mode}"
        profile_round=100
        measured_rounds=110
        validate_every=110
        if [[ "${mode}" == total ]]; then
            profile_round=0
            measured_rounds=10
            validate_every=10
        fi
        env "${common_env[@]}" \
            TILEXR_DEMO_PROFILE_DIR="${profile_root}" \
            TILEXR_DEMO_WARMUP_ROUNDS=20 \
            TILEXR_DEMO_ROUNDS="${measured_rounds}" \
            TILEXR_DEMO_VALIDATE_EVERY="${validate_every}" \
            TILEXR_DEMO_PROFILE_ROUND="${profile_round}" \
            TILEXR_DEMO_PROFILE_SAMPLES=10 \
            TILEXR_DEMO_PROFILE_DETAIL="${profile_detail}" \
            TILEXR_DEMO_STRICT_KERNEL_LATENCY=0 \
            bash "${RUNNER}" 8 >"${launcher}" 2>&1
        trace_count="$(find "${profile_root}" -type f -name trace.json | wc -l)"
        boundary_count="$(grep -R -h -c '"kernel_timing_boundary": "pipe_all_bracketed_pre_flush"' "${profile_root}"/*/launch*/trace.json | awk '{sum += $1} END {print sum + 0}')"
        scope_count="$(grep -R -h -c "\"profile_scope\": \"${profile_scope}\"" "${profile_root}"/*/launch*/trace.json | awk '{sum += $1} END {print sum + 0}')"
        pass_count="$(grep -R -h -c 'validation PASS' "${log_root}"/*/rank_*.log | awk '{sum += $1} END {print sum + 0}')"
        if [[ "${trace_count}" -ne 80 || "${boundary_count}" -ne 80 || \
                "${scope_count}" -ne 80 || "${pass_count}" -ne 16 ]]; then
            echo "PROFILE_TOTAL_STAGE_INVALID stage=$((stage + 1))/20 traces=${trace_count} boundaries=${boundary_count} scopes=${scope_count} passes=${pass_count}" >&2
            exit 5
        fi
        echo "PROFILE_TOTAL_STAGE_COMPLETE stage=$((stage + 1))/20 layout=${layout} bs=${bs} mode=${mode} traces=${trace_count} boundaries=${boundary_count} scopes=${scope_count} passes=${pass_count}"
    fi

    if ((stage < STAGE_END)); then
        echo "PROFILE_TOTAL_TEARDOWN_WAIT stage=$((stage + 1))/20 seconds=${TEARDOWN_SEC}"
        sleep "${TEARDOWN_SEC}"
    fi
done

if ((STAGE_BEGIN == 0 && STAGE_END == 19)); then
    touch "${CAMPAIGN_ROOT}/campaign.complete"
else
    touch "${CAMPAIGN_ROOT}/recovery.complete"
fi
echo "PROFILE_TOTAL_COMPLETE stage_begin=$((STAGE_BEGIN + 1)) stage_end=$((STAGE_END + 1)) campaign_root=${CAMPAIGN_ROOT}"
