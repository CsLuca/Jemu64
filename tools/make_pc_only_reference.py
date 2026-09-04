#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path


def extract_pc_rows(src: Path, dst: Path) -> int:
    count = 0
    with src.open("r", newline="", encoding="utf-8") as fin, dst.open("w", newline="", encoding="utf-8") as fout:
        reader = csv.reader(fin)
        writer = csv.writer(fout)
        writer.writerow(["half", "pc", "a", "x", "y", "p", "sp", "addr", "rw", "data"])
        header_skipped = False
        for row in reader:
            if not row:
                continue
            if not header_skipped:
                header_skipped = True
                # accept runtime header and continue
                continue
            if len(row) < 2:
                continue
            # Keep only pc, normalize everything else to zero placeholders.
            writer.writerow([0, row[1], 0, 0, 0, 0, 0, 0, "R", 0])
            count += 1
    return count


def main() -> int:
    parser = argparse.ArgumentParser(description="Create pc_only reference traces from runtime traces.")
    parser.add_argument("--source", required=True, help="Source runtime trace CSV")
    parser.add_argument("--dest", required=True, help="Destination reference trace CSV")
    args = parser.parse_args()

    src = Path(args.source)
    dst = Path(args.dest)
    if not src.exists():
        raise SystemExit(f"missing source trace: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    count = extract_pc_rows(src, dst)
    print(f"[PC-REF] wrote {count} rows -> {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
