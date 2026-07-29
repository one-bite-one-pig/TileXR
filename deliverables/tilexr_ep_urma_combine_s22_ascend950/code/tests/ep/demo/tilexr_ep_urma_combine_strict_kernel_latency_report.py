#!/usr/bin/env python3

import argparse
import csv
import json
import math
import os
import re
import statistics
import tempfile
from pathlib import Path


SAMPLE_RE = re.compile(
    r"STRICT_KERNEL_LATENCY rank=(\d+) round=(\d+) "
    r"max_core=(\d+) max_cycles=(\d+) "
    r"elapsed_us=([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)"
)
STRICT_KERNEL_CYCLES_PER_US = 1000.0


def parse_samples(paths):
    samples = {}
    origins = {}
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as stream:
            for line_number, line in enumerate(stream, 1):
                text = line.strip()
                if not text.startswith("STRICT_KERNEL_LATENCY "):
                    continue
                match = SAMPLE_RE.fullmatch(text)
                if match is None:
                    raise ValueError(
                        f"malformed strict kernel latency sample at {path}:{line_number}: {text}"
                    )
                rank = int(match.group(1))
                round_index = int(match.group(2))
                max_core = int(match.group(3))
                max_cycles = int(match.group(4))
                elapsed_us = float(match.group(5))
                if (max_core < 0 or max_core >= 64 or max_cycles <= 0 or
                        not math.isfinite(elapsed_us) or elapsed_us <= 0.0):
                    raise ValueError(
                        f"invalid strict kernel sample at {path}:{line_number}: {text}"
                    )
                expected_us = max_cycles / STRICT_KERNEL_CYCLES_PER_US
                if not math.isclose(elapsed_us, expected_us, rel_tol=0.0, abs_tol=1e-6):
                    raise ValueError(
                        f"cycles/us mismatch at {path}:{line_number}: "
                        f"cycles={max_cycles} elapsed_us={elapsed_us}"
                    )
                key = (rank, round_index)
                if key in samples:
                    raise ValueError(
                        f"duplicate sample rank={rank} round={round_index} at "
                        f"{path}:{line_number}; first seen at {origins[key]}"
                    )
                samples[key] = {
                    "max_core": max_core,
                    "max_cycles": max_cycles,
                    "elapsed_us": elapsed_us,
                }
                origins[key] = f"{path}:{line_number}"
    return samples


def validate_samples(samples, rank_size, rounds):
    if rank_size <= 0 or rounds <= 0:
        raise ValueError("rank-size and rounds must be positive")
    expected_ranks = set(range(rank_size))
    expected_rounds = set(range(rounds))
    actual_ranks = {rank for rank, _ in samples}
    actual_rounds = {round_index for _, round_index in samples}
    unexpected_ranks = sorted(actual_ranks - expected_ranks)
    unexpected_rounds = sorted(actual_rounds - expected_rounds)
    if unexpected_ranks or unexpected_rounds:
        raise ValueError(
            f"unexpected sample coordinates: ranks={unexpected_ranks}, "
            f"rounds={unexpected_rounds}"
        )
    missing = [
        (rank, round_index)
        for rank in range(rank_size)
        for round_index in range(rounds)
        if (rank, round_index) not in samples
    ]
    if missing:
        preview = ", ".join(f"r{rank}/round{round_index}" for rank, round_index in missing[:12])
        suffix = " ..." if len(missing) > 12 else ""
        raise ValueError(f"missing {len(missing)} rank-round samples: {preview}{suffix}")
    expected_count = rank_size * rounds
    if len(samples) != expected_count:
        raise ValueError(f"expected {expected_count} samples, found {len(samples)}")


def percentile(values, quantile):
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def aggregate_round_maxima(samples, rank_size, rounds):
    maxima = []
    for round_index in range(rounds):
        max_cycles = max(samples[(rank, round_index)]["max_cycles"] for rank in range(rank_size))
        max_rank = min(
            rank for rank in range(rank_size)
            if samples[(rank, round_index)]["max_cycles"] == max_cycles
        )
        critical = samples[(max_rank, round_index)]
        maxima.append({
            "round": round_index,
            "max_rank": max_rank,
            "max_core": critical["max_core"],
            "max_cycles": max_cycles,
            "max_us": critical["elapsed_us"],
        })
    return maxima


def summarize(maxima):
    values = [row["max_us"] for row in maxima]
    return {
        "mean": statistics.fmean(values),
        "p50": statistics.median(values),
        "p95": percentile(values, 0.95),
        "min": min(values),
        "max": max(values),
    }


def summarize_cycles(maxima):
    values = [row["max_cycles"] for row in maxima]
    return {
        "mean": statistics.fmean(values),
        "p50": statistics.median(values),
        "p95": percentile(values, 0.95),
        "min": min(values),
        "max": max(values),
    }


def parse_topology(hosts_spec, device_maps_spec, ranks_per_host, rank_size):
    hosts = [host for host in hosts_spec.split(",") if host]
    raw_maps = [device_map for device_map in device_maps_spec.split(";") if device_map]
    if not hosts or len(hosts) != len(raw_maps) or ranks_per_host <= 0:
        raise ValueError("hosts, device maps, and ranks-per-host describe an invalid topology")
    nodes = []
    for index, (host, raw_map) in enumerate(zip(hosts, raw_maps)):
        try:
            devices = [int(device) for device in raw_map.split(",")]
        except ValueError as error:
            raise ValueError(f"invalid device map for {host}: {raw_map}") from error
        if (len(devices) != ranks_per_host or len(set(devices)) != len(devices) or
                any(device < 0 for device in devices)):
            raise ValueError(f"invalid device map for {host}: {raw_map}")
        rank_begin = index * ranks_per_host
        nodes.append({
            "host": host,
            "devices": devices,
            "rank_begin": rank_begin,
            "rank_end": rank_begin + ranks_per_host - 1,
        })
    if len(nodes) * ranks_per_host != rank_size:
        raise ValueError(
            f"topology provides {len(nodes) * ranks_per_host} ranks, expected {rank_size}"
        )
    return {
        "host_count": len(nodes),
        "ranks_per_host": ranks_per_host,
        "nodes": nodes,
    }


def write_report(output_dir, rank_size, warmup_rounds, rounds, maxima, summary,
                 bs, hidden, top_k, route_seed, artifact_variant,
                 parallel_round_publish, topology, pack_cores=48, send_cores=16,
                 qp_count=16, doorbell_batch=1):
    output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / "strict_kernel_latency_round_max.csv"
    json_path = output_dir / "strict_kernel_latency_summary.json"
    for path in (csv_path, json_path):
        if path.exists():
            raise FileExistsError(f"refusing to overwrite existing strict kernel report: {path}")

    publish_mode = (
        f"{send_cores} Send-core parallel" if parallel_round_publish else "sender0 serial"
    )
    report = {
        "schema": "tilexr_ep_urma_combine_strict_kernel_latency.v1",
        "measurement": (
            "in-kernel max-core critical latency from PIPE_ALL-bracketed "
            "GetSystemCycle durations"
        ),
        "aggregation": (
            "maximum across 64 AIV cores per rank, then maximum across ranks per round, "
            "then summarize round maxima"
        ),
        "design": (
            f"{pack_cores}-pack/{send_cores}-send/QP{qp_count}/DB{doorbell_batch}, "
            f"{publish_mode} Round Publish"
        ),
        "artifact_variant": artifact_variant,
        "config": {
            "bs": bs,
            "top_k": top_k,
            "hidden": hidden,
            "route_seed": route_seed,
            "pack_cores": pack_cores,
            "send_cores": send_cores,
            "qp_count": qp_count,
            "doorbell_batch": doorbell_batch,
            "profiling": False,
            "parallel_round_publish": bool(parallel_round_publish),
            "round_publish_mode": publish_mode,
            "cycle_to_us_divisor": int(STRICT_KERNEL_CYCLES_PER_US),
        },
        "topology": topology,
        "rank_size": rank_size,
        "warmup_rounds": warmup_rounds,
        "measured_rounds": rounds,
        "round_maxima": maxima,
        "summary_cycles": summarize_cycles(maxima),
        "summary_us": summary,
    }
    temporary_paths = []
    created_paths = []
    try:
        for target in (csv_path, json_path):
            descriptor, temporary = tempfile.mkstemp(
                prefix=f".{target.name}.", suffix=".tmp", dir=output_dir
            )
            os.close(descriptor)
            temporary_paths.append(Path(temporary))
        with temporary_paths[0].open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(
                stream, fieldnames=("round", "max_rank", "max_core", "max_cycles", "max_us")
            )
            writer.writeheader()
            writer.writerows(maxima)
        with temporary_paths[1].open("w", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2, sort_keys=True)
            stream.write("\n")

        for path in (csv_path, json_path):
            if path.exists():
                raise FileExistsError(f"refusing to overwrite existing strict kernel report: {path}")
        for temporary, target in zip(temporary_paths, (csv_path, json_path)):
            os.link(temporary, target)
            created_paths.append(target)
    except Exception:
        for path in created_paths:
            path.unlink(missing_ok=True)
        raise
    finally:
        for path in temporary_paths:
            path.unlink(missing_ok=True)
    return csv_path, json_path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Aggregate strict TileXR Combine kernel latency samples across ranks"
    )
    parser.add_argument("logs", nargs="+", type=Path, help="launcher host logs")
    parser.add_argument("--rank-size", type=int, default=64)
    parser.add_argument("--warmup-rounds", type=int, default=20)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--bs", type=int, required=True)
    parser.add_argument("--hidden", type=int, required=True)
    parser.add_argument("--top-k", type=int, required=True)
    parser.add_argument("--route-seed", type=int, required=True)
    parser.add_argument("--artifact-variant", required=True)
    parser.add_argument("--hosts", required=True)
    parser.add_argument("--device-maps", required=True)
    parser.add_argument("--ranks-per-host", type=int, required=True)
    parser.add_argument("--parallel-round-publish", type=int, choices=(0, 1), required=True)
    parser.add_argument("--pack-cores", type=int, default=48)
    parser.add_argument("--send-cores", type=int, default=16)
    parser.add_argument("--qp-count", type=int, default=16)
    parser.add_argument("--doorbell-batch", type=int, default=1)
    parser.add_argument("--output-dir", required=True, type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    if (args.pack_cores <= 0 or args.send_cores <= 0 or
            args.pack_cores + args.send_cores != 64 or args.qp_count <= 0 or
            args.doorbell_batch <= 0):
        raise ValueError("invalid Pack/Send/QP/doorbell configuration")
    samples = parse_samples(args.logs)
    validate_samples(samples, args.rank_size, args.rounds)
    maxima = aggregate_round_maxima(samples, args.rank_size, args.rounds)
    summary = summarize(maxima)
    cycle_summary = summarize_cycles(maxima)
    topology = parse_topology(
        args.hosts, args.device_maps, args.ranks_per_host, args.rank_size
    )
    csv_path, json_path = write_report(
        args.output_dir, args.rank_size, args.warmup_rounds, args.rounds, maxima, summary,
        args.bs, args.hidden, args.top_k, args.route_seed, args.artifact_variant,
        args.parallel_round_publish, topology, args.pack_cores, args.send_cores,
        args.qp_count, args.doorbell_batch
    )
    print(
        "STRICT_KERNEL_LATENCY_SUMMARY "
        f"rank_size={args.rank_size} rounds={args.rounds} "
        "aggregation=max_across_ranks_per_round "
        f"p50_cycles={cycle_summary['p50']:.3f} "
        f"mean_us={summary['mean']:.6f} p50_us={summary['p50']:.6f} "
        f"p95_us={summary['p95']:.6f} min_us={summary['min']:.6f} "
        f"max_us={summary['max']:.6f}"
    )
    print(f"STRICT_KERNEL_LATENCY_REPORT csv={csv_path} json={json_path}")


if __name__ == "__main__":
    main()
