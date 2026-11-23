"""Portable regression tests for the independent frametime analysis core."""
import csv
import math
import sys
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from session_analyzer import Frame, analyze, load_frames, load_presentmon, percentile, presentmon_report

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


def test_presentmon_selects_stream_and_keeps_presented_and_displayed_separate(tmp_path):
    capture = tmp_path / "capture.csv"
    capture.write_text(
        "Application,ProcessID,SwapChainAddress,TimeInSeconds,msBetweenPresents,"
        "msBetweenDisplayChange,msUntilDisplayed\n"
        "game.exe,123,0x001,0.020,20,18,3\n"
        "game.exe,123,0x002,0.025,25,23,3\n"
        "game.exe,123,0x001,0.070,50,NA,NA\n"
        "game.exe,123,0x001,0.075,5,55,2\n"
        "other.exe,456,0x001,0.100,100,100,3\n",
        encoding="utf-8-sig",
    )
    with pytest.raises(ValueError, match="Select exactly one stream"):
        load_presentmon(capture, "presented", 123, None)
    frames, stream = load_presentmon(capture, "presented", 123, "0x1")
    report = presentmon_report(frames, stream, 30, None, None, 250)
    assert report["frame_count"] == 3
    assert report["p99_frametime_ms"] == pytest.approx(49.4)
    assert report["longest_frame"] == {"timestamp_ms": 70.0, "frame_time_ms": 50.0}
    assert report["telemetry_context"]["status"] == "not_provided"
    displayed, stream = load_presentmon(capture, "displayed", 123, "0x1")
    assert displayed == [Frame(23, 18), Frame(77, 55)]
    assert stream["unmeasured_rows"] == 1


def test_presentmon_raw_qpc_and_aligned_context(tmp_path):
    capture = tmp_path / "capture.csv"
    capture.write_text(
        "Application,ProcessID,SwapChainAddress,CPUStartQPC,FrameTime,DisplayLatency,DisplayedTime\n"
        "game.exe,123,0x1,1000000,15,5,16\n"
        "game.exe,123,0x1,1100000,10,9,45\n"
        "game.exe,123,0x1,1200000,10,NA,NA\n",
    )
    with pytest.raises(ValueError, match="qpc-frequency"):
        load_presentmon(capture, "displayed", None, None)
    frames, stream = load_presentmon(capture, "displayed", None, None, 10_000_000)
    telemetry = tmp_path / "telemetry.csv"
    telemetry.write_text("timestamp_ms,gpu_temp_c,dpc_latency_us\n"
                         "100,,3\n106,67,\n109,69,\n")
    with pytest.raises(ValueError, match="telemetry-offset"):
        presentmon_report(frames, stream, 30, telemetry, None, 2)
    report = presentmon_report(frames, stream, 30, telemetry, 12, 2)
    assert report["longest_frame"] == {"timestamp_ms": 119, "frame_time_ms": 45}
    context = report["telemetry_context"]
    assert context["gpu"]["nearest_before"]["gpu_temp_c"] == 67
    assert context["gpu"]["nearest_at_or_after"]["offset_ms"] == 2
    assert context["etw"]["status"] == "outside_window"
