"""Record a Windows performance session without changing game settings."""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import time
from uuid import uuid4

from session_analyzer import QPC_MS_COLUMNS, load_presentmon, presentmon_report


def stop_sampler(process: subprocess.Popen) -> None:
    if process.stdin:
        process.stdin.close()
    try:
        status = process.wait(timeout=10)
    except subprocess.TimeoutExpired as exc:
        process.kill()
        process.wait()
        raise RuntimeError("Telemetry did not stop after its control pipe closed") from exc
    if status:
        raise RuntimeError(f"Telemetry exited with code {status}. See telemetry.log")


def capture(args: argparse.Namespace) -> dict:
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    frames_path = directory / "frames.csv"
    sensor_path = directory / "telemetry.csv"
    command = [
        str(args.presentmon.resolve()), "--process_id", str(args.pid),
        "--session_name", f"NeuroPace-{uuid4()}", "--timed", str(args.seconds),
        "--terminate_after_timed", "--terminate_on_proc_exit", "--no_console_stats",
        "--no_track_input", "--v2_metrics", "--qpc_time_ms",
        "--output_file", str(frames_path),
    ]
    with (directory / "telemetry.log").open("wb") as sensor_log:
        sampler = None
        try:
            if args.telemetry:
                sampler = subprocess.Popen(
                    [str(args.telemetry.resolve()), "--session-csv", str(sensor_path)],
                    stdin=subprocess.PIPE, stdout=sensor_log, stderr=subprocess.STDOUT,
                )
                deadline = time.monotonic() + 12
                while not sensor_path.exists() or sensor_path.stat().st_size == 0:
                    if sampler.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("Telemetry did not produce samples. See telemetry.log")
                    time.sleep(0.05)
            with (directory / "presentmon.log").open("wb") as presentmon_log:
                with subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=presentmon_log,
                                      stderr=subprocess.STDOUT) as recorder:
                    try:
                        status = recorder.wait(timeout=args.seconds + 20)
                    except (KeyboardInterrupt, subprocess.TimeoutExpired):
                        recorder.kill()
                        raise
                if status:
                    raise RuntimeError(f"PresentMon exited with code {status}. See presentmon.log")
        finally:
            if sampler:
                stop_sampler(sampler)

    frames, stream = load_presentmon(frames_path, "displayed", args.pid, args.swap_chain)
    if stream["timestamp_column"] not in QPC_MS_COLUMNS or not stream["display_offset_column"]:
        raise ValueError("Capture did not include QPC milliseconds and display offsets")
    report = presentmon_report(frames, stream, args.threshold_ms,
                               sensor_path if args.telemetry else None, 0, args.context_ms)
    report["capture"] = {
        "requested_seconds": args.seconds,
        "mode": "telemetry" if args.telemetry else "frames_only",
        "clock": "Windows QPC milliseconds",
        "presentmon_executable": args.presentmon.name,
    }
    (directory / "report.json").write_text(
        json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8",
    )
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True, help="PID of the running game")
    parser.add_argument("--presentmon", type=Path, required=True, help="PresentMon console EXE")
    parser.add_argument("--telemetry", type=Path, help="Optional NeuroPace telemetry EXE")
    parser.add_argument("--output-dir", type=Path, required=True, help="New session directory")
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--swap-chain")
    parser.add_argument("--threshold-ms", type=float, default=33.3)
    parser.add_argument("--context-ms", type=float, default=250)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Live capture requires Windows")
    if args.pid <= 0 or not 1 <= args.seconds <= 3600:
        parser.error("Use a positive PID and a duration from 1 to 3600 seconds")
    if any(not math.isfinite(value) or value <= 0 for value in (args.threshold_ms, args.context_ms)):
        parser.error("Threshold and context window must be positive finite numbers")
    for binary in (args.presentmon, args.telemetry):
        if binary and not binary.is_file():
            parser.error(f"Executable not found: {binary}")
    try:
        capture(args)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        parser.exit(1, f"Capture failed: {exc}\n")
    except KeyboardInterrupt:
        parser.exit(130, "Capture interrupted. Raw files may be incomplete.\n")
    print(args.output_dir.resolve() / "report.json")


if __name__ == "__main__":
    main()
