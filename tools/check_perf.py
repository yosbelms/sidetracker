#!/usr/bin/env python3
"""Check CE audio benchmark signatures, pool usage and optional M4 budgets."""
import argparse
import json
from pathlib import Path


def read_results(path):
    results = {}
    for line in Path(path).read_text().splitlines():
        if not line.startswith("{"):
            continue  # Make and compiler output may precede JSON records.
        row = json.loads(line)
        name = row["scenario"]
        if name in results:
            raise ValueError(f"Duplicate scenario {name}")
        for key in ("average", "maximum", "checksum", "peak_blocks"):
            if not isinstance(row[key], int) or row[key] < 0:
                raise ValueError(f"Invalid {key} for {name}")
        if row["maximum"] < row["average"]:
            raise ValueError(f"Maximum is below average for {name}")
        results[name] = row
    if not results:
        raise ValueError(f"No benchmark records in {path}")
    return results


def check_results(baseline, results, metric, max_block_us=None):
    expected = baseline["scenarios"]
    errors = []
    if set(results) != set(expected):
        return [f"Scenario set differs. Expected {sorted(expected)}, got {sorted(results)}"]
    for name, row in results.items():
        limits = expected[name]
        if row["metric"] != metric:
            errors.append(f"{name} uses {row['metric']}, expected {metric}")
        if row["peak_blocks"] > limits["peak_blocks"]:
            errors.append(f"{name} uses {row['peak_blocks']} blocks, limit {limits['peak_blocks']}")
        checksum_key = "checksum" if metric == "ns" else "m4_checksum"
        if limits.get(checksum_key) is not None and row["checksum"] != limits[checksum_key]:
            errors.append(f"{name} output checksum changed")
        if metric == "instructions" and limits.get("instructions") is not None:
            if row["average"] * 100 > limits["instructions"] * 105:
                errors.append(f"{name} instruction count exceeds its baseline by more than 5%")
        if metric == "ns" and max_block_us is not None and row["maximum"] > max_block_us * 1000:
            errors.append(f"{name} exceeds the requested {max_block_us:g} us host block limit")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--m4", type=Path)
    parser.add_argument("--max-block-us", type=float)
    parser.add_argument("--record", action="store_true", help="explicitly update measured baselines")
    args = parser.parse_args()
    try:
        host = read_results(args.host)
        m4 = read_results(args.m4) if args.m4 else None
        if args.record:
            if set(host) != {"idle", "dry8", "filter8", "drive8", "changes8"}:
                raise ValueError("Recording requires all five benchmark scenarios")
            if any(row["metric"] != "ns" for row in host.values()):
                raise ValueError("Host recording requires nanosecond measurements")
            if m4 is not None and (set(m4) != set(host) or
                                   any(row["metric"] != "instructions" for row in m4.values())):
                raise ValueError("M4 recording requires matching instruction measurements")
            old = json.loads(args.baseline.read_text()) if args.baseline.exists() else {"scenarios": {}}
            scenarios = {}
            for name, row in host.items():
                scenarios[name] = {
                    "checksum": row["checksum"], "peak_blocks": row["peak_blocks"],
                    "instructions": old["scenarios"].get(name, {}).get("instructions"),
                    "m4_checksum": old["scenarios"].get(name, {}).get("m4_checksum"),
                }
                if m4 is not None:
                    scenarios[name].update(instructions=m4[name]["average"], m4_checksum=m4[name]["checksum"])
            args.baseline.write_text(json.dumps({"scenarios": scenarios}, indent=2) + "\n")
            print(f"Recorded measurements in {args.baseline}")
            return 0
        baseline = json.loads(args.baseline.read_text())
        errors = check_results(baseline, host, "ns", args.max_block_us)
        if m4 is not None:
            errors += check_results(baseline, m4, "instructions")
            if any(row.get("instructions") is None for row in baseline["scenarios"].values()):
                print("M4 counts measured. Record an M4 baseline to enable the 5% instruction gate.")
        for name, row in host.items():
            print(f"{name:10} avg {row['average'] / 1000:8.3f} us, max {row['maximum'] / 1000:8.3f} us, "
                  f"pool {row['peak_blocks']} blocks")
        if errors:
            for error in errors:
                print(f"FAIL {error}")
            return 1
        print("PASS audio signatures and block pool limits")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
