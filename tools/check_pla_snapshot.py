#!/usr/bin/env python3
import csv
from pathlib import Path


FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211

OP_MAP = {
    "REFRESH": 0,
    "VIDEO": 1,
    "SPRITE": 2,
}

FETCH_MAP = {
    "NONE": 0,
    "VIDEO_MEMPTR_SCREEN": 1,
    "VIDEO_COLOR": 2,
    "VIDEO_PATTERN": 3,
    "REFRESH": 4,
    "SPRITE_PTR": 5,
    "SPRITE_DATA0": 6,
    "SPRITE_DATA1": 7,
    "SPRITE_DATA2": 8,
}

PLAN_MAP = {
    "NONE": 0,
    "VIDEO_MEMPTR_SCREEN": 1,
    "VIDEO_COLOR": 2,
    "VIDEO_PATTERN": 3,
    "REFRESH": 4,
    "SPRITE_PTR": 5,
    "SPRITE_DATA0": 6,
    "SPRITE_DATA1": 7,
    "SPRITE_DATA2": 8,
}

ADDR_MAP = {
    "NONE": 0,
    "VIDEO_MEMPTR": 1,
    "VIDEO_SCREEN": 2,
    "VIDEO_COLOR": 3,
    "VIDEO_PATTERN": 4,
    "REFRESH": 5,
    "SPRITE_PTR": 6,
    "SPRITE_DATA0": 7,
    "SPRITE_DATA1": 8,
    "SPRITE_DATA2": 9,
}


def parse_snapshot_digest(snapshot_path: Path) -> str:
    key = "- digest: `"
    for line in snapshot_path.read_text(encoding="utf-8").splitlines():
        i = line.find(key)
        if i >= 0:
            s = i + len(key)
            e = line.find("`", s)
            if e > s:
                return line[s:e].strip().upper()
    raise RuntimeError(f"digest not found in {snapshot_path}")


def compute_csv_digest(csv_path: Path) -> str:
    d = FNV_OFFSET
    with csv_path.open("r", encoding="utf-8", newline="") as f:
        rows = csv.DictReader(f)
        for row in rows:
            values = [
                OP_MAP[row["op"]],
                int(row["bus_window"]),
                FETCH_MAP[row["fetch"]],
                int(row["tick_actions"]),
                int(row["predicate_mask"]),
                PLAN_MAP[row["fetch_plan"]],
                PLAN_MAP[row["commit_plan"]],
                ADDR_MAP[row["addr_formula0"]],
                ADDR_MAP[row["addr_formula1"]],
            ]
            for v in values:
                d ^= int(v) & 0xFFFFFFFFFFFFFFFF
                d = (d * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{d:X}".upper()


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    csv_path = repo / "reference" / "edge" / "vic_pla_spec.csv"
    snapshot_path = repo / "reference" / "edge" / "vic_pla_snapshot.md"

    if not csv_path.exists():
        raise SystemExit(f"missing file: {csv_path}")
    if not snapshot_path.exists():
        raise SystemExit(f"missing file: {snapshot_path}")

    got = compute_csv_digest(csv_path)
    want = parse_snapshot_digest(snapshot_path)
    if got != want:
        raise SystemExit(f"PLA digest mismatch: got={got} expected={want}")

    print(f"[PLA-CHECK] PASS digest={got}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
