# Measured Frametime Session Analyzer (experimental)

This portable analysis module accepts **real measured** per-frame timings in CSV format. It does not generate synthetic benchmark claims, change GPU settings, or infer causal stutter explanations.

## Input contract

```csv
timestamp_ms,frame_time_ms
0,16.2
16.2,16.5
32.7,41.0
73.7,16.1
```

Each timestamp is nonnegative and strictly increasing. Frametime must be positive and finite. Timestamps must refer to a single, consistent clock and are expected to represent each frame's observation time. Do not combine streams from different clocks without alignment.

## Usage

```powershell
python ai-engine/src/session_analyzer.py samples.csv --threshold-ms 30 --output session-report.json
python -m pytest ai-engine/tests/test_session_analyzer.py -q
```

The report contains observed spike events, median/mean/P95/P99 frametime and mean FPS calculated from the durations. It does not claim that a spike was caused by any individual hardware signal. This is an **offline analysis foundation**; capture integration, session persistence and the user interface are not part of this patch.

For now export frame timing recordings from a trusted capture tool and convert its output to the two-column input schema above. File compatibility with third-party PresentMon output is **not** yet implemented or promised.
