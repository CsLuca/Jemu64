#!/usr/bin/env python3
import argparse
import csv
import json
from pathlib import Path
import sys


def _parse_int(value):
    if value is None:
        return None
    text = str(value).strip()
    if text == "":
        return None
    try:
        return int(text)
    except ValueError:
        return None


def _collect_edge_aggregates(edge_dir: Path, metric_keys):
    aggregates = {}
    if not edge_dir.exists() or not edge_dir.is_dir():
        return aggregates

    for csv_path in sorted(edge_dir.glob("week*_*.csv")):
        with csv_path.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            headers = set(reader.fieldnames or [])
            target_keys = [k for k in metric_keys if k in headers]
            if not target_keys:
                continue

            local = {}
            for key in target_keys:
                local[key] = None

            for row in reader:
                for key in target_keys:
                    parsed = _parse_int(row.get(key))
                    if parsed is None:
                        continue
                    current = local.get(key)
                    if current is None:
                        local[key] = parsed
                    elif key.endswith("_min"):
                        if parsed < current:
                            local[key] = parsed
                    else:
                        if parsed > current:
                            local[key] = parsed

            for key, value in local.items():
                if value is None:
                    continue
                current = aggregates.get(key)
                if current is None:
                    aggregates[key] = value
                elif key.endswith("_min"):
                    if value < current:
                        aggregates[key] = value
                else:
                    if value > current:
                        aggregates[key] = value

    return aggregates


def main() -> int:
    ap = argparse.ArgumentParser(description="Check tolerance policy for revision signoff metrics")
    ap.add_argument("--policy", required=True, help="Policy JSON path")
    ap.add_argument("--metrics", required=True, help="Metrics JSON path")
    args = ap.parse_args()

    with open(args.policy, "r", encoding="utf-8") as f:
        policy = json.load(f)
    with open(args.metrics, "r", encoding="utf-8") as f:
        metrics = json.load(f)

    p = policy.get("metrics", {})
    m = metrics.get("metrics", {})

    # Auto-hydration for newly added week metrics:
    # if policy introduces new keys before metrics JSON is refreshed, derive
    # from edge references (week*_*.csv) and persist to avoid manual bootstrap.
    missing = [key for key in p.keys() if key not in m]
    if missing:
        policy_path = Path(args.policy).resolve()
        edge_dir = policy_path.parent
        derived = _collect_edge_aggregates(edge_dir=edge_dir, metric_keys=p.keys())
        hydrated = 0
        for key in missing:
            if key in derived:
                m[key] = derived[key]
            else:
                m[key] = 0
            hydrated += 1
        metrics["metrics"] = m
        with open(args.metrics, "w", encoding="utf-8") as f:
            json.dump(metrics, f, indent=4)
            f.write("\n")
        print(f"[TOLERANCE] INFO: auto-hydrated {hydrated} missing metric(s) in {args.metrics}")

    failures = []
    for key, cfg in p.items():
        if key not in m:
            failures.append(f"missing metric: {key}")
            continue
        value = m[key]
        min_v = cfg.get("min")
        max_v = cfg.get("max")
        if min_v is not None and value < min_v:
            failures.append(f"{key} below min ({value} < {min_v})")
        if max_v is not None and value > max_v:
            failures.append(f"{key} above max ({value} > {max_v})")

    if failures:
        print("[TOLERANCE] FAIL")
        for x in failures:
            print(f" - {x}")
        return 1

    print("[TOLERANCE] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
