#!/usr/bin/env python3

"""Compare any number of repeated TileXR 64-rank Combine profile variants."""

import argparse
import csv
import hashlib
import html
import json
import re
import statistics
from pathlib import Path


OP_NAME = "TileXRMoeEpCombineUrma"
COARSE_STAGES = (
    "kernel_total", "pack_total", "receive_total", "send_total", "round_publish",
)
OPTIONAL_COARSE_STAGES = ("start_gate", "global_round_wait")
FINE_LATENCY_STAGES = (
    "tx_meta_scan", "pack_tx_data_submit", "pack_first_tx_ready",
    "pack_mte3_exposed_wait", "rx_ready_mte2_wait", "rx_ready_vector",
)
WORKLOAD_KEYS = ("bs", "h", "top_k", "self_send_count", "route_stride", "route_seed")
DESIGN_KEYS = (
    "qp_count", "doorbell_batch_size", "tx_ready_batch_size", "tx_ready_shared_flag",
    "tx_ready_in_data", "tx_meta_prefetch_full", "tx_ready_early_publish",
    "rx_ready_sticky_mask", "rx_ready_batch_mte2", "rx_ready_batch_vector",
    "send_route_balanced",
    "parallel_round_publish", "start_gate", "start_gate_policy",
    "rx_schedule", "core_roles", "enqueue_window", "qdc_version",
)
RUN_RE = re.compile(r"run\d+", re.IGNORECASE)
RANK_RE = re.compile(r"rank(\d+)", re.IGNORECASE)
LAUNCH_RE = re.compile(r"launch(\d+)", re.IGNORECASE)


def config_value(config, key):
    if key in {
            "tx_ready_shared_flag", "tx_ready_in_data", "tx_meta_prefetch_full",
            "tx_ready_early_publish", "rx_ready_sticky_mask",
            "rx_ready_batch_mte2", "rx_ready_batch_vector",
            "send_route_balanced"}:
        return bool(config.get(key, False))
    if key == "tx_ready_batch_size":
        try:
            value = int(config.get(key, 1))
        except (TypeError, ValueError) as error:
            raise ValueError(
                f"invalid tx_ready_batch_size {config.get(key)!r}") from error
        if value not in (1, 2, 4):
            raise ValueError(
                f"invalid tx_ready_batch_size {value}; expected 1, 2, or 4")
        return value
    if key == "qdc_version":
        if key not in config:
            return -1
        try:
            value = int(config[key])
        except (TypeError, ValueError) as error:
            raise ValueError(f"invalid qdc_version {config.get(key)!r}") from error
        if value not in (0, 1, 2, 3):
            raise ValueError(f"invalid qdc_version {value}; expected 0, 1, 2, or 3")
        return value
    if key == "start_gate_policy":
        start_gate = bool(config.get("start_gate", False))
        fallback = "every_launch" if start_gate else "disabled"
        value = str(config.get(key, fallback))
        valid = {"disabled", "every_launch", "first_after_stream_synchronize"}
        if value not in valid:
            raise ValueError(f"invalid start_gate_policy {value!r}")
        if start_gate != (value != "disabled"):
            raise ValueError(
                f"start_gate={start_gate} conflicts with start_gate_policy={value!r}")
        return value
    if key != "enqueue_window":
        return config.get(key)
    try:
        value = int(config.get(key, 1))
    except (TypeError, ValueError) as error:
        raise ValueError(f"invalid enqueue_window {config.get(key)!r}") from error
    if value <= 0:
        raise ValueError(f"invalid enqueue_window {value}; expected a positive integer")
    return value


def host_timing_interpretation(enqueue_window):
    if enqueue_window <= 1:
        return ""
    return (
        f"enqueue_window={enqueue_window}: host_round_us and unprofiled_mean_us are "
        "window-amortized throughput proxies, not single-request latency."
    )


def stat_sum_us(stat, divisor):
    if "sum_us" in stat:
        return float(stat["sum_us"])
    return float(stat.get("raw_cycles", 0)) / divisor


def normalized_config(trace):
    config = trace.get("config", {})
    design = {key: config_value(config, key) for key in DESIGN_KEYS}
    design["parallel_round_publish"] = bool(config.get("parallel_round_publish", False))
    design["start_gate"] = bool(config.get("start_gate", False))
    return {
        "max_core_count": int(trace.get("max_core_count", 0)),
        "stage_count": int(trace.get("stage_count", 0)),
        **{key: config_value(config, key) for key in WORKLOAD_KEYS},
        **design,
    }


def workload_signature(trace):
    config = trace.get("config", {})
    return (
        int(trace.get("rank_size", 0)),
        *(config_value(config, key) for key in WORKLOAD_KEYS),
    )


def config_signature(trace):
    return json.dumps(normalized_config(trace), sort_keys=True, separators=(",", ":"))


def signature_id(signature):
    stable_config = json.loads(signature)
    if stable_config.get("tx_ready_shared_flag") is False:
        stable_config.pop("tx_ready_shared_flag")
    if stable_config.get("tx_ready_in_data") is False:
        stable_config.pop("tx_ready_in_data")
    if stable_config.get("tx_meta_prefetch_full") is False:
        stable_config.pop("tx_meta_prefetch_full")
    for key in ("tx_ready_early_publish", "rx_ready_sticky_mask",
                "rx_ready_batch_mte2", "rx_ready_batch_vector"):
        if stable_config.get(key) is False:
            stable_config.pop(key)
    stable_signature = json.dumps(stable_config, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(stable_signature.encode("utf-8")).hexdigest()[:12]


def trace_identity(path, root):
    path.relative_to(root)
    parts = path.parts
    run = next((part.lower() for part in reversed(parts) if RUN_RE.fullmatch(part)), None)
    mode = next((part.lower() for part in reversed(parts)
                 if part.lower() in {"coarse", "fine"}), None)
    rank_match = next((RANK_RE.fullmatch(part) for part in reversed(parts)
                       if RANK_RE.fullmatch(part)), None)
    launch_match = next((LAUNCH_RE.fullmatch(part) for part in reversed(parts)
                         if LAUNCH_RE.fullmatch(part)), None)
    if run is None or mode is None or rank_match is None or launch_match is None:
        return None
    return run, mode, f"launch{int(launch_match.group(1))}", int(rank_match.group(1))


def read_trace(path):
    trace = json.loads(path.read_text(encoding="utf-8"))
    if trace.get("op_name") != OP_NAME:
        return None
    divisor = int(trace.get("cycle_to_us_divisor", 0))
    if divisor <= 0:
        raise ValueError(f"invalid cycle_to_us_divisor in {path}")
    trace["_path"] = str(path)
    trace["_divisor"] = divisor
    return trace


def discover_traces(root):
    found = {}
    for path in sorted(root.rglob("trace.json")):
        identity = trace_identity(path, root)
        if identity is None:
            continue
        trace = read_trace(path)
        if trace is None:
            continue
        if identity in found:
            raise ValueError(f"duplicate trace for {identity}: {found[identity]['_path']} and {path}")
        found[identity] = trace
    if not found:
        raise ValueError(f"no coarse/fine {OP_NAME} traces found under {root}")
    return found


def stage_maximum(trace, stage, required=True):
    candidates = [
        (stat_sum_us(stat, trace["_divisor"]), int(stat.get("core", -1)))
        for stat in trace.get("stats", [])
        if stat.get("stage") == stage and int(stat.get("count", 0)) > 0
    ]
    if not candidates:
        if not required:
            return {"us": 0.0, "core": -1}
        raise ValueError(f"missing {stage} in {trace['_path']}")
    value, core = max(candidates)
    return {"us": value, "core": core}


def stage_counter(trace, stage, field):
    return sum(
        int(stat.get(field, 0))
        for stat in trace.get("stats", [])
        if stat.get("stage") == stage and int(stat.get("count", 0)) > 0
    )


def max_rank_value(records, key):
    return max(records, key=lambda record: record[key])


def describe_design(trace):
    config = trace.get("config", {})
    roles = config.get("core_roles") or []
    role_counts = {str(role.get("name")): int(role.get("count", 0)) for role in roles}
    pack = role_counts.get("pack_rx", 0)
    send = role_counts.get("send", 0)
    self_copy = role_counts.get("self_copy", 0)
    qp_count = int(config.get("qp_count") or send)
    doorbell = int(config.get("doorbell_batch_size") or 1)
    tx_ready_batch = int(config.get("tx_ready_batch_size") or 1)
    tx_ready_shared_flag = config_value(config, "tx_ready_shared_flag")
    tx_ready_in_data = config_value(config, "tx_ready_in_data")
    tx_meta_prefetch_full = config_value(config, "tx_meta_prefetch_full")
    tx_ready_early_publish = config_value(config, "tx_ready_early_publish")
    rx_ready_sticky_mask = config_value(config, "rx_ready_sticky_mask")
    rx_ready_batch_mte2 = config_value(config, "rx_ready_batch_mte2")
    rx_ready_batch_vector = config_value(config, "rx_ready_batch_vector")
    send_route_balanced = config_value(config, "send_route_balanced")
    parallel_round_publish = bool(config.get("parallel_round_publish", False))
    start_gate = bool(config.get("start_gate", False))
    start_gate_policy = config_value(config, "start_gate_policy")
    enqueue_window = config_value(config, "enqueue_window")
    rx_schedule = str(config.get("rx_schedule") or "sequential")
    qdc_version = config_value(config, "qdc_version")
    core_layout = f"{pack}/{send}" + (f"/{self_copy}" if self_copy else "")
    return {
        "pack_cores": pack,
        "send_cores": send,
        "self_copy_cores": self_copy,
        "core_layout": core_layout,
        "qp_count": qp_count,
        "doorbell_batch_size": doorbell,
        "tx_ready_batch_size": tx_ready_batch,
        "tx_ready_shared_flag": tx_ready_shared_flag,
        "tx_ready_in_data": tx_ready_in_data,
        "tx_meta_prefetch_full": tx_meta_prefetch_full,
        "tx_ready_early_publish": tx_ready_early_publish,
        "rx_ready_sticky_mask": rx_ready_sticky_mask,
        "rx_ready_batch_mte2": rx_ready_batch_mte2,
        "rx_ready_batch_vector": rx_ready_batch_vector,
        "send_route_balanced": send_route_balanced,
        "parallel_round_publish": parallel_round_publish,
        "start_gate": start_gate,
        "start_gate_policy": start_gate_policy,
        "enqueue_window": enqueue_window,
        "rx_schedule": rx_schedule,
        "qdc_version": qdc_version,
    }


def validate_sample(label, run, launch, traces, expected_rank_size):
    mode_ranks = {
        mode: {
            rank for trace_run, trace_mode, trace_launch, rank in traces
            if trace_run == run and trace_mode == mode and trace_launch == launch
        }
        for mode in ("coarse", "fine")
    }
    expected_ranks = set(range(expected_rank_size))
    for mode, ranks in mode_ranks.items():
        if ranks != expected_ranks:
            missing = sorted(expected_ranks - ranks)
            extra = sorted(ranks - expected_ranks)
            raise ValueError(
                f"{label} {run}/{launch} {mode}: expected ranks 0..{expected_rank_size - 1}; "
                f"missing={missing}, extra={extra}")

    signatures = set()
    workload_signatures = set()
    start_gate_executions = set()
    for rank in sorted(expected_ranks):
        coarse = traces[(run, "coarse", launch, rank)]
        fine = traces[(run, "fine", launch, rank)]
        for mode, trace in (("coarse", coarse), ("fine", fine)):
            rank_size = int(trace.get("rank_size", 0))
            if rank_size != expected_rank_size:
                raise ValueError(
                    f"{label} {run}/{launch} {mode} rank{rank}: trace rank_size={rank_size}, "
                    f"expected {expected_rank_size}")
            bad_stat_ranks = {
                int(stat["rank"]) for stat in trace.get("stats", [])
                if "rank" in stat and int(stat["rank"]) != rank
            }
            if bad_stat_ranks:
                raise ValueError(
                    f"{label} {run}/{launch} {mode} rank{rank}: stats contain ranks {sorted(bad_stat_ranks)}")
            signatures.add(config_signature(trace))
            workload_signatures.add(workload_signature(trace))
            start_gate_executions.add(bool(trace.get("config", {}).get(
                "start_gate_executed", trace.get("config", {}).get("start_gate", False))))
        if config_signature(coarse) != config_signature(fine):
            raise ValueError(f"{label} {run}/{launch} rank{rank}: coarse/fine config mismatch")
    if len(signatures) != 1:
        raise ValueError(
            f"{label} {run}/{launch}: config signature differs across ranks or profile modes")
    if len(workload_signatures) != 1:
        raise ValueError(
            f"{label} {run}/{launch}: workload signature differs across ranks or profile modes")
    if len(start_gate_executions) != 1:
        raise ValueError(
            f"{label} {run}/{launch}: start_gate_executed differs across ranks or profile modes")
    return signatures.pop(), workload_signatures.pop()


def summarize_sample(label, run, launch, traces, expected_rank_size):
    signature, workload = validate_sample(label, run, launch, traces, expected_rank_size)
    sample_config = traces[(run, "coarse", launch, 0)].get("config", {})
    coarse_records = []
    fine_records = []
    for rank in range(expected_rank_size):
        coarse = traces[(run, "coarse", launch, rank)]
        fine = traces[(run, "fine", launch, rank)]
        maxima = {stage: stage_maximum(coarse, stage) for stage in COARSE_STAGES}
        maxima.update({
            stage: stage_maximum(coarse, stage, required=False)
            for stage in OPTIONAL_COARSE_STAGES
        })
        fine_maxima = {
            stage: stage_maximum(fine, stage, required=(stage == "tx_meta_scan"))
            for stage in FINE_LATENCY_STAGES
        }
        coarse_records.append({"rank": rank, **{
            f"{stage}_us": maximum["us"] for stage, maximum in maxima.items()
        }, **{
            f"{stage}_core": maximum["core"] for stage, maximum in maxima.items()
        }})
        round_publish_wqes = stage_counter(fine, "round_publish", "aux0")
        round_publish_doorbells = stage_counter(fine, "round_publish", "aux2")
        round_publish_quiets = stage_counter(fine, "round_publish", "aux3")
        start_gate_wqes = stage_counter(fine, "start_gate", "aux0")
        start_gate_doorbells = stage_counter(fine, "start_gate", "aux2")
        start_gate_quiets = stage_counter(fine, "start_gate", "aux3")
        fine_records.append({
            "rank": rank,
            **{f"{stage}_us": maximum["us"] for stage, maximum in fine_maxima.items()},
            **{f"{stage}_core": maximum["core"] for stage, maximum in fine_maxima.items()},
            "udma_wqes": stage_counter(fine, "udma_post", "count"),
            "doorbell_commits": stage_counter(fine, "udma_post", "aux1"),
            "active_sqs": stage_counter(fine, "udma_post", "aux3"),
            "round_publish_wqes": round_publish_wqes,
            "round_publish_bytes": stage_counter(fine, "round_publish", "aux1"),
            "round_publish_doorbells": round_publish_doorbells or round_publish_wqes,
            "round_publish_quiets": round_publish_quiets or round_publish_wqes,
            "start_gate_wqes": start_gate_wqes,
            "start_gate_bytes": stage_counter(fine, "start_gate", "aux1"),
            "start_gate_doorbells": start_gate_doorbells or start_gate_wqes,
            "start_gate_quiets": start_gate_quiets or start_gate_wqes,
            "rx_poll_passes": stage_counter(fine, "rx_flag_poll_wait", "aux0"),
            "rx_route_checks": stage_counter(fine, "rx_flag_poll_wait", "aux1"),
            "rx_ready_misses": stage_counter(fine, "rx_flag_poll_wait", "aux2"),
            "rx_bypassed_tokens": stage_counter(fine, "rx_output", "aux1"),
            "rx_out_of_order_completions": stage_counter(fine, "rx_output", "aux2"),
        })

    row = {
        "variant": label,
        "run": run,
        "launch": launch,
        "rank_count": expected_rank_size,
        "route_seed": workload[-1],
        "config_signature": signature_id(signature),
        "tx_ready_shared_flag": config_value(sample_config, "tx_ready_shared_flag"),
        "tx_ready_in_data": config_value(sample_config, "tx_ready_in_data"),
        "tx_meta_prefetch_full": config_value(sample_config, "tx_meta_prefetch_full"),
        "tx_ready_early_publish": config_value(sample_config, "tx_ready_early_publish"),
        "rx_ready_sticky_mask": config_value(sample_config, "rx_ready_sticky_mask"),
        "rx_ready_batch_mte2": config_value(sample_config, "rx_ready_batch_mte2"),
        "rx_ready_batch_vector": config_value(sample_config, "rx_ready_batch_vector"),
        "send_route_balanced": config_value(sample_config, "send_route_balanced"),
        "start_gate_executed": bool(sample_config.get(
            "start_gate_executed", sample_config.get("start_gate", False))),
    }
    for stage in COARSE_STAGES + OPTIONAL_COARSE_STAGES:
        critical = max_rank_value(coarse_records, f"{stage}_us")
        prefix = stage.removesuffix("_total")
        row[f"{prefix}_max_us"] = critical[f"{stage}_us"]
        row[f"{prefix}_max_rank"] = critical["rank"]
        row[f"{prefix}_max_core"] = critical[f"{stage}_core"]

    for stage in FINE_LATENCY_STAGES:
        critical = max_rank_value(fine_records, f"{stage}_us")
        prefix = stage.removesuffix("_total")
        row[f"{prefix}_max_us"] = critical[f"{stage}_us"]
        row[f"{prefix}_max_rank"] = critical["rank"]
        row[f"{prefix}_max_core"] = critical[f"{stage}_core"]

    for field in (
        "udma_wqes", "doorbell_commits", "active_sqs",
        "round_publish_wqes", "round_publish_bytes",
        "round_publish_doorbells", "round_publish_quiets",
        "start_gate_wqes", "start_gate_bytes",
        "start_gate_doorbells", "start_gate_quiets",
        "rx_poll_passes", "rx_route_checks", "rx_ready_misses",
        "rx_bypassed_tokens", "rx_out_of_order_completions",
    ):
        values = [record[field] for record in fine_records]
        critical = max_rank_value(fine_records, field)
        row[f"{field}_total"] = sum(values)
        row[f"{field}_rank_max"] = critical[field]
        row[f"{field}_rank"] = critical["rank"]
    row["wqes_per_doorbell"] = (
        row["udma_wqes_total"] / row["doorbell_commits_total"]
        if row["doorbell_commits_total"] else 0.0)
    row["all_udma_wqes_total"] = (
        row["udma_wqes_total"] + row["round_publish_wqes_total"] +
        row["start_gate_wqes_total"])
    row["all_doorbell_commits_total"] = (
        row["doorbell_commits_total"] + row["round_publish_doorbells_total"] +
        row["start_gate_doorbells_total"])
    row["rx_ready_miss_ratio"] = (
        row["rx_ready_misses_total"] / row["rx_route_checks_total"]
        if row["rx_route_checks_total"] else 0.0)
    return row, signature, workload


def collect_variant(label, root, expected_rank_size=64):
    traces = discover_traces(root)
    samples = sorted(
        {(run, launch) for run, _, launch, _ in traces},
        key=lambda item: (item[0], int(item[1][6:])),
    )
    rows = []
    signatures = set()
    workloads = set()
    for run, launch in samples:
        row, signature, workload = summarize_sample(
            label, run, launch, traces, expected_rank_size)
        rows.append(row)
        signatures.add(signature)
        workloads.add(workload)
    if len(signatures) != 1:
        raise ValueError(f"{label}: config signature differs across runs or launches")
    if len(workloads) != 1:
        raise ValueError(f"{label}: workload signature differs across runs")
    first_run, first_launch = samples[0]
    first_trace = traces[(first_run, "coarse", first_launch, 0)]
    return {
        "label": label,
        "root": str(root),
        "runs": rows,
        "signature": signatures.pop(),
        "workload": workloads.pop(),
        "design": describe_design(first_trace),
    }


def median(rows, key):
    return statistics.median(float(row[key]) for row in rows)


def aggregate_variants(variants, baseline):
    baseline_variant = next(variant for variant in variants if variant["label"] == baseline)
    baseline_kernel = median(baseline_variant["runs"], "kernel_max_us")
    baseline_tx_meta = median(baseline_variant["runs"], "tx_meta_scan_max_us")
    baseline_pack = median(baseline_variant["runs"], "pack_max_us")
    baseline_receive = median(baseline_variant["runs"], "receive_max_us")
    baseline_send = median(baseline_variant["runs"], "send_max_us")
    summaries = []
    for variant in variants:
        rows = variant["runs"]
        executed_gate_rows = [row for row in rows if row["start_gate_executed"]]
        executed_gate_median = lambda key: (
            median(executed_gate_rows, key) if executed_gate_rows else 0.0)
        kernel = median(rows, "kernel_max_us")
        design = variant["design"]
        summaries.append({
            "variant": variant["label"],
            "runs": len(rows),
            **design,
            "config_signature": signature_id(variant["signature"]),
            "kernel_max_us": kernel,
            "kernel_min_us": min(row["kernel_max_us"] for row in rows),
            "kernel_max_run_us": max(row["kernel_max_us"] for row in rows),
            "kernel_reduction_vs_baseline_pct": (
                (1.0 - kernel / baseline_kernel) * 100.0 if baseline_kernel else 0.0),
            "kernel_speedup_vs_baseline": baseline_kernel / kernel if kernel else 0.0,
            "pack_max_us": median(rows, "pack_max_us"),
            "pack_reduction_vs_baseline_pct": (
                (1.0 - median(rows, "pack_max_us") / baseline_pack) * 100.0
                if baseline_pack else 0.0),
            "receive_max_us": median(rows, "receive_max_us"),
            "receive_reduction_vs_baseline_pct": (
                (1.0 - median(rows, "receive_max_us") / baseline_receive) * 100.0
                if baseline_receive else 0.0),
            "send_max_us": median(rows, "send_max_us"),
            "send_reduction_vs_baseline_pct": (
                (1.0 - median(rows, "send_max_us") / baseline_send) * 100.0
                if baseline_send else 0.0),
            "tx_meta_scan_max_us": median(rows, "tx_meta_scan_max_us"),
            "tx_meta_scan_reduction_vs_baseline_pct": (
                (1.0 - median(rows, "tx_meta_scan_max_us") / baseline_tx_meta) * 100.0
                if baseline_tx_meta else 0.0),
            "pack_tx_data_submit_max_us": median(rows, "pack_tx_data_submit_max_us"),
            "pack_first_tx_ready_max_us": median(rows, "pack_first_tx_ready_max_us"),
            "pack_mte3_exposed_wait_max_us": median(rows, "pack_mte3_exposed_wait_max_us"),
            "rx_ready_mte2_wait_max_us": median(rows, "rx_ready_mte2_wait_max_us"),
            "rx_ready_vector_max_us": median(rows, "rx_ready_vector_max_us"),
            "start_gate_max_us": median(rows, "start_gate_max_us"),
            "round_publish_max_us": median(rows, "round_publish_max_us"),
            "global_round_wait_max_us": median(rows, "global_round_wait_max_us"),
            "udma_wqes_total": median(rows, "udma_wqes_total"),
            "doorbell_commits_total": median(rows, "doorbell_commits_total"),
            "round_publish_wqes_total": median(rows, "round_publish_wqes_total"),
            "round_publish_bytes_total": median(rows, "round_publish_bytes_total"),
            "round_publish_doorbells_total": median(rows, "round_publish_doorbells_total"),
            "round_publish_quiets_total": median(rows, "round_publish_quiets_total"),
            "start_gate_wqes_total": median(rows, "start_gate_wqes_total"),
            "start_gate_bytes_total": median(rows, "start_gate_bytes_total"),
            "start_gate_doorbells_total": median(rows, "start_gate_doorbells_total"),
            "start_gate_quiets_total": median(rows, "start_gate_quiets_total"),
            "start_gate_executed_launches": len(executed_gate_rows),
            "start_gate_total_launches": len(rows),
            "start_gate_execution_ratio": len(executed_gate_rows) / len(rows),
            "start_gate_executed_only_max_us": executed_gate_median("start_gate_max_us"),
            "start_gate_executed_only_wqes_total": executed_gate_median(
                "start_gate_wqes_total"),
            "start_gate_executed_only_bytes_total": executed_gate_median(
                "start_gate_bytes_total"),
            "start_gate_executed_only_doorbells_total": executed_gate_median(
                "start_gate_doorbells_total"),
            "start_gate_executed_only_quiets_total": executed_gate_median(
                "start_gate_quiets_total"),
            "all_udma_wqes_total": median(rows, "all_udma_wqes_total"),
            "all_doorbell_commits_total": median(rows, "all_doorbell_commits_total"),
            "wqes_per_doorbell": median(rows, "wqes_per_doorbell"),
            "active_sqs_total": median(rows, "active_sqs_total"),
            "active_sqs_rank_max": median(rows, "active_sqs_rank_max"),
            "rx_poll_passes_total": median(rows, "rx_poll_passes_total"),
            "rx_route_checks_total": median(rows, "rx_route_checks_total"),
            "rx_ready_misses_total": median(rows, "rx_ready_misses_total"),
            "rx_ready_miss_ratio": median(rows, "rx_ready_miss_ratio"),
            "rx_bypassed_tokens_total": median(rows, "rx_bypassed_tokens_total"),
            "rx_bypassed_tokens_rank_max": median(rows, "rx_bypassed_tokens_rank_max"),
            "rx_out_of_order_completions_total": median(
                rows, "rx_out_of_order_completions_total"),
            "rx_out_of_order_completions_rank_max": median(
                rows, "rx_out_of_order_completions_rank_max"),
        })
    return summaries


def duplicate_signature_groups(variants):
    labels_by_signature = {}
    for variant in variants:
        labels_by_signature.setdefault(variant["signature"], []).append(variant["label"])
    return [labels for labels in labels_by_signature.values() if len(labels) > 1]


def validate_matrix(variants, allow_duplicate_signatures=False):
    workloads = {variant["workload"] for variant in variants}
    if len(workloads) != 1:
        details = ", ".join(f"{variant['label']}={variant['workload']}" for variant in variants)
        raise ValueError(f"workload/route seed mismatch across variants: {details}")
    duplicates = duplicate_signature_groups(variants)
    if duplicates and not allow_duplicate_signatures:
        details = "; ".join("/".join(labels) for labels in duplicates)
        raise ValueError(f"duplicate design signature across variants: {details}")
    return workloads.pop()


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def fmt(value, digits=1):
    return f"{float(value):.{digits}f}"


def design_text(summary):
    rx = str(summary["rx_schedule"]).replace("_", " ")
    publish = "parallel publish" if summary["parallel_round_publish"] else "serial publish"
    gate = (f"start gate {str(summary['start_gate_policy']).replace('_', ' ')}"
            if summary["start_gate"] else "no start gate")
    qdc = (f"Q/D v{summary['qdc_version']}" if summary["qdc_version"] >= 0
           else "Q/D version unspecified")
    shared_ready = "shared ready flag on" if summary["tx_ready_shared_flag"] else \
        "shared ready flag off"
    ready_in_data = "TX ready in data on" if summary["tx_ready_in_data"] else \
        "TX ready in data off"
    meta_prefetch = "TX metadata full prefetch on" if summary["tx_meta_prefetch_full"] else \
        "TX metadata full prefetch off"
    early_publish = "TX early ready on" if summary["tx_ready_early_publish"] else \
        "TX early ready off"
    sticky = "RX sticky on" if summary["rx_ready_sticky_mask"] else "RX sticky off"
    batch_mte2 = "RX batch MTE2 on" if summary["rx_ready_batch_mte2"] else \
        "RX batch MTE2 off"
    batch_vector = "RX ready Vector on" if summary["rx_ready_batch_vector"] else \
        "RX ready Vector off"
    balanced_send = "balanced send routes on" if summary["send_route_balanced"] else \
        "balanced send routes off"
    return (
        f"{summary['variant']} [{summary['core_layout']} cores, "
        f"QP{summary['qp_count']}, DB{summary['doorbell_batch_size']}, "
        f"TXB{summary['tx_ready_batch_size']}, {shared_ready}, {ready_in_data}, "
        f"{meta_prefetch}, {early_publish}, {sticky}, {batch_mte2}, {batch_vector}, "
        f"{balanced_send}, RX {rx}, "
        f"{publish}, {gate}, {qdc}, enqueue window {summary['enqueue_window']}]")


def render_html(run_rows, summaries, baseline, workload, signature_override_note=None):
    rank_size, bs, h, top_k, self_send_count, route_stride, route_seed = workload
    host_timing_lines = []
    for summary in summaries:
        enqueue_window = int(summary["enqueue_window"])
        note = host_timing_interpretation(enqueue_window)
        if not note:
            note = (
                "enqueue_window=1: host_round_us and unprofiled_mean_us use one synchronized "
                "request per timing window."
            )
        host_timing_lines.append(
            f'<p><strong>{html.escape(summary["variant"])} host timing:</strong> '
            f'{html.escape(note)}</p>')
    host_timing_html = "".join(host_timing_lines)
    signature_override_html = ""
    if signature_override_note:
        signature_override_html = (
            '<p><strong>Historical trace identity override:</strong> '
            f'{html.escape(signature_override_note)}</p>')
    title_designs = " | ".join(design_text(summary) for summary in summaries)
    max_kernel = max(summary["kernel_max_us"] for summary in summaries) or 1.0
    colors = ["#176b52", "#b45309", "#2563a8", "#8b3a62", "#5c6470", "#6750a4"]
    cards = "".join(
        f'<section class="card"><span>{html.escape(summary["variant"])}</span>'
        f'<strong>{fmt(summary["kernel_max_us"])} us</strong>'
        f'<small>{summary["runs"]} samples; {fmt(summary["kernel_reduction_vs_baseline_pct"])}% '
        f'vs {html.escape(baseline)}</small></section>'
        for summary in summaries
    )
    bars = "".join(
        f'<div class="bar-row"><span>{html.escape(summary["variant"])}</span>'
        f'<div class="track"><i style="width:{100 * summary["kernel_max_us"] / max_kernel:.2f}%;'
        f'background:{colors[index % len(colors)]}"></i></div>'
        f'<b>{fmt(summary["kernel_max_us"])} us</b></div>'
        for index, summary in enumerate(summaries)
    )
    summary_rows = "".join(
        "<tr>" + "".join(f"<td>{value}</td>" for value in (
            html.escape(summary["variant"]),
            html.escape(summary["core_layout"]),
            summary["qp_count"], summary["doorbell_batch_size"], summary["tx_ready_batch_size"],
            "on" if summary["tx_ready_shared_flag"] else "off",
            "on" if summary["tx_ready_in_data"] else "off",
            "on" if summary["tx_meta_prefetch_full"] else "off",
            "on" if summary["tx_ready_early_publish"] else "off",
            "on" if summary["rx_ready_sticky_mask"] else "off",
            "on" if summary["rx_ready_batch_mte2"] else "off",
            "on" if summary["rx_ready_batch_vector"] else "off",
            "on" if summary["send_route_balanced"] else "off",
            (f'v{summary["qdc_version"]}' if summary["qdc_version"] >= 0 else "unspecified"),
            "parallel" if summary["parallel_round_publish"] else "serial",
            (str(summary["start_gate_policy"]).replace("_", " ")
             if summary["start_gate"] else "disabled"),
            summary["enqueue_window"],
            html.escape(str(summary["rx_schedule"]).replace("_", " ")),
            summary["runs"],
            f'{summary["start_gate_executed_launches"]}/{summary["start_gate_total_launches"]}',
            fmt(summary["kernel_max_us"]), fmt(summary["pack_max_us"]),
            fmt(summary["pack_reduction_vs_baseline_pct"]),
            fmt(summary["receive_max_us"]),
            fmt(summary["receive_reduction_vs_baseline_pct"]),
            fmt(summary["send_max_us"]),
            fmt(summary["send_reduction_vs_baseline_pct"]),
            fmt(summary["tx_meta_scan_max_us"]),
            fmt(summary["tx_meta_scan_reduction_vs_baseline_pct"]),
            fmt(summary["pack_tx_data_submit_max_us"]),
            fmt(summary["pack_first_tx_ready_max_us"]),
            fmt(summary["pack_mte3_exposed_wait_max_us"]),
            fmt(summary["rx_ready_mte2_wait_max_us"]),
            fmt(summary["rx_ready_vector_max_us"]),
            fmt(summary["start_gate_executed_only_max_us"]),
            fmt(summary["round_publish_max_us"]),
            fmt(summary["global_round_wait_max_us"]),
            fmt(summary["udma_wqes_total"], 0), fmt(summary["doorbell_commits_total"], 0),
            fmt(summary["round_publish_wqes_total"], 0),
            fmt(summary["round_publish_doorbells_total"], 0),
            fmt(summary["start_gate_executed_only_wqes_total"], 0),
            fmt(summary["start_gate_executed_only_doorbells_total"], 0),
            fmt(summary["all_udma_wqes_total"], 0),
            fmt(summary["all_doorbell_commits_total"], 0),
            fmt(summary["wqes_per_doorbell"], 2), fmt(summary["active_sqs_rank_max"], 0),
            fmt(summary["rx_route_checks_total"], 0),
            fmt(summary["rx_ready_miss_ratio"] * 100.0, 2) + "%",
            fmt(summary["rx_bypassed_tokens_total"], 0),
            fmt(summary["rx_out_of_order_completions_total"], 0),
            fmt(summary["kernel_reduction_vs_baseline_pct"]),
        )) + "</tr>"
        for summary in summaries
    )
    run_table_rows = "".join(
        "<tr>" + "".join(f"<td>{value}</td>" for value in (
            html.escape(row["variant"]), html.escape(row["run"]), html.escape(row["launch"]),
            "on" if row["tx_ready_shared_flag"] else "off",
            "on" if row["tx_ready_in_data"] else "off",
            "on" if row["tx_meta_prefetch_full"] else "off",
            "on" if row["tx_ready_early_publish"] else "off",
            "on" if row["rx_ready_sticky_mask"] else "off",
            "on" if row["rx_ready_batch_mte2"] else "off",
            "on" if row["rx_ready_batch_vector"] else "off",
            "on" if row["send_route_balanced"] else "off",
            "yes" if row["start_gate_executed"] else "no",
            fmt(row["kernel_max_us"]), f'rank{row["kernel_max_rank"]}/core{row["kernel_max_core"]}',
            fmt(row["pack_max_us"]), fmt(row["receive_max_us"]), fmt(row["send_max_us"]),
            fmt(row["tx_meta_scan_max_us"]),
            fmt(row["pack_tx_data_submit_max_us"]), fmt(row["pack_first_tx_ready_max_us"]),
            fmt(row["pack_mte3_exposed_wait_max_us"]), fmt(row["rx_ready_mte2_wait_max_us"]),
            fmt(row["rx_ready_vector_max_us"]),
            fmt(row["start_gate_max_us"]),
            fmt(row["round_publish_max_us"]),
            fmt(row["global_round_wait_max_us"]),
            row["udma_wqes_total"], row["doorbell_commits_total"],
            row["round_publish_wqes_total"], row["round_publish_doorbells_total"],
            row["start_gate_wqes_total"], row["start_gate_doorbells_total"],
            row["all_udma_wqes_total"], row["all_doorbell_commits_total"],
            fmt(row["wqes_per_doorbell"], 2), row["active_sqs_rank_max"],
            row["rx_route_checks_total"], fmt(row["rx_ready_miss_ratio"] * 100.0, 2) + "%",
            row["rx_bypassed_tokens_total"], row["rx_out_of_order_completions_total"],
        )) + "</tr>"
        for row in run_rows
    )
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TileXR {rank_size}-rank Combine Matrix | {html.escape(title_designs)}</title><style>
:root{{--ink:#17211b;--muted:#5e6b64;--line:#d7ddd9;--paper:#fff}}*{{box-sizing:border-box}}
body{{margin:0;background:#f4f6f5;color:var(--ink);font:14px/1.45 Arial,sans-serif;letter-spacing:0}}
header,main{{max-width:1380px;margin:auto}}header{{padding:30px 24px 18px}}h1{{font-size:27px;margin:0 0 7px}}
p{{color:var(--muted);margin:5px 0}}main{{padding:0 24px 40px}}.cards{{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:9px;margin:8px 0 18px}}
.card,.panel{{background:var(--paper);border:1px solid var(--line);border-radius:6px}}.card{{padding:13px}}.card span,.card small{{display:block;color:var(--muted)}}.card strong{{display:block;font-size:23px;margin:4px 0}}
.panel{{padding:15px;margin:11px 0}}h2{{font-size:17px;margin:0 0 11px}}.bar-row{{display:grid;grid-template-columns:150px 1fr 90px;gap:10px;align-items:center;margin:7px 0}}
.track{{height:14px;background:#edf0ee}}.track i{{display:block;height:100%}}.bar-row b{{text-align:right;font-size:12px}}.scroll{{overflow:auto}}
table{{border-collapse:collapse;width:100%;font-size:12px}}th,td{{padding:7px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}}th:first-child,td:first-child,th:nth-child(2),td:nth-child(2){{text-align:left}}
code{{font-size:12px}}@media(max-width:760px){{.bar-row{{grid-template-columns:100px 1fr 76px}}}}</style></head><body>
<header><h1>TileXR {rank_size}-rank Combine Design Matrix</h1>
  <p>{html.escape(title_designs)}</p><p>bs={bs}, topK={top_k}, h={h}, selfSendCnt={self_send_count}, routeStride={route_stride}B; deterministic randomized routes, seed={route_seed}.</p>{signature_override_html}{host_timing_html}</header><main>
<div class="cards">{cards}</div><section class="panel"><h2>Kernel Critical Latency</h2>{bars}
<p>Per rank: maximum <code>kernel_total</code> across AIV cores. Per sample: maximum across all {rank_size} ranks. Headline: median across paired coarse/fine samples.</p></section>
  <section class="panel scroll"><h2>Cross-sample Median</h2><table><thead><tr><th>Variant</th><th>Pack/Send[/Self] cores</th><th>QPs</th><th>DB batch</th><th>TX-ready batch</th><th>Shared ready flag</th><th>TX ready in data</th><th>TX metadata full prefetch</th><th>TX early publish</th><th>RX sticky mask</th><th>RX batch MTE2</th><th>RX ready Vector</th><th>Balanced send routes</th><th>Q/D</th><th>Round publish</th><th>Start gate</th><th>Enqueue window</th><th>RX scheduler</th><th>Samples</th><th>Gate executed</th><th>Kernel max us</th><th>Pack max us</th><th>Pack reduction vs baseline %</th><th>Receive max us</th><th>Receive reduction vs baseline %</th><th>Send max us</th><th>Send reduction vs baseline %</th><th>TX meta scan max us</th><th>TX meta reduction vs baseline %</th><th>TX data submit max us</th><th>First TX ready max us</th><th>MTE3 exposed wait max us</th><th>RX ready MTE2 wait max us</th><th>RX ready Vector max us</th><th>Gate max us (executed only)</th><th>Publish join max us</th><th>Global wait max us</th><th>Data WQEs</th><th>Data doorbells</th><th>Publish WQEs</th><th>Publish doorbells</th><th>Gate WQEs (executed only)</th><th>Gate doorbells (executed only)</th><th>All WQEs</th><th>All doorbells</th><th>Data WQE/DB</th><th>Active SQ rank max</th><th>RX flag checks</th><th>RX miss %</th><th>RX bypass total</th><th>RX OOO total</th><th>Kernel reduction vs baseline %</th></tr></thead><tbody>{summary_rows}</tbody></table></section>
  <section class="panel scroll"><h2>Every Sample</h2><table><thead><tr><th>Variant</th><th>Run</th><th>Launch</th><th>Shared ready flag</th><th>TX ready in data</th><th>TX metadata full prefetch</th><th>TX early publish</th><th>RX sticky mask</th><th>RX batch MTE2</th><th>RX ready Vector</th><th>Balanced send routes</th><th>Gate executed</th><th>Kernel max us</th><th>Source</th><th>Pack max us</th><th>Receive max us</th><th>Send max us</th><th>TX meta scan max us</th><th>TX data submit max us</th><th>First TX ready max us</th><th>MTE3 exposed wait max us</th><th>RX ready MTE2 wait max us</th><th>RX ready Vector max us</th><th>Start gate max us</th><th>Publish join max us</th><th>Global wait max us</th><th>Data WQEs</th><th>Data doorbells</th><th>Publish WQEs</th><th>Publish doorbells</th><th>Gate WQEs</th><th>Gate doorbells</th><th>All WQEs</th><th>All doorbells</th><th>Data WQE/DB</th><th>Active SQ rank max</th><th>RX flag checks</th><th>RX miss %</th><th>RX bypass total</th><th>RX OOO total</th></tr></thead><tbody>{run_table_rows}</tbody></table></section>
  <section class="panel"><h2>Measurement Contract</h2><p>Kernel and role-total latency stages use coarse traces; <code>tx_meta_scan</code> uses the fine trace with the same launch index. Every latency stage independently selects the maximum core, then maximum rank, then the median across paired launches; stage columns are not additive. For parallel Publish, the Round Publish value is the sender0 join span through all Send-core PublishDone lines. Start-gate traffic and time are reported separately and do not contaminate data or Round Publish stages. Gate latency/WQE/doorbell headline fields are medians over launches whose trace metadata says <code>start_gate_executed=true</code>; the executed/total launch count is always shown so skipped launches cannot turn those fields into a misleading zero. WQE, doorbell, active-SQ and RX scheduling counters use the fine trace with the same launch index. Data, Round Publish and Start Gate WQEs/doorbells are reported separately and together; all transport and RX counters are {rank_size}-rank totals. Active SQ is the maximum per-rank sum across Send AIVs. All variants passed exact rank/launch sets, route-seed, workload-signature and per-variant enqueue-window/config-signature checks. {"An explicit historical-trace override admitted duplicate design signatures; the full variant labels identify the implementation difference that old traces did not encode." if signature_override_note else "Duplicate design signatures under different labels are rejected."} Baseline: {html.escape(baseline)}.</p></section>
</main></body></html>"""


def parse_variant_specs(specs):
    parsed = []
    labels = set()
    for spec in specs:
        if "=" not in spec:
            raise ValueError(f"variant must be LABEL=ROOT, got {spec!r}")
        label, root = spec.split("=", 1)
        label = label.strip()
        root = root.strip()
        if not label or not root:
            raise ValueError(f"variant must be LABEL=ROOT, got {spec!r}")
        if label in labels:
            raise ValueError(f"duplicate variant label {label!r}")
        labels.add(label)
        parsed.append((label, Path(root).resolve()))
    return parsed


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compare arbitrary repeated TileXR 64-rank Combine profile variants")
    parser.add_argument("output_dir")
    parser.add_argument("--variant", action="append", required=True, metavar="LABEL=ROOT")
    parser.add_argument("--baseline", required=True, metavar="LABEL")
    parser.add_argument("--rank-size", type=int, default=64)
    parser.add_argument("--allow-duplicate-signatures", action="store_true",
                        help="allow explicitly labeled historical variants whose old traces omit the distinguishing design field")
    return parser.parse_args()


def main():
    args = parse_args()
    specs = parse_variant_specs(args.variant)
    if len(specs) < 2:
        raise SystemExit("at least two --variant LABEL=ROOT arguments are required")
    if args.baseline not in {label for label, _ in specs}:
        raise SystemExit(f"baseline {args.baseline!r} is not one of the variant labels")
    variants = [collect_variant(label, root, args.rank_size) for label, root in specs]
    duplicate_groups = duplicate_signature_groups(variants)
    workload = validate_matrix(variants, args.allow_duplicate_signatures)
    summaries = aggregate_variants(variants, args.baseline)
    run_rows = [row for variant in variants for row in variant["runs"]]
    signature_override = bool(duplicate_groups)
    for row in summaries + run_rows:
        row["design_signature_override"] = signature_override
    signature_override_note = None
    if signature_override:
        signature_override_note = (
            "The selected historical traces have identical recorded design signatures. "
            "The full variant labels below state the unrecorded implementation difference: " +
            "; ".join(" / ".join(labels) for labels in duplicate_groups) + ".")
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    write_csv(output / "combine_matrix_runs.csv", run_rows)
    write_csv(output / "combine_matrix_summary.csv", summaries)
    report = output / "combine_matrix_report.html"
    report.write_text(
        render_html(run_rows, summaries, args.baseline, workload, signature_override_note),
        encoding="utf-8")
    print(f"wrote {report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
