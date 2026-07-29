import importlib.util
import json
import statistics
import tempfile
import unittest
from pathlib import Path


EP_DIR = Path(__file__).resolve().parents[1]
ROOT = EP_DIR.parents[1]
DEMO_DIR = EP_DIR / "demo"
DEMO_PATH = DEMO_DIR / "tilexr_ep_urma_combine_demo.cpp"
HOST_HEADER_PATH = ROOT / "src" / "ep" / "host" / "ep_urma_combine_host.h"
HOST_SOURCE_PATH = ROOT / "src" / "ep" / "host" / "ep_urma_combine_host.cpp"
RUNNER_PATH = DEMO_DIR / "run_tilexr_ep_urma_combine_multihost.sh"
WRAPPER_PATH = DEMO_DIR / "run_tilexr_ep_urma_combine_s1_48_db1_strict_kernel_latency.sh"
WRAPPER_8_PATH = DEMO_DIR / "run_tilexr_ep_urma_combine_s1_48_db1_strict_kernel_latency_8card.sh"
PARALLEL_BUILD_PATH = DEMO_DIR / "build_tilexr_ep_urma_combine_s1_48_parallel_round_publish_variant.sh"
PARALLEL_PROFILE_BUILD_PATH = (
    DEMO_DIR / "build_tilexr_ep_urma_combine_s1_48_parallel_round_publish_profile_variant.sh"
)
PARALLEL_WRAPPER_PATH = DEMO_DIR / "run_tilexr_ep_urma_combine_s1_48_parallel_round_publish_strict_kernel_latency.sh"
REPORT_PATH = DEMO_DIR / "tilexr_ep_urma_combine_strict_kernel_latency_report.py"
BUILD_PATH = DEMO_DIR / "build_tilexr_ep_urma_combine_variant.sh"
CMAKE_PATH = EP_DIR / "CMakeLists.txt"

spec = importlib.util.spec_from_file_location("strict_kernel_latency_report", REPORT_PATH)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def function_body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function body for {signature}")


def compact(source):
    return " ".join(source.split()).replace("( ", "(").replace(" )", ")")


class StrictKernelSourceGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.demo = DEMO_PATH.read_text(encoding="utf-8")
        cls.host_header = HOST_HEADER_PATH.read_text(encoding="utf-8")
        cls.host_source = HOST_SOURCE_PATH.read_text(encoding="utf-8")
        cls.runner = RUNNER_PATH.read_text(encoding="utf-8")
        cls.wrapper = WRAPPER_PATH.read_text(encoding="utf-8")
        cls.wrapper8 = WRAPPER_8_PATH.read_text(encoding="utf-8")
        cls.parallel_build = PARALLEL_BUILD_PATH.read_text(encoding="utf-8")
        cls.parallel_profile_build = PARALLEL_PROFILE_BUILD_PATH.read_text(encoding="utf-8")
        cls.parallel_wrapper = PARALLEL_WRAPPER_PATH.read_text(encoding="utf-8")
        cls.build = BUILD_PATH.read_text(encoding="utf-8")
        cls.cmake = CMAKE_PATH.read_text(encoding="utf-8")

    def test_prepared_helper_body_is_one_direct_kernel_submission(self):
        signature = "int TileXREpLaunchPreparedUrmaCombineKernel("
        self.assertIn(signature, self.host_header)
        body = function_body(self.host_source, signature).strip()
        self.assertEqual(body.count("launch_tilexr_ep_urma_combine_kernel("), 1)
        self.assertEqual(body.count("LaunchUrmaCombineOperator("), 1)
        self.assertIn("return TileXR::TILEXR_SUCCESS;", body)
        for forbidden in (
            "TileXRCommNextMagic",
            "Validate",
            "Prepare",
            "UDMARegistry",
        ):
            self.assertNotIn(forbidden, body)

    def test_existing_launch_path_allocates_magic_then_delegates(self):
        body = compact(function_body(self.host_source, "int TileXREpLaunchUrmaCombineKernel("))
        magic = body.index("TileXRCommNextMagic(params.comm, &magic)")
        prepared = body.index("TileXREpLaunchPreparedUrmaCombineKernel(params, context, magic, true)")
        self.assertLess(magic, prepared)
        self.assertTrue(body.endswith(
            "return TileXREpLaunchPreparedUrmaCombineKernel(params, context, magic, true);"
        ))
        self.assertNotIn("launch_tilexr_ep_urma_combine_kernel(", body)

    def test_strict_measurement_uses_in_kernel_cycles_without_events(self):
        begin = self.demo.index("auto measureStrictKernel")
        end = self.demo.index("for (int64_t warmup", begin)
        measurement = compact(self.demo[begin:end])
        ordered = [
            "aclrtMemset(strictKernelCyclesDev",
            "prepareOutput()",
            "TileXRCommNextMagic(resources.comm, &magic)",
            "TileXREpLaunchPreparedUrmaCombineKernel(strictParams, strictContext, magic)",
            "aclrtSynchronizeStream(resources.stream)",
            "aclrtMemcpy(strictKernelCycles.data()",
        ]
        positions = [measurement.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))
        self.assertNotIn("aclrtEvent", measurement)
        for forbidden in (
            "TileXREpValidateBasicUrmaCombineParams",
            "TileXREpPrepareUrmaCombineLaunchContext",
        ):
            self.assertNotIn(forbidden, measurement)

    def test_kernel_records_one_nonzero_duration_per_aiv_after_end_cycle(self):
        self.assertIn("void *strictKernelCycles = nullptr", self.host_header)
        self.assertIn("int64_t strictKernelCyclesBytes = 0", self.host_header)
        finish = function_body(self.host_source, "int TileXREpLaunchPreparedUrmaCombineKernel(")
        self.assertIn("static_cast<GM_ADDR>(params.strictKernelCycles)", finish)
        kernel_source = (ROOT / "src" / "ep" / "kernels" /
                         "tilexr_ep_urma_combine_kernel.cpp").read_text(encoding="utf-8")
        timing_begin = function_body(
            kernel_source,
            "__aicore__ TILEXR_EP_LOCAL_FUNCTION uint64_t StrictKernelTimingBegin(",
        )
        self.assertLess(timing_begin.index("PipeBarrier<PIPE_ALL>()"),
                        timing_begin.index("AscendC::GetSystemCycle()"))
        timing_finish = function_body(
            kernel_source,
            "__aicore__ TILEXR_EP_LOCAL_FUNCTION void StrictKernelTimingFinish(",
        )
        drain = timing_finish.index("PipeBarrier<PIPE_ALL>()")
        end = timing_finish.index("AscendC::GetSystemCycle()")
        write = timing_finish.index("WriteGmByPassDCache")
        barrier = timing_finish.index("DataSyncBarrier<AscendC::MemDsbT::DDR>()")
        self.assertLess(drain, end)
        self.assertLess(end, write)
        self.assertLess(write, barrier)
        self.assertIn("endCycle - startCycle", timing_finish)
        self.assertIn("strictKernelCycles != nullptr && perfTrace != nullptr", kernel_source)
        self.assertEqual(kernel_source.count(
            "StrictKernelTimingFinish(strictKernelCycles, perfCore, strictKernelStart)"), 2)

    def test_context_is_prepared_once_after_registration_and_reused_by_warmups(self):
        register = self.demo.index("TileXRUDMARegister(resources.comm")
        prepare = self.demo.index("TileXREpPrepareUrmaCombineLaunchContext(strictParams")
        warmup_lambda = self.demo.index("auto runStrictKernelWarmup")
        warmup_loop = self.demo.index("for (int64_t warmup")
        self.assertLess(register, prepare)
        self.assertLess(prepare, warmup_lambda)
        self.assertLess(warmup_lambda, warmup_loop)
        warmup = compact(
            self.demo[warmup_lambda:self.demo.index("auto measureStrictKernel", warmup_lambda)]
        )
        self.assertIn("prepareOutput()", warmup)
        self.assertIn("TileXRCommNextMagic(resources.comm, &magic)", warmup)
        self.assertIn("TileXREpLaunchPreparedUrmaCombineKernel(strictParams, strictContext, magic)", warmup)
        self.assertNotIn("aclrtRecordEvent", warmup)

    def test_strict_mode_contract_and_internal_include_are_narrow(self):
        self.assertIn('GetEnvInt("TILEXR_DEMO_STRICT_KERNEL_LATENCY", 0)', self.demo)
        self.assertIn('"STRICT_KERNEL_LATENCY rank="', self.demo)
        self.assertIn('" max_core=" << strictMaxCore << " max_cycles=" << strictMaxCycles', self.demo)
        self.assertIn("deviceEventLatency && strictKernelLatency", self.demo)
        self.assertIn("const bool full64LatencyShape", self.demo)
        self.assertIn("const bool singleHost8StrictShape", self.demo)
        self.assertIn("kStrictKernelSingleHostRankSize = 8", self.demo)
        self.assertIn("kStrictKernelSingleHostBatchSize = 32", self.demo)
        self.assertIn("kStrictKernelSingleHostHidden = 5120", self.demo)
        self.assertIn("kStrictKernelSingleHostTopK = 6", self.demo)
        self.assertIn("kR141StrictMeasuredRounds = 10", self.demo)
        self.assertIn("kR141StrictExtendedMeasuredRounds = 100", self.demo)
        self.assertIn("kR141StrictTotalCores = 64", self.demo)
        self.assertIn(
            "sendCores == 20 || sendCores == 22 || sendCores == 24 ||\n"
            "        sendCores == 28 || sendCores == 32",
            self.demo,
        )
        self.assertIn(
            "r141StrictPackCores + r141StrictSendCores == kR141StrictTotalCores",
            self.demo,
        )
        self.assertIn("TileXR::TILEXR_UDMA_QP_COUNT == r141StrictSendCores", self.demo)
        self.assertNotIn("kR141StrictPackCores = 44", self.demo)
        self.assertNotIn("kR141StrictSendCores = 20", self.demo)
        self.assertIn(
            "rounds == kR141StrictMeasuredRounds ||\n"
            "        rounds == kR141StrictExtendedMeasuredRounds",
            self.demo,
        )
        self.assertIn("strictParams.perfTrace = nullptr", self.demo)
        self.assertIn("strictParams.perfTraceBytes = 0", self.demo)
        self.assertIn("params.strictKernelCycles != nullptr && (params.perfTrace != nullptr", self.host_source)
        include_block = (
            'target_include_directories(tilexr_ep_urma_combine_demo PRIVATE\n'
            '        "${TILEXR_ROOT}/src/ep/host")'
        )
        self.assertIn(include_block, self.cmake)
        self.assertNotIn(
            'target_include_directories(tilexr_ep_dispatch_demo PRIVATE\n'
            '        "${TILEXR_ROOT}/src/ep/host")',
            self.cmake,
        )

    def test_launcher_keeps_old_protocol_and_routes_strict_samples_separately(self):
        self.assertIn('local device_event_latency="${22:-0}"', self.runner)
        self.assertIn('local strict_kernel_latency="${23:-0}"', self.runner)
        self.assertIn("[[ \"$#\" -lt 21 || \"$#\" -gt 25 ]]", self.runner)
        self.assertIn('local profile_samples="${24:-1}"', self.runner)
        self.assertIn('local enqueue_window="${25:-1}"', self.runner)
        self.assertIn('export TILEXR_DEMO_ENQUEUE_WINDOW="${enqueue_window}"', self.runner)
        self.assertIn("grep -h -E '^DEVICE_EVENT_LATENCY '", self.runner)
        self.assertIn("grep -h -E '^STRICT_KERNEL_LATENCY '", self.runner)
        self.assertIn("tilexr_ep_urma_combine_device_latency_report.py", self.runner)
        self.assertIn("tilexr_ep_urma_combine_strict_kernel_latency_report.py", self.runner)
        self.assertIn("reason=log-root-exists-or-create-failed", self.runner)
        self.assertIn("reason=node-log-dir-exists-or-create-failed", self.runner)
        self.assertIn("must match and be at least one", self.runner)
        for option in (
            '--bs "${bs}"', '--hidden "${h}"', '--top-k "${topk}"',
            '--route-seed "${route_seed}"', '--artifact-variant "${build_variant:--}"',
            '--hosts "${hosts_spec}"', '--device-maps "${selected_device_maps_report}"',
            '--ranks-per-host "${ranks_per_host}"',
            '--pack-cores "${TILEXR_VARIANT_PACK_CORES:-48}"',
            '--send-cores "${TILEXR_VARIANT_SEND_CORES:-16}"',
            '--qp-count "${TILEXR_VARIANT_QP_COUNT:-16}"',
            '--doorbell-batch "${TILEXR_VARIANT_DOORBELL_BATCH:-1}"',
        ):
            self.assertIn(option, self.runner)

    def test_launcher_explicitly_forwards_validated_cann_home(self):
        self.assertIn('remote_cann_home="${TILEXR_950A3_CANN_HOME:-}"', self.runner)
        self.assertIn('! "${remote_cann_home}" =~ ^/[A-Za-z0-9._/-]+$', self.runner)
        self.assertIn('remote_env=(env "TILEXR_950A3_CANN_HOME=${remote_cann_home}")', self.runner)
        self.assertEqual(self.runner.count('"${hosts[$index]}" "${remote_env[@]}" bash'), 2)

    def test_dedicated_wrapper_pins_strict_contract(self):
        for assignment in (
            "EXPECTED_VARIANT=\"s1-p48-s16-db1-strict-cycle-v1\"",
            "TILEXR_DEMO_DEVICE_EVENT_LATENCY=0",
            "TILEXR_DEMO_STRICT_KERNEL_LATENCY=1",
            "TILEXR_DEMO_BS=128",
            "TILEXR_DEMO_TOPK=8",
            "TILEXR_DEMO_H=7168",
            "TILEXR_DEMO_ROUTE_SEED=20260721",
            "TILEXR_DEMO_WARMUP_ROUNDS=20",
            "TILEXR_DEMO_ROUNDS=100",
            'exec bash "${RUNNER}" 8',
        ):
            self.assertIn(assignment, self.wrapper)
        self.assertEqual(self.wrapper.count("0,1,2,3,4,5,6,7"), 8)
        self.assertIn("unset TILEXR_DEMO_PROFILE_DIR", self.wrapper)

    def test_single_host_8card_wrapper_pins_shape_and_avoids_the_old_busy_card(self):
        for assignment in (
            'EXPECTED_VARIANT="s1-p48-s16-db1-strict-cycle-v2-8card"',
            "TILEXR_COMBINE_HOSTS='root@141.61.52.35'",
            "TILEXR_COMBINE_DEVICE_MAPS='0,1,2,3,4,5,6,7'",
            "TILEXR_DEMO_DEVICE_EVENT_LATENCY=0",
            "TILEXR_DEMO_STRICT_KERNEL_LATENCY=1",
            "TILEXR_DEMO_BS=32",
            "TILEXR_DEMO_TOPK=6",
            "TILEXR_DEMO_H=5120",
            "TILEXR_DEMO_ROUTE_SEED=20260721",
            "TILEXR_DEMO_WARMUP_ROUNDS=20",
            "TILEXR_DEMO_ROUNDS=100",
            'exec bash "${RUNNER}" 8',
        ):
            self.assertIn(assignment, self.wrapper8)
        self.assertNotIn("141.61.52.43", self.wrapper8)

    def test_strict_variant_has_an_independent_profile_off_manifest_contract(self):
        self.assertIn('profile_build="${TILEXR_EP_ENABLE_PROFILING_BUILD:-ON}"', self.build)
        self.assertIn('-DTILEXR_EP_ENABLE_PROFILING="${profile_build}"', self.build)
        self.assertIn('"TILEXR_VARIANT_PROFILING=${profile_build}"', self.build)
        self.assertIn('"${build_variant}" == "s1-p48-s16-db1-strict-cycle-v1"', self.runner)
        self.assertIn('"${build_variant}" == "s1-p48-s16-db1-strict-cycle-v2-8card"', self.runner)
        for token in (
            '"${TILEXR_VARIANT_PACK_CORES:-}" != "48"',
            '"${TILEXR_VARIANT_SEND_CORES:-}" != "16"',
            '"${TILEXR_VARIANT_QP_COUNT:-}" != "16"',
            '"${TILEXR_VARIANT_DOORBELL_BATCH:-}" != "1"',
            '"${TILEXR_VARIANT_RX_SCHEDULER:-}" != "1"',
            '"${TILEXR_VARIANT_PROFILING:-}" != "OFF"',
        ):
            self.assertIn(token, self.runner)

    def test_parallel_publish_variant_is_independent_and_manifest_gated(self):
        variant = "s1-p48-s16-db1-prp16-strict-cycle-v1"
        self.assertIn(f'EXPECTED_VARIANT="{variant}"', self.parallel_build)
        self.assertIn("TILEXR_EP_ENABLE_PROFILING_BUILD=OFF", self.parallel_build)
        self.assertIn("TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD=ON", self.parallel_build)
        self.assertIn('"${EXPECTED_VARIANT}" 16 1 1', self.parallel_build)
        for assignment in (
            f'EXPECTED_VARIANT="{variant}"',
            "TILEXR_DEMO_STRICT_KERNEL_LATENCY=1",
            "TILEXR_DEMO_BS=128",
            "TILEXR_DEMO_TOPK=8",
            "TILEXR_DEMO_H=7168",
            "TILEXR_DEMO_WARMUP_ROUNDS=20",
            "TILEXR_DEMO_ROUNDS=100",
            'exec bash "${RUNNER}" 8',
        ):
            self.assertIn(assignment, self.parallel_wrapper)
        self.assertIn("TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD", self.build)
        self.assertIn(
            '"TILEXR_VARIANT_PARALLEL_ROUND_PUBLISH=${parallel_round_publish}"',
            self.build,
        )
        self.assertIn(f"{variant})", self.runner)
        self.assertIn(
            "s1-p48-s16-db1-strict-cycle-v1|s1-p48-s16-db1-strict-cycle-v2-8card",
            self.runner,
        )
        self.assertIn("expected_parallel_round_publish=0", self.runner)
        self.assertIn("expected_parallel_round_publish=1", self.runner)
        self.assertIn(
            'actual_parallel_round_publish="${TILEXR_VARIANT_PARALLEL_ROUND_PUBLISH:-0}"',
            self.runner,
        )
        self.assertIn('"${expected_parallel_round_publish}"', self.runner)
        self.assertIn(
            '--parallel-round-publish "${variant_parallel_round_publish}"', self.runner
        )
        self.assertNotIn(variant, self.wrapper)
        self.assertNotIn(variant, self.wrapper8)

    def test_parallel_publish_has_distinct_profiled_build_variant(self):
        self.assertIn(
            'EXPECTED_VARIANT="s1-p48-s16-db1-prp16-profile-v1"',
            self.parallel_profile_build,
        )
        self.assertIn("TILEXR_EP_ENABLE_PROFILING_BUILD=ON", self.parallel_profile_build)
        self.assertIn(
            "TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH_BUILD=ON",
            self.parallel_profile_build,
        )
        self.assertIn('"${EXPECTED_VARIANT}" 16 1 1', self.parallel_profile_build)
        self.assertIn("TILEXR_EP_ENABLE_PROFILING_BUILD=OFF", self.parallel_build)


class StrictKernelReportTest(unittest.TestCase):
    def test_parser_validates_nonzero_cycles_core_and_1000_cycles_per_us(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = root / "valid.log"
            valid.write_text(
                "STRICT_KERNEL_LATENCY rank=0 round=0 max_core=17 "
                "max_cycles=123456 elapsed_us=123.456000\n",
                encoding="utf-8",
            )
            samples = report.parse_samples([valid])
            self.assertEqual(
                samples[(0, 0)],
                {"max_core": 17, "max_cycles": 123456, "elapsed_us": 123.456},
            )
            invalid = root / "invalid.log"
            invalid.write_text(
                "STRICT_KERNEL_LATENCY rank=0 round=0 max_core=64 "
                "max_cycles=0 elapsed_us=1.000000\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "invalid strict kernel sample"):
                report.parse_samples([invalid])
            mismatch = root / "mismatch.log"
            mismatch.write_text(
                "STRICT_KERNEL_LATENCY rank=0 round=0 max_core=1 "
                "max_cycles=1000 elapsed_us=2.000000\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "cycles/us mismatch"):
                report.parse_samples([mismatch])

    def test_exact_64_rank_100_round_shape_aggregates_round_max_first(self):
        samples = {
            (rank, round_index): {
                "max_core": rank % 64,
                "max_cycles": (rank + 1) * 10000 + round_index * 10,
                "elapsed_us": (rank + 1) * 10.0 + round_index * 0.01,
            }
            for rank in range(64)
            for round_index in range(100)
        }
        report.validate_samples(samples, rank_size=64, rounds=100)
        maxima = report.aggregate_round_maxima(samples, rank_size=64, rounds=100)
        self.assertEqual(len(maxima), 100)
        self.assertTrue(all(row["max_rank"] == 63 for row in maxima))
        self.assertEqual(maxima[0]["max_us"], 640.0)
        self.assertEqual(maxima[-1]["max_us"], 640.99)

    def test_exact_8_rank_100_round_shape_aggregates_round_max_first(self):
        samples = {
            (rank, round_index): {
                "max_core": (rank + round_index) % 64,
                "max_cycles": (rank + 1) * 1000 + round_index,
                "elapsed_us": (rank + 1) + round_index / 1000.0,
            }
            for rank in range(8)
            for round_index in range(100)
        }
        report.validate_samples(samples, rank_size=8, rounds=100)
        maxima = report.aggregate_round_maxima(samples, rank_size=8, rounds=100)
        self.assertEqual(len(maxima), 100)
        self.assertTrue(all(row["max_rank"] == 7 for row in maxima))
        self.assertEqual(maxima[0]["max_us"], 8.0)
        self.assertEqual(maxima[-1]["max_us"], 8.099)

    def test_topology_parser_records_single_host_devices_and_rank_range(self):
        topology = report.parse_topology(
            "root@141.61.52.35", "0,1,2,3,4,5,6,7", 8, 8
        )
        self.assertEqual(topology["host_count"], 1)
        self.assertEqual(topology["ranks_per_host"], 8)
        self.assertEqual(
            topology["nodes"],
            [{
                "host": "root@141.61.52.35",
                "devices": list(range(8)),
                "rank_begin": 0,
                "rank_end": 7,
            }],
        )
        with self.assertRaisesRegex(ValueError, "expected 8"):
            report.parse_topology("root@141.61.52.35", "0,1,2,3", 4, 8)

    def test_summary_does_not_average_ranks_first(self):
        per_rank = {
            0: [100.0, 1.0, 1.0, 1.0],
            1: [2.0, 90.0, 2.0, 2.0],
            2: [3.0, 3.0, 80.0, 3.0],
        }
        samples = {
            (rank, round_index): {
                "max_core": rank,
                "max_cycles": int(value * 1000),
                "elapsed_us": value,
            }
            for rank, values in per_rank.items()
            for round_index, value in enumerate(values)
        }
        maxima = report.aggregate_round_maxima(samples, rank_size=3, rounds=4)
        summary = report.summarize(maxima)
        self.assertEqual([row["max_us"] for row in maxima], [100.0, 90.0, 80.0, 3.0])
        self.assertEqual(summary["mean"], 68.25)
        self.assertEqual(summary["p50"], 85.0)
        self.assertAlmostEqual(summary["p95"], 98.5)
        self.assertNotEqual(
            summary["mean"], max(statistics.fmean(values) for values in per_rank.values())
        )

    def test_report_uses_distinct_strict_schema_and_artifacts(self):
        maxima = [
            {"round": 0, "max_rank": 1, "max_core": 7, "max_cycles": 12000, "max_us": 12.0},
            {"round": 1, "max_rank": 0, "max_core": 9, "max_cycles": 14000, "max_us": 14.0},
        ]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            old_csv = output / "device_event_latency_round_max.csv"
            old_json = output / "device_event_latency_summary.json"
            old_csv.write_text("old stream makespan\n", encoding="utf-8")
            old_json.write_text("old stream makespan\n", encoding="utf-8")
            topology = report.parse_topology("root@host", "0,1", 2, 2)
            csv_path, json_path = report.write_report(
                output, 2, 20, 2, maxima, report.summarize(maxima),
                32, 5120, 6, 20260721,
                "s1-p48-s16-db1-strict-cycle-v2-8card", 0, topology
            )
            payload = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertEqual(csv_path.name, "strict_kernel_latency_round_max.csv")
            self.assertEqual(json_path.name, "strict_kernel_latency_summary.json")
            self.assertEqual(
                payload["schema"], "tilexr_ep_urma_combine_strict_kernel_latency.v1"
            )
            self.assertEqual(
                payload["artifact_variant"], "s1-p48-s16-db1-strict-cycle-v2-8card"
            )
            self.assertEqual(
                payload["measurement"],
                "in-kernel max-core critical latency from PIPE_ALL-bracketed "
                "GetSystemCycle durations",
            )
            self.assertEqual(payload["config"]["cycle_to_us_divisor"], 1000)
            self.assertEqual(payload["config"]["bs"], 32)
            self.assertEqual(payload["config"]["hidden"], 5120)
            self.assertEqual(payload["config"]["top_k"], 6)
            self.assertEqual(payload["config"]["pack_cores"], 48)
            self.assertEqual(payload["config"]["send_cores"], 16)
            self.assertEqual(payload["config"]["qp_count"], 16)
            self.assertEqual(payload["config"]["doorbell_batch"], 1)
            self.assertFalse(payload["config"]["parallel_round_publish"])
            self.assertEqual(payload["config"]["round_publish_mode"], "sender0 serial")
            self.assertIn("sender0 serial Round Publish", payload["design"])
            self.assertEqual(payload["topology"], topology)
            self.assertEqual(payload["round_maxima"][0]["max_cycles"], 12000)
            self.assertEqual(payload["summary_cycles"]["p50"], 13000.0)
            self.assertEqual(payload["summary_cycles"]["min"], 12000)
            self.assertEqual(payload["summary_cycles"]["max"], 14000)
            self.assertEqual(old_csv.read_text(encoding="utf-8"), "old stream makespan\n")
            self.assertEqual(old_json.read_text(encoding="utf-8"), "old stream makespan\n")
            with self.assertRaisesRegex(FileExistsError, "refusing to overwrite"):
                report.write_report(
                    output, 2, 20, 2, maxima, report.summarize(maxima),
                    32, 5120, 6, 20260721,
                    "s1-p48-s16-db1-strict-cycle-v2-8card", 0, topology
                )
            self.assertEqual(list(output.glob(".*.tmp")), [])

    def test_parallel_publish_report_metadata_is_explicit(self):
        maxima = [{
            "round": 0,
            "max_rank": 0,
            "max_core": 48,
            "max_cycles": 12000,
            "max_us": 12.0,
        }]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            topology = report.parse_topology("root@host", "0", 1, 1)
            _, json_path = report.write_report(
                output, 1, 20, 1, maxima, report.summarize(maxima),
                128, 7168, 8, 20260721,
                "s1-p48-s16-db1-prp16-strict-cycle-v1", 1, topology,
            )
            payload = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertTrue(payload["config"]["parallel_round_publish"])
            self.assertEqual(
                payload["config"]["round_publish_mode"], "16 Send-core parallel"
            )
            self.assertIn("16 Send-core parallel Round Publish", payload["design"])

    def test_report_records_explicit_p44_s20_qp20_design(self):
        maxima = [{
            "round": 0,
            "max_rank": 0,
            "max_core": 44,
            "max_cycles": 101000,
            "max_us": 101.0,
        }]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            topology = report.parse_topology("root@host", "0", 1, 1)
            _, json_path = report.write_report(
                output, 1, 20, 1, maxima, report.summarize(maxima),
                128, 5120, 6, 20260729, "r141-o2-inline-strict100", 1,
                topology, pack_cores=44, send_cores=20, qp_count=20,
                doorbell_batch=1,
            )
            payload = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertEqual(payload["config"]["pack_cores"], 44)
            self.assertEqual(payload["config"]["send_cores"], 20)
            self.assertEqual(payload["config"]["qp_count"], 20)
            self.assertEqual(payload["config"]["doorbell_batch"], 1)
            self.assertIn("44-pack/20-send/QP20/DB1", payload["design"])
            self.assertIn("20 Send-core parallel Round Publish", payload["design"])


if __name__ == "__main__":
    unittest.main()
