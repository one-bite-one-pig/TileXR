#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TILEXR_ROOT="$(cd "${EP_DIR}/../.." && pwd)"

usage() {
    cat >&2 <<'EOF'
Usage:
  run_tilexr_ep_urma_combine_multihost.sh <ranks-per-host>

The delivery runner has no machine-specific topology defaults. Set
TILEXR_COMBINE_HOSTS, TILEXR_COMBINE_DEVICE_MAPS,
TILEXR_COMBINE_REMOTE_ROOT, and TILEXR_COMM_ID explicitly.

Useful environment variables:
  TILEXR_COMM_ID                    Rank-0 socket address (required)
  TILEXR_DEMO_ROUNDS                Measured rounds (default 3)
  TILEXR_DEMO_WARMUP_ROUNDS         Warmup rounds (default 0)
  TILEXR_DEMO_VALIDATE_EVERY        Validation interval (default 1)
  TILEXR_DEMO_BS                    Batch size (default 128)
  TILEXR_DEMO_H                     Hidden size (default 7168)
  TILEXR_DEMO_TOPK                  TopK (default 8)
  TILEXR_DEMO_ROUTE_SEED            Deterministic route seed (default 20260721)
  TILEXR_DEMO_DEVICE_EVENT_LATENCY  Emit stream-envelope event samples (default 0)
  TILEXR_DEMO_STRICT_KERNEL_LATENCY Emit in-kernel cycle samples (default 0)
  TILEXR_DEMO_TIMEOUT_SEC           Per-rank timeout (default 1200)
  TILEXR_DEMO_RUN_ID                Log directory name
  TILEXR_DEMO_PROFILE_DIR           Optional profile root on every node
  TILEXR_DEMO_PROFILE_SAMPLES       Consecutive profiled rounds (default 1)
  TILEXR_DEMO_ENQUEUE_WINDOW        Consecutive normal/profile launches per stream sync (default 1)
  TILEXR_COMBINE_BUILD_VARIANT      Select install_variants/<name> on every node
  TILEXR_950A3_CANN_HOME            Explicit CANN root forwarded to every remote node
EOF
}

run_node() {
    if [[ "$#" -lt 21 || "$#" -gt 25 ]]; then
        echo "invalid node launcher arguments" >&2
        return 2
    fi

    local rank_size="$1"
    local rank_begin="$2"
    local device_csv="$3"
    local local_rank_count="$4"
    local comm_id="$5"
    local run_id="$6"
    local log_root="$7"
    local bs="$8"
    local h="$9"
    local topk="${10}"
    local rounds="${11}"
    local warmup_rounds="${12}"
    local validate_every="${13}"
    local capacity_multiplier="${14}"
    local timeout_sec="${15}"
    local profile_dir="${16}"
    local profile_round="${17}"
    local profile_detail="${18}"
    local route_seed="${19}"
    local root_install="${20}"
    local ep_install="${21}"
    local device_event_latency="${22:-0}"
    local strict_kernel_latency="${23:-0}"
    local profile_samples="${24:-1}"
    local enqueue_window="${25:-1}"
    if [[ ! "${device_event_latency}" =~ ^(0|1)$ ||
          ! "${strict_kernel_latency}" =~ ^(0|1)$ ]] ||
       [[ "${device_event_latency}" == "1" && "${strict_kernel_latency}" == "1" ]]; then
        echo "invalid or conflicting latency flags: device=${device_event_latency} strict=${strict_kernel_latency}" >&2
        return 2
    fi
    if [[ ! "${profile_samples}" =~ ^[1-9][0-9]*$ ]]; then
        echo "invalid profile sample count: ${profile_samples}" >&2
        return 2
    fi
    if [[ ! "${enqueue_window}" =~ ^[1-9][0-9]*$ ]]; then
        echo "invalid enqueue window: ${enqueue_window}" >&2
        return 2
    fi

    local -a devices=()
    IFS=',' read -r -a devices <<< "${device_csv}"
    if [[ "${#devices[@]}" -ne "${local_rank_count}" ]]; then
        echo "device count ${#devices[@]} does not match local rank count ${local_rank_count}" >&2
        return 2
    fi

    cd "${TILEXR_ROOT}"
    set +u
    source "${TILEXR_ROOT}/scripts/env_950a3.sh"
    set -u

    export TILEXR_COMM_ID="${comm_id}"
    export TILEXR_IPC_MODE=udma-only
    export TILEXR_ENABLE_SDMA=0
    export TILEXR_DEMO_BS="${bs}"
    export TILEXR_DEMO_H="${h}"
    export TILEXR_DEMO_TOPK="${topk}"
    export TILEXR_DEMO_ROUNDS="${rounds}"
    export TILEXR_DEMO_WARMUP_ROUNDS="${warmup_rounds}"
    export TILEXR_DEMO_VALIDATE_EVERY="${validate_every}"
    export TILEXR_DEMO_CAPACITY_MULTIPLIER="${capacity_multiplier}"
    export TILEXR_DEMO_PROFILE_ROUND="${profile_round}"
    export TILEXR_DEMO_PROFILE_DETAIL="${profile_detail}"
    export TILEXR_DEMO_PROFILE_SAMPLES="${profile_samples}"
    export TILEXR_DEMO_ROUTE_SEED="${route_seed}"
    export TILEXR_DEMO_DEVICE_EVENT_LATENCY="${device_event_latency}"
    export TILEXR_DEMO_STRICT_KERNEL_LATENCY="${strict_kernel_latency}"
    export TILEXR_DEMO_ENQUEUE_WINDOW="${enqueue_window}"
    export LD_LIBRARY_PATH="${root_install}/lib64:${ep_install}/lib64:${LD_LIBRARY_PATH:-}"
    if [[ "${profile_dir}" == "-" ]]; then
        unset TILEXR_DEMO_PROFILE_DIR
    else
        export TILEXR_DEMO_PROFILE_DIR="${profile_dir}"
    fi

    local bin="${ep_install}/bin/tilexr_ep_urma_combine_demo"
    if [[ ! -x "${bin}" ]]; then
        echo "missing demo binary: ${bin}" >&2
        return 1
    fi

    local node_name
    node_name="$(hostname)"
    local node_log_dir="${log_root}/${node_name}"
    mkdir -p "${log_root}"
    if ! mkdir "${node_log_dir}"; then
        echo "NODE_ABORTED node=${node_name} run=${run_id} reason=node-log-dir-exists-or-create-failed path=${node_log_dir}" >&2
        return 5
    fi

    local -a pids=()
    local -a ranks=()
    local -a logs=()
    local index rank device log
    for ((index = 0; index < local_rank_count; ++index)); do
        rank=$((rank_begin + index))
        device="${devices[$index]}"
        log="${node_log_dir}/rank_${rank}.log"
        ranks+=("${rank}")
        logs+=("${log}")
        (
            export RANK="${rank}"
            export RANK_SIZE="${rank_size}"
            export ASCEND_PROCESS_LOG_PATH="${node_log_dir}/plog_rank_${rank}"
            mkdir -p "${ASCEND_PROCESS_LOG_PATH}"
            exec timeout --signal=TERM --kill-after=30s "${timeout_sec}s" \
                "${bin}" "${rank_size}" "${rank}" 1 "${device}"
        ) >"${log}" 2>&1 &
        pids+=("$!")
    done

    local ret=0
    local status
    for index in "${!pids[@]}"; do
        set +e
        wait "${pids[$index]}"
        status=$?
        set -e
        if [[ "${status}" -ne 0 ]]; then
            echo "RANK_RESULT rank=${ranks[$index]} status=${status} log=${logs[$index]}"
            if [[ "${ret}" -eq 0 ]]; then
                ret="${status}"
            fi
        else
            echo "RANK_RESULT rank=${ranks[$index]} status=0 log=${logs[$index]}"
        fi
    done

    for index in "${!logs[@]}"; do
        echo "===== rank ${ranks[$index]} summary ====="
        grep -E "stress config|validation PASS|completed .*average|ERROR|failed" "${logs[$index]}" | tail -n 12 || true
    done
    if [[ "${device_event_latency}" == "1" ]]; then
        grep -h -E '^DEVICE_EVENT_LATENCY ' "${logs[@]}" || true
    fi
    if [[ "${strict_kernel_latency}" == "1" ]]; then
        grep -h -E '^STRICT_KERNEL_LATENCY ' "${logs[@]}" || true
    fi
    echo "NODE_RESULT node=${node_name} run=${run_id} status=${ret}"
    return "${ret}"
}

check_devices_idle() {
    if [[ "$#" -ne 9 ]]; then
        echo "invalid preflight arguments" >&2
        return 2
    fi

    local device_csv="$1"
    local root_install="$2"
    local ep_install="$3"
    local build_variant="$4"
    local -a reference_hashes=("$5" "$6" "$7" "$8")
    local reference_script_hash="$9"
    local -a devices=()
    local device output status
    local ret=0
    local actual_script_hash
    if [[ ! "${reference_script_hash}" =~ ^[0-9a-f]{64}$ ]]; then
        echo "PREFLIGHT_SCRIPT node=$(hostname) status=invalid-reference-hash" >&2
        return 4
    fi
    actual_script_hash="$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
    if [[ "${actual_script_hash}" != "${reference_script_hash}" ]]; then
        echo "PREFLIGHT_SCRIPT node=$(hostname) status=script-hash-mismatch expected=${reference_script_hash} actual=${actual_script_hash}" >&2
        return 4
    fi
    echo "PREFLIGHT_SCRIPT node=$(hostname) status=verified"
    if [[ "${build_variant}" != "-" ]]; then
        local manifest="${root_install}/share/tilexr/ep_urma_combine_variant.env"
        if [[ ! -f "${manifest}" ]]; then
            echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=missing-manifest path=${manifest}" >&2
            return 4
        fi
        source "${manifest}"
        if [[ "${TILEXR_VARIANT_NAME:-}" != "${build_variant}" ]]; then
            echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=name-mismatch manifest=${TILEXR_VARIANT_NAME:-unset}" >&2
            return 4
        fi
        if [[ "${build_variant}" == "s1-p48-s16-db1-strict-cycle-v1" ||
              "${build_variant}" == "s1-p48-s16-db1-strict-cycle-v2-8card" ||
              "${build_variant}" == "s1-p48-s16-db1-prp16-strict-cycle-v1" ]]; then
            if [[ "${TILEXR_VARIANT_PACK_CORES:-}" != "48" ||
                  "${TILEXR_VARIANT_SEND_CORES:-}" != "16" ||
                  "${TILEXR_VARIANT_QP_COUNT:-}" != "16" ||
                  "${TILEXR_VARIANT_DOORBELL_BATCH:-}" != "1" ||
                  "${TILEXR_VARIANT_RX_SCHEDULER:-}" != "1" ||
                  "${TILEXR_VARIANT_PROFILING:-}" != "OFF" ]]; then
                echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=strict-config-mismatch" >&2
                return 4
            fi
        fi
        if [[ "${build_variant}" == "s1-p48-s16-db1-prp16-start-gate-profile-v1" ]]; then
            if [[ "${TILEXR_VARIANT_PACK_CORES:-}" != "48" ||
                  "${TILEXR_VARIANT_SEND_CORES:-}" != "16" ||
                  "${TILEXR_VARIANT_QP_COUNT:-}" != "16" ||
                  "${TILEXR_VARIANT_DOORBELL_BATCH:-}" != "1" ||
                  "${TILEXR_VARIANT_RX_SCHEDULER:-}" != "1" ||
                  "${TILEXR_VARIANT_PROFILING:-}" != "ON" ]]; then
                echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=start-gate-profile-config-mismatch" >&2
                return 4
            fi
        fi
        local expected_parallel_round_publish=""
        case "${build_variant}" in
            s1-p48-s16-db1-strict-cycle-v1|s1-p48-s16-db1-strict-cycle-v2-8card)
                expected_parallel_round_publish=0
                ;;
            s1-p48-s16-db1-prp16-strict-cycle-v1|s1-p48-s16-db1-prp16-start-gate-profile-v1)
                expected_parallel_round_publish=1
                ;;
        esac
        local actual_parallel_round_publish="${TILEXR_VARIANT_PARALLEL_ROUND_PUBLISH:-0}"
        if [[ ! "${actual_parallel_round_publish}" =~ ^(0|1)$ ]]; then
            echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=invalid-parallel-round-publish actual=${actual_parallel_round_publish}" >&2
            return 4
        fi
        local expected_start_gate=""
        case "${build_variant}" in
            s1-p48-s16-db1-strict-cycle-v1|s1-p48-s16-db1-strict-cycle-v2-8card|s1-p48-s16-db1-prp16-strict-cycle-v1)
                expected_start_gate=0
                ;;
            s1-p48-s16-db1-prp16-start-gate-profile-v1)
                expected_start_gate=1
                ;;
        esac
        local actual_start_gate="${TILEXR_VARIANT_START_GATE:-0}"
        if [[ ! "${actual_start_gate}" =~ ^(0|1)$ ]]; then
            echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=invalid-start-gate actual=${actual_start_gate}" >&2
            return 4
        fi
        if [[ -n "${expected_start_gate}" &&
              "${actual_start_gate}" != "${expected_start_gate}" ]]; then
            echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=start-gate-mismatch expected=${expected_start_gate} actual=${actual_start_gate}" >&2
            return 4
        fi
        if [[ -n "${expected_parallel_round_publish}" &&
              "${actual_parallel_round_publish}" != "${expected_parallel_round_publish}" ]]; then
            echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=parallel-round-publish-mismatch expected=${expected_parallel_round_publish} actual=${actual_parallel_round_publish}" >&2
            return 4
        fi
        local -a artifacts=(
            "${root_install}/lib64/libtile-comm.so"
            "${root_install}/lib64/libtilexr-ep.so"
            "${root_install}/lib64/libtilexr_ep_urma_combine_kernel.so"
            "${ep_install}/bin/tilexr_ep_urma_combine_demo"
        )
        local -a expected_hashes=(
            "${TILEXR_VARIANT_COMM_SHA256:-}"
            "${TILEXR_VARIANT_EP_SHA256:-}"
            "${TILEXR_VARIANT_KERNEL_SHA256:-}"
            "${TILEXR_VARIANT_DEMO_SHA256:-}"
        )
        local index actual_hash
        for index in "${!artifacts[@]}"; do
            if [[ ! "${reference_hashes[$index]}" =~ ^[0-9a-f]{64}$ ||
                  ! "${expected_hashes[$index]}" =~ ^[0-9a-f]{64}$ ||
                  ! -f "${artifacts[$index]}" ]]; then
                echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=invalid-artifact path=${artifacts[$index]}" >&2
                return 4
            fi
            if [[ "${expected_hashes[$index]}" != "${reference_hashes[$index]}" ]]; then
                echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=manifest-hash-mismatch path=${artifacts[$index]} reference=${reference_hashes[$index]} local=${expected_hashes[$index]}" >&2
                return 4
            fi
            actual_hash="$(sha256sum "${artifacts[$index]}" | awk '{print $1}')"
            if [[ "${actual_hash}" != "${reference_hashes[$index]}" ]]; then
                echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=hash-mismatch path=${artifacts[$index]} expected=${reference_hashes[$index]} actual=${actual_hash}" >&2
                return 4
            fi
        done
        echo "PREFLIGHT_VARIANT node=$(hostname) variant=${build_variant} status=verified"
    fi
    IFS=',' read -r -a devices <<< "${device_csv}"
    for device in "${devices[@]}"; do
        set +e
        output="$(npu-smi info -t proc-mem -i "${device}" 2>&1)"
        status=$?
        set -e
        if [[ "${status}" -eq 0 ]] && grep -Fq "No process in device." <<< "${output}"; then
            echo "PREFLIGHT_DEVICE node=$(hostname) device=${device} status=idle"
            continue
        fi
        echo "PREFLIGHT_DEVICE node=$(hostname) device=${device} status=busy-or-unknown" >&2
        echo "${output}" >&2
        ret=3
    done
    return "${ret}"
}

if [[ "${1:-}" == "node" ]]; then
    shift
    run_node "$@"
    exit $?
fi
if [[ "${1:-}" == "preflight" ]]; then
    shift
    check_devices_idle "$@"
    exit $?
fi

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if [[ "$#" -ne 1 || ! "$1" =~ ^[1-9][0-9]*$ ]]; then
    usage
    exit 2
fi

ranks_per_host="$1"
hosts_spec="${TILEXR_COMBINE_HOSTS:-}"
device_maps_spec="${TILEXR_COMBINE_DEVICE_MAPS:-}"
remote_root="${TILEXR_COMBINE_REMOTE_ROOT:-}"
for required_name in TILEXR_COMBINE_HOSTS TILEXR_COMBINE_DEVICE_MAPS TILEXR_COMBINE_REMOTE_ROOT TILEXR_COMM_ID; do
    if [[ -z "${!required_name:-}" ]]; then
        echo "${required_name} is required by the delivery runner" >&2
        exit 2
    fi
done
remote_script="${remote_root}/tests/ep/demo/$(basename "${BASH_SOURCE[0]}")"
build_variant="${TILEXR_COMBINE_BUILD_VARIANT:-}"
remote_cann_home="${TILEXR_950A3_CANN_HOME:-}"
if [[ -n "${remote_cann_home}" &&
      ! "${remote_cann_home}" =~ ^/[A-Za-z0-9._/-]+$ ]]; then
    echo "TILEXR_950A3_CANN_HOME must be an absolute path without shell metacharacters" >&2
    exit 2
fi
remote_env=()
if [[ -n "${remote_cann_home}" ]]; then
    remote_env=(env "TILEXR_950A3_CANN_HOME=${remote_cann_home}")
fi
if [[ -n "${build_variant}" && ! "${build_variant}" =~ ^[a-z0-9-]+$ ]]; then
    echo "TILEXR_COMBINE_BUILD_VARIANT must contain only lowercase letters, digits, and hyphens" >&2
    exit 2
fi
if [[ -n "${build_variant}" ]]; then
    root_install="${remote_root}/install_variants/${build_variant}"
    ep_install="${remote_root}/tests/ep/install_variants/${build_variant}"
else
    root_install="${remote_root}/install"
    ep_install="${remote_root}/tests/ep/install"
fi
variant_parallel_round_publish=0
variant_start_gate=0
reference_variant_hashes=(- - - -)
reference_script_hash="$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
if [[ -n "${build_variant}" ]]; then
    variant_manifest="${root_install}/share/tilexr/ep_urma_combine_variant.env"
    if [[ ! -f "${variant_manifest}" ]]; then
        echo "missing local build variant manifest: ${variant_manifest}" >&2
        exit 4
    fi
    unset TILEXR_VARIANT_PARALLEL_ROUND_PUBLISH TILEXR_VARIANT_START_GATE
    source "${variant_manifest}"
    if [[ ! "${TILEXR_VARIANT_PARALLEL_ROUND_PUBLISH:-0}" =~ ^(0|1)$ ]]; then
        echo "invalid parallel round publish metadata in ${variant_manifest}" >&2
        exit 4
    fi
    variant_parallel_round_publish="${TILEXR_VARIANT_PARALLEL_ROUND_PUBLISH:-0}"
    if [[ ! "${TILEXR_VARIANT_START_GATE:-0}" =~ ^(0|1)$ ]]; then
        echo "invalid start gate metadata in ${variant_manifest}" >&2
        exit 4
    fi
    variant_start_gate="${TILEXR_VARIANT_START_GATE:-0}"
    reference_variant_hashes=(
        "${TILEXR_VARIANT_COMM_SHA256:-}"
        "${TILEXR_VARIANT_EP_SHA256:-}"
        "${TILEXR_VARIANT_KERNEL_SHA256:-}"
        "${TILEXR_VARIANT_DEMO_SHA256:-}"
    )
    for reference_hash in "${reference_variant_hashes[@]}"; do
        if [[ ! "${reference_hash}" =~ ^[0-9a-f]{64}$ ]]; then
            echo "invalid artifact hash metadata in ${variant_manifest}" >&2
            exit 4
        fi
    done
fi

IFS=',' read -r -a hosts <<< "${hosts_spec}"
IFS=';' read -r -a device_maps <<< "${device_maps_spec}"
if [[ "${#hosts[@]}" -lt 1 || "${#hosts[@]}" -ne "${#device_maps[@]}" ]]; then
    echo "host count and device-map count must match and be at least one" >&2
    exit 2
fi

rank_size=$((${#hosts[@]} * ranks_per_host))
comm_id="${TILEXR_COMM_ID}"
bs="${TILEXR_DEMO_BS:-128}"
h="${TILEXR_DEMO_H:-7168}"
topk="${TILEXR_DEMO_TOPK:-8}"
rounds="${TILEXR_DEMO_ROUNDS:-3}"
warmup_rounds="${TILEXR_DEMO_WARMUP_ROUNDS:-0}"
validate_every="${TILEXR_DEMO_VALIDATE_EVERY:-1}"
capacity_multiplier="${TILEXR_DEMO_CAPACITY_MULTIPLIER:-2}"
timeout_sec="${TILEXR_DEMO_TIMEOUT_SEC:-1200}"
profile_dir="${TILEXR_DEMO_PROFILE_DIR:--}"
profile_round="${TILEXR_DEMO_PROFILE_ROUND:-0}"
profile_detail="${TILEXR_DEMO_PROFILE_DETAIL:-2}"
profile_samples="${TILEXR_DEMO_PROFILE_SAMPLES:-1}"
enqueue_window="${TILEXR_DEMO_ENQUEUE_WINDOW:-1}"
route_seed="${TILEXR_DEMO_ROUTE_SEED:-20260721}"
device_event_latency="${TILEXR_DEMO_DEVICE_EVENT_LATENCY:-0}"
strict_kernel_latency="${TILEXR_DEMO_STRICT_KERNEL_LATENCY:-0}"
if [[ ! "${device_event_latency}" =~ ^(0|1)$ ||
      ! "${strict_kernel_latency}" =~ ^(0|1)$ ]] ||
   [[ "${device_event_latency}" == "1" && "${strict_kernel_latency}" == "1" ]]; then
    echo "latency flags must be 0 or 1 and mutually exclusive" >&2
    exit 2
fi
if [[ ! "${profile_samples}" =~ ^[1-9][0-9]*$ ]]; then
    echo "TILEXR_DEMO_PROFILE_SAMPLES must be a positive integer" >&2
    exit 2
fi
if [[ ! "${enqueue_window}" =~ ^[1-9][0-9]*$ ]]; then
    echo "TILEXR_DEMO_ENQUEUE_WINDOW must be a positive integer" >&2
    exit 2
fi
run_id="${TILEXR_DEMO_RUN_ID:-ep-combine-${rank_size}rank-bs${bs}-k${topk}-h${h}-$(date +%Y%m%d-%H%M%S)}"
log_root="${TILEXR_DEMO_MULTIHOST_LOG_ROOT:-${remote_root}/tests/ep/logs/${run_id}}"
mkdir -p "$(dirname "${log_root}")"
if ! mkdir "${log_root}"; then
    echo "RUN_ABORTED run=${run_id} reason=log-root-exists-or-create-failed path=${log_root}" >&2
    exit 5
fi

declare -a selected_device_maps=()
for index in "${!hosts[@]}"; do
    IFS=',' read -r -a available_devices <<< "${device_maps[$index]}"
    if [[ "${#available_devices[@]}" -lt "${ranks_per_host}" ]]; then
        echo "host ${hosts[$index]} has only ${#available_devices[@]} configured devices" >&2
        exit 2
    fi
    selected_devices=("${available_devices[@]:0:ranks_per_host}")
    selected_device_maps+=("$(IFS=','; echo "${selected_devices[*]}")")
done
selected_device_maps_report="$(IFS=';'; echo "${selected_device_maps[*]}")"

declare -a preflight_pids=()
declare -a preflight_logs=()
for index in "${!hosts[@]}"; do
    preflight_log="${log_root}/preflight_$((index + 1)).log"
    preflight_logs+=("${preflight_log}")
    ssh -o BatchMode=yes -o ConnectTimeout=10 \
        "${hosts[$index]}" "${remote_env[@]}" bash "${remote_script}" preflight \
        "${selected_device_maps[$index]}" \
        "${root_install}" "${ep_install}" "${build_variant:--}" "${reference_variant_hashes[@]}" \
        "${reference_script_hash}" \
        >"${preflight_log}" 2>&1 &
    preflight_pids+=("$!")
done

preflight_ret=0
for index in "${!preflight_pids[@]}"; do
    set +e
    wait "${preflight_pids[$index]}"
    status=$?
    set -e
    cat "${preflight_logs[$index]}"
    if [[ "${status}" -ne 0 ]]; then
        preflight_ret="${status}"
    fi
done
if [[ "${preflight_ret}" -ne 0 ]]; then
    echo "RUN_ABORTED run=${run_id} reason=preflight-failed status=${preflight_ret}" >&2
    exit "${preflight_ret}"
fi

declare -a ssh_pids=()
declare -a host_logs=()
cleanup() {
    local pid
    for pid in "${ssh_pids[@]:-}"; do
        kill "${pid}" 2>/dev/null || true
    done
}
trap cleanup INT TERM

echo "RUN_CONFIG run=${run_id} rankSize=${rank_size} hosts=${#hosts[@]} ranksPerHost=${ranks_per_host} commId=${comm_id} routeSeed=${route_seed} parallelRoundPublish=${variant_parallel_round_publish} startGate=${variant_start_gate} profileSamples=${profile_samples} enqueueWindow=${enqueue_window} deviceEventLatency=${device_event_latency} strictKernelLatency=${strict_kernel_latency}"
for index in "${!hosts[@]}"; do
    selected_csv="${selected_device_maps[$index]}"
    rank_begin=$((index * ranks_per_host))
    host_log="${log_root}/host_$((index + 1)).log"
    host_logs+=("${host_log}")
    ssh -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=30 \
        "${hosts[$index]}" "${remote_env[@]}" bash "${remote_script}" node \
        "${rank_size}" "${rank_begin}" "${selected_csv}" "${ranks_per_host}" \
        "${comm_id}" "${run_id}" "${log_root}" "${bs}" "${h}" "${topk}" \
        "${rounds}" "${warmup_rounds}" "${validate_every}" "${capacity_multiplier}" \
        "${timeout_sec}" "${profile_dir}" "${profile_round}" "${profile_detail}" \
        "${route_seed}" "${root_install}" "${ep_install}" "${device_event_latency}" \
        "${strict_kernel_latency}" "${profile_samples}" "${enqueue_window}" \
        >"${host_log}" 2>&1 &
    ssh_pids+=("$!")
done

ret=0
for index in "${!ssh_pids[@]}"; do
    set +e
    wait "${ssh_pids[$index]}"
    status=$?
    set -e
    if [[ "${status}" -ne 0 && "${ret}" -eq 0 ]]; then
        ret="${status}"
    fi
    echo "HOST_RESULT host=${hosts[$index]} status=${status} log=${host_logs[$index]}"
done
trap - INT TERM

if [[ "${ret}" -eq 0 && "${device_event_latency}" == "1" ]]; then
    set +e
    python3 "${SCRIPT_DIR}/tilexr_ep_urma_combine_device_latency_report.py" \
        --rank-size "${rank_size}" --warmup-rounds "${warmup_rounds}" --rounds "${rounds}" \
        --output-dir "${log_root}" "${host_logs[@]}"
    report_status=$?
    set -e
    if [[ "${report_status}" -ne 0 ]]; then
        echo "DEVICE_EVENT_LATENCY_REPORT_FAILED status=${report_status}" >&2
        ret="${report_status}"
    fi
fi
if [[ "${ret}" -eq 0 && "${strict_kernel_latency}" == "1" ]]; then
    set +e
    python3 "${SCRIPT_DIR}/tilexr_ep_urma_combine_strict_kernel_latency_report.py" \
        --rank-size "${rank_size}" --warmup-rounds "${warmup_rounds}" --rounds "${rounds}" \
        --bs "${bs}" --hidden "${h}" --top-k "${topk}" --route-seed "${route_seed}" \
        --artifact-variant "${build_variant:--}" --hosts "${hosts_spec}" \
        --device-maps "${selected_device_maps_report}" --ranks-per-host "${ranks_per_host}" \
        --parallel-round-publish "${variant_parallel_round_publish}" \
        --pack-cores "${TILEXR_VARIANT_PACK_CORES:-48}" \
        --send-cores "${TILEXR_VARIANT_SEND_CORES:-16}" \
        --qp-count "${TILEXR_VARIANT_QP_COUNT:-16}" \
        --doorbell-batch "${TILEXR_VARIANT_DOORBELL_BATCH:-1}" \
        --output-dir "${log_root}" "${host_logs[@]}"
    report_status=$?
    set -e
    if [[ "${report_status}" -ne 0 ]]; then
        echo "STRICT_KERNEL_LATENCY_REPORT_FAILED status=${report_status}" >&2
        ret="${report_status}"
    fi
fi

for log in "${host_logs[@]}"; do
    echo "===== ${log} ====="
    tail -n 100 "${log}" || true
done
echo "RUN_RESULT run=${run_id} rankSize=${rank_size} status=${ret} logRoot=${log_root}"
exit "${ret}"
