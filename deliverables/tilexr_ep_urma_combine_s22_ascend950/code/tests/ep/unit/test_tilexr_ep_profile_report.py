import importlib.util
import math
from pathlib import Path
import tempfile
import unittest


REPORT_PATH = Path(__file__).resolve().parents[1] / "demo" / "tilexr_ep_urma_combine_profile_report.py"
SPEC = importlib.util.spec_from_file_location("tilexr_ep_urma_combine_profile_report", REPORT_PATH)
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


def stat(core, stage, count=1, sum_us=1.0, aux0=0, aux1=0, aux2=0, aux3=0):
    return {
        "core": core,
        "stage": stage,
        "count": count,
        "sum_us": sum_us,
        "max_us": sum_us,
        "start_us": 0.0,
        "end_us": sum_us,
        "aux0": aux0,
        "aux1": aux1,
        "aux2": aux2,
        "aux3": aux3,
    }


class ProfileReportTest(unittest.TestCase):
    def test_report_context_identifies_design_run_shape_and_routing(self):
        rank = {
            "max_core_count": 64,
            "core_roles": [
                {"name": "pack_rx", "begin": 0, "count": 54},
                {"name": "send", "begin": 54, "count": 10},
            ],
            "config": {
                "bs": 128,
                "h": 7168,
                "top_k": 8,
                "self_send_count": 1024,
                "route_stride": 7680,
                "route_seed": 20260721,
                "qp_count": 10,
                "doorbell_batch_size": 8,
                "rx_schedule": "token_round_robin_sticky",
            },
        }
        context = REPORT.build_report_context(
            Path("results/cacheless/run03/fine"), [rank])
        self.assertEqual(context["design"], "Cacheless DataAsFlag")
        self.assertEqual(context["run"], "run03")
        self.assertEqual(context["role_text"], "54 Pack/Receive + 10 Send")
        self.assertIn("bs=128", context["shape_text"])
        self.assertIn("seed=20260721", context["route_text"])
        self.assertEqual(context["transport_text"],
                          "10 QPs, 8-WQE doorbell, TX-ready batch 1, shared ready flag off, "
                          "TX ready in data off, "
                          "TX metadata full prefetch off, "
                          "TX early ready off, RX sticky off, "
                          "RX batched MTE2 off, RX Vector ready off, "
                          "balanced send routes off, "
                         "RX token round robin sticky, round publish serial, "
                         "start gate disabled, Q/D version unspecified")

    def test_report_context_and_signature_include_start_gate(self):
        rank = {
            "max_core_count": 64,
            "stage_count": 22,
            "core_roles": REPORT.OLD_CORE_ROLES,
            "config": {
                "qp_count": 16,
                "doorbell_batch_size": 1,
                "parallel_round_publish": True,
                "start_gate": True,
            },
        }
        legacy = {**rank, "config": {**rank["config"], "start_gate": False}}
        context = REPORT.build_report_context(None, [rank])
        self.assertIn("16 QPs", context["transport_text"])
        self.assertIn("start gate enabled", context["transport_text"])
        self.assertNotEqual(REPORT.profile_signature(rank), REPORT.profile_signature(legacy))
        self.assertIn("start_gate", REPORT.COARSE_STAGES)
        self.assertIn("start_gate", REPORT.FINE_STAGES)

        windowed = {**rank, "config": {
            **rank["config"],
            "start_gate_policy": "first_after_stream_synchronize",
            "start_gate_executed": False,
        }}
        windowed_context = REPORT.build_report_context(None, [windowed])
        self.assertIn("first after stream synchronize", windowed_context["transport_text"])
        self.assertIn("skipped in representative launch", windowed_context["transport_text"])
        self.assertNotEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(windowed))

        tx_batched = {**rank, "config": {**rank["config"], "tx_ready_batch_size": 2}}
        self.assertNotEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(tx_batched))

        explicit_shared_off = {
            **rank, "config": {**rank["config"], "tx_ready_shared_flag": False}}
        shared_on = {
            **rank, "config": {**rank["config"], "tx_ready_shared_flag": True}}
        self.assertEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(explicit_shared_off))
        self.assertNotEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(shared_on))
        self.assertIn(
            "shared ready flag on",
            REPORT.build_report_context(None, [shared_on])["transport_text"])

        explicit_in_data_off = {
            **rank, "config": {**rank["config"], "tx_ready_in_data": False}}
        in_data_on = {
            **rank, "config": {**rank["config"], "tx_ready_in_data": True}}
        self.assertEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(explicit_in_data_off))
        self.assertNotEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(in_data_on))
        in_data_context = REPORT.build_report_context(None, [in_data_on])
        self.assertTrue(in_data_context["tx_ready_in_data"])
        self.assertIn("TX ready in data on", in_data_context["transport_text"])

        explicit_meta_prefetch_off = {
            **rank, "config": {**rank["config"], "tx_meta_prefetch_full": False}}
        meta_prefetch_on = {
            **rank, "config": {**rank["config"], "tx_meta_prefetch_full": True}}
        self.assertEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(explicit_meta_prefetch_off))
        self.assertNotEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(meta_prefetch_on))
        meta_prefetch_context = REPORT.build_report_context(None, [meta_prefetch_on])
        self.assertTrue(meta_prefetch_context["tx_meta_prefetch_full"])
        self.assertIn(
            "TX metadata full prefetch on", meta_prefetch_context["transport_text"])

        explicit_balanced_off = {
            **rank, "config": {**rank["config"], "send_route_balanced": False}}
        balanced_on = {
            **rank, "config": {**rank["config"], "send_route_balanced": True}}
        self.assertEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(explicit_balanced_off))
        self.assertNotEqual(
            REPORT.profile_signature(rank), REPORT.profile_signature(balanced_on))
        balanced_context = REPORT.build_report_context(None, [balanced_on])
        self.assertTrue(balanced_context["send_route_balanced"])
        self.assertIn("balanced send routes on", balanced_context["transport_text"])

        sampled_context = REPORT.build_report_context(
            None, [windowed], sample_count=2, samples=[
                {
                    "start_gate_executed": True,
                    "start_gate_max_us": 42.0,
                    "start_gate_wqes_total": 7,
                    "start_gate_doorbells_total": 7,
                    "start_gate_quiets_total": 7,
                },
                {"start_gate_executed": False},
            ])
        self.assertEqual(sampled_context["start_gate_executed_launches"], 1)
        self.assertEqual(sampled_context["start_gate_total_launches"], 2)
        self.assertEqual(sampled_context["start_gate_executed_only_median_us"], 42.0)
        self.assertEqual(sampled_context["start_gate_executed_only_median_wqes"], 7)

    def test_representative_sample_is_nearest_to_cluster_median(self):
        samples = [
            {"launch": "launch0", "coarse_kernel_max_us": 100.0},
            {"launch": "launch1", "coarse_kernel_max_us": 140.0},
            {"launch": "launch2", "coarse_kernel_max_us": 110.0},
        ]
        launch, median_us = REPORT.representative_sample(samples)
        self.assertEqual(launch, "launch2")
        self.assertEqual(median_us, 110.0)

    def test_total_only_profile_is_validated_against_attribution_capture(self):
        def sample(launch, detail, scope, kernel_us, extra_stage=False):
            stats = [stat(0, "kernel_total", sum_us=kernel_us)]
            if extra_stage:
                stats.append(stat(0, "pack_total", sum_us=1.0))
            return {
                "rank": 0,
                "launch": launch,
                "rank_size": 1,
                "max_core_count": 1,
                "stage_count": 27,
                "kernel_timing_boundary": "pipe_all_bracketed_pre_flush",
                "profile_detail": detail,
                "profile_scope": scope,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {"route_seed": 7, "enqueue_window": 1},
                "stats": stats,
            }

        reference = {
            "launch0": [sample("launch0", 1, "coarse_attribution", 12.0, True)],
            "launch1": [sample("launch1", 1, "coarse_attribution", 14.0, True)],
        }
        total = {
            "launch0": [sample("launch0", 0, "kernel_total_only", 10.0)],
            "launch1": [sample("launch1", 0, "kernel_total_only", 11.0)],
        }
        rows = REPORT.validate_total_profile_samples(total, reference)
        self.assertEqual([row["total_kernel_max_us"] for row in rows], [10.0, 11.0])

        total["launch1"] = [sample(
            "launch1", 0, "kernel_total_only", 11.0, extra_stage=True)]
        with self.assertRaisesRegex(ValueError, "contains attribution stages"):
            REPORT.validate_total_profile_samples(total, reference)

    def test_coarse_phase_maxima_record_the_source_core(self):
        rank = {"stats": [
            stat(0, "pack_total", sum_us=2.0),
            stat(1, "pack_total", sum_us=3.0),
            stat(1, "send_total", sum_us=4.0),
        ]}
        maxima = {item["stage"]: item for item in REPORT.coarse_phase_maxima(rank)}
        self.assertEqual(maxima["pack_total"]["core"], 1)
        self.assertEqual(maxima["pack_total"]["sum_us"], 3.0)
        self.assertEqual(maxima["send_total"]["core"], 1)

    def test_legacy_pack_input_copy_name_is_canonicalized(self):
        self.assertEqual(REPORT.canonical_stage_name("pack_input_copy"), "pack_input_wait")
        self.assertEqual(REPORT.canonical_stage_name("pack_quantize"), "pack_quantize")

    def test_legacy_role_fallback(self):
        trace = {"max_core_count": 64, "config": {}}
        roles = REPORT.normalize_core_roles(trace, [
            stat(0, "pack_total"),
            stat(56, "send_total"),
        ])
        self.assertEqual(roles, REPORT.OLD_CORE_ROLES)

    def test_explicit_dedicated_self_roles_remain_supported(self):
        trace = {
            "max_core_count": 64,
            "config": {"core_roles": [
                {"name": "pack_rx", "begin": 0, "count": 51},
                {"name": "send", "begin": 51, "count": 12},
                {"name": "self_copy", "begin": 63, "count": 1},
            ]},
        }
        self.assertEqual(len(REPORT.normalize_core_roles(trace, [])), 3)

    def test_merged_send_self_routes_have_one_scan_per_completed_route(self):
        roles = [
            {"name": "pack_rx", "begin": 0, "count": 54},
            {"name": "send", "begin": 54, "count": 10},
        ]
        rank = {
            "rank": 0,
            "host": "node0",
            "max_core_count": 64,
            "stage_count": 21,
            "cycle_to_us_divisor": 1000,
            "host_round_us": 10.0,
            "unprofiled_mean_us": 9.0,
            "config": {},
            "core_roles": roles,
            "stats": [
                stat(54, "kernel_total", sum_us=8.0),
                stat(54, "send_total", sum_us=4.0),
                stat(54, "tx_meta_scan", count=100, aux1=8, aux2=92),
                stat(54, "tx_ready_poll", count=100, aux1=100),
                stat(54, "self_copy", count=8, sum_us=2.0, aux0=4096),
                stat(54, "udma_post", count=92, sum_us=1.0, aux0=47104),
                stat(54, "dcci_total", count=10, sum_us=10.0,
                     aux0=1000, aux1=2000, aux2=3000, aux3=4000),
            ],
        }
        summary = REPORT.build_summary([rank])[0]
        self.assertFalse(summary["tx_ready_shared_flag"])
        self.assertFalse(summary["tx_ready_in_data"])
        self.assertFalse(summary["tx_meta_prefetch_full"])
        self.assertFalse(summary["send_route_balanced"])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            REPORT.write_csv_files(root, [rank], [summary])
            rank_header = (root / "combine_rank_summary.csv").read_text(
                encoding="utf-8").splitlines()[0]
            self.assertIn("tx_ready_in_data", rank_header)
            self.assertIn("tx_meta_prefetch_full", rank_header)
            self.assertIn("send_route_balanced", rank_header)
        self.assertEqual(summary["fine_baseline_us"], 9.0)
        self.assertEqual(summary["completed_routes"], 100)
        self.assertEqual(summary["self_copies"], 8)
        self.assertTrue(math.isclose(summary["meta_scan_amplification"], 1.0))
        self.assertTrue(math.isclose(summary["send_meta_scan_amplification"], 1.0))
        self.assertTrue(math.isclose(summary["self_copy_active_max_us"], 2.0))
        self.assertEqual(summary["self_copy_critical_us"], 0.0)
        self.assertTrue(math.isclose(summary["dcci_critical_us"], 10.0))
        self.assertTrue(math.isclose(summary["dcci_tx_data_us"], 1.0))
        self.assertTrue(math.isclose(summary["dcci_rx_flag_us"], 2.0))
        self.assertTrue(math.isclose(summary["dcci_rx_data_us"], 3.0))
        self.assertTrue(math.isclose(summary["dcci_control_other_us"], 4.0))
        self.assertTrue(math.isclose(summary["dcci_all_kernel_fraction"], 10.0 / 8.0))
        self.assertEqual(summary["fine_critical_core"], 54)
        self.assertTrue(math.isclose(summary["dcci_on_critical_core_us"], 10.0))
        self.assertTrue(math.isclose(summary["dcci_critical_core_fraction"], 10.0 / 8.0))

    def test_coarse_send_core_ols_slope_and_spread_reach_all_report_outputs(self):
        roles = [
            {"name": "pack_rx", "begin": 0, "count": 2},
            {"name": "send", "begin": 2, "count": 4},
        ]

        def rank_data(host_round_us, unprofiled_mean_us, send_values):
            return {
                "rank": 0,
                "host": "node0",
                "max_core_count": 6,
                "stage_count": 21,
                "cycle_to_us_divisor": 1000,
                "host_round_us": host_round_us,
                "unprofiled_mean_us": unprofiled_mean_us,
                "config": {},
                "core_roles": roles,
                "stats": [stat(0, "kernel_total", sum_us=max(send_values))] + [
                    stat(core, "send_total", sum_us=value)
                    for core, value in zip(range(2, 6), send_values)
                ],
            }

        fine = rank_data(10.0, 9.0, [100.0, 100.0, 100.0, 100.0])
        coarse = rank_data(20.0, 19.0, [10.0, 12.0, 14.0, 16.0])
        summary = REPORT.build_summary([fine], [coarse])[0]
        self.assertTrue(math.isclose(summary["send_core_ols_slope_us_per_core"], 2.0))
        self.assertTrue(math.isclose(summary["send_core_spread_us"], 6.0))
        self.assertTrue(math.isclose(summary["send_core_spread_pct"], 6.0 / 13.0 * 100.0))

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            REPORT.write_csv_files(root, [fine], [summary])
            REPORT.write_analysis(root, [fine], [summary])
            rank_header = (root / "combine_rank_summary.csv").read_text(
                encoding="utf-8").splitlines()[0]
            self.assertIn("send_core_ols_slope_us_per_core", rank_header)
            self.assertIn("send_core_spread_us", rank_header)
            self.assertIn("send_core_spread_pct", rank_header)
            analysis = (root / "combine_analysis.md").read_text(encoding="utf-8")
            self.assertIn("coarse Send-core OLS slope: 2.000 us/core", analysis)
            self.assertIn("coarse Send-core spread: 6.000 us", analysis)
            self.assertIn("coarse Send-core spread / mean: 46.154%", analysis)

        rendered = REPORT.render_html([fine], [summary])
        self.assertIn("<th>Send OLS slope us/core</th>", rendered)
        self.assertIn("<th>Send spread us</th>", rendered)
        self.assertIn("<th>Send spread % mean</th>", rendered)

    def test_profile_signature_includes_route_seed(self):
        rank = {
            "max_core_count": 64,
            "stage_count": 21,
            "core_roles": REPORT.OLD_CORE_ROLES,
            "config": {
                "bs": 128,
                "h": 7168,
                "top_k": 8,
                "self_send_count": 1024,
                "route_stride": 7680,
                "route_seed": 1,
            },
        }
        other = {**rank, "config": {**rank["config"], "route_seed": 2}}
        self.assertNotEqual(REPORT.profile_signature(rank), REPORT.profile_signature(other))

    def test_profile_signature_includes_parallel_round_publish(self):
        rank = {
            "max_core_count": 64,
            "stage_count": 21,
            "core_roles": REPORT.OLD_CORE_ROLES,
            "config": {"parallel_round_publish": False},
        }
        other = {**rank, "config": {"parallel_round_publish": True}}
        self.assertNotEqual(REPORT.profile_signature(rank), REPORT.profile_signature(other))

    def test_profile_signature_and_validation_include_enqueue_window(self):
        def sample(launch, enqueue_window):
            return {
                "rank": 0,
                "launch": launch,
                "rank_size": 1,
                "max_core_count": 1,
                "stage_count": 21,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {"enqueue_window": enqueue_window},
                "stats": [stat(0, "kernel_total", sum_us=10.0)],
            }

        legacy = sample("launch0", 1)
        windowed = sample("launch0", 8)
        self.assertNotEqual(REPORT.profile_signature(legacy), REPORT.profile_signature(windowed))
        fine_groups = {"launch0": [legacy], "launch1": [sample("launch1", 8)]}
        coarse_groups = {"launch0": [legacy], "launch1": [sample("launch1", 8)]}
        with self.assertRaisesRegex(ValueError, "enqueue_window mismatch"):
            REPORT.validate_profile_samples(fine_groups, coarse_groups)

    def test_profile_validation_allows_gate_execution_to_vary_by_launch_only(self):
        def sample(launch, executed):
            return {
                "rank": 0,
                "launch": launch,
                "rank_size": 1,
                "max_core_count": 1,
                "stage_count": 22,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {
                    "start_gate": True,
                    "start_gate_policy": "first_after_stream_synchronize",
                    "start_gate_executed": executed,
                    "enqueue_window": 100,
                },
                "stats": [stat(0, "kernel_total", sum_us=10.0)],
            }

        fine = {
            "launch0": [sample("launch0", True)],
            "launch1": [sample("launch1", False)],
        }
        coarse = {
            "launch0": [sample("launch0", True)],
            "launch1": [sample("launch1", False)],
        }
        samples = REPORT.validate_profile_samples(fine, coarse)
        self.assertEqual(
            [row["start_gate_executed"] for row in samples], [True, False])
        self.assertEqual(
            [row["tx_ready_shared_flag"] for row in samples], [False, False])
        self.assertEqual(
            [row["tx_ready_in_data"] for row in samples], [False, False])
        self.assertEqual(
            [row["tx_meta_prefetch_full"] for row in samples], [False, False])
        self.assertEqual(
            [row["send_route_balanced"] for row in samples], [False, False])

        coarse["launch1"] = [sample("launch1", True)]
        with self.assertRaisesRegex(ValueError, "start_gate_executed mismatch"):
            REPORT.validate_profile_samples(fine, coarse)

    def test_profile_validation_rejects_cross_rank_and_cross_launch_config_mismatch(self):
        def sample(rank, rank_size, launch, route_seed):
            return {
                "rank": rank,
                "launch": launch,
                "rank_size": rank_size,
                "max_core_count": 1,
                "stage_count": 21,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {"route_seed": route_seed, "enqueue_window": 1},
                "stats": [stat(0, "kernel_total", sum_us=10.0)],
            }

        rank_mismatch = {
            "launch0": [sample(0, 2, "launch0", 1), sample(1, 2, "launch0", 2)],
        }
        launch_mismatch = {
            "launch0": [sample(0, 1, "launch0", 1)],
            "launch1": [sample(0, 1, "launch1", 2)],
        }
        for groups in (rank_mismatch, launch_mismatch):
            with self.subTest(launches=tuple(groups)):
                with self.assertRaisesRegex(ValueError, "profile configuration mismatch"):
                    REPORT.validate_profile_samples(groups, groups)

    def test_profile_validation_rejects_shared_ready_flag_mismatch(self):
        def sample(mode_shared_flag):
            return {
                "rank": 0,
                "launch": "launch0",
                "rank_size": 1,
                "max_core_count": 1,
                "stage_count": 21,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {"tx_ready_shared_flag": mode_shared_flag},
                "stats": [stat(0, "kernel_total", sum_us=10.0)],
            }

        legacy = sample(False)
        legacy["config"].pop("tx_ready_shared_flag")
        self.assertEqual(
            REPORT.profile_signature(legacy), REPORT.profile_signature(sample(False)))
        with self.assertRaisesRegex(ValueError, "fine/coarse profile configuration mismatch"):
            REPORT.validate_profile_samples(
                {"launch0": [sample(True)]}, {"launch0": [sample(False)]})

    def test_profile_validation_rejects_balanced_send_route_mismatch(self):
        def sample(balanced_routes):
            return {
                "rank": 0,
                "launch": "launch0",
                "rank_size": 1,
                "max_core_count": 1,
                "stage_count": 21,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {"send_route_balanced": balanced_routes},
                "stats": [stat(0, "kernel_total", sum_us=10.0)],
            }

        legacy = sample(False)
        legacy["config"].pop("send_route_balanced")
        self.assertEqual(
            REPORT.profile_signature(legacy), REPORT.profile_signature(sample(False)))
        with self.assertRaisesRegex(ValueError, "fine/coarse profile configuration mismatch"):
            REPORT.validate_profile_samples(
                {"launch0": [sample(True)]}, {"launch0": [sample(False)]})

    def test_profile_validation_rejects_tx_ready_in_data_mismatch(self):
        def sample(in_data):
            return {
                "rank": 0,
                "launch": "launch0",
                "rank_size": 1,
                "max_core_count": 1,
                "stage_count": 22,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {"tx_ready_in_data": in_data},
                "stats": [stat(0, "kernel_total", sum_us=10.0)],
            }

        legacy = sample(False)
        legacy["config"].pop("tx_ready_in_data")
        self.assertEqual(
            REPORT.profile_signature(legacy), REPORT.profile_signature(sample(False)))
        with self.assertRaisesRegex(ValueError, "fine/coarse profile configuration mismatch"):
            REPORT.validate_profile_samples(
                {"launch0": [sample(True)]}, {"launch0": [sample(False)]})

    def test_profile_validation_rejects_tx_meta_prefetch_full_mismatch(self):
        def sample(prefetch_full):
            return {
                "rank": 0,
                "launch": "launch0",
                "rank_size": 1,
                "max_core_count": 1,
                "stage_count": 22,
                "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
                "config": {"tx_meta_prefetch_full": prefetch_full},
                "stats": [stat(0, "kernel_total", sum_us=10.0)],
            }

        legacy = sample(False)
        legacy["config"].pop("tx_meta_prefetch_full")
        self.assertEqual(
            REPORT.profile_signature(legacy), REPORT.profile_signature(sample(False)))
        with self.assertRaisesRegex(ValueError, "fine/coarse profile configuration mismatch"):
            REPORT.validate_profile_samples(
                {"launch0": [sample(True)]}, {"launch0": [sample(False)]})

    def test_windowed_host_metrics_are_labeled_as_throughput_proxies(self):
        rank = {
            "rank": 0,
            "host": "node0",
            "max_core_count": 1,
            "stage_count": 21,
            "cycle_to_us_divisor": 1000,
            "host_round_us": 10.0,
            "unprofiled_mean_us": 9.0,
            "config": {"enqueue_window": 8},
            "core_roles": [{"name": "pack_rx", "begin": 0, "count": 1}],
            "stats": [stat(0, "kernel_total", sum_us=8.0)],
        }
        summary = REPORT.build_summary([rank])
        context = REPORT.build_report_context(None, [rank], kernel_median_us=8.0)
        rendered = REPORT.render_html([rank], summary, context)
        warning = (
            "host_round_us and unprofiled_mean_us are window-amortized throughput proxies, "
            "not single-request latency."
        )
        self.assertIn(warning, rendered)
        self.assertIn("median cluster kernel 8.00 us", rendered)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            REPORT.write_analysis(root, [rank], summary, kernel_median_us=8.0)
            analysis = (root / "combine_analysis.md").read_text(encoding="utf-8")
        self.assertIn(warning, analysis)
        self.assertIn("Kernel timing boundary: legacy_unbracketed", analysis)
        self.assertIn("Headline max-core -> max-rank -> launch median: 8.000 us", analysis)

    def test_parallel_round_publish_aux_is_summed_but_tail_uses_join_span(self):
        roles = [
            {"name": "pack_rx", "begin": 0, "count": 48},
            {"name": "send", "begin": 48, "count": 16},
        ]
        rank = {
            "rank": 0,
            "host": "node0",
            "max_core_count": 64,
            "stage_count": 21,
            "cycle_to_us_divisor": 1000,
            "host_round_us": 100.0,
            "unprofiled_mean_us": 90.0,
            "config": {"parallel_round_publish": True},
            "core_roles": roles,
            "stats": [
                stat(48, "kernel_total", sum_us=80.0),
                stat(48, "round_publish", sum_us=50.0,
                     aux0=3, aux1=192, aux2=3, aux3=3),
                stat(49, "round_publish", sum_us=10.0,
                     aux0=4, aux1=256, aux2=4, aux3=4),
                stat(48, "udma_post", count=9, sum_us=2.0, aux0=4096, aux1=3),
            ],
        }
        summary = REPORT.build_summary([rank])[0]
        self.assertEqual(summary["sender0_tail_us"], 50.0)
        self.assertEqual(summary["round_publish_wqes"], 7)
        self.assertEqual(summary["round_publish_bytes"], 448)
        self.assertEqual(summary["round_publish_doorbells"], 7)
        self.assertEqual(summary["round_publish_quiets"], 7)
        self.assertEqual(summary["all_udma_wqes"], 16)
        self.assertEqual(summary["all_doorbell_commits"], 10)

    def test_coarse_timeline_splits_receive_with_fine_stage_ratio(self):
        roles = [
            {"name": "pack_rx", "begin": 0, "count": 1},
            {"name": "send", "begin": 1, "count": 1},
        ]
        rank = {
            "rank": 0,
            "host": "node0",
            "max_core_count": 2,
            "stage_count": 21,
            "cycle_to_us_divisor": 1000,
            "host_round_us": 10.0,
            "unprofiled_mean_us": 9.0,
            "config": {
                "qp_count": 1,
                "doorbell_batch_size": 1,
                "rx_schedule": "sequential",
            },
            "core_roles": roles,
            "stats": [
                stat(0, "rx_flag_poll_wait", count=4, sum_us=3.0,
                     aux0=9, aux1=40, aux2=5),
                stat(0, "rx_unpack_wait", count=4, sum_us=0.5),
                stat(0, "rx_unpack_dequant_clear", count=4, sum_us=2.0),
                stat(0, "rx_output", count=4, sum_us=0.1),
                stat(0, "dcci_total", count=4, sum_us=0.4,
                     aux0=100, aux1=100, aux2=100, aux3=100),
            ],
            "coarse_stats": [
                stat(0, "kernel_total", sum_us=8.0),
                stat(0, "receive_total", count=4, sum_us=5.0),
            ],
        }
        html = REPORT.render_html([rank], REPORT.build_summary([rank]))
        summary = REPORT.build_summary([rank])[0]
        self.assertEqual(summary["rx_poll_passes"], 9)
        self.assertEqual(summary["rx_route_checks"], 40)
        self.assertEqual(summary["rx_ready_misses"], 5)
        self.assertTrue(math.isclose(summary["rx_ready_miss_ratio"], 0.125))
        self.assertIn("TileXR EP URMA Combine &middot; DataAsFlag", html)
        self.assertIn(
            "1 Pack/Receive + 1 Send &middot; 1 QPs, 1-WQE doorbell, "
            "TX-ready batch 1, shared ready flag off, TX ready in data off, "
            "TX metadata full prefetch off, "
            "TX early ready off, RX sticky off, RX batched MTE2 off, "
            "RX Vector ready off, "
            "balanced send routes off, RX sequential, "
            "round publish serial",
            html,
        )
        self.assertIn("<th>Shared ready flag</th>", html)
        self.assertIn("<th>TX ready in data</th>", html)
        self.assertIn("<th>TX metadata full prefetch</th>", html)
        self.assertIn("<th>Balanced send routes</th>", html)
        self.assertIn("Coarse Stage Maxima Across AIVs", html)
        self.assertIn("Maxima can come from different cores", html)
        self.assertIn("coarse_phase_maxima", html)
        self.assertIn("phase-max", html)
        self.assertIn("Kernel critical (coarse)", html)
        self.assertIn("Fine Route Transport AIV Diagnostics", html)
        self.assertIn("WQE / DB", html)
        self.assertIn("RX bypasses", html)
        self.assertIn("RX OOO completions (fine)", html)
        self.assertIn("RX flag checks (fine)", html)
        self.assertIn("RX flag miss (fine)", html)
        self.assertIn("receiveParts(fineMap,core)", html)
        self.assertIn("class=\"bar receive-total", html)
        self.assertIn("RX flag wait", html)
        self.assertIn("unpack exposed wait", html)
        self.assertIn("dequant+clear", html)
        self.assertIn("includes next unpack submission, excludes async in-flight time", html)
        self.assertIn("Fine profiling preserves the quant/dequant double-buffer pipeline", html)
        self.assertIn("Explicit DCCI Breakdown", html)
        self.assertIn("renderDcci(rank,map)", html)
        self.assertNotIn("dcci_total", REPORT.COARSE_STAGES)
        self.assertIn("DCCI / kernel (fine)", html)
        self.assertIn("Explicit DCCI / critical core", html)
        self.assertIn("Explicit DCCI / Pack+Rx core-time", html)
        self.assertIn("SQ/CQ cache maintenance inside the UDMA helper", html)
        self.assertIn("composition, not chronological ordering", html)

    def test_coarse_and_fine_active_stage_sets_use_their_own_sources(self):
        roles = [
            {"name": "pack_rx", "begin": 0, "count": 1},
            {"name": "send", "begin": 1, "count": 1},
        ]
        rank = {
            "rank": 0,
            "host": "node0",
            "max_core_count": 2,
            "stage_count": 21,
            "cycle_to_us_divisor": 1000,
            "host_round_us": 10.0,
            "unprofiled_mean_us": 9.0,
            "config": {},
            "core_roles": roles,
            "stats": [stat(0, "pack_total", sum_us=3.0), stat(1, "tx_ready_poll")],
            "coarse_stats": [stat(0, "receive_total", sum_us=4.0)],
        }
        html = REPORT.render_html([rank], REPORT.build_summary([rank]))
        self.assertIn('"coarseStages":["receive_total"]', html)
        self.assertIn('"fineStages":["tx_ready_poll"]', html)

    def test_start_gate_is_collapsed_from_steady_state_views_by_default(self):
        roles = [
            {"name": "pack_rx", "begin": 0, "count": 1},
            {"name": "send", "begin": 1, "count": 1},
        ]
        rank = {
            "rank": 0,
            "host": "node0",
            "max_core_count": 2,
            "stage_count": 21,
            "cycle_to_us_divisor": 1000,
            "host_round_us": 10.0,
            "unprofiled_mean_us": 9.0,
            "config": {"start_gate": True},
            "core_roles": roles,
            "stats": [
                stat(0, "kernel_total", sum_us=8.0),
                stat(0, "tx_meta_scan", sum_us=2.0),
                stat(0, "start_gate", sum_us=40.0),
            ],
            "coarse_stats": [
                stat(0, "kernel_total", sum_us=8.0),
                stat(0, "pack_total", sum_us=3.0),
                stat(0, "start_gate", sum_us=40.0),
            ],
        }
        summary = REPORT.build_summary([rank])
        steady = REPORT.render_html([rank], summary)
        full = REPORT.render_html([rank], summary, show_start_gate=True)
        self.assertIn('"coarseStages":["pack_total"]', steady)
        self.assertIn('"fineStages":["tx_meta_scan"]', steady)
        self.assertIn("start gate collapsed from steady-state views", steady)
        self.assertIn('"coarseStages":["pack_total","start_gate"]', full)
        self.assertIn('"fineStages":["tx_meta_scan","start_gate"]', full)
        self.assertIn("start gate shown proportionally", full)


if __name__ == "__main__":
    unittest.main()
