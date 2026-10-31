"""Deterministic, offline frametime analysis without synthetic performance claims.

Input CSV requires timestamp_ms and frame_time_ms columns. This module does not
claim causality; it reports observed frame delivery statistics and spike events.
"""
from __future__ import annotations
import csv
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from statistics import mean, median
from typing import Iterable

@dataclass(frozen=True)
class Frame:
    timestamp_ms: float
    frame_time_ms: float

def load_frames(path: str | Path) -> list[Frame]:
    frames: list[Frame] = []
    with open(path, newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or not {"timestamp_ms", "frame_time_ms"}.issubset(reader.fieldnames):
            raise ValueError("CSV must contain timestamp_ms and frame_time_ms")
        last_timestamp = -math.inf
        for line_number, row in enumerate(reader, start=2):
            try:
                timestamp = float(row["timestamp_ms"])
                duration = float(row["frame_time_ms"])
            except (ValueError, TypeError, KeyError) as exc:
                raise ValueError(f"Invalid frame at CSV line {line_number}") from exc
            if not (math.isfinite(timestamp) and math.isfinite(duration)):
                raise ValueError(f"Non-finite frame at CSV line {line_number}")
            if timestamp < 0 or timestamp <= last_timestamp or duration <= 0:
                raise ValueError(f"Non-monotonic timestamp or invalid frametime at line {line_number}")
            frames.append(Frame(timestamp, duration))
            last_timestamp = timestamp
    if not frames:
        raise ValueError("No frame records")
    return frames

def percentile(values: list[float], fraction: float) -> float:
    """Linear-interpolated nearest-rank-style percentile, explicitly documented."""
    if not values or not 0 <= fraction <= 1:
        raise ValueError("Invalid percentile input")
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    low = math.floor(index)
    high = math.ceil(index)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)

def analyze(frames: Iterable[Frame], threshold_ms: float = 33.3) -> dict:
    frames = list(frames)
    if not frames or not math.isfinite(threshold_ms) or threshold_ms <= 0:
        raise ValueError("Need frames and a positive finite threshold")
    last = -math.inf
    for frame in frames:
        if (not math.isfinite(frame.timestamp_ms) or
                not math.isfinite(frame.frame_time_ms) or
                frame.timestamp_ms <= last or frame.frame_time_ms <= 0):
            raise ValueError("Invalid or unordered frames")
        last = frame.timestamp_ms
    durations = [f.frame_time_ms for f in frames]
    # FPS derived from total delivered frame time; not the arithmetic mean of instant FPS.
    total_ms = sum(durations)
    events = [asdict(f) for f in frames if f.frame_time_ms > threshold_ms]
    return {
        "schema_version": 1,
        "source": "measured_frametimes_only",
        "frame_count": len(frames),
        "mean_fps": 1000.0 * len(frames) / total_ms,
        "median_frametime_ms": median(durations),
        "mean_frametime_ms": mean(durations),
        "p95_frametime_ms": percentile(durations, 0.95),
        "p99_frametime_ms": percentile(durations, 0.99),
        "spike_threshold_ms": threshold_ms,
        "spike_count": len(events),
        "spikes": events,
        "note": "Temporal coincidence does not establish the cause of a spike.",
    }

def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="Analyze real frametime CSV recordings")
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--threshold-ms", type=float, default=33.3)
    parser.add_argument("--output", type=Path, help="Write JSON report to this path")
    args = parser.parse_args()
    report = analyze(load_frames(args.csv_path), args.threshold_ms)
    output = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    else:
        print(output)

if __name__ == "__main__":
    main()
