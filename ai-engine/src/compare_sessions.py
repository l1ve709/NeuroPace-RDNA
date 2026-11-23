"""Compare two compatible NeuroPace session reports."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


METRICS = ("mean_fps", "median_frametime_ms", "p95_frametime_ms",
           "p99_frametime_ms", "spikes_per_minute")


def compare(baseline: dict, candidate: dict) -> dict:
    for report in (baseline, candidate):
        if (not isinstance(report, dict) or report.get("schema_version") != 2
                or report.get("source") != "presentmon" or not isinstance(report.get("stream"), dict)):
            raise ValueError("Both inputs must be PresentMon session reports")

    for field in ("application", "metric", "duration_column"):
        left, right = baseline["stream"].get(field), candidate["stream"].get(field)
        if not isinstance(left, str) or not isinstance(right, str) or left.casefold() != right.casefold():
            raise ValueError(f"Session {field} differs or is missing")
    if baseline.get("spike_threshold_ms") != candidate.get("spike_threshold_ms"):
        raise ValueError("Session spike thresholds differ")

    measurements = {}
    for field in METRICS:
        left, right = baseline.get(field), candidate.get(field)
        if any(not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0
               for value in (left, right)):
            raise ValueError(f"Both sessions need finite, nonnegative {field}")
        measurements[field] = {"baseline": left, "candidate": right, "delta": right - left}
    warnings = []
    if any(report.get("frame_count", 0) < 100 for report in (baseline, candidate)):
        warnings.append("At least one run has fewer than 100 measured frames. Interpret P99 cautiously.")
    return {
        "application": baseline["stream"]["application"],
        "metric": baseline["stream"]["metric"],
        "baseline_frame_count": baseline.get("frame_count"),
        "candidate_frame_count": candidate.get("frame_count"),
        "baseline_span_ms": baseline.get("observed_span_ms"),
        "candidate_span_ms": candidate.get("observed_span_ms"),
        "measurements": measurements,
        "warnings": warnings,
        "note": "Pairwise observation only. Repeat comparable runs to estimate run-to-run variation.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    try:
        left = json.loads(args.baseline.read_text(encoding="utf-8"))
        right = json.loads(args.candidate.read_text(encoding="utf-8"))
        print(json.dumps(compare(left, right), indent=2, allow_nan=False))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
