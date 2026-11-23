import csv
import os
from pathlib import Path
import subprocess
import time

import pytest

from capture_session import stop_sampler
from compare_sessions import METRICS, compare


def test_capture_sampler_uses_qpc_and_stops_when_parent_closes_pipe(tmp_path):
    executable = os.environ.get("NEUROPACE_TEST_TELEMETRY")
    if not executable:
        pytest.skip("Set NEUROPACE_TEST_TELEMETRY to the built telemetry EXE")
    output = tmp_path / "telemetry.csv"
    before = time.perf_counter() * 1000
    process = subprocess.Popen(
        [str(Path(executable).resolve()), "--session-csv", str(output)],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline and process.poll() is None:
            if output.exists() and output.read_bytes().count(b"\n") >= 3:
                break
            time.sleep(0.05)
        else:
            pytest.fail("Sampler did not write at least two rows")
    finally:
        stop_sampler(process)
    after = time.perf_counter() * 1000
    with output.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    times = [float(row["timestamp_ms"]) for row in rows]
    assert before <= times[0] < times[-1] <= after
    assert all(a < b for a, b in zip(times, times[1:]))
    assert all(row["gpu_clock_mhz"] == "" for row in rows)  # The CI executable has ADLX disabled.


def test_compare_rejects_changes_to_the_metric_or_threshold():
    report = dict.fromkeys(METRICS, 10)
    report.update(schema_version=2, source="presentmon", spike_threshold_ms=33.3,
                  stream={"application": "game.exe", "metric": "displayed", "duration_column": "displayedtime"})
    candidate = dict(report, p99_frametime_ms=8)
    assert compare(report, candidate)["measurements"]["p99_frametime_ms"]["delta"] == -2
    with pytest.raises(ValueError, match="thresholds"):
        compare(report, dict(candidate, spike_threshold_ms=50))
    with pytest.raises(ValueError, match="metric"):
        compare(report, dict(candidate, stream=dict(report["stream"], metric="cpu")))
