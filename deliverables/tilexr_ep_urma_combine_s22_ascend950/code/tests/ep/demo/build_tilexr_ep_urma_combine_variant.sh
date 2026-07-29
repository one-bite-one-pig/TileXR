#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TILEXR_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
EP_DIR="${TILEXR_ROOT}/tests/ep"

if [[ "$#" -ne 4 || ! "$1" =~ ^[a-z0-9-]+$ || ! "$2" =~ ^[1-9][0-9]*$ ||
      ! "$3" =~ ^(1|2|4|8)$ || ! "$4" =~ ^(0|1|2)$ ]]; then
    echo "Usage: $0 <variant> <send-cores> <doorbell-batch:1|2|4|8> <rx-scheduler:0|1|2>" >&2
    exit 2
fi

variant="$1"
send_cores="$2"
doorbell_batch="$3"
rx_scheduler="$4"
build_jobs="${TILEXR_BUILD_JOBS:-$(nproc)}"
profile_build="${TILEXR_EP_ENABLE_PROFILING_BUILD:-ON}"
parallel_round_publish_build="${TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD:-OFF}"
deferred_round_credit_build="${TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT_BUILD:-OFF}"
start_gate_build="${TILEXR_EP_URMA_START_GATE_BUILD:-OFF}"
qdc_version="${TILEXR_EP_URMA_QDC_VERSION_BUILD:-0}"
tx_ready_batch="${TILEXR_EP_URMA_TX_READY_BATCH_SIZE_BUILD:-1}"
tx_ready_shared_flag_build="${TILEXR_EP_URMA_TX_READY_SHARED_FLAG_BUILD:-OFF}"
tx_ready_in_data_build="${TILEXR_EP_URMA_TX_READY_IN_DATA_BUILD:-OFF}"
tx_meta_prefetch_full_build="${TILEXR_EP_URMA_TX_META_PREFETCH_FULL_BUILD:-OFF}"
tx_ready_early_publish_build="${TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH_BUILD:-OFF}"
rx_ready_sticky_mask_build="${TILEXR_EP_URMA_RX_READY_STICKY_MASK_BUILD:-OFF}"
rx_ready_batch_mte2_build="${TILEXR_EP_URMA_RX_READY_BATCH_MTE2_BUILD:-OFF}"
rx_ready_batch_vector_build="${TILEXR_EP_URMA_RX_READY_BATCH_VECTOR_BUILD:-OFF}"
qp_count_build="${TILEXR_EP_URMA_QP_COUNT_BUILD:-${send_cores}}"
operator_launch_build="${TILEXR_EP_URMA_OPERATOR_LAUNCH_BUILD:-OFF}"
kernel_opt_level="${TILEXR_EP_KERNEL_OPT_LEVEL_BUILD:-DEFAULT}"
kernel_diagnostic_mode="${TILEXR_EP_URMA_KERNEL_DIAGNOSTIC_MODE_BUILD:-NONE}"
cmake_build_type="${TILEXR_CMAKE_BUILD_TYPE_BUILD:-}"
if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "TILEXR_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi
if [[ ! "${profile_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_ENABLE_PROFILING_BUILD must be ON or OFF" >&2
    exit 2
fi
if [[ ! "${operator_launch_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_URMA_OPERATOR_LAUNCH_BUILD must be ON or OFF" >&2
    exit 2
fi
if [[ ! "${kernel_opt_level}" =~ ^(DEFAULT|O0|O1|O2|O3)$ ]]; then
    echo "TILEXR_EP_KERNEL_OPT_LEVEL_BUILD must be DEFAULT, O0, O1, O2, or O3" >&2
    exit 2
fi
if [[ ! "${kernel_diagnostic_mode}" =~ ^(NONE|NO_STRICT_ALIASING|NO_INLINE|NO_INLINE_FUNCTIONS|SEND_NOINLINE|UDMA_POST_NOINLINE|UDMA_HELPERS_NOINLINE|TILEXR_NOINLINE|TX_READY_RELEASE_FENCE|TX_READY_POLL_COMPILER_FENCE|CONTROL_LOAD_COMPILER_FENCE|UDMA_WQE_COMPILER_FENCE|UDMA_WQE_RELEASE_FENCE|PACK_LOCAL_DATACOPY|PACK_INIT_FENCE|PACK_SINGLE_SLOT|PROGRESS_TIMEOUT)$ ]]; then
    echo "Unsupported TILEXR_EP_URMA_KERNEL_DIAGNOSTIC_MODE_BUILD" >&2
    exit 2
fi
if [[ -n "${cmake_build_type}" &&
      ! "${cmake_build_type}" =~ ^(Debug|Release|RelWithDebInfo|MinSizeRel)$ ]]; then
    echo "TILEXR_CMAKE_BUILD_TYPE_BUILD is empty or a standard CMake build type" >&2
    exit 2
fi
if [[ ! "${parallel_round_publish_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD must be ON or OFF" >&2
    exit 2
fi
if [[ ! "${deferred_round_credit_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT_BUILD must be ON or OFF" >&2
    exit 2
fi
if [[ "${deferred_round_credit_build}" == "ON" &&
      "${parallel_round_publish_build}" != "ON" ]]; then
    echo "deferred round credit requires parallel round publish" >&2
    exit 2
fi
if [[ ! "${start_gate_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_URMA_START_GATE_BUILD must be ON or OFF" >&2
    exit 2
fi
if [[ ! "${qdc_version}" =~ ^(0|1|2|3)$ ]]; then
    echo "TILEXR_EP_URMA_QDC_VERSION_BUILD must be 0, 1, 2, or 3" >&2
    exit 2
fi
if [[ ! "${tx_ready_batch}" =~ ^(1|2|4)$ ]]; then
    echo "TILEXR_EP_URMA_TX_READY_BATCH_SIZE_BUILD must be 1, 2, or 4" >&2
    exit 2
fi
if [[ ! "${tx_ready_shared_flag_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_URMA_TX_READY_SHARED_FLAG_BUILD must be ON or OFF" >&2
    exit 2
fi
if [[ ! "${tx_ready_in_data_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_URMA_TX_READY_IN_DATA_BUILD must be ON or OFF" >&2
    exit 2
fi
if [[ ! "${tx_meta_prefetch_full_build}" =~ ^(ON|OFF)$ ]]; then
    echo "TILEXR_EP_URMA_TX_META_PREFETCH_FULL_BUILD must be ON or OFF" >&2
    exit 2
fi
for bool_setting in \
    "${tx_ready_early_publish_build}" \
    "${rx_ready_sticky_mask_build}" \
    "${rx_ready_batch_mte2_build}" \
    "${rx_ready_batch_vector_build}"; do
    if [[ ! "${bool_setting}" =~ ^(ON|OFF)$ ]]; then
        echo "early-publish and RX-ready feature build settings must be ON or OFF" >&2
        exit 2
    fi
done
if [[ ! "${qp_count_build}" =~ ^[1-9][0-9]*$ ]] || ((qp_count_build < send_cores)); then
    echo "TILEXR_EP_URMA_QP_COUNT_BUILD must be an integer greater than or equal to send-cores" >&2
    exit 2
fi
if [[ "${tx_ready_in_data_build}" == "ON" ]] &&
   { [[ "${tx_ready_batch}" != "1" ]] ||
     [[ "${tx_ready_shared_flag_build}" != "OFF" ]]; }; then
    echo "in-data TX-ready requires TX-ready batch 1 and shared-ready OFF" >&2
    exit 2
fi
if [[ "${tx_ready_early_publish_build}" == "ON" ]] &&
   { [[ "${tx_ready_in_data_build}" != "OFF" ]] ||
     [[ "${tx_ready_batch}" != "1" ]] ||
     [[ "${tx_ready_shared_flag_build}" != "OFF" ]]; }; then
    echo "early TX-ready publish requires separate per-route ready lines" >&2
    exit 2
fi
if [[ "${rx_ready_sticky_mask_build}" == "ON" ||
      "${rx_ready_batch_mte2_build}" == "ON" ]] && [[ "${rx_scheduler}" == "0" ]]; then
    echo "RX sticky masks and batched ready reads require a round-robin scheduler" >&2
    exit 2
fi
if [[ "${rx_ready_batch_vector_build}" == "ON" &&
      "${rx_ready_batch_mte2_build}" != "ON" ]]; then
    echo "RX ready Vector reduction requires batched MTE2 reads" >&2
    exit 2
fi
if [[ "${start_gate_build}" == "ON" &&
      "${parallel_round_publish_build}" != "ON" ]]; then
    echo "start gate requires parallel round publish" >&2
    exit 2
fi
if ((send_cores >= 64)); then
    echo "send-cores must be less than 64" >&2
    exit 2
fi
pack_cores=$((64 - send_cores))
parallel_round_publish=0
if [[ "${parallel_round_publish_build}" == "ON" ]]; then
    parallel_round_publish=1
fi
deferred_round_credit=0
if [[ "${deferred_round_credit_build}" == "ON" ]]; then
    deferred_round_credit=1
fi
start_gate=0
if [[ "${start_gate_build}" == "ON" ]]; then
    start_gate=1
fi
tx_ready_shared_flag=0
if [[ "${tx_ready_shared_flag_build}" == "ON" ]]; then
    tx_ready_shared_flag=1
fi
tx_ready_in_data=0
if [[ "${tx_ready_in_data_build}" == "ON" ]]; then
    tx_ready_in_data=1
fi
tx_meta_prefetch_full=0
if [[ "${tx_meta_prefetch_full_build}" == "ON" ]]; then
    tx_meta_prefetch_full=1
fi
tx_ready_early_publish=0
if [[ "${tx_ready_early_publish_build}" == "ON" ]]; then
    tx_ready_early_publish=1
fi
rx_ready_sticky_mask=0
if [[ "${rx_ready_sticky_mask_build}" == "ON" ]]; then
    rx_ready_sticky_mask=1
fi
rx_ready_batch_mte2=0
if [[ "${rx_ready_batch_mte2_build}" == "ON" ]]; then
    rx_ready_batch_mte2=1
fi
rx_ready_batch_vector=0
if [[ "${rx_ready_batch_vector_build}" == "ON" ]]; then
    rx_ready_batch_vector=1
fi
qp_count="${qp_count_build}"

source_sha256="$({
    while IFS= read -r -d '' source_file; do
        relative_path="${source_file#${TILEXR_ROOT}/}"
        content_sha256="$(sha256sum "${source_file}" | awk '{print $1}')"
        printf '%s\t%s\n' "${relative_path}" "${content_sha256}"
    done < <(find \
        "${TILEXR_ROOT}/CMakeLists.txt" \
        "${TILEXR_ROOT}/src" \
        "${TILEXR_ROOT}/3rdparty" \
        "${EP_DIR}/CMakeLists.txt" \
        "${EP_DIR}/demo" \
        "${TILEXR_ROOT}/scripts/env_950a3.sh" \
        -type f \
        -not -path '*/.git/*' \
        -not -path '*/__pycache__/*' \
        -not -name '*.pyc' \
        -not -name '*.pyo' \
        -print0 | LC_ALL=C sort -z)
} | sha256sum | awk '{print $1}')"

set +u
source "${TILEXR_ROOT}/scripts/env_950a3.sh"
set -u
export ARCH="${TILEXR_OS_ARCH}"

root_build="${TILEXR_ROOT}/build_ep_variants/${variant}"
root_install="${TILEXR_ROOT}/install_variants/${variant}"
ep_build="${EP_DIR}/build_variants/${variant}"
ep_install="${EP_DIR}/install_variants/${variant}"
for output_root in "${root_build}" "${root_install}" "${ep_build}" "${ep_install}"; do
    if [[ -e "${output_root}" ]]; then
        echo "Refusing to overwrite existing variant output: ${output_root}" >&2
        exit 3
    fi
done

common_args=(
    -DTILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT="${send_cores}"
    -DTILEXR_EP_URMA_DOORBELL_BATCH_SIZE="${doorbell_batch}"
    -DTILEXR_EP_URMA_TX_READY_BATCH_SIZE="${tx_ready_batch}"
    -DTILEXR_EP_URMA_TX_READY_SHARED_FLAG="${tx_ready_shared_flag}"
    -DTILEXR_EP_URMA_TX_READY_IN_DATA="${tx_ready_in_data}"
    -DTILEXR_EP_URMA_TX_META_PREFETCH_FULL="${tx_meta_prefetch_full}"
    -DTILEXR_EP_URMA_TX_READY_EARLY_PUBLISH="${tx_ready_early_publish}"
    -DTILEXR_EP_URMA_RX_READY_STICKY_MASK="${rx_ready_sticky_mask}"
    -DTILEXR_EP_URMA_RX_READY_BATCH_MTE2="${rx_ready_batch_mte2}"
    -DTILEXR_EP_URMA_RX_READY_BATCH_VECTOR="${rx_ready_batch_vector}"
    -DTILEXR_EP_URMA_RX_SCHEDULER="${rx_scheduler}"
    -DTILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH="${parallel_round_publish}"
    -DTILEXR_EP_URMA_DEFERRED_ROUND_CREDIT="${deferred_round_credit}"
    -DTILEXR_EP_URMA_START_GATE="${start_gate}"
    -DTILEXR_EP_URMA_QDC_VERSION="${qdc_version}"
    -DTILEXR_UDMA_QP_COUNT="${qp_count}"
    -DTILEXR_EP_URMA_OPERATOR_LAUNCH="${operator_launch_build}"
    -DTILEXR_EP_KERNEL_OPT_LEVEL="${kernel_opt_level}"
    -DTILEXR_EP_URMA_KERNEL_DIAGNOSTIC_MODE="${kernel_diagnostic_mode}"
)

cmake -S "${TILEXR_ROOT}" -B "${root_build}" \
    -DCMAKE_INSTALL_PREFIX="${root_install}" \
    -DCMAKE_BUILD_TYPE="${cmake_build_type}" \
    -DTILEXR_BUILD_EP=ON \
    -DTILEXR_EP_ENABLE_PROFILING="${profile_build}" \
    "${common_args[@]}"
root_build_log="${root_build}/tilexr_build_verbose.log"
cmake --build "${root_build}" --target install --verbose -j"${build_jobs}" 2>&1 | tee "${root_build_log}"

cmake -S "${EP_DIR}" -B "${ep_build}" \
    -DCMAKE_INSTALL_PREFIX="${ep_install}" \
    -DCMAKE_BUILD_TYPE="${cmake_build_type}" \
    -DTILEXR_INSTALL_PREFIX="${root_install}" \
    -DBUILD_TILEXR_EP_DEMO=ON \
    "${common_args[@]}"
ep_build_log="${ep_build}/tilexr_ep_demo_build_verbose.log"
cmake --build "${ep_build}" --target install --verbose -j"${build_jobs}" 2>&1 | tee "${ep_build_log}"

artifacts=(
    "${root_install}/lib64/libtile-comm.so"
    "${root_install}/lib64/libtilexr-ep.so"
    "${root_install}/lib64/libtilexr_ep_urma_combine_kernel.so"
    "${ep_install}/bin/tilexr_ep_urma_combine_demo"
)
manifest_dir="${root_install}/share/tilexr"
manifest="${manifest_dir}/ep_urma_combine_variant.env"
mkdir -p "${manifest_dir}"
artifact_hashes=()
for artifact in "${artifacts[@]}"; do
    artifact_hashes+=("$(sha256sum "${artifact}" | awk '{print $1}')")
done
kernel_command_file="${manifest_dir}/ep_urma_combine_bisheng_command.txt"
kernel_driver_file="${manifest_dir}/ep_urma_combine_bisheng_driver.txt"
kernel_driver_status_file="${manifest_dir}/ep_urma_combine_bisheng_driver.status"
bisheng_executable="$(sed -n 's/^BISHENG_EXECUTABLE:FILEPATH=//p' "${root_build}/CMakeCache.txt")"
if [[ -z "${bisheng_executable}" || ! -x "${bisheng_executable}" ]]; then
    echo "Unable to resolve BiSheng executable from ${root_build}/CMakeCache.txt" >&2
    exit 4
fi
kernel_command="$(grep -m1 -E 'bisheng .*tilexr_ep_urma_combine_kernel.cpp' "${root_build_log}" || true)"
if [[ -z "${kernel_command}" ]]; then
    echo "Unable to find expanded URMA BiSheng command in ${root_build_log}" >&2
    exit 4
fi
printf '%s\n' "${kernel_command}" >"${kernel_command_file}"
audit_kernel_so="${manifest_dir}/ep_urma_combine_kernel_audit.so"
verbose_command="${kernel_command/${bisheng_executable}/${bisheng_executable} -v}"
verbose_command="${verbose_command% -o *} -o ${audit_kernel_so}"
set +e
bash -c "${verbose_command}" >"${kernel_driver_file}" 2>&1
kernel_driver_status=$?
set -e
printf '%s\n' "${kernel_driver_status}" >"${kernel_driver_status_file}"
if [[ "${kernel_driver_status}" -ne 0 || ! -f "${audit_kernel_so}" ]]; then
    echo "BiSheng verbose audit failed with status ${kernel_driver_status}" >&2
    exit 4
fi
audit_kernel_sha256="$(sha256sum "${audit_kernel_so}" | awk '{print $1}')"
if [[ "${audit_kernel_sha256}" != "${artifact_hashes[2]}" ]]; then
    echo "Installed kernel SHA256 does not match the audited BiSheng rebuild" >&2
    echo "installed=${artifact_hashes[2]} audited=${audit_kernel_sha256}" >&2
    exit 4
fi

compiler_version="$(${bisheng_executable} --version 2>&1 | head -n 1 | tr '[:space:]' '_' | tr -cd '[:alnum:]_.:+-')"
soc_type="$(sed -n 's/^TILEXR_EP_SOC_TYPE:STRING=//p' "${root_build}/CMakeCache.txt")"
actual_cmake_build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${root_build}/CMakeCache.txt")"
[[ -n "${actual_cmake_build_type}" ]] || actual_cmake_build_type="EMPTY"
operator_launch=0
if [[ "${operator_launch_build}" == "ON" ]]; then
    operator_launch=1
fi
kernel_source_sha256="$(sha256sum "${TILEXR_ROOT}/src/ep/kernels/tilexr_ep_urma_combine_kernel.cpp" | awk '{print $1}')"
kernel_command_sha256="$(sha256sum "${kernel_command_file}" | awk '{print $1}')"
kernel_driver_sha256="$(sha256sum "${kernel_driver_file}" | awk '{print $1}')"
operator_binary_sha256="NONE"
if [[ "${operator_launch_build}" == "ON" ]]; then
    operator_binary="${root_install}/lib64/tilexr_ep_urma_combine_kernel.o"
    if [[ ! -f "${operator_binary}" ]] ||
       ! readelf -h "${operator_binary}" | grep -Eq 'Type:[[:space:]]+EXEC'; then
        echo "Missing or invalid linked URMA AIV operator binary: ${operator_binary}" >&2
        exit 4
    fi
    operator_binary_sha256="$(sha256sum "${operator_binary}" | awk '{print $1}')"
fi
printf '%s\n' \
    "TILEXR_VARIANT_NAME=${variant}" \
    "TILEXR_VARIANT_PACK_CORES=${pack_cores}" \
    "TILEXR_VARIANT_SEND_CORES=${send_cores}" \
    "TILEXR_VARIANT_QP_COUNT=${qp_count}" \
    "TILEXR_VARIANT_DOORBELL_BATCH=${doorbell_batch}" \
    "TILEXR_VARIANT_TX_READY_BATCH=${tx_ready_batch}" \
    "TILEXR_VARIANT_TX_READY_SHARED_FLAG=${tx_ready_shared_flag}" \
    "TILEXR_VARIANT_TX_READY_IN_DATA=${tx_ready_in_data}" \
    "TILEXR_VARIANT_TX_META_PREFETCH_FULL=${tx_meta_prefetch_full}" \
    "TILEXR_VARIANT_TX_READY_EARLY_PUBLISH=${tx_ready_early_publish}" \
    "TILEXR_VARIANT_RX_READY_STICKY_MASK=${rx_ready_sticky_mask}" \
    "TILEXR_VARIANT_RX_READY_BATCH_MTE2=${rx_ready_batch_mte2}" \
    "TILEXR_VARIANT_RX_READY_BATCH_VECTOR=${rx_ready_batch_vector}" \
    "TILEXR_VARIANT_RX_SCHEDULER=${rx_scheduler}" \
    "TILEXR_VARIANT_PARALLEL_ROUND_PUBLISH=${parallel_round_publish}" \
    "TILEXR_VARIANT_DEFERRED_ROUND_CREDIT=${deferred_round_credit}" \
    "TILEXR_VARIANT_START_GATE=${start_gate}" \
    "TILEXR_VARIANT_QDC_VERSION=${qdc_version}" \
    "TILEXR_VARIANT_SEND_ROUTE_BALANCED=1" \
    "TILEXR_VARIANT_PROFILING=${profile_build}" \
    "TILEXR_VARIANT_OPERATOR_LAUNCH=${operator_launch}" \
    "TILEXR_VARIANT_KERNEL_OPT_LEVEL=${kernel_opt_level}" \
    "TILEXR_VARIANT_KERNEL_DIAGNOSTIC_MODE=${kernel_diagnostic_mode}" \
    "TILEXR_VARIANT_CMAKE_BUILD_TYPE=${actual_cmake_build_type}" \
    "TILEXR_VARIANT_COMPILER_VERSION=${compiler_version}" \
    "TILEXR_VARIANT_SOC_NAME=${TILEXR_SOC_NAME:-}" \
    "TILEXR_VARIANT_SOC_TYPE=${soc_type}" \
    "TILEXR_VARIANT_AICORE_ARCH=dav-c310-vec" \
    "TILEXR_VARIANT_NPU_ARCH=dav-3510" \
    "TILEXR_VARIANT_SOURCE_SHA256=${source_sha256}" \
    "TILEXR_VARIANT_KERNEL_SOURCE_SHA256=${kernel_source_sha256}" \
    "TILEXR_VARIANT_KERNEL_COMMAND_SHA256=${kernel_command_sha256}" \
    "TILEXR_VARIANT_KERNEL_DRIVER_SHA256=${kernel_driver_sha256}" \
    "TILEXR_VARIANT_OPERATOR_BINARY_SHA256=${operator_binary_sha256}" \
    "TILEXR_VARIANT_COMM_SHA256=${artifact_hashes[0]}" \
    "TILEXR_VARIANT_EP_SHA256=${artifact_hashes[1]}" \
    "TILEXR_VARIANT_KERNEL_SHA256=${artifact_hashes[2]}" \
    "TILEXR_VARIANT_DEMO_SHA256=${artifact_hashes[3]}" \
    >"${manifest}"
sha256sum "${artifacts[@]}"
sha256sum "${manifest}" "${kernel_command_file}" "${kernel_driver_file}" "${audit_kernel_so}"
echo "VARIANT_BUILD_RESULT variant=${variant} pack=${pack_cores} send=${send_cores} qps=${qp_count} db=${doorbell_batch} tx_ready_batch=${tx_ready_batch} tx_ready_shared_flag=${tx_ready_shared_flag} tx_ready_in_data=${tx_ready_in_data} tx_meta_prefetch_full=${tx_meta_prefetch_full} tx_ready_early_publish=${tx_ready_early_publish} rx_ready_sticky_mask=${rx_ready_sticky_mask} rx_ready_batch_mte2=${rx_ready_batch_mte2} rx_ready_batch_vector=${rx_ready_batch_vector} rx=${rx_scheduler} parallel_round_publish=${parallel_round_publish} deferred_round_credit=${deferred_round_credit} start_gate=${start_gate} qdc_version=${qdc_version} send_route_balanced=1 profiling=${profile_build} operator_launch=${operator_launch} kernel_opt_level=${kernel_opt_level} kernel_diagnostic_mode=${kernel_diagnostic_mode} cmake_build_type=${actual_cmake_build_type} source_sha256=${source_sha256} status=0"
