#!/usr/bin/env python3

import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "SHA256SUMS"


def main():
    if not MANIFEST.is_file():
        raise SystemExit(f"missing {MANIFEST}")
    failures = []
    checked = 0
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        digest, relative = line.split("  ", 1)
        path = ROOT / relative
        if not path.is_file():
            failures.append(f"missing: {relative}")
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != digest:
            failures.append(f"hash mismatch: {relative}")
        checked += 1
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"verified {checked} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
