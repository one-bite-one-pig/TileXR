#!/usr/bin/env python3

import argparse
import json
import struct
from pathlib import Path


CACHE_LINE_BYTES = 64
PACK_RECEIVE_LANES = 44
SEND_LANES = 20

NORMAL_STEPS = {
    0: "empty",
    1: "tx_route_ready",
    2: "rx_lane_done",
    3: "sender_done",
    4: "rx_buffer_released",
    5: "publish_done",
    6: "start_local_ready",
    7: "start_rank_ready",
    8: "start_publish_done",
    9: "start_run",
}

DIAGNOSTIC_STAGES = {
    0x00010000: "pack_start",
    0x00020000: "pack_done",
    0x00030000: "rx_poll",
    0x00040000: "rx_unpack",
    0x00050000: "send_scan",
    0x00060000: "send_scan_done",
    0x00070000: "udma_quiet",
    0x00080000: "udma_quiet_error",
    0x00090000: "local_sender_wait",
    0x000A0000: "local_rx_wait",
    0x000B0000: "round_publish",
    0x000C0000: "global_round_wait",
    0x000D0000: "poll_timeout",
}


def read_value(workspace: bytes, offset: int) -> dict:
    if offset < 0 or offset + 8 > len(workspace):
        raise ValueError(f"control offset {offset} is outside workspace")
    value = struct.unpack_from("<Q", workspace, offset)[0]
    magic = value >> 32
    step = value & 0xFFFFFFFF
    stage = step & 0xFFFF0000
    detail = step & 0xFFFF
    if stage in DIAGNOSTIC_STAGES:
        name = DIAGNOSTIC_STAGES[stage]
    else:
        name = NORMAL_STEPS.get(step, "unknown")
        detail = 0
    return {
        "offset": offset,
        "value": value,
        "magic": magic,
        "step": step,
        "stage": name,
        "detail": detail,
    }


def read_raw_u64(workspace: bytes, offset: int) -> int:
    if offset < 0 or offset + 8 > len(workspace):
        raise ValueError(f"diagnostic offset {offset} is outside workspace")
    return struct.unpack_from("<Q", workspace, offset)[0]


def read_lines(workspace: bytes, offset: int, count: int) -> list[dict]:
    lines = []
    for index in range(count):
        line_offset = offset + index * CACHE_LINE_BYTES
        line = read_value(workspace, line_offset)
        if line["stage"] == "udma_quiet_error":
            cqe_meta = read_raw_u64(workspace, line_offset + 8)
            queue_indices = read_raw_u64(workspace, line_offset + 16)
            sqe_words_01 = read_raw_u64(workspace, line_offset + 40)
            sqe_words_23 = read_raw_u64(workspace, line_offset + 48)
            sqe_words = [
                sqe_words_01 & 0xFFFFFFFF,
                sqe_words_01 >> 32,
                sqe_words_23 & 0xFFFFFFFF,
                sqe_words_23 >> 32,
            ]
            line["udma_cq"] = {
                "cqe_word0": cqe_meta & 0xFFFFFFFF,
                "cqe_owner": (cqe_meta >> 2) & 1,
                "expected_owner": (cqe_meta >> 32) & 1,
                "retries": cqe_meta >> 33,
                "cur_tail": queue_indices & 0xFFFFFFFF,
                "target_index": queue_indices >> 32,
                "cqe_address": f"0x{read_raw_u64(workspace, line_offset + 24):016x}",
                "wqe_address": f"0x{read_raw_u64(workspace, line_offset + 32):016x}",
                "sqe_words": sqe_words,
                "sqe_index": sqe_words[0] & 0xFFFF,
                "sqe_flag": (sqe_words[0] >> 16) & 0xFF,
                "sqe_owner": (sqe_words[0] >> 31) & 1,
            }
        lines.append(line)
    return lines




def main() -> int:
    parser = argparse.ArgumentParser(description="Decode TileXR URMA Combine diagnostic progress lines")
    parser.add_argument("snapshot", type=Path, help="rankN/roundN workspace snapshot directory")
    parser.add_argument("--output", type=Path, help="exclusive JSON output path")
    args = parser.parse_args()

    metadata = json.loads((args.snapshot / "layout.json").read_text(encoding="utf-8"))
    workspace = (args.snapshot / "workspace.bin").read_bytes()
    magic = int(metadata["magic"])
    round_index = magic & 1
    result = {
        "schema": "tilexr_ep_urma_combine_progress.v2",
        "snapshot": str(args.snapshot),
        "rank": int(metadata["rank"]),
        "round": int(metadata["round"]),
        "magic": magic,
        "rx_lanes": read_lines(workspace, int(metadata["rx_lane_done_offset"]), PACK_RECEIVE_LANES),
        "send_lanes": read_lines(workspace, int(metadata["sender_done_offset"]), SEND_LANES),
        "round_publish": read_value(workspace, int(metadata["round_publish_offset"])),
        "round_done": read_lines(
            workspace,
            int(metadata[f"round_done_offset_{round_index}"]),
            int(metadata["rank_size"]),
        ),
        "start_gate": read_lines(
            workspace, int(metadata["start_gate_offset"]), int(metadata["rank_size"])
        ),
        "error_status": read_value(workspace, int(metadata["error_status_offset"])),
    }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(encoded, end="")
    else:
        with args.output.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
