#!/usr/bin/env python3
import argparse
import json
import sys


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
