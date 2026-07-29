#!/usr/bin/env python3

import argparse
import csv
import html
import importlib.util
import json
import re
import statistics
from collections import defaultdict
from pathlib import Path
from types import SimpleNamespace


COARSE_STAGES = [
    "pack_total",
    "receive_total",
    "send_total",
    "self_copy_total",
    "local_sender_wait",
    "local_self_copy_wait",
    "local_rx_wait",
    "start_gate",
    "round_publish",
    "global_round_wait",
]

FINE_STAGES = [
    "pack_input_wait",
    "pack_quantize",
    "pack_tx_publish",
    "pack_tx_data_submit",
    "pack_first_tx_ready",
    "pack_mte3_exposed_wait",
    "rx_flag_poll_wait",
    "rx_ready_mte2_wait",
    "rx_ready_vector",
    "rx_unpack_wait",
    "rx_unpack_dequant_clear",
    "rx_output",
    "tx_meta_scan",
    "tx_ready_poll",
    "self_copy",
    "udma_post",
    "udma_quiet",
    "local_sender_wait",
    "local_self_copy_wait",
    "local_rx_wait",
    "start_gate",
    "round_publish",
    "global_round_wait",
]

COLORS = {
    "pack_total": "#d97706",
    "receive_total": "#15803d",
    "send_total": "#2563eb",
    "self_copy_total": "#64748b",
    "local_sender_wait": "#0f766e",
    "local_self_copy_wait": "#9333ea",
    "local_rx_wait": "#dc2626",
    "start_gate": "#0369a1",
    "round_publish": "#7c3aed",
    "global_round_wait": "#475569",
    "pack_input_wait": "#f59e0b",
    "pack_quantize": "#b45309",
    "pack_tx_publish": "#92400e",
    "pack_tx_data_submit": "#a16207",
    "pack_first_tx_ready": "#ca8a04",
    "pack_mte3_exposed_wait": "#ea580c",
    "rx_flag_poll_wait": "#ef4444",
    "rx_ready_mte2_wait": "#be123c",
    "rx_ready_vector": "#9f1239",
    "rx_unpack_wait": "#f97316",
    "rx_unpack_dequant_clear": "#16a34a",
    "rx_output": "#4d7c0f",
    "tx_meta_scan": "#0ea5e9",
    "tx_ready_poll": "#e11d48",
    "self_copy": "#64748b",
    "udma_post": "#1d4ed8",
    "udma_quiet": "#0f766e",
    "dcci_total": "#7c2d12",
}

OLD_CORE_ROLES = [
    {"name": "pack_rx", "begin": 0, "count": 56},
    {"name": "send", "begin": 56, "count": 8},
]

LEGACY_STAGE_NAMES = {
    "pack_input_copy": "pack_input_wait",
}


def canonical_stage_name(stage):
    return LEGACY_STAGE_NAMES.get(str(stage), str(stage))


def normalize_core_roles(trace, stats):
    max_core_count = int(trace.get("max_core_count", 64))
    roles = trace.get("config", {}).get("core_roles")
    if not roles:
        has_dedicated_self = any(stat.get("stage") == "self_copy_total" for stat in stats)
        roles = ([
            {"name": "pack_rx", "begin": 0, "count": 51},
            {"name": "send", "begin": 51, "count": 12},
            {"name": "self_copy", "begin": 63, "count": 1},
        ] if has_dedicated_self else OLD_CORE_ROLES)
    normalized = []
    covered = set()
    for role in roles:
        name = str(role.get("name", ""))
        begin = int(role.get("begin", -1))
        count = int(role.get("count", 0))
        if name not in {"pack_rx", "send", "self_copy"} or begin < 0 or count <= 0 or begin + count > max_core_count:
            raise ValueError(f"invalid core role in rank {trace.get('rank_size')}: {role}")
        role_cores = set(range(begin, begin + count))
        if covered & role_cores:
            raise ValueError(f"overlapping core role: {role}")
        covered |= role_cores
        normalized.append({"name": name, "begin": begin, "count": count})
    if covered != set(range(max_core_count)):
        raise ValueError(f"core roles do not cover 0..{max_core_count - 1}: {normalized}")
    return normalized


def parse_args():
    parser = argparse.ArgumentParser(description="Build TileXR EP URMA combine profile visualizations")
    parser.add_argument("profile_dir")
    parser.add_argument("--coarse-dir")
    parser.add_argument("--total-dir")
    parser.add_argument("--design-label")
    parser.add_argument("--skip-generic-report", action="store_true")
    parser.add_argument(
        "--show-start-gate", action="store_true",
        help=("Show start_gate as a proportional timeline/heatmap stage. By default the "
              "gate remains in summary metadata but is collapsed from steady-state views."))
    return parser.parse_args()


def infer_path_identity(root):
    if root is None:
        return {"variant": None, "run": None}
    parts = [part.lower() for part in Path(root).parts]
    variant = next((name for name in ("cacheless", "legacy") if name in parts), None)
    run_label = next((part for part in reversed(Path(root).parts)
                      if re.fullmatch(r"run\d+", part.lower())), None)
    return {"variant": variant, "run": run_label}


def infer_design_label(root):
    variant = infer_path_identity(root)["variant"]
    if variant == "cacheless":
        return "Cacheless DataAsFlag"
    if variant == "legacy":
        return "Legacy DCCI + DataAsFlag"
    return "DataAsFlag"


def normalized_enqueue_window(config):
    try:
        value = int(config.get("enqueue_window", 1))
    except (TypeError, ValueError) as error:
        raise ValueError(f"invalid enqueue_window {config.get('enqueue_window')!r}") from error
    if value <= 0:
        raise ValueError(f"invalid enqueue_window {value}; expected a positive integer")
    return value


def normalized_start_gate_policy(config):
    start_gate = bool(config.get("start_gate", False))
    fallback = "every_launch" if start_gate else "disabled"
    value = str(config.get("start_gate_policy", fallback))
    valid = {"disabled", "every_launch", "first_after_stream_synchronize"}
    if value not in valid:
        raise ValueError(f"invalid start_gate_policy {value!r}")
    if start_gate != (value != "disabled"):
        raise ValueError(
            f"start_gate={start_gate} conflicts with start_gate_policy={value!r}")
    return value


def normalized_start_gate_executed(config):
    return bool(config.get("start_gate_executed", config.get("start_gate", False)))


def normalized_tx_ready_shared_flag(config):
    return bool(config.get("tx_ready_shared_flag", False))


def normalized_tx_ready_in_data(config):
    return bool(config.get("tx_ready_in_data", False))


def normalized_tx_meta_prefetch_full(config):
    return bool(config.get("tx_meta_prefetch_full", False))


def normalized_tx_ready_early_publish(config):
    return bool(config.get("tx_ready_early_publish", False))


def normalized_rx_ready_sticky_mask(config):
    return bool(config.get("rx_ready_sticky_mask", False))


def normalized_rx_ready_batch_mte2(config):
    return bool(config.get("rx_ready_batch_mte2", False))


def normalized_rx_ready_batch_vector(config):
    return bool(config.get("rx_ready_batch_vector", False))


def normalized_send_route_balanced(config):
    return bool(config.get("send_route_balanced", False))


def host_timing_interpretation(enqueue_window):
    if enqueue_window <= 1:
        return ""
    return (
        f"enqueue_window={enqueue_window}: host_round_us and unprofiled_mean_us are "
        "window-amortized throughput proxies, not single-request latency."
    )


def build_report_context(root, ranks, design_label=None, sample_count=1,
                          representative_launch="launch0", kernel_median_us=0.0,
                          samples=None, total_kernel_median_us=None,
                          coarse_kernel_median_us=None, fine_kernel_median_us=None):
    first = ranks[0]
    config = first.get("config", {})
    run_label = infer_path_identity(root)["run"] or "capture"
    role_names = {
        "pack_rx": "Pack/Receive",
        "send": "Send",
        "self_copy": "SelfCopy",
    }
    role_text = " + ".join(
        f"{role['count']} {role_names.get(role['name'], role['name'])}"
        for role in first["core_roles"])
    route_seed = config.get("route_seed")
    route_text = (f"deterministic randomized routes, seed={route_seed}"
                  if route_seed is not None else "route seed unavailable")
    qp_count = int(config.get("qp_count", 0))
    doorbell_batch = int(config.get("doorbell_batch_size", 1))
    tx_ready_batch = int(config.get("tx_ready_batch_size", 1))
    tx_ready_shared_flag = normalized_tx_ready_shared_flag(config)
    tx_ready_in_data = normalized_tx_ready_in_data(config)
    tx_meta_prefetch_full = normalized_tx_meta_prefetch_full(config)
    tx_ready_early_publish = normalized_tx_ready_early_publish(config)
    rx_ready_sticky_mask = normalized_rx_ready_sticky_mask(config)
    rx_ready_batch_mte2 = normalized_rx_ready_batch_mte2(config)
    rx_ready_batch_vector = normalized_rx_ready_batch_vector(config)
    send_route_balanced = normalized_send_route_balanced(config)
    parallel_round_publish = bool(config.get("parallel_round_publish", False))
    start_gate = bool(config.get("start_gate", False))
    start_gate_policy = normalized_start_gate_policy(config)
    start_gate_executed = normalized_start_gate_executed(config)
    enqueue_window = normalized_enqueue_window(config)
    rx_schedule = str(config.get("rx_schedule", "sequential")).replace("_", " ")
    qdc_version = int(config.get("qdc_version", -1))
    qdc_text = f"Q/D v{qdc_version}" if qdc_version >= 0 else "Q/D version unspecified"
    start_gate_text = "start gate disabled" if not start_gate else (
        f"start gate enabled: {start_gate_policy.replace('_', ' ')} "
        f"({'executed' if start_gate_executed else 'skipped'} in representative launch)")
    samples = samples or []
    executed_samples = [sample for sample in samples if sample.get("start_gate_executed", False)]
    executed_gate_us = [float(sample.get("start_gate_max_us", 0.0)) for sample in executed_samples]
    executed_gate_wqes = [int(sample.get("start_gate_wqes_total", 0)) for sample in executed_samples]
    executed_gate_doorbells = [
        int(sample.get("start_gate_doorbells_total", 0)) for sample in executed_samples]
    executed_gate_quiets = [int(sample.get("start_gate_quiets_total", 0)) for sample in executed_samples]
    return {
        "design": design_label or infer_design_label(root),
        "run": run_label,
        "role_text": role_text,
        "shape_text": (
            f"bs={int(config.get('bs', 0))}, topK={int(config.get('top_k', 0))}, "
            f"h={int(config.get('h', 0))}, selfSendCnt={int(config.get('self_send_count', 0))}, "
            f"routeStride={int(config.get('route_stride', 0))}B, enqueueWindow={enqueue_window}"),
        "route_text": route_text,
        "transport_text": (
            f"{qp_count} QPs, {doorbell_batch}-WQE doorbell, "
            f"TX-ready batch {tx_ready_batch}, "
            f"shared ready flag {'on' if tx_ready_shared_flag else 'off'}, "
            f"TX ready in data {'on' if tx_ready_in_data else 'off'}, "
            f"TX metadata full prefetch {'on' if tx_meta_prefetch_full else 'off'}, "
            f"TX early ready {'on' if tx_ready_early_publish else 'off'}, "
            f"RX sticky {'on' if rx_ready_sticky_mask else 'off'}, "
            f"RX batched MTE2 {'on' if rx_ready_batch_mte2 else 'off'}, "
            f"RX Vector ready {'on' if rx_ready_batch_vector else 'off'}, "
            f"balanced send routes {'on' if send_route_balanced else 'off'}, "
            f"RX {rx_schedule}, "
            f"round publish {'parallel' if parallel_round_publish else 'serial'}, "
            f"{start_gate_text}, {qdc_text}"),
        "sample_count": sample_count,
        "enqueue_window": enqueue_window,
        "tx_ready_shared_flag": tx_ready_shared_flag,
        "tx_ready_in_data": tx_ready_in_data,
        "tx_meta_prefetch_full": tx_meta_prefetch_full,
        "tx_ready_early_publish": tx_ready_early_publish,
        "rx_ready_sticky_mask": rx_ready_sticky_mask,
        "rx_ready_batch_mte2": rx_ready_batch_mte2,
        "rx_ready_batch_vector": rx_ready_batch_vector,
        "send_route_balanced": send_route_balanced,
        "start_gate_policy": start_gate_policy,
        "start_gate_executed": start_gate_executed,
        "start_gate_executed_launches": len(executed_samples),
        "start_gate_total_launches": len(samples) if samples else sample_count,
        "start_gate_executed_only_median_us": (
            statistics.median(executed_gate_us) if executed_gate_us else 0.0),
        "start_gate_executed_only_median_wqes": (
            statistics.median(executed_gate_wqes) if executed_gate_wqes else 0.0),
        "start_gate_executed_only_median_doorbells": (
            statistics.median(executed_gate_doorbells) if executed_gate_doorbells else 0.0),
        "start_gate_executed_only_median_quiets": (
            statistics.median(executed_gate_quiets) if executed_gate_quiets else 0.0),
        "representative_launch": representative_launch,
        "kernel_median_us": kernel_median_us,
        "kernel_headline_scope": (
            "total-only strict-equivalent profile" if total_kernel_median_us is not None else
            "coarse attribution profile"),
        "total_kernel_median_us": total_kernel_median_us,
        "coarse_kernel_median_us": (
            kernel_median_us if coarse_kernel_median_us is None else coarse_kernel_median_us),
        "fine_kernel_median_us": (
            kernel_median_us if fine_kernel_median_us is None else fine_kernel_median_us),
    }


def load_generic_reporter(repo_root):
    path = repo_root / "tests" / "collectives" / "tilexr_collective_profile_report.py"
    spec = importlib.util.spec_from_file_location("tilexr_collective_profile_report", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_rank_traces(root):
    ranks = []
    for path in sorted(root.glob("rank*/launch*/trace.json")):
        trace = json.loads(path.read_text(encoding="utf-8"))
        if trace.get("op_name") != "TileXRMoeEpCombineUrma":
            continue
        rank = int(path.parts[-3][4:])
        launch = path.parts[-2]
        divisor = int(trace["cycle_to_us_divisor"])
        stats = [dict(stat) for stat in trace.get("stats", []) if int(stat.get("count", 0)) > 0]
        for stat in stats:
            stat["stage"] = canonical_stage_name(stat.get("stage", ""))
        starts = [int(stat["first_start_cycle"]) for stat in stats if int(stat["first_start_cycle"]) > 0]
        zero = min(starts) if starts else 0
        for stat in stats:
            first = int(stat["first_start_cycle"])
            last = int(stat["last_end_cycle"])
            stat["start_us"] = max(0, first - zero) / divisor
            stat["end_us"] = max(first, last) / divisor - zero / divisor
            stat["sum_us"] = float(stat.get("sum_us", int(stat["raw_cycles"]) / divisor))
            stat["max_us"] = int(stat.get("max_cycles", 0)) / divisor
        host_path = path.parents[1] / "host_info.json"
        host_info = json.loads(host_path.read_text(encoding="utf-8")) if host_path.exists() else {}
        ranks.append({
            "rank": rank,
            "launch": launch,
            "host": host_info.get("host", f"rank{rank}"),
            "host_ip": host_info.get("ip", ""),
            "rank_size": int(trace.get("rank_size", 0)),
            "max_core_count": int(trace.get("max_core_count", 64)),
            "stage_count": int(trace.get("stage_count", 0)),
            "cycle_to_us_divisor": divisor,
            "kernel_timing_boundary": trace.get(
                "kernel_timing_boundary", "legacy_unbracketed"),
            "profile_detail": int(trace.get("profile_detail", 2)),
            "profile_scope": trace.get("profile_scope", "legacy_attribution"),
            "host_round_us": float(trace.get("host_round_us", 0.0)),
            "unprofiled_mean_us": float(trace.get("unprofiled_mean_us", 0.0)),
            "config": trace.get("config", {}),
            "core_roles": normalize_core_roles(trace, stats),
            "stats": stats,
        })
    return sorted(ranks, key=lambda item: item["rank"])


def launch_sort_key(launch):
    match = re.fullmatch(r"launch(\d+)", str(launch))
    return (0, int(match.group(1))) if match else (1, str(launch))


def group_rank_traces(ranks):
    grouped = defaultdict(dict)
    for rank_data in ranks:
        launch = rank_data.get("launch", "launch0")
        rank = int(rank_data["rank"])
        if rank in grouped[launch]:
            raise ValueError(f"duplicate rank{rank} trace in {launch}")
        grouped[launch][rank] = rank_data
    return {
        launch: [by_rank[rank] for rank in sorted(by_rank)]
        for launch, by_rank in sorted(grouped.items(), key=lambda item: launch_sort_key(item[0]))
    }


def cluster_kernel_critical(ranks):
    candidates = []
    for rank_data in ranks:
        maximum = stage_max_stat(rank_data, "kernel_total")
        if maximum is None:
            raise ValueError(
                f"missing kernel_total in {rank_data.get('launch', 'launch0')} "
                f"rank{rank_data['rank']}")
        candidates.append({
            "us": float(maximum["sum_us"]),
            "rank": int(rank_data["rank"]),
            "core": int(maximum["core"]),
        })
    return max(candidates, key=lambda item: item["us"])


def cluster_optional_stage_critical(ranks, stage):
    candidates = []
    for rank_data in ranks:
        maximum = stage_max_stat(rank_data, stage)
        if maximum is not None:
            candidates.append({
                "us": float(maximum["sum_us"]),
                "rank": int(rank_data["rank"]),
                "core": int(maximum["core"]),
            })
    return max(candidates, key=lambda item: item["us"]) if candidates else {
        "us": 0.0, "rank": -1, "core": -1,
    }


def validate_total_profile_samples(total_groups, reference_groups):
    if set(total_groups) != set(reference_groups):
        raise ValueError("total/reference launch sets do not match")
    samples = []
    for launch in sorted(reference_groups, key=launch_sort_key):
        total_ranks = total_groups[launch]
        reference_ranks = reference_groups[launch]
        total_by_rank = {item["rank"]: item for item in total_ranks}
        reference_by_rank = {item["rank"]: item for item in reference_ranks}
        if set(total_by_rank) != set(reference_by_rank):
            raise ValueError(f"total/reference rank sets do not match in {launch}")
        for rank in sorted(reference_by_rank):
            total_rank = total_by_rank[rank]
            reference_rank = reference_by_rank[rank]
            if total_rank.get("profile_detail") != 0 or \
                    total_rank.get("profile_scope") != "kernel_total_only":
                raise ValueError(
                    f"total profile must use profile_detail=0 in {launch} at rank {rank}")
            if total_rank.get("kernel_timing_boundary") != "pipe_all_bracketed_pre_flush":
                raise ValueError(
                    f"total profile must use PIPE_ALL timing in {launch} at rank {rank}")
            if profile_signature(total_rank) != profile_signature(reference_rank):
                raise ValueError(
                    f"total/reference profile configuration mismatch in {launch} at rank {rank}")
            populated = {stat["stage"] for stat in total_rank["stats"]}
            if populated != {"kernel_total"}:
                raise ValueError(
                    f"total profile contains attribution stages in {launch} at rank {rank}: "
                    f"{sorted(populated)}")
        critical = cluster_kernel_critical(total_ranks)
        samples.append({
            "launch": launch,
            "total_kernel_max_us": critical["us"],
            "total_kernel_max_rank": critical["rank"],
            "total_kernel_max_core": critical["core"],
        })
    return samples


def validate_profile_samples(fine_groups, coarse_groups):
    if set(fine_groups) != set(coarse_groups):
        raise ValueError("fine/coarse launch sets do not match")
    samples = []
    enqueue_windows = set()
    profile_signatures = set()
    for launch in sorted(fine_groups, key=launch_sort_key):
        fine_ranks = fine_groups[launch]
        coarse_ranks = coarse_groups[launch]
        fine_by_rank = {item["rank"]: item for item in fine_ranks}
        coarse_by_rank = {item["rank"]: item for item in coarse_ranks}
        rank_sizes = {
            int(item.get("rank_size", 0)) for item in fine_ranks + coarse_ranks
        }
        if len(rank_sizes) != 1 or next(iter(rank_sizes)) <= 0:
            raise ValueError(f"invalid or inconsistent rank_size in {launch}")
        expected_ranks = set(range(next(iter(rank_sizes))))
        if set(fine_by_rank) != expected_ranks or set(coarse_by_rank) != expected_ranks:
            raise ValueError(f"fine/coarse rank sets do not match rank_size in {launch}")
        launch_gate_executions = set()
        for rank in sorted(expected_ranks):
            enqueue_windows.add(normalized_enqueue_window(fine_by_rank[rank]["config"]))
            enqueue_windows.add(normalized_enqueue_window(coarse_by_rank[rank]["config"]))
            profile_signatures.add(profile_signature(fine_by_rank[rank]))
            profile_signatures.add(profile_signature(coarse_by_rank[rank]))
            launch_gate_executions.add(
                normalized_start_gate_executed(fine_by_rank[rank]["config"]))
            launch_gate_executions.add(
                normalized_start_gate_executed(coarse_by_rank[rank]["config"]))
            if profile_signature(fine_by_rank[rank]) != profile_signature(coarse_by_rank[rank]):
                raise ValueError(
                    f"fine/coarse profile configuration mismatch in {launch} at rank {rank}")
        if len(launch_gate_executions) != 1:
            raise ValueError(f"start_gate_executed mismatch across coarse/fine ranks in {launch}")
        start_gate_executed = launch_gate_executions.pop()
        coarse_critical = cluster_kernel_critical(coarse_ranks)
        fine_critical = cluster_kernel_critical(fine_ranks)
        gate_critical = cluster_optional_stage_critical(coarse_ranks, "start_gate")
        samples.append({
            "launch": launch,
            "tx_ready_shared_flag": normalized_tx_ready_shared_flag(
                fine_ranks[0]["config"]),
            "tx_ready_in_data": normalized_tx_ready_in_data(
                fine_ranks[0]["config"]),
            "tx_meta_prefetch_full": normalized_tx_meta_prefetch_full(
                fine_ranks[0]["config"]),
            "tx_ready_early_publish": normalized_tx_ready_early_publish(
                fine_ranks[0]["config"]),
            "rx_ready_sticky_mask": normalized_rx_ready_sticky_mask(
                fine_ranks[0]["config"]),
            "rx_ready_batch_mte2": normalized_rx_ready_batch_mte2(
                fine_ranks[0]["config"]),
            "rx_ready_batch_vector": normalized_rx_ready_batch_vector(
                fine_ranks[0]["config"]),
            "send_route_balanced": normalized_send_route_balanced(
                fine_ranks[0]["config"]),
            "coarse_kernel_max_us": coarse_critical["us"],
            "coarse_kernel_max_rank": coarse_critical["rank"],
            "coarse_kernel_max_core": coarse_critical["core"],
            "fine_kernel_max_us": fine_critical["us"],
            "fine_kernel_max_rank": fine_critical["rank"],
            "fine_kernel_max_core": fine_critical["core"],
            "start_gate_executed": start_gate_executed,
            "start_gate_max_us": gate_critical["us"],
            "start_gate_max_rank": gate_critical["rank"],
            "start_gate_max_core": gate_critical["core"],
            "start_gate_wqes_total": sum(
                stat_counter(rank_data, "start_gate", "aux0") for rank_data in fine_ranks),
            "start_gate_doorbells_total": sum(
                stat_counter(rank_data, "start_gate", "aux2") for rank_data in fine_ranks),
            "start_gate_quiets_total": sum(
                stat_counter(rank_data, "start_gate", "aux3") for rank_data in fine_ranks),
        })
    if len(enqueue_windows) != 1:
        raise ValueError(
            f"enqueue_window mismatch across coarse/fine ranks or launches: {sorted(enqueue_windows)}")
    if len(profile_signatures) != 1:
        raise ValueError("profile configuration mismatch across coarse/fine ranks or launches")
    return samples


def representative_sample(samples):
    median_us = statistics.median(row["coarse_kernel_max_us"] for row in samples)
    selected = min(
        samples,
        key=lambda row: (abs(row["coarse_kernel_max_us"] - median_us),
                         launch_sort_key(row["launch"])),
    )
    return selected["launch"], median_us


def stage_stats(rank_data, stage, core=None):
    return [
        stat for stat in rank_data["stats"]
        if stat["stage"] == stage and (core is None or int(stat["core"]) == core)
    ]


def stage_max_stat(rank_data, stage):
    return max(stage_stats(rank_data, stage), key=lambda stat: stat["sum_us"], default=None)


def stage_max(rank_data, stage):
    maximum = stage_max_stat(rank_data, stage)
    return float(maximum["sum_us"]) if maximum else 0.0


def coarse_phase_maxima(rank_data):
    maxima = []
    for stage in COARSE_STAGES:
        maximum = stage_max_stat(rank_data, stage)
        if maximum is None:
            continue
        maxima.append({
            "stage": stage,
            "core": int(maximum["core"]),
            "sum_us": float(maximum["sum_us"]),
            "count": int(maximum["count"]),
        })
    return maxima


def stage_aux_us(rank_data, stage, aux_index):
    divisor = int(rank_data.get("cycle_to_us_divisor", 1))
    if divisor <= 0:
        return 0.0
    field = f"aux{aux_index}"
    return sum(int(stat.get(field, 0)) for stat in stage_stats(rank_data, stage)) / divisor


def stage_sum(rank_data, stage, core=None):
    return sum(stat["sum_us"] for stat in stage_stats(rank_data, stage, core))


def stat_counter(rank_data, stage, field):
    return sum(int(stat.get(field, 0)) for stat in stage_stats(rank_data, stage))


def role_cores(rank_data, role_name):
    cores = []
    for role in rank_data["core_roles"]:
        if role["name"] == role_name:
            cores.extend(range(role["begin"], role["begin"] + role["count"]))
    return set(cores)


def role_stat_counter(rank_data, role_name, stage, field):
    cores = role_cores(rank_data, role_name)
    return sum(int(stat.get(field, 0)) for stat in stage_stats(rank_data, stage)
               if int(stat["core"]) in cores)


def role_stage_sum(rank_data, role_name, stage):
    cores = role_cores(rank_data, role_name)
    return sum(float(stat.get("sum_us", 0.0)) for stat in stage_stats(rank_data, stage)
               if int(stat["core"]) in cores)


def send_core_ols_stats(rank_data):
    points = []
    for core in sorted(role_cores(rank_data, "send")):
        stats = stage_stats(rank_data, "send_total", core)
        if stats:
            points.append((float(core), sum(float(stat["sum_us"]) for stat in stats)))
    if not points:
        return {"slope_us_per_core": 0.0, "spread_us": 0.0, "spread_pct": 0.0}

    values = [value for _, value in points]
    mean_value = statistics.mean(values)
    spread = max(values) - min(values)
    if len(points) < 2:
        slope = 0.0
    else:
        mean_core = statistics.mean(core for core, _ in points)
        denominator = sum((core - mean_core) ** 2 for core, _ in points)
        slope = sum(
            (core - mean_core) * (value - mean_value) for core, value in points
        ) / denominator if denominator else 0.0
    return {
        "slope_us_per_core": slope,
        "spread_us": spread,
        "spread_pct": spread / mean_value * 100.0 if mean_value else 0.0,
    }


def profile_signature(rank_data):
    config = rank_data["config"]
    return (
        rank_data["max_core_count"],
        rank_data["stage_count"],
        tuple((role["name"], role["begin"], role["count"]) for role in rank_data["core_roles"]),
        int(config.get("bs", 0)),
        int(config.get("h", 0)),
        int(config.get("top_k", 0)),
        int(config.get("self_send_count", 0)),
        int(config.get("route_stride", 0)),
        int(config.get("route_seed", 0)),
        int(config.get("qp_count", 0)),
        int(config.get("doorbell_batch_size", 1)),
        int(config.get("tx_ready_batch_size", 1)),
        normalized_tx_ready_shared_flag(config),
        normalized_tx_ready_in_data(config),
        normalized_tx_meta_prefetch_full(config),
        normalized_tx_ready_early_publish(config),
        normalized_rx_ready_sticky_mask(config),
        normalized_rx_ready_batch_mte2(config),
        normalized_rx_ready_batch_vector(config),
        normalized_send_route_balanced(config),
        bool(config.get("parallel_round_publish", False)),
        bool(config.get("start_gate", False)),
        normalized_start_gate_policy(config),
        normalized_enqueue_window(config),
        str(config.get("rx_schedule", "sequential")),
        int(config.get("qdc_version", -1)),
        str(config.get("variant", "")),
        str(config.get("pair_id", "")),
        str(config.get("build_id", "")),
    )


def build_summary(ranks, coarse_ranks=None):
    coarse_by_rank = {item["rank"]: item for item in (coarse_ranks or ranks)}
    rows = []
    for rank_data in ranks:
        timing_data = coarse_by_rank.get(rank_data["rank"], rank_data)
        kernel = stage_max(timing_data, "kernel_total")
        tail = sum(stage_max(timing_data, stage) for stage in (
            "local_sender_wait", "local_self_copy_wait", "local_rx_wait",
            "round_publish", "global_round_wait"))
        udma_puts = stat_counter(rank_data, "udma_post", "count")
        doorbell_commits = stat_counter(rank_data, "udma_post", "aux1")
        round_publish_wqes = stat_counter(rank_data, "round_publish", "aux0")
        round_publish_bytes = stat_counter(rank_data, "round_publish", "aux1")
        round_publish_doorbells = stat_counter(rank_data, "round_publish", "aux2")
        round_publish_quiets = stat_counter(rank_data, "round_publish", "aux3")
        if round_publish_wqes and not round_publish_doorbells:
            round_publish_doorbells = round_publish_wqes
        if round_publish_wqes and not round_publish_quiets:
            round_publish_quiets = round_publish_wqes
        self_copies = stat_counter(rank_data, "self_copy", "count")
        send_self_copies = role_stat_counter(rank_data, "send", "self_copy", "count")
        dedicated_self_copies = role_stat_counter(rank_data, "self_copy", "self_copy", "count")
        completed_routes = udma_puts + self_copies
        send_meta_scans = role_stat_counter(rank_data, "send", "tx_meta_scan", "count")
        self_meta_scans = role_stat_counter(rank_data, "self_copy", "tx_meta_scan", "count")
        meta_scans = send_meta_scans + self_meta_scans
        ready_misses = stat_counter(rank_data, "tx_ready_poll", "aux0")
        ready_hits = stat_counter(rank_data, "tx_ready_poll", "aux1")
        rx_poll_passes = stat_counter(rank_data, "rx_flag_poll_wait", "aux0")
        rx_route_checks = stat_counter(rank_data, "rx_flag_poll_wait", "aux1")
        rx_ready_misses = stat_counter(rank_data, "rx_flag_poll_wait", "aux2")
        fine_kernel_stats = stage_stats(rank_data, "kernel_total")
        fine_critical_kernel = max(
            fine_kernel_stats, key=lambda stat: stat["sum_us"], default=None)
        fine_critical_core = int(fine_critical_kernel["core"]) if fine_critical_kernel else -1
        fine_critical_kernel_us = (
            float(fine_critical_kernel["sum_us"]) if fine_critical_kernel else 0.0)
        dcci_aggregate = stage_sum(rank_data, "dcci_total")
        all_kernel_core_time = stage_sum(rank_data, "kernel_total")
        dcci_on_critical_core = (
            stage_sum(rank_data, "dcci_total", fine_critical_core)
            if fine_critical_core >= 0 else 0.0)
        pack_rx_dcci = role_stage_sum(rank_data, "pack_rx", "dcci_total")
        pack_rx_phase = role_stage_sum(rank_data, "pack_rx", "pack_total") + \
            role_stage_sum(rank_data, "pack_rx", "receive_total")
        send_distribution = send_core_ols_stats(timing_data)
        rows.append({
            "rank": rank_data["rank"],
            "host": rank_data["host"],
            "tx_ready_shared_flag": normalized_tx_ready_shared_flag(rank_data["config"]),
            "tx_ready_in_data": normalized_tx_ready_in_data(rank_data["config"]),
            "tx_meta_prefetch_full": normalized_tx_meta_prefetch_full(rank_data["config"]),
            "tx_ready_early_publish": normalized_tx_ready_early_publish(rank_data["config"]),
            "rx_ready_sticky_mask": normalized_rx_ready_sticky_mask(rank_data["config"]),
            "rx_ready_batch_mte2": normalized_rx_ready_batch_mte2(rank_data["config"]),
            "rx_ready_batch_vector": normalized_rx_ready_batch_vector(rank_data["config"]),
            "send_route_balanced": normalized_send_route_balanced(rank_data["config"]),
            "baseline_us": timing_data["unprofiled_mean_us"],
            "fine_baseline_us": rank_data["unprofiled_mean_us"],
            "coarse_profile_host_us": timing_data["host_round_us"],
            "kernel_critical_us": kernel,
            "pack_critical_us": stage_max(timing_data, "pack_total"),
            "receive_critical_us": stage_max(timing_data, "receive_total"),
            "send_critical_us": stage_max(timing_data, "send_total"),
            "send_core_ols_slope_us_per_core": send_distribution["slope_us_per_core"],
            "send_core_spread_us": send_distribution["spread_us"],
            "send_core_spread_pct": send_distribution["spread_pct"],
            "self_copy_critical_us": stage_max(timing_data, "self_copy_total"),
            "self_copy_active_max_us": stage_max(rank_data, "self_copy"),
            "sender0_tail_us": tail,
            "dcci_critical_us": stage_max(rank_data, "dcci_total"),
            "dcci_aggregate_us": dcci_aggregate,
            "dcci_tx_data_us": stage_aux_us(rank_data, "dcci_total", 0),
            "dcci_rx_flag_us": stage_aux_us(rank_data, "dcci_total", 1),
            "dcci_rx_data_us": stage_aux_us(rank_data, "dcci_total", 2),
            "dcci_control_other_us": stage_aux_us(rank_data, "dcci_total", 3),
            "dcci_all_kernel_fraction": (
                dcci_aggregate / all_kernel_core_time if all_kernel_core_time else 0.0),
            "fine_critical_core": fine_critical_core,
            "fine_critical_kernel_us": fine_critical_kernel_us,
            "dcci_on_critical_core_us": dcci_on_critical_core,
            "dcci_critical_core_fraction": (
                dcci_on_critical_core / fine_critical_kernel_us
                if fine_critical_kernel_us else 0.0),
            "dcci_pack_receive_fraction": pack_rx_dcci / pack_rx_phase if pack_rx_phase else 0.0,
            "meta_scans": meta_scans,
            "send_meta_scans": send_meta_scans,
            "self_meta_scans": self_meta_scans,
            "completed_routes": completed_routes,
            "meta_scan_amplification": meta_scans / completed_routes if completed_routes else 0.0,
            "send_meta_scan_amplification": send_meta_scans / (udma_puts + send_self_copies)
                if udma_puts + send_self_copies else 0.0,
            "self_meta_scan_amplification": self_meta_scans / dedicated_self_copies
                if dedicated_self_copies else 0.0,
            "ready_miss_ratio": ready_misses / (ready_misses + ready_hits) if ready_misses + ready_hits else 0.0,
            "udma_puts": udma_puts,
            "udma_bytes": stat_counter(rank_data, "udma_post", "aux0"),
            "doorbell_commits": doorbell_commits,
            "wqes_per_doorbell": udma_puts / doorbell_commits if doorbell_commits else 0.0,
            "active_send_sqs": stat_counter(rank_data, "udma_post", "aux3"),
            "round_publish_wqes": round_publish_wqes,
            "round_publish_bytes": round_publish_bytes,
            "round_publish_doorbells": round_publish_doorbells,
            "round_publish_quiets": round_publish_quiets,
            "all_udma_wqes": udma_puts + round_publish_wqes,
            "all_doorbell_commits": doorbell_commits + round_publish_doorbells,
            "rx_poll_passes": rx_poll_passes,
            "rx_route_checks": rx_route_checks,
            "rx_ready_misses": rx_ready_misses,
            "rx_ready_miss_ratio": (
                rx_ready_misses / rx_route_checks if rx_route_checks else 0.0),
            "rx_bypassed_tokens": stat_counter(rank_data, "rx_output", "aux1"),
            "rx_out_of_order_completions": stat_counter(rank_data, "rx_output", "aux2"),
            "self_copies": self_copies,
            "self_copy_bytes": stat_counter(rank_data, "self_copy", "aux0"),
        })
    return rows


def median(rows, key, include_zero=False):
    values = [
        float(row[key]) for row in rows
        if (float(row[key]) >= 0 if include_zero else float(row[key]) > 0)
    ]
    return statistics.median(values) if values else 0.0


def median_all(rows, key):
    values = [float(row[key]) for row in rows]
    return statistics.median(values) if values else 0.0


def write_csv_files(root, ranks, summary):
    with (root / "combine_core_stage.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow([
            "rank", "host", "core", "stage", "count", "sum_us", "max_us",
            "start_us", "end_us", "aux0", "aux1", "aux2", "aux3",
        ])
        for rank_data in ranks:
            for stat in rank_data["stats"]:
                writer.writerow([
                    rank_data["rank"], rank_data["host"], stat["core"], stat["stage"], stat["count"],
                    stat["sum_us"], stat["max_us"], stat["start_us"], stat["end_us"],
                    stat.get("aux0", 0), stat.get("aux1", 0), stat.get("aux2", 0), stat.get("aux3", 0),
                ])
    with (root / "combine_rank_summary.csv").open("w", newline="", encoding="utf-8") as handle:
        if not summary:
            return
        writer = csv.DictWriter(handle, fieldnames=list(summary[0].keys()))
        writer.writeheader()
        writer.writerows(summary)


def write_analysis(root, ranks, summary, sample_count=1,
                   representative_launch="launch0", kernel_median_us=0.0, samples=None,
                   total_kernel_median_us=None, coarse_kernel_median_us=None,
                   fine_kernel_median_us=None):
    baseline = median(summary, "baseline_us")
    fine_baseline = median(summary, "fine_baseline_us")
    coarse_profile = median(summary, "coarse_profile_host_us")
    fine_values = [rank["host_round_us"] for rank in ranks if rank["host_round_us"] > 0]
    fine_profile = statistics.median(fine_values) if fine_values else 0.0
    has_dedicated_self = bool(role_cores(ranks[0], "self_copy"))
    enqueue_window = normalized_enqueue_window(ranks[0]["config"])
    host_timing_note = host_timing_interpretation(enqueue_window)
    coarse_intrusion = (coarse_profile / baseline - 1.0) * 100.0 if baseline else 0.0
    fine_intrusion = (fine_profile / fine_baseline - 1.0) * 100.0 if fine_baseline else 0.0
    role_text = " + ".join(f"{role['count']} {role['name']}" for role in ranks[0]["core_roles"])
    samples = samples or []
    executed_gate_samples = [
        sample for sample in samples if sample.get("start_gate_executed", False)]
    executed_gate_us = [float(sample.get("start_gate_max_us", 0.0))
                        for sample in executed_gate_samples]
    executed_gate_wqes = [int(sample.get("start_gate_wqes_total", 0))
                          for sample in executed_gate_samples]
    executed_gate_doorbells = [int(sample.get("start_gate_doorbells_total", 0))
                               for sample in executed_gate_samples]
    executed_gate_quiets = [int(sample.get("start_gate_quiets_total", 0))
                            for sample in executed_gate_samples]
    coarse_kernel_median_us = (
        kernel_median_us if coarse_kernel_median_us is None else coarse_kernel_median_us)
    fine_kernel_median_us = (
        kernel_median_us if fine_kernel_median_us is None else fine_kernel_median_us)
    headline_scope = (
        "total-only strict-equivalent profile" if total_kernel_median_us is not None else
        "coarse attribution profile")
    lines = [
        "# TileXR EP URMA Combine Profile",
        "",
        f"- Ranks captured: {len(ranks)}",
        f"- Core roles: {role_text}",
        (f"- Shared ready flag: "
         f"{'on' if normalized_tx_ready_shared_flag(ranks[0]['config']) else 'off'}"),
        (f"- TX ready in data: "
         f"{'on' if normalized_tx_ready_in_data(ranks[0]['config']) else 'off'}"),
        (f"- TX metadata full prefetch: "
         f"{'on' if normalized_tx_meta_prefetch_full(ranks[0]['config']) else 'off'}"),
        (f"- TX early ready publish: "
         f"{'on' if normalized_tx_ready_early_publish(ranks[0]['config']) else 'off'}"),
        (f"- RX sticky ready mask: "
         f"{'on' if normalized_rx_ready_sticky_mask(ranks[0]['config']) else 'off'}"),
        (f"- RX batched MTE2 ready reads: "
         f"{'on' if normalized_rx_ready_batch_mte2(ranks[0]['config']) else 'off'}"),
        (f"- RX Vector ready reduction: "
         f"{'on' if normalized_rx_ready_batch_vector(ranks[0]['config']) else 'off'}"),
        (f"- Statistic scope: {sample_count} paired distributed launches; per launch take max core "
         "per rank, then max rank; headline is the median across launches"),
        (f"- Kernel timing boundary: "
         f"{ranks[0].get('kernel_timing_boundary', 'legacy_unbracketed')}"),
        f"- Kernel headline source: {headline_scope}",
        f"- Headline max-core -> max-rank -> launch median: {kernel_median_us:.3f} us",
        f"- Coarse attribution kernel median: {coarse_kernel_median_us:.3f} us",
        f"- Fine attribution kernel median: {fine_kernel_median_us:.3f} us",
        f"- Representative detailed launch: {representative_launch} (nearest the headline median)",
        (f"- Start Gate executed launches: {len(executed_gate_samples)}/{len(samples)}; "
         f"executed-only medians: latency="
         f"{statistics.median(executed_gate_us) if executed_gate_us else 0.0:.3f} us, "
         f"WQEs={statistics.median(executed_gate_wqes) if executed_gate_wqes else 0.0:.3f}, "
         f"doorbells={statistics.median(executed_gate_doorbells) if executed_gate_doorbells else 0.0:.3f}, "
         f"quiets={statistics.median(executed_gate_quiets) if executed_gate_quiets else 0.0:.3f}"),
    ]
    if host_timing_note:
        lines.append(f"- Host timing contract: {host_timing_note}")
    lines.extend([
        f"- Cross-rank median unprofiled host round: {baseline:.3f} us",
        f"- Cross-rank median fine-run unprofiled host round: {fine_baseline:.3f} us",
        f"- Cross-rank median coarse-profile host round: {coarse_profile:.3f} us "
        f"({coarse_intrusion:+.2f}% difference; treat a negative value as run-to-run noise)",
        f"- Cross-rank median fine-profile host round: {fine_profile:.3f} us ({fine_intrusion:+.2f}% intrusion)",
        (f"- Representative-launch cross-rank median device critical path: "
         f"{median(summary, 'kernel_critical_us'):.3f} us"),
        f"- Cross-rank median critical Pack phase: {median(summary, 'pack_critical_us'):.3f} us",
        f"- Cross-rank median critical Receive phase: {median(summary, 'receive_critical_us'):.3f} us",
        f"- Cross-rank median critical Send phase: {median(summary, 'send_critical_us'):.3f} us",
        f"- Cross-rank median coarse Send-core OLS slope: "
        f"{median_all(summary, 'send_core_ols_slope_us_per_core'):.3f} us/core",
        f"- Cross-rank median coarse Send-core spread: "
        f"{median(summary, 'send_core_spread_us', include_zero=True):.3f} us",
        f"- Cross-rank median coarse Send-core spread / mean: "
        f"{median(summary, 'send_core_spread_pct', include_zero=True):.3f}%",
        f"- Cross-rank median WQEs per doorbell: {median(summary, 'wqes_per_doorbell'):.3f}",
        f"- Cross-rank median Round Publish control WQEs: "
        f"{median(summary, 'round_publish_wqes', include_zero=True):.3f}",
        f"- Cross-rank median Round Publish control doorbells: "
        f"{median(summary, 'round_publish_doorbells', include_zero=True):.3f}",
        f"- Cross-rank median Round Publish completion waits: "
        f"{median(summary, 'round_publish_quiets', include_zero=True):.3f}",
        f"- Cross-rank median RX flag checks: {median(summary, 'rx_route_checks', include_zero=True):.3f}",
        f"- Cross-rank median RX flag miss ratio: "
        f"{median(summary, 'rx_ready_miss_ratio', include_zero=True) * 100.0:.3f}%",
        f"- Cross-rank median RX bypassed tokens: {median(summary, 'rx_bypassed_tokens', include_zero=True):.3f}",
        f"- Cross-rank median RX out-of-order completions: "
        f"{median(summary, 'rx_out_of_order_completions', include_zero=True):.3f}",
    ])
    if has_dedicated_self:
        lines.append(f"- Cross-rank median dedicated SelfCopy phase: "
                     f"{median(summary, 'self_copy_critical_us'):.3f} us")
    lines.extend([
        f"- Cross-rank median active SelfCopy substage max: "
        f"{median(summary, 'self_copy_active_max_us'):.3f} us",
        f"- Cross-rank median sender0 tail: {median(summary, 'sender0_tail_us'):.3f} us",
        f"- Cross-rank median max per-core explicit DCCI span: "
        f"{median(summary, 'dcci_critical_us', include_zero=True):.3f} us",
        f"- Cross-rank median aggregate explicit DCCI core-time: "
        f"{median(summary, 'dcci_aggregate_us', include_zero=True):.3f} us "
        f"(tx_data={median(summary, 'dcci_tx_data_us', include_zero=True):.3f}, "
        f"rx_flag={median(summary, 'dcci_rx_flag_us', include_zero=True):.3f}, "
        f"rx_data={median(summary, 'dcci_rx_data_us', include_zero=True):.3f}, "
        f"control_other={median(summary, 'dcci_control_other_us', include_zero=True):.3f})",
        f"- Cross-rank median explicit DCCI share of all AIV kernel core-time: "
        f"{median(summary, 'dcci_all_kernel_fraction', include_zero=True) * 100.0:.2f}%",
        f"- Cross-rank median explicit DCCI share on the fine-profile critical core: "
        f"{median(summary, 'dcci_critical_core_fraction', include_zero=True) * 100.0:.2f}% "
        f"(dcci={median(summary, 'dcci_on_critical_core_us', include_zero=True):.3f} us, "
        f"kernel={median(summary, 'fine_critical_kernel_us', include_zero=True):.3f} us)",
        f"- Cross-rank median explicit DCCI share of Pack+Receive core-time: "
        f"{median(summary, 'dcci_pack_receive_fraction', include_zero=True) * 100.0:.2f}%",
        f"- Cross-rank median metadata scan amplification: "
        f"{median(summary, 'meta_scan_amplification', include_zero=True):.3f}x",
        f"- Cross-rank median Send metadata scan amplification: "
        f"{median(summary, 'send_meta_scan_amplification', include_zero=True):.3f}x",
    ])
    if has_dedicated_self:
        lines.append(f"- Cross-rank median SelfCopy metadata scan amplification: "
                     f"{median(summary, 'self_meta_scan_amplification', include_zero=True):.3f}x")
    lines.extend([
        f"- Cross-rank median TxReady attempt miss ratio: "
        f"{median(summary, 'ready_miss_ratio', include_zero=True) * 100.0:.3f}%",
        "",
        "The HTML coarse timeline uses first/last activity envelopes. Its receive_total composition comes from",
        "same-core fine exposed wall-time while the double-buffer pipeline remains enabled; the colored parts are not",
        "a chronological sequence and must not be added to infer hardware-engine occupancy.",
        "The dequant+clear interval also contains next-route unpack submission bookkeeping, but not its asynchronous",
        "in-flight transfer time.",
        "Repeated fine stages are compared by sum_us in the heatmap, and their envelope must not be interpreted as",
        "continuous execution.",
        "Explicit DCCI spans cover combine-kernel UDMACleanCacheLines calls but exclude surrounding barriers and the",
        "SQ/CQ cache maintenance inside UDMA post/quiet. Category totals are accumulated core-time.",
        "The all-AIV ratio describes aggregate kernel core-time; the critical-core ratio is the latency-oriented",
        "view. Neither ratio is a direct prediction of speedup because AIVs execute concurrently and synchronize.",
        "Device clocks are normalized independently per rank, so cross-rank absolute event ordering is intentionally",
        "not inferred. Fine-stage durations include material instrumentation overhead.",
    ])
    (root / "combine_analysis.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def render_html(ranks, summary, report_context=None, show_start_gate=False):
    rank_count = len(ranks)
    core_count = ranks[0]["max_core_count"]
    report_context = report_context or build_report_context(None, ranks)
    payload_ranks = []
    for rank in ranks:
        payload_rank = dict(rank)
        coarse_rank = dict(rank)
        coarse_rank["stats"] = rank.get("coarse_stats", rank["stats"])
        payload_rank["coarse_phase_maxima"] = coarse_phase_maxima(coarse_rank)
        payload_ranks.append(payload_rank)
    coarse_stats = [stat for rank in payload_ranks
                    for stat in rank.get("coarse_stats", rank["stats"])]
    fine_stats = [stat for rank in payload_ranks for stat in rank["stats"]]
    hidden_stages = set() if show_start_gate else {"start_gate"}
    active_coarse_stages = [stage for stage in COARSE_STAGES
                            if stage not in hidden_stages and
                            any(stat["stage"] == stage for stat in coarse_stats)]
    active_fine_stages = [stage for stage in FINE_STAGES
                          if stage not in hidden_stages and
                          any(stat["stage"] == stage for stat in fine_stats)]
    payload = json.dumps({
        "ranks": payload_ranks,
        "summary": summary,
        "coarseStages": active_coarse_stages,
        "fineStages": active_fine_stages,
        "showStartGate": show_start_gate,
        "colors": COLORS,
    }, separators=(",", ":")).replace("&", "\\u0026").replace("<", "\\u003c").replace(
        ">", "\\u003e").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
    design = html.escape(report_context["design"])
    run_label = html.escape(report_context["run"])
    role_text = html.escape(report_context["role_text"])
    shape_text = html.escape(report_context["shape_text"])
    route_text = html.escape(report_context["route_text"])
    transport_text = html.escape(report_context["transport_text"])
    sample_count = int(report_context.get("sample_count", 1))
    representative_launch = html.escape(
        str(report_context.get("representative_launch", "launch0")))
    kernel_median_us = float(report_context.get("kernel_median_us", 0.0))
    kernel_headline_scope = html.escape(str(
        report_context.get("kernel_headline_scope", "coarse attribution profile")))
    coarse_kernel_median_us = float(
        report_context.get("coarse_kernel_median_us", kernel_median_us))
    fine_kernel_median_us = float(
        report_context.get("fine_kernel_median_us", kernel_median_us))
    start_gate_executed_launches = int(report_context.get("start_gate_executed_launches", 0))
    start_gate_total_launches = int(report_context.get("start_gate_total_launches", sample_count))
    start_gate_executed_only_median_us = float(
        report_context.get("start_gate_executed_only_median_us", 0.0))
    start_gate_executed_only_median_wqes = float(
        report_context.get("start_gate_executed_only_median_wqes", 0.0))
    start_gate_executed_only_median_doorbells = float(
        report_context.get("start_gate_executed_only_median_doorbells", 0.0))
    enqueue_window = int(report_context.get("enqueue_window", 1))
    host_timing_note = host_timing_interpretation(enqueue_window)
    host_timing_html = (
        f'<div class="meta"><strong>Host timing contract:</strong> '
        f'{html.escape(host_timing_note)}</div>' if host_timing_note else ""
    )
    page_title = (
        f"TileXR EP URMA Combine - {design} - {role_text} - {transport_text} - {run_label}")
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{page_title}</title>
<style>
:root{{--ink:#172033;--muted:#64748b;--line:#cbd5e1;--bg:#f8fafc;--panel:#fff}}
*{{box-sizing:border-box}} body{{margin:0;background:var(--bg);color:var(--ink);font:13px/1.4 Arial,sans-serif}}
header{{padding:18px 24px;background:#fff;border-bottom:1px solid var(--line);display:flex;align-items:start;justify-content:space-between;gap:20px}}
h1{{font-size:22px;margin:0}} h2{{font-size:16px;margin:0 0 8px}} .sub,.meta,.section-note{{color:var(--muted);margin-top:3px}}
.eyebrow{{font-size:11px;font-weight:700;color:#334155;margin-bottom:3px}} .meta{{font-size:12px}}
main{{padding:0 24px 32px}} section{{padding:18px 0;border-bottom:1px solid var(--line)}}
select{{height:32px;border:1px solid #94a3b8;border-radius:4px;background:#fff;padding:0 28px 0 8px}}
.kpis{{display:grid;grid-template-columns:repeat(7,minmax(130px,1fr));gap:8px}}
.kpi{{background:#fff;border:1px solid var(--line);border-radius:4px;padding:10px}} .kpi b{{display:block;font-size:18px;margin-top:3px}}
.legend{{display:flex;gap:12px;flex-wrap:wrap;margin:8px 0}} .swatch{{width:10px;height:10px;display:inline-block;margin-right:4px}}
.scroll{{overflow:auto;border:1px solid var(--line);background:#fff}}
.timeline{{min-width:1000px;padding:8px 0}} .lane{{height:23px;display:grid;grid-template-columns:150px 1fr;border-bottom:1px solid #eef2f7}}
.lane-label{{padding:3px 8px;font-family:monospace;border-right:1px solid var(--line);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}} .track{{position:relative}}
.bar{{position:absolute;top:3px;height:16px;min-width:1px;border:1px solid rgba(15,23,42,.3);overflow:hidden;white-space:nowrap;font-size:10px;padding-left:2px;color:#fff}}
.phase-max{{outline:2px solid #111827;outline-offset:-1px;z-index:2}}
.receive-total{{display:flex;padding-left:0;background:#fff}} .receive-part{{height:100%;overflow:hidden;white-space:nowrap;padding-left:2px}}
.axis{{height:25px;display:grid;grid-template-columns:150px 1fr}} .axis-track{{position:relative;border-bottom:1px solid var(--line)}}
.tick{{position:absolute;height:100%;border-left:1px solid #94a3b8;font-size:10px;padding-left:3px;color:var(--muted)}}
table{{border-collapse:collapse;width:100%;background:#fff}} th,td{{border:1px solid var(--line);padding:5px 7px;text-align:right;white-space:nowrap}}
th{{position:sticky;top:0;background:#e2e8f0;z-index:1}} th:first-child,td:first-child{{text-align:left;position:sticky;left:0;background:#fff;z-index:2}}
.heat td{{font-size:10px;min-width:74px}} .role-sep td{{border-top:3px solid #475569}}
.phase-row{{display:grid;grid-template-columns:260px 1fr 90px;align-items:center;gap:8px;margin:5px 0}}
.phase-label{{display:flex;align-items:baseline;justify-content:space-between;gap:8px;min-width:0}} .phase-source{{color:var(--muted);font-size:11px;white-space:nowrap}}
.phase-track{{height:14px;background:#e2e8f0}} .phase-fill{{height:100%}}
.note{{color:var(--muted);max-width:1100px}}
@media(max-width:900px){{.kpis{{grid-template-columns:repeat(2,1fr)}} header{{align-items:start;gap:12px;flex-direction:column}}}}
</style>
</head>
<body>
<header><div><div class="eyebrow">{run_label}</div><h1>TileXR EP URMA Combine &middot; {design} &middot; {role_text} &middot; {transport_text}</h1>
<div class="sub">{rank_count} ranks &middot; {core_count} AIV &middot; URMA &middot; {sample_count} paired profile samples &middot; representative {representative_launch} &middot; median cluster kernel {kernel_median_us:.2f} us ({kernel_headline_scope}) &middot; coarse/fine attribution medians {coarse_kernel_median_us:.2f}/{fine_kernel_median_us:.2f} us &middot; Gate executed {start_gate_executed_launches}/{start_gate_total_launches}, executed-only median {start_gate_executed_only_median_us:.2f} us / {start_gate_executed_only_median_wqes:.0f} WQEs / {start_gate_executed_only_median_doorbells:.0f} DBs</div>
<div class="meta">{shape_text} &middot; INT8 per-route quantization &middot; {route_text} &middot; start gate {'shown proportionally' if show_start_gate else 'collapsed from steady-state views'}</div>{host_timing_html}</div>
<label>Selected rank <select id="rankSelect"></select></label></header>
<main>
<section><div class="kpis" id="kpis"></div></section>
<section><h2 id="phaseTitle">Coarse Stage Maxima Across AIVs</h2><p class="section-note">Each value is the maximum coarse <code>sum_us</code> for that stage across the selected rank's AIVs. Maxima can come from different cores, overlap in time, and must not be added as a critical path.</p><div id="phaseBars"></div></section>
<section><h2>Fine Explicit DCCI Breakdown</h2><p class="section-note">Fine diagnostic capture; not the coarse timing launch.</p><div class="scroll"><table id="dcciTable"></table></div></section>
<section><h2 id="timelineTitle">{core_count}-Core Coarse Timeline</h2><p class="section-note">Outlined bars are the exact per-stage maxima listed above. Receive bar width and position are coarse; its internal colors are fine-derived composition, not chronology. {'The axis includes the start gate.' if show_start_gate else 'The steady-state axis is rebased after the collapsed start gate.'}</p><div class="legend" id="legend"></div><div class="scroll"><div class="timeline" id="timeline"></div></div></section>
<section><h2>Core x Fine Exposed-Time Heatmap</h2><p class="section-note">Fine diagnostic capture; absolute durations include instrumentation overhead.</p><div class="scroll"><table class="heat" id="heatmap"></table></div></section>
<section><h2>Fine Route Transport AIV Diagnostics</h2><p class="section-note">Fine diagnostic capture; route counters and exposed stage time are not from the coarse launch.</p><div class="scroll"><table id="sendTable"></table></div></section>
<section><h2>Rank Critical Paths</h2><div class="scroll"><table id="rankTable"></table></div></section>
<section class="note">The total-only capture provides the timing headline when supplied; it disables stage attribution inside the measured kernel. Coarse bars are per-core activity envelopes for attribution. Each receive_total envelope is divided into RX flag wait, unpack exposed wait, dequant+clear, and output using the same core's fine exposed wall-time. Fine profiling preserves the quant/dequant double-buffer pipeline: hidden asynchronous transfer time is not charged to an exposed-wait stage. Dequant+clear includes next-route unpack submission bookkeeping, but not that transfer's asynchronous in-flight time. Explicit DCCI spans cover combine-kernel UDMACleanCacheLines calls and exclude their surrounding barriers; SQ/CQ cache maintenance inside the UDMA helper remains part of UDMA post/quiet. DCCI category values are accumulated core-time, not additional coarse wall-time. The colored parts show composition, not chronological ordering, and must not be added to infer hardware-engine occupancy. Legacy fine captures without rx_unpack_wait predate this contract. Repeated fine stages use accumulated exposed wall-time in the heatmap, and their durations include material instrumentation overhead. The detailed timeline uses {representative_launch}, selected as the coarse sample nearest the median after maximum core per rank and maximum rank per launch. The sample CSV contains all {sample_count} paired launches. Each rank is independently normalized; the report does not assume synchronized device clocks across NPUs. Open perfetto_trace.json for the standard TileXR drill-down.</section>
</main>
<script>
const DATA={payload};
const fmt=v=>Number(v||0).toFixed(2);
const statMap=rank=>{{const m=new Map();for(const s of rank.stats)m.set(`${{s.core}}:${{s.stage}}`,s);return m}};
function rgba(hex,a){{const n=parseInt(hex.slice(1),16);return `rgba(${{n>>16}},${{(n>>8)&255}},${{n&255}},${{a}})`}}
function selected(){{return DATA.ranks.find(r=>r.rank===Number(document.getElementById('rankSelect').value))}}
function coreRole(rank,core){{const role=rank.core_roles.find(r=>core>=r.begin&&core<r.begin+r.count);if(!role)return{{name:'unknown',label:'Unknown'}};const index=core-role.begin;return{{name:role.name,label:role.name==='pack_rx'?'Pack/Rx':role.name==='send'?`Send${{index}}`:'SelfCopy'}}}}
function render(){{const rank=selected();const summary=DATA.summary.find(r=>r.rank===rank.rank);const map=statMap(rank);const timing={{...rank,stats:rank.coarse_stats||rank.stats}};const maxima=rank.coarse_phase_maxima||[];renderKpis(summary);renderPhases(timing,maxima);renderTimeline(timing,statMap(timing),map,maxima);renderHeatmap(rank,map);renderDcci(rank,map);renderTransport(rank,map)}}
function renderKpis(s){{const items=[['Baseline (coarse run)',fmt(s.baseline_us)+' us'],['Coarse profile host',fmt(s.coarse_profile_host_us)+' us'],['Kernel critical (coarse)',fmt(s.kernel_critical_us)+' us'],['DCCI max core (fine)',fmt(s.dcci_critical_us)+' us'],['DCCI / kernel (fine)',fmt(s.dcci_all_kernel_fraction*100)+'%'],['TxReady miss (fine)',fmt(s.ready_miss_ratio*100)+'%'],['UDMA WQEs (fine)',s.udma_puts],['WQE / DB (fine)',fmt(s.wqes_per_doorbell)],['RX flag checks (fine)',s.rx_route_checks],['RX flag miss (fine)',fmt(s.rx_ready_miss_ratio*100)+'%'],['RX bypasses (fine)',s.rx_bypassed_tokens],['RX OOO completions (fine)',s.rx_out_of_order_completions]];document.getElementById('kpis').innerHTML=items.map(x=>`<div class="kpi">${{x[0]}}<b>${{x[1]}}</b></div>`).join('')}}
function renderPhases(rank,maxima){{const byStage=new Map(maxima.map(item=>[item.stage,item]));const shown=DATA.coarseStages.map(stage=>byStage.get(stage)).filter(Boolean);const max=Math.max(1,...shown.map(item=>item.sum_us));document.getElementById('phaseTitle').textContent=`rank${{rank.rank}} Coarse Stage Maxima Across AIVs`;document.getElementById('phaseBars').innerHTML=shown.map(item=>`<div class="phase-row" title="${{item.stage}} coarse max active time on core${{item.core}}; this row is not additive with other stages"><div class="phase-label"><code>${{item.stage}}</code><span class="phase-source">core${{item.core}} ${{coreRole(rank,item.core).label}}</span></div><div class="phase-track"><div class="phase-fill" style="width:${{item.sum_us/max*100}}%;background:${{DATA.colors[item.stage]}}"></div></div><b>${{fmt(item.sum_us)}} us</b></div>`).join('')}}
function receiveParts(fineMap,core){{const flagWait=fineMap.get(`${{core}}:rx_flag_poll_wait`)||{{}},unpackWait=fineMap.get(`${{core}}:rx_unpack_wait`)||{{}},dequant=fineMap.get(`${{core}}:rx_unpack_dequant_clear`)||{{}},output=fineMap.get(`${{core}}:rx_output`)||{{}};const flagWaitUs=Math.max(0,Number(flagWait.sum_us)||0),unpackWaitUs=Math.max(0,Number(unpackWait.sum_us)||0),dequantUs=Math.max(0,Number(dequant.sum_us)||0),outputUs=Math.max(0,Number(output.sum_us)||0),total=flagWaitUs+unpackWaitUs+dequantUs+outputUs;return total>0?{{flagWait,unpackWait,dequant,output,flagWaitUs,unpackWaitUs,dequantUs,outputUs,total}}:null}}
function receiveBar(s,left,width,parts,isMax){{const maxClass=isMax?' phase-max':'';if(!parts)return`<div class="bar${{maxClass}}" style="left:${{left}}%;width:${{width}}%;background:${{DATA.colors.receive_total}}" title="receive_total coarse span=${{fmt(s.end_us-s.start_us)}}us active=${{fmt(s.sum_us)}}us count=${{s.count}}${{isMax?'; stage maximum shown above':''}}">receive_total</div>`;const flagPct=parts.flagWaitUs/parts.total*100,unpackPct=parts.unpackWaitUs/parts.total*100,dequantPct=parts.dequantUs/parts.total*100,outputPct=parts.outputUs/parts.total*100,span=s.end_us-s.start_us,common=`receive_total coarse span=${{fmt(span)}}us; internal composition from same-core fine exposed wall-time; not chronological${{isMax?'; stage maximum shown above':''}}`;return`<div class="bar receive-total${{maxClass}}" style="left:${{left}}%;width:${{width}}%" title="${{common}}"><div class="receive-part" style="width:${{flagPct}}%;background:${{DATA.colors.rx_flag_poll_wait}}" title="RX flag wait: ${{fmt(flagPct)}}%; fine exposed=${{fmt(parts.flagWaitUs)}}us count=${{parts.flagWait.count||0}}">flag wait</div><div class="receive-part" style="width:${{unpackPct}}%;background:${{DATA.colors.rx_unpack_wait}}" title="Unpack exposed wait: ${{fmt(unpackPct)}}%; fine exposed=${{fmt(parts.unpackWaitUs)}}us count=${{parts.unpackWait.count||0}}">unpack wait</div><div class="receive-part" style="width:${{dequantPct}}%;background:${{DATA.colors.rx_unpack_dequant_clear}}" title="Dequant+clear: ${{fmt(dequantPct)}}%; fine exposed=${{fmt(parts.dequantUs)}}us count=${{parts.dequant.count||0}}; includes next unpack submission, excludes async in-flight time">dequant+clear</div><div class="receive-part" style="width:${{outputPct}}%;background:${{DATA.colors.rx_output}}" title="Output: ${{fmt(outputPct)}}%; fine exposed=${{fmt(parts.outputUs)}}us count=${{parts.output.count||0}}">output</div></div>`}}
function renderTimeline(rank,map,fineMap,maxima){{const visible=rank.stats.filter(s=>DATA.coarseStages.includes(s.stage)),origin=DATA.showStartGate?0:Math.min(...visible.map(s=>s.start_us)),end=Math.max(1,...visible.map(s=>s.end_us))-origin,maxSet=new Set(maxima.map(item=>`${{item.core}}:${{item.stage}}`));document.getElementById('timelineTitle').textContent=`rank${{rank.rank}} ${{rank.max_core_count}}-Core ${{DATA.showStartGate?'Full':'Steady-State'}} Coarse Timeline`;let html='<div class="axis"><div></div><div class="axis-track">';for(let i=0;i<=5;i++)html+=`<span class="tick" style="left:${{i*20}}%">${{fmt(end*i/5)}} us</span>`;html+='</div></div>';for(let core=0;core<rank.max_core_count;core++){{html+=`<div class="lane"><div class="lane-label">core${{core}} ${{coreRole(rank,core).label}}</div><div class="track">`;for(const stage of DATA.coarseStages){{const s=map.get(`${{core}}:${{stage}}`);if(!s)continue;const left=Math.max(0,(s.start_us-origin)/end*100),width=Math.max(.12,(s.end_us-s.start_us)/end*100),isMax=maxSet.has(`${{core}}:${{stage}}`),maxClass=isMax?' phase-max':'';html+=stage==='receive_total'?receiveBar(s,left,width,receiveParts(fineMap,core),isMax):`<div class="bar${{maxClass}}" style="left:${{left}}%;width:${{width}}%;background:${{DATA.colors[stage]}}" title="${{stage}} coarse span=${{fmt(s.end_us-s.start_us)}}us active=${{fmt(s.sum_us)}}us count=${{s.count}}${{isMax?'; stage maximum shown above':''}}">${{stage}}</div>`}}html+='</div></div>'}}document.getElementById('timeline').innerHTML=html;const labels={{rx_flag_poll_wait:'RX flag wait (fine composition)',rx_unpack_wait:'unpack wait (fine composition)',rx_unpack_dequant_clear:'dequant+clear (fine composition)',rx_output:'output (fine composition)'}};const legendStages=DATA.coarseStages.flatMap(s=>s==='receive_total'?['rx_flag_poll_wait','rx_unpack_wait','rx_unpack_dequant_clear','rx_output']:s);document.getElementById('legend').innerHTML=legendStages.map(s=>`<span><i class="swatch" style="background:${{DATA.colors[s]}}"></i>${{labels[s]||s}}</span>`).join('')+'<span><i class="swatch" style="background:#fff;border:2px solid #111827"></i>stage maximum shown above</span>'}}
function renderHeatmap(rank,map){{let html='<thead><tr><th>Core</th>'+DATA.fineStages.map(s=>`<th>${{s}}</th>`).join('')+'</tr></thead><tbody>';const maxByStage={{}};const begins=new Set(rank.core_roles.filter(r=>r.begin>0).map(r=>r.begin));for(const stage of DATA.fineStages)maxByStage[stage]=Math.max(1,...rank.stats.filter(s=>s.stage===stage).map(s=>s.sum_us));for(let core=0;core<rank.max_core_count;core++){{html+=`<tr class="${{begins.has(core)?'role-sep':''}}"><td>core${{core}} ${{coreRole(rank,core).label}}</td>`;for(const stage of DATA.fineStages){{const s=map.get(`${{core}}:${{stage}}`),v=s?s.sum_us:0,a=v?(.16+.84*v/maxByStage[stage]):0;html+=`<td style="background:${{v?rgba(DATA.colors[stage]||'#64748b',a):'#fff'}}" title="count=${{s?s.count:0}} max=${{fmt(s?s.max_us:0)}}us aux=${{s?[s.aux0,s.aux1,s.aux2,s.aux3].join('/'):'0/0/0/0'}}">${{v?fmt(v):''}}</td>`}}html+='</tr>'}}document.getElementById('heatmap').innerHTML=html+'</tbody>'}}
function renderDcci(rank,map){{const divisor=Math.max(1,Number(rank.cycle_to_us_divisor)||1);let body='';for(let core=0;core<rank.max_core_count;core++){{const s=map.get(`${{core}}:dcci_total`);if(!s)continue;body+=`<tr><td>${{coreRole(rank,core).label}}</td><td>core${{core}}</td><td>${{s.count||0}}</td><td>${{fmt(s.sum_us)}}</td><td>${{fmt((s.aux0||0)/divisor)}}</td><td>${{fmt((s.aux1||0)/divisor)}}</td><td>${{fmt((s.aux2||0)/divisor)}}</td><td>${{fmt((s.aux3||0)/divisor)}}</td></tr>`}}if(!body)body='<tr><td colspan="8">No explicit DCCI samples in this capture</td></tr>';document.getElementById('dcciTable').innerHTML='<thead><tr><th>Role</th><th>Core</th><th>Calls</th><th>Total us</th><th>Tx data us</th><th>RX flag us</th><th>RX data us</th><th>Control/other us</th></tr></thead><tbody>'+body+'</tbody>'}}
function renderTransport(rank,map){{let html='<thead><tr><th>Role</th><th>Core</th><th>Total us</th><th>Meta scans</th><th>Local routes</th><th>Remote routes</th><th>Ready checks</th><th>Ready misses</th><th>Self copies</th><th>Self bytes</th><th>UDMA WQEs</th><th>UDMA bytes</th><th>DB commits</th><th>WQE / DB</th><th>Batch target</th><th>Active SQs</th><th>Quiet us</th></tr></thead><tbody>';for(const role of rank.core_roles.filter(r=>r.name==='send'||r.name==='self_copy'))for(let core=role.begin;core<role.begin+role.count;core++){{const meta=map.get(`${{core}}:tx_meta_scan`)||{{}},ready=map.get(`${{core}}:tx_ready_poll`)||{{}},self=map.get(`${{core}}:self_copy`)||{{}},post=map.get(`${{core}}:udma_post`)||{{}},quiet=map.get(`${{core}}:udma_quiet`)||{{}},total=map.get(`${{core}}:${{role.name==='send'?'send_total':'self_copy_total'}}`)||{{}},localRoutes=role.name==='send'?(meta.aux1||0):(meta.aux2||0),remoteRoutes=role.name==='send'?(meta.aux2||0):(meta.aux1||0),doorbells=post.aux1||0,wqesPerDoorbell=doorbells?(post.count||0)/doorbells:0;html+=`<tr><td>${{coreRole(rank,core).label}}</td><td>core${{core}}</td><td>${{fmt(total.sum_us)}}</td><td>${{meta.count||0}}</td><td>${{localRoutes}}</td><td>${{remoteRoutes}}</td><td>${{ready.count||0}}</td><td>${{ready.aux0||0}}</td><td>${{self.count||0}}</td><td>${{self.aux0||0}}</td><td>${{post.count||0}}</td><td>${{post.aux0||0}}</td><td>${{doorbells}}</td><td>${{fmt(wqesPerDoorbell)}}</td><td>${{post.aux2||1}}</td><td>${{post.aux3||0}}</td><td>${{fmt(quiet.sum_us)}}</td></tr>`}}document.getElementById('sendTable').innerHTML=html+'</tbody>'}}
function renderRanks(){{let html='<thead><tr><th>Rank</th><th>Host</th><th>Shared ready flag</th><th>TX ready in data</th><th>TX metadata full prefetch</th><th>Balanced send routes</th><th>Baseline us</th><th>Coarse profile host us</th><th>Device critical us</th><th>Explicit DCCI max core us</th><th>Explicit DCCI aggregate core-us</th><th>Explicit DCCI / all kernel core-time</th><th>Explicit DCCI / critical core</th><th>Explicit DCCI / Pack+Rx core-time</th><th>Pack max us</th><th>Receive max us</th><th>Send max us</th><th>Send OLS slope us/core</th><th>Send spread us</th><th>Send spread % mean</th><th>SelfCopy active max us</th><th>Sender0 tail us</th><th>Total meta amp</th><th>Send meta amp</th><th>Ready miss attempts</th></tr></thead><tbody>';for(const s of DATA.summary)html+=`<tr><td>rank${{s.rank}}</td><td>${{s.host}}</td><td>${{s.tx_ready_shared_flag?'on':'off'}}</td><td>${{s.tx_ready_in_data?'on':'off'}}</td><td>${{s.tx_meta_prefetch_full?'on':'off'}}</td><td>${{s.send_route_balanced?'on':'off'}}</td><td>${{fmt(s.baseline_us)}}</td><td>${{fmt(s.coarse_profile_host_us)}}</td><td>${{fmt(s.kernel_critical_us)}}</td><td>${{fmt(s.dcci_critical_us)}}</td><td>${{fmt(s.dcci_aggregate_us)}}</td><td>${{fmt(s.dcci_all_kernel_fraction*100)}}%</td><td>${{fmt(s.dcci_critical_core_fraction*100)}}%</td><td>${{fmt(s.dcci_pack_receive_fraction*100)}}%</td><td>${{fmt(s.pack_critical_us)}}</td><td>${{fmt(s.receive_critical_us)}}</td><td>${{fmt(s.send_critical_us)}}</td><td>${{fmt(s.send_core_ols_slope_us_per_core)}}</td><td>${{fmt(s.send_core_spread_us)}}</td><td>${{fmt(s.send_core_spread_pct)}}%</td><td>${{fmt(s.self_copy_active_max_us)}}</td><td>${{fmt(s.sender0_tail_us)}}</td><td>${{fmt(s.meta_scan_amplification)}}x</td><td>${{fmt(s.send_meta_scan_amplification)}}x</td><td>${{fmt(s.ready_miss_ratio*100)}}%</td></tr>`;document.getElementById('rankTable').innerHTML=html+'</tbody>'}}
const select=document.getElementById('rankSelect');select.innerHTML=DATA.ranks.map(r=>`<option value="${{r.rank}}">rank${{r.rank}} @ ${{r.host}}</option>`).join('');const slowest=DATA.summary.reduce((a,b)=>b.kernel_critical_us>a.kernel_critical_us?b:a,DATA.summary[0]);select.value=String(slowest.rank);select.onchange=render;renderRanks();render();
</script>
</body></html>"""


def main():
    args = parse_args()
    root = Path(args.profile_dir).resolve()
    all_fine_ranks = load_rank_traces(root)
    if not all_fine_ranks:
        raise SystemExit(f"no EP URMA combine traces found under {root}")

    repo_root = Path(__file__).resolve().parents[3]
    if not args.skip_generic_report:
        reporter = load_generic_reporter(repo_root)
        report_args = SimpleNamespace(warmup_iters=0, iters=1, profile_sample_every=1)
        index = reporter.build_index(root, report_args)
        reporter.write_outputs(root, index, False)

    coarse_root = Path(args.coarse_dir).resolve() if args.coarse_dir else root
    fine_identity = infer_path_identity(root)
    coarse_identity = infer_path_identity(coarse_root)
    for field in ("variant", "run"):
        if (fine_identity[field] is not None and coarse_identity[field] is not None and
                fine_identity[field] != coarse_identity[field]):
            raise SystemExit(
                f"fine/coarse {field} mismatch: {fine_identity[field]} != {coarse_identity[field]}")
    all_coarse_ranks = load_rank_traces(coarse_root) if args.coarse_dir else all_fine_ranks
    fine_groups = group_rank_traces(all_fine_ranks)
    coarse_groups = group_rank_traces(all_coarse_ranks)
    try:
        samples = validate_profile_samples(fine_groups, coarse_groups)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    selected_launch, coarse_kernel_median_us = representative_sample(samples)
    fine_kernel_median_us = statistics.median(
        float(sample["fine_kernel_max_us"]) for sample in samples)
    total_kernel_median_us = None
    if args.total_dir:
        total_root = Path(args.total_dir).resolve()
        total_groups = group_rank_traces(load_rank_traces(total_root))
        try:
            total_samples = validate_total_profile_samples(total_groups, coarse_groups)
        except ValueError as error:
            raise SystemExit(str(error)) from error
        total_by_launch = {sample["launch"]: sample for sample in total_samples}
        for sample in samples:
            sample.update(total_by_launch[sample["launch"]])
        total_kernel_median_us = statistics.median(
            float(sample["total_kernel_max_us"]) for sample in total_samples)
    kernel_median_us = (
        coarse_kernel_median_us if total_kernel_median_us is None else total_kernel_median_us)
    ranks = fine_groups[selected_launch]
    coarse_ranks = coarse_groups[selected_launch]
    coarse_by_rank = {item["rank"]: item for item in coarse_ranks}
    for rank_data in ranks:
        coarse_rank = coarse_by_rank[rank_data["rank"]]
        rank_data["coarse_stats"] = coarse_rank["stats"]
    summary = build_summary(ranks, coarse_ranks)
    write_csv_files(root, ranks, summary)
    write_analysis(
        root, ranks, summary, len(samples), selected_launch, kernel_median_us, samples,
        total_kernel_median_us, coarse_kernel_median_us, fine_kernel_median_us)
    with (root / "combine_profile_samples.csv").open(
            "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(samples[0]))
        writer.writeheader()
        writer.writerows(samples)
    context = build_report_context(
        root, ranks, args.design_label, len(samples), selected_launch, kernel_median_us, samples,
        total_kernel_median_us, coarse_kernel_median_us, fine_kernel_median_us)
    (root / "combine_report.html").write_text(
        render_html(ranks, summary, context, args.show_start_gate), encoding="utf-8")
    print(f"wrote {root / 'combine_report.html'}")
    print(f"wrote {root / 'perfetto_trace.json'}")
    print(f"wrote {root / 'combine_profile_samples.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
