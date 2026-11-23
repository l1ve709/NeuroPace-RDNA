"""Analyze normalized frametimes or an offline PresentMon capture."""
from __future__ import annotations
import csv
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from statistics import mean, median
from typing import Iterable
from bisect import bisect_left, bisect_right

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
                frame.timestamp_ms < 0 or frame.timestamp_ms <= last or frame.frame_time_ms <= 0):
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


METRIC_COLUMNS = {
    "presented": ("msbetweenpresents",),
    "displayed": ("displayedtime", "msbetweendisplaychange"),
    "cpu": ("frametime", "msbetweenappstart"),
}
TIME_COLUMNS = (
    ("timeinms", 1.0),
    ("timeinseconds", 1000.0),
    ("cpustartqpctime", 1.0),
    ("cpustarttime", 1.0),
)
SENSOR_FIELDS = {
    "gpu": ("gpu_clock_mhz", "gpu_temp_c", "gpu_tgp_w", "gpu_utilization_pct", "vram_used_mb"),
    "etw": ("dpc_latency_us", "isr_latency_us", "dpc_count", "isr_count"),
}


def _number(value: str | None, field: str, line: int) -> float:
    try:
        number = float(value)
    except (ValueError, TypeError) as exc:
        raise ValueError(f"Invalid {field} at CSV line {line}") from exc
    if not math.isfinite(number) or number < 0:
        raise ValueError(f"Invalid {field} at CSV line {line}")
    return number


def _swap_key(value: str) -> str:
    value = value.strip().casefold()
    try:
        return hex(int(value, 16)) if value.startswith("0x") else value
    except ValueError:
        return value


def load_presentmon(path: Path, metric: str, pid: int | None, swap_chain: str | None,
                    qpc_frequency: int | None = None) -> tuple[list[Frame], dict]:
    """Read one stream, requiring explicit selection whenever capture is ambiguous."""
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        reader.fieldnames = [field.strip().casefold() for field in reader.fieldnames or []]
        columns = set(reader.fieldnames)
        if len(columns) != len(reader.fieldnames) or not {
            "application", "processid", "swapchainaddress"
        }.issubset(columns):
            raise ValueError("PresentMon CSV needs unique Application, ProcessID and SwapChainAddress columns")
        duration_column = next((name for name in METRIC_COLUMNS[metric] if name in columns), None)
        if duration_column is None:
            raise ValueError(f"No {metric} duration column. Expected one of {', '.join(METRIC_COLUMNS[metric])}")
        time_columns = TIME_COLUMNS
        cpu_timestamp = metric == "cpu" or duration_column == "displayedtime"
        if cpu_timestamp:
            time_columns = (*TIME_COLUMNS[2:], *TIME_COLUMNS[:2])
        if qpc_frequency is not None:
            if qpc_frequency <= 0:
                raise ValueError("--qpc-frequency-hz must be positive")
            raw_column = "cpustartqpc" if cpu_timestamp else "timeinqpc"
            time_columns = ((raw_column, 1000.0 / qpc_frequency), *time_columns)
        time_column, scale = next(((name, factor) for name, factor in time_columns if name in columns), (None, 1))
        if time_column is None:
            raise ValueError("No supported timestamp column. Export --qpc_time_ms or supply --qpc-frequency-hz for raw QPC")

        # Display time uses the display instant when the export includes its offset.
        display_offset = {"displayedtime": "displaylatency",
                          "msbetweendisplaychange": "msuntildisplayed"}.get(duration_column)
        display_offset = display_offset if display_offset in columns else None
        groups = {}
        rows = []
        for line, row in enumerate(reader, 2):
            try:
                process_id = int(row["processid"])
            except (TypeError, ValueError) as exc:
                raise ValueError(f"Invalid ProcessID at CSV line {line}") from exc
            address = _swap_key(row["swapchainaddress"] or "")
            if process_id <= 0 or not address:
                raise ValueError(f"Invalid process or swap chain at CSV line {line}")
            if pid is not None and process_id != pid:
                continue
            if swap_chain is not None and address != _swap_key(swap_chain):
                continue
            key = (process_id, address, (row["application"] or "").strip())
            groups[key] = groups.get(key, 0) + 1
            rows.append((line, row))
        if len(groups) != 1:
            candidates = "; ".join(f"{app} PID {p} swap {s} ({count} rows)"
                                   for (p, s, app), count in sorted(groups.items()))
            raise ValueError(f"Select exactly one stream with --pid and --swap-chain. Matches: {candidates or 'none'}")

        frames = []
        for line, row in rows:
            if row[duration_column] is None:
                raise ValueError(f"Missing {duration_column} at CSV line {line}")
            value = (row[duration_column] or "").strip()
            if value.casefold() in {"", "na", "n/a"}:
                continue
            duration = _number(value, duration_column, line)
            if duration == 0:
                continue
            timestamp = _number(row[time_column], time_column, line) * scale
            if display_offset:
                timestamp += _number(row[display_offset], display_offset, line)
            frames.append(Frame(timestamp, duration))
        frames.sort(key=lambda frame: frame.timestamp_ms)
        process_id, address, application = next(iter(groups))
        metadata = {
            "application": application, "pid": process_id, "swap_chain": address,
            "metric": metric, "duration_column": duration_column,
            "timestamp_column": time_column, "display_offset_column": display_offset,
            "selected_rows": len(rows), "unmeasured_rows": len(rows) - len(frames),
        }
        return frames, metadata


def sensor_context(path: Path, timestamp: float, offset: float, window: float) -> dict:
    """Match sampled telemetry in a bounded window with explicit clock alignment."""
    samples = {source: [] for source in SENSOR_FIELDS}
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if "timestamp_ms" not in (reader.fieldnames or []):
            raise ValueError("Telemetry CSV needs a timestamp_ms column")
        for line, row in enumerate(reader, 2):
            time_ms = _number(row["timestamp_ms"], "timestamp_ms", line) + offset
            if not math.isfinite(time_ms):
                raise ValueError(f"Aligned telemetry timestamp overflow at CSV line {line}")
            for source, fields in SENSOR_FIELDS.items():
                values = {field: _number(row[field], field, line) for field in fields
                          if (row.get(field) or "").strip().casefold() not in {"", "na", "n/a"}}
                if values:
                    samples[source].append((time_ms, values))
    context = {}
    for source, values in samples.items():
        values.sort(key=lambda entry: entry[0])
        times = [time for time, _ in values]
        start = bisect_left(times, timestamp - window)
        split = bisect_left(times, timestamp)
        end = bisect_right(times, timestamp + window)
        def sample(index):
            time, metrics = values[index]
            return {"timestamp_ms": time, "offset_ms": time - timestamp, **metrics}
        nearby = end - start
        status = "available" if nearby else "outside_window" if values else "missing"
        context[source] = {
            "status": status, "samples_in_window": nearby,
            "nearest_before": sample(split - 1) if split > start else None,
            "nearest_at_or_after": sample(split) if split < end else None,
        }
    return context


def presentmon_report(frames: list[Frame], metadata: dict, threshold: float, telemetry: Path | None,
                      offset: float | None, window: float) -> dict:
    report = analyze(frames, threshold)
    longest = max(frames, key=lambda frame: frame.frame_time_ms)
    elapsed = frames[-1].timestamp_ms - frames[0].timestamp_ms
    report.update(
        schema_version=2, source="presentmon", stream=metadata,
        observed_span_ms=elapsed,
        spikes_per_minute=report["spike_count"] * 60000 / elapsed if elapsed else None,
        longest_frame=asdict(longest),
        telemetry_context={"status": "not_provided"},
    )
    if telemetry is not None:
        if offset is None or not math.isfinite(offset):
            raise ValueError("Provide a finite --telemetry-offset-ms (0 if both inputs share a clock)")
        if not math.isfinite(window) or window <= 0:
            raise ValueError("--context-ms must be positive and finite")
        report["telemetry_context"] = {
            "status": "provided", "offset_ms": offset, "window_ms": window,
            **sensor_context(telemetry, longest.timestamp_ms, offset, window),
        }
    return report

def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="Analyze real frametime CSV recordings")
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--threshold-ms", type=float, default=33.3)
    parser.add_argument("--output", type=Path, help="Write JSON report to this path")
    parser.add_argument("--format", choices=("normalized", "presentmon"), default="normalized")
    parser.add_argument("--metric", choices=METRIC_COLUMNS, help="Defaults to presented for PresentMon")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--swap-chain")
    parser.add_argument("--qpc-frequency-hz", type=int, help="Tick frequency of the machine that recorded raw QPC")
    parser.add_argument("--telemetry", type=Path, help="Optional aligned telemetry CSV")
    parser.add_argument("--telemetry-offset-ms", type=float, help="Offset added to telemetry timestamps")
    parser.add_argument("--context-ms", type=float, default=250.0, help="Window on each side of the longest frame")
    args = parser.parse_args()
    try:
        if args.format == "presentmon":
            if args.telemetry_offset_ms is not None and args.telemetry is None:
                parser.error("--telemetry-offset-ms requires --telemetry")
            frames, metadata = load_presentmon(args.csv_path, args.metric or "presented", args.pid,
                                               args.swap_chain, args.qpc_frequency_hz)
            report = presentmon_report(frames, metadata, args.threshold_ms, args.telemetry,
                                       args.telemetry_offset_ms, args.context_ms)
        else:
            if (args.metric or args.pid is not None or args.swap_chain or args.qpc_frequency_hz is not None
                    or args.telemetry or args.telemetry_offset_ms is not None):
                parser.error("Stream selection and telemetry context require --format presentmon")
            report = analyze(load_frames(args.csv_path), args.threshold_ms)
        output = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
        if args.output:
            if args.output.resolve() in {path.resolve() for path in (args.csv_path, args.telemetry) if path}:
                raise ValueError("Output path must differ from input paths")
            args.output.write_text(output + "\n", encoding="utf-8")
        else:
            print(output)
    except (OSError, ValueError, csv.Error) as exc:
        parser.error(str(exc))

if __name__ == "__main__":
    main()
