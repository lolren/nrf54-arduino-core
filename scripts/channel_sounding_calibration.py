#!/usr/bin/env python3
"""Analyze clean-core or Zephyr channel-sounding logs and fit calibration terms."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from pathlib import Path
from statistics import median


FLOAT_RE = r"([+-]?(?:\d+(?:\.\d+)?|\.\d+|nan|na))"
SPEED_OF_LIGHT_M_PER_S = 299_792_458.0
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


def distance_meters_to_equivalent_delay_ns(distance_m: float) -> float:
    return (distance_m / SPEED_OF_LIGHT_M_PER_S) * 1.0e9


def characterize_board_pair(
    reference_distance: float, measured_median: float
) -> tuple[float | None, float | None, float | None]:
    if not math.isfinite(reference_distance) or not math.isfinite(measured_median):
        return None, None, None
    pair_bias_m = measured_median - reference_distance
    pair_delay_ns = distance_meters_to_equivalent_delay_ns(pair_bias_m)
    symmetric_per_board_delay_ns = 0.5 * pair_delay_ns
    return pair_bias_m, pair_delay_ns, symmetric_per_board_delay_ns


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
    analyze.add_argument(
        "--profile-name",
        default="",
        help="Optional name used when emitting a calibration profile artifact.",
    )
    analyze.add_argument(
        "--board-pair",
        default="",
        help="Optional board-pair label stored in emitted profile artifacts.",
    )
    analyze.add_argument(
        "--notes",
        default="",
        help="Optional free-form notes stored in emitted profile artifacts.",
    )
    analyze.add_argument(
        "--emit-profile-json",
        default="",
        help="Write a JSON calibration profile for this single-log analysis.",
    )
    analyze.add_argument(
        "--emit-profile-header",
        default="",
        help="Write a C++ calibration-profile header for this single-log analysis.",
    )
    analyze.add_argument(
        "--validation-log",
        default="",
        help="Optional second log captured after applying the suggested calibration profile.",
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
    fit.add_argument(
        "--profile-name",
        default="",
        help="Optional name used when emitting a calibration profile artifact.",
    )
    fit.add_argument(
        "--board-pair",
        default="",
        help="Optional board-pair label stored in emitted profile artifacts.",
    )
    fit.add_argument(
        "--notes",
        default="",
        help="Optional free-form notes stored in emitted profile artifacts.",
    )
    fit.add_argument(
        "--emit-profile-json",
        default="",
        help="Write a JSON calibration profile for the fitted calibration set.",
    )
    fit.add_argument(
        "--emit-profile-header",
        default="",
        help="Write a C++ calibration-profile header for the fitted calibration set.",
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


def summarize_validation(
    values: list[float], reference_distance: float
) -> dict[str, float] | None:
    if not math.isfinite(reference_distance):
        return None
    positive_values = [value for value in values if math.isfinite(value) and value > 0.0]
    if not positive_values:
        return None

    summary = summarize(positive_values)
    abs_errors = sorted(abs(value - reference_distance) for value in positive_values)
    p90_index = min(len(abs_errors) - 1, math.ceil(0.9 * len(abs_errors)) - 1)
    return {
        "count": float(len(positive_values)),
        "median": summary["median"],
        "mad": summary["mad"],
        "p90_abs_error": abs_errors[p90_index],
    }


def sanitize_identifier(text: str) -> str:
    cleaned = re.sub(r"[^0-9A-Za-z_]+", "_", text.strip())
    cleaned = cleaned.strip("_")
    if not cleaned:
        return "BleCsCalibrationProfileGenerated"
    if cleaned[0].isdigit():
        cleaned = f"_{cleaned}"
    return cleaned


def build_profile_dict(
    *,
    profile_name: str,
    metric: str,
    scale: float,
    offset: float,
    reference_distance: float,
    measured_median: float,
    measured_mad: float,
    sample_count: int,
    board_pair: str,
    notes: str,
    source_logs: list[str],
    source_points: list[dict[str, float | str]],
    validation: dict[str, float] | None = None,
) -> dict[str, object]:
    def finite_or_none(value: float) -> float | None:
        return value if math.isfinite(value) else None

    pair_bias_m, pair_delay_ns, symmetric_per_board_delay_ns = characterize_board_pair(
        reference_distance, measured_median
    )

    return {
        "profile_version": 3,
        "profile_name": profile_name,
        "metric": metric,
        "scale": scale,
        "offset_m": offset,
        "reference_distance_m": finite_or_none(reference_distance),
        "measured_median_m": finite_or_none(measured_median),
        "measured_mad_m": finite_or_none(measured_mad),
        "board_pair_bias_m": finite_or_none(
            pair_bias_m if pair_bias_m is not None else math.nan
        ),
        "board_pair_equivalent_delay_ns": finite_or_none(
            pair_delay_ns if pair_delay_ns is not None else math.nan
        ),
        "symmetric_per_board_delay_ns": finite_or_none(
            symmetric_per_board_delay_ns
            if symmetric_per_board_delay_ns is not None
            else math.nan
        ),
        "validated_median_m": finite_or_none(
            validation["median"] if validation is not None else math.nan
        ),
        "validated_mad_m": finite_or_none(
            validation["mad"] if validation is not None else math.nan
        ),
        "validated_p90_abs_error_m": finite_or_none(
            validation["p90_abs_error"] if validation is not None else math.nan
        ),
        "sample_count": sample_count,
        "validated_sample_count": (
            int(validation["count"]) if validation is not None else 0
        ),
        "board_pair": board_pair,
        "notes": notes,
        "source_logs": source_logs,
        "source_points": source_points,
    }


def write_profile_json(path: Path, profile: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(profile, handle, indent=2, sort_keys=False)
        handle.write("\n")


def write_profile_header(path: Path, profile: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    identifier = sanitize_identifier(str(profile["profile_name"]))
    reference_distance = (
        float(profile["reference_distance_m"])
        if profile["reference_distance_m"] is not None
        else 0.0
    )
    measured_median = (
        float(profile["measured_median_m"])
        if profile["measured_median_m"] is not None
        else 0.0
    )
    measured_mad = (
        float(profile["measured_mad_m"])
        if profile["measured_mad_m"] is not None
        else 0.0
    )
    board_pair_bias = (
        float(profile["board_pair_bias_m"])
        if profile["board_pair_bias_m"] is not None
        else 0.0
    )
    board_pair_delay = (
        float(profile["board_pair_equivalent_delay_ns"])
        if profile["board_pair_equivalent_delay_ns"] is not None
        else 0.0
    )
    per_board_delay = (
        float(profile["symmetric_per_board_delay_ns"])
        if profile["symmetric_per_board_delay_ns"] is not None
        else 0.0
    )
    validated_median = (
        float(profile["validated_median_m"])
        if profile["validated_median_m"] is not None
        else 0.0
    )
    validated_mad = (
        float(profile["validated_mad_m"])
        if profile["validated_mad_m"] is not None
        else 0.0
    )
    validated_p90 = (
        float(profile["validated_p90_abs_error_m"])
        if profile["validated_p90_abs_error_m"] is not None
        else 0.0
    )
    header = f"""#pragma once

#include "ble_channel_sounding.h"

// Generated by scripts/channel_sounding_calibration.py
// metric={profile["metric"]}
// board_pair={profile["board_pair"]}
// notes={profile["notes"]}
// board_pair_bias_m={profile["board_pair_bias_m"]}
// board_pair_equivalent_delay_ns={profile["board_pair_equivalent_delay_ns"]}
// symmetric_per_board_delay_ns={profile["symmetric_per_board_delay_ns"]}
// validated_median_m={profile["validated_median_m"]}
// validated_mad_m={profile["validated_mad_m"]}
// validated_p90_abs_error_m={profile["validated_p90_abs_error_m"]}

static constexpr xiao_nrf54l15::BleCsCalibrationProfile {identifier} {{
    {float(profile["scale"]):.6f}f,
    {float(profile["offset_m"]):.4f}f,
    {reference_distance:.4f}f,
    {measured_median:.4f}f,
    {measured_mad:.4f}f,
    {board_pair_bias:.4f}f,
    {board_pair_delay:.4f}f,
    {per_board_delay:.4f}f,
    {validated_median:.4f}f,
    {validated_mad:.4f}f,
    {validated_p90:.4f}f,
    {int(profile["sample_count"])}U,
    {int(profile["validated_sample_count"])}U,
}};
"""
    with path.open("w", encoding="utf-8") as handle:
        handle.write(header)


def maybe_emit_profile(
    *,
    json_path: str,
    header_path: str,
    profile_name: str,
    metric: str,
    scale: float,
    offset: float,
    reference_distance: float,
    measured_median: float,
    measured_mad: float,
    sample_count: int,
    board_pair: str,
    notes: str,
    source_logs: list[str],
    source_points: list[dict[str, float | str]],
    validation: dict[str, float] | None = None,
) -> None:
    if not json_path and not header_path:
        return
    if not profile_name:
        raise RuntimeError("--profile-name is required when emitting a profile")

    profile = build_profile_dict(
        profile_name=profile_name,
        metric=metric,
        scale=scale,
        offset=offset,
        reference_distance=reference_distance,
        measured_median=measured_median,
        measured_mad=measured_mad,
        sample_count=sample_count,
        board_pair=board_pair,
        notes=notes,
        source_logs=source_logs,
        source_points=source_points,
        validation=validation,
    )
    if json_path:
        write_profile_json(Path(json_path).expanduser().resolve(), profile)
    if header_path:
        write_profile_header(Path(header_path).expanduser().resolve(), profile)


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
    emitted = False
    for raw_path in args.logs:
        path = Path(raw_path).expanduser().resolve()
        values = extract_metric_values(path, args.metric)
        file_ok = print_summary(path, args.metric, values)
        ok &= file_ok
        if file_ok and math.isfinite(args.reference_distance):
            summary = summarize(values)
            median_value = summary["median"]
            recommended_offset = args.reference_distance - median_value
            pair_bias_m, pair_delay_ns, symmetric_per_board_delay_ns = characterize_board_pair(
                args.reference_distance, median_value
            )
            validation_summary = None
            print(
                "  "
                f"reference_m={args.reference_distance:.4f} "
                f"recommended_offset_m={recommended_offset:.4f}"
            )
            if pair_bias_m is not None and pair_delay_ns is not None:
                print(
                    "  "
                    f"board_pair_bias_m={pair_bias_m:.4f} "
                    f"board_pair_delay_ns={pair_delay_ns:.4f} "
                    f"symmetric_per_board_delay_ns={symmetric_per_board_delay_ns:.4f}"
                )
            if median_value != 0.0:
                print(
                    "  "
                    f"single_point_scale={args.reference_distance / median_value:.6f}"
                )
            if args.validation_log:
                validation_path = Path(args.validation_log).expanduser().resolve()
                validation_values = extract_metric_values(validation_path, args.metric)
                validation_summary = summarize_validation(
                    validation_values, args.reference_distance
                )
                if validation_summary is None:
                    raise RuntimeError(
                        f"No positive validated {args.metric} samples found in {validation_path}"
                    )
                print(
                    "  "
                    f"validation_samples={int(validation_summary['count'])} "
                    f"validation_median={validation_summary['median']:.4f} "
                    f"validation_mad={validation_summary['mad']:.4f} "
                    f"validation_p90_abs_error_m={validation_summary['p90_abs_error']:.4f}"
                )
            maybe_emit_profile(
                json_path=args.emit_profile_json,
                header_path=args.emit_profile_header,
                profile_name=args.profile_name,
                metric=args.metric,
                scale=1.0,
                offset=recommended_offset,
                reference_distance=args.reference_distance,
                measured_median=median_value,
                measured_mad=summary["mad"],
                sample_count=int(summary["count"]),
                board_pair=args.board_pair,
                notes=args.notes,
                source_logs=(
                    [str(path), str(Path(args.validation_log).expanduser().resolve())]
                    if args.validation_log
                    else [str(path)]
                ),
                source_points=[
                    {
                        "actual_m": args.reference_distance,
                        "measured_median_m": median_value,
                        "log_path": str(path),
                    }
                ],
                validation=validation_summary,
            )
            emitted = bool(args.emit_profile_json or args.emit_profile_header)

    if emitted:
        print("profile_artifact=written")

    return 0 if ok else 1


def run_fit(args: argparse.Namespace) -> int:
    csv_path = Path(args.csv_path).expanduser().resolve()
    points: list[tuple[float, float]] = []
    source_points: list[dict[str, float | str]] = []

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
            source_points.append(
                {
                    "actual_m": actual_m,
                    "measured_median_m": measured_median,
                    "log_path": str(log_path),
                }
            )
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
    print("sketch_profile:")
    print("  kCalibrationProfileDefault = {")
    print(f"    .scale = {scale:.6f}f,")
    print(f"    .offsetMeters = {offset:.4f}f,")
    print("  }")
    maybe_emit_profile(
        json_path=args.emit_profile_json,
        header_path=args.emit_profile_header,
        profile_name=args.profile_name,
        metric=args.metric,
        scale=scale,
        offset=offset,
        reference_distance=math.nan,
        measured_median=math.nan,
        measured_mad=math.nan,
        sample_count=len(points),
        board_pair=args.board_pair,
        notes=args.notes,
        source_logs=[str(csv_path)],
        source_points=source_points,
    )
    if args.emit_profile_json or args.emit_profile_header:
        print("profile_artifact=written")
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
