"""Portable regression tests for the independent frametime analysis core."""
import csv
import math
import sys
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from session_analyzer import Frame, analyze, load_frames, percentile

def test_known_frame_statistics():
    frames = [Frame(i * 20.0, duration) for i, duration in enumerate([10., 10., 10., 50.])]
    report = analyze(frames, threshold_ms=30.0)
    assert report["frame_count"] == 4
    assert report["mean_fps"] == 50.0
    assert report["median_frametime_ms"] == 10.0
    assert report["p99_frametime_ms"] == pytest.approx(48.8)
    assert report["spike_count"] == 1
    assert report["spikes"][0]["timestamp_ms"] == 60.0

def test_invalid_input_rejected():
    for frames in ([], [Frame(0, 0)], [Frame(0, math.nan)],
                   [Frame(1, 10), Frame(1, 12)], [Frame(10, 10), Frame(5, 20)]):
        with pytest.raises(ValueError):
            analyze(frames)
    with pytest.raises(ValueError):
        analyze([Frame(0, 16)], float("nan"))

def test_csv_validation(tmp_path):
    p = tmp_path / "frames.csv"
    p.write_text("timestamp_ms,frame_time_ms\n0,16\n16,17\n", encoding="utf-8")
    assert len(load_frames(p)) == 2
    p.write_text("timestamp_ms,frame_time_ms\n0,16\n0,17\n", encoding="utf-8")
    with pytest.raises(ValueError):
        load_frames(p)
    p.write_text("foo,bar\n1,2\n", encoding="utf-8")
    with pytest.raises(ValueError):
        load_frames(p)

def test_percentile_boundaries():
    assert percentile([1, 3], 0) == 1
    assert percentile([1, 3], 1) == 3
    assert percentile([1, 3], .5) == 2
