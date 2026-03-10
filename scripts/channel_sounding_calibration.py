#!/usr/bin/env python3
"""Analyze clean-core or Zephyr channel-sounding logs and fit calibration terms."""

from __future__ import annotations

import argparse
import csv
import math
import re
import sys
from pathlib import Path
from statistics import median


FLOAT_RE = r"([+-]?(?:\d+(?:\.\d+)?|\.\d+|nan|na))"
METRIC_PATTERNS = {
    "phase": (
        re.compile(rf"\bphase_raw_m={FLOAT_RE}\b", re.IGNORECASE),
        re.compile(rf"\bphase_m={FLOAT_RE}\b", re.IGNORECASE),
        re.compile(
            rf"Phase-Based Ranging method:\s*{FLOAT_RE}\s+meters", re.IGNORECASE
        ),
    ),
    "distance": (
        re.compile(rf"\bdist_m={FLOAT_RE}\b", re.IGNORECASE),
        re.compile(
            rf"Phase-Based Ranging method:\s*{FLOAT_RE}\s+meters", re.IGNORECASE
        ),
    ),
    "rtt": (
        re.compile(
            rf"Round-Trip Timing method:\s*{FLOAT_RE}\s+meters", re.IGNORECASE
        ),
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fit channel-sounding calibration values from captured logs."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    analyze = subparsers.add_parser(
        "analyze", help="Summarize one or more captured log files."
    )
    analyze.add_argument("logs", nargs="+", help="Log files to analyze.")
    analyze.add_argument(
        "--metric",
        choices=tuple(METRIC_PATTERNS.keys()),
        default="phase",
        help="Which metric to extract from the logs.",
    )
    analyze.add_argument(
        "--reference-distance",
        type=float,
        default=math.nan,
        help="Known true distance in meters for a single-point offset/scale suggestion.",
    )

    fit = subparsers.add_parser(
        "fit",
        help="Fit scale/offset from multiple reference captures listed in a CSV.",
    )
    fit.add_argument("csv_path", help="CSV with columns actual_m and log_path.")
    fit.add_argument(
        "--metric",
        choices=tuple(METRIC_PATTERNS.keys()),
        default="phase",
        help="Which metric to fit against actual distance.",
    )

    return parser.parse_args()


def parse_measurement(token: str) -> float | None:
    lowered = token.strip().lower()
    if lowered in {"", "na", "nan"}:
        return None

    try:
        value = float(lowered)
    except ValueError:
        return None
    if not math.isfinite(value):
        return None
    return value


def extract_metric_values(path: Path, metric: str) -> list[float]:
    patterns = METRIC_PATTERNS[metric]
    values: list[float] = []

    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            for pattern in patterns:
                match = pattern.search(line)
                if not match:
                    continue
                value = parse_measurement(match.group(1))
                if value is not None:
                    values.append(value)
                break

    return values


def median_absolute_deviation(values: list[float], median_value: float) -> float:
    deviations = [abs(value - median_value) for value in values]
    return median(deviations)


def summarize(values: list[float]) -> dict[str, float]:
    count = len(values)
    mean = sum(values) / float(count)
    median_value = median(values)
    mad = median_absolute_deviation(values, median_value)
    variance = sum((value - mean) ** 2 for value in values) / float(count)
    return {
        "count": float(count),
        "mean": mean,
        "median": median_value,
        "mad": mad,
        "stddev": math.sqrt(variance),
        "minimum": min(values),
        "maximum": max(values),
    }


def print_summary(path: Path, metric: str, values: list[float]) -> bool:
    if not values:
        print(f"{path}: no {metric} samples found", file=sys.stderr)
        return False

    summary = summarize(values)
    print(f"{path}:")
    print(
        "  "
        f"samples={int(summary['count'])} "
        f"median={summary['median']:.4f} "
        f"mean={summary['mean']:.4f} "
        f"mad={summary['mad']:.4f} "
        f"stddev={summary['stddev']:.4f} "
        f"min={summary['minimum']:.4f} "
        f"max={summary['maximum']:.4f}"
    )
    return True


def fit_scale_and_offset(points: list[tuple[float, float]]) -> tuple[float, float]:
    if len(points) == 1:
        actual, measured = points[0]
        return 1.0, actual - measured

    measured_mean = sum(measured for _, measured in points) / float(len(points))
    actual_mean = sum(actual for actual, _ in points) / float(len(points))
    numerator = 0.0
    denominator = 0.0
    for actual, measured in points:
        numerator += (measured - measured_mean) * (actual - actual_mean)
        denominator += (measured - measured_mean) ** 2

    if denominator == 0.0:
        raise RuntimeError("measured medians are identical; cannot fit a scale")

    scale = numerator / denominator
    offset = actual_mean - (scale * measured_mean)
    return scale, offset


def run_analyze(args: argparse.Namespace) -> int:
    ok = True
    for raw_path in args.logs:
        path = Path(raw_path).expanduser().resolve()
        values = extract_metric_values(path, args.metric)
        file_ok = print_summary(path, args.metric, values)
        ok &= file_ok
        if file_ok and math.isfinite(args.reference_distance):
            median_value = summarize(values)["median"]
            recommended_offset = args.reference_distance - median_value
            print(
                "  "
                f"reference_m={args.reference_distance:.4f} "
                f"recommended_offset_m={recommended_offset:.4f}"
            )
            if median_value != 0.0:
                print(
                    "  "
                    f"single_point_scale={args.reference_distance / median_value:.6f}"
                )

    return 0 if ok else 1


def run_fit(args: argparse.Namespace) -> int:
    csv_path = Path(args.csv_path).expanduser().resolve()
    points: list[tuple[float, float]] = []

    with csv_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise RuntimeError("CSV is missing a header row")
        if "actual_m" not in reader.fieldnames or "log_path" not in reader.fieldnames:
            raise RuntimeError("CSV must contain actual_m and log_path columns")

        for row in reader:
            actual_m = parse_measurement(row["actual_m"])
            if actual_m is None:
                raise RuntimeError(f"Invalid actual_m value in {csv_path}")

            raw_log_path = row["log_path"].strip()
            log_path = Path(raw_log_path)
            if not log_path.is_absolute():
                log_path = (csv_path.parent / log_path).resolve()

            values = extract_metric_values(log_path, args.metric)
            if not values:
                raise RuntimeError(f"No {args.metric} samples found in {log_path}")

            measured_median = summarize(values)["median"]
            points.append((actual_m, measured_median))
            print(
                f"{log_path}: actual_m={actual_m:.4f} measured_median={measured_median:.4f}"
            )

    if not points:
        raise RuntimeError("No calibration points loaded from CSV")

    scale, offset = fit_scale_and_offset(points)
    print()
    print(f"recommended_scale={scale:.6f}")
    print(f"recommended_offset_m={offset:.4f}")
    print("serial_commands:")
    print(f"  scale {scale:.6f}")
    print(f"  offset {offset:.4f}")
    print("sketch_constants:")
    print(f"  kCalibrationScaleDefault = {scale:.6f}")
    print(f"  kCalibrationOffsetMetersDefault = {offset:.4f}")
    return 0


def main() -> int:
    args = parse_args()
    if args.command == "analyze":
        return run_analyze(args)
    if args.command == "fit":
        return run_fit(args)
    raise RuntimeError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
