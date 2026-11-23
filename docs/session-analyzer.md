# Measured Frametime Session Analyzer (experimental)

This portable CLI analyzes per-frame timings from a normalized CSV or a
PresentMon console capture. It operates offline and makes no system changes.

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

The normalized input retains the original report format. It reports observed
spikes, median/mean/P95/P99 frametime and FPS calculated from the durations.

## PresentMon capture

On Windows the capture command can start PresentMon and an optional NeuroPace
telemetry sampler together. Provide a running game's PID and a new output
directory. Use the ADLX build for supported Radeon GPU metrics. Run the shell
with administrator privileges for kernel ETW events.

```powershell
python ai-engine/src/capture_session.py --pid 1234 --presentmon C:/Tools/PresentMon.exe --telemetry telemetry/build/Release/neuropace-telemetry.exe --seconds 30 --output-dir sessions/run-01
```

The session directory retains raw frame and telemetry CSVs, logs and the JSON
report. Both captures use the Windows performance-counter clock. Telemetry is
sampled roughly every 33 ms, so nearby samples provide context rather than exact
GPU or kernel event times. Omit `--telemetry` for a frames-only baseline.
If the game uses multiple swap chains, keep the raw files and re-run the
offline command with `--pid` and `--swap-chain`. For these same-session files use
`--telemetry-offset-ms 0`.

Compare two reports after measuring a repeatable scene with consistent settings:

```powershell
python ai-engine/src/compare_sessions.py sessions/baseline/report.json sessions/run-01/report.json
```

The comparison checks the application and metric and gives the observed deltas.
Use alternating, repeated runs before drawing conclusions about overhead.

## Import an existing PresentMon CSV

Pass one CSV from the [PresentMon console](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md).
When multiple streams match, the CLI lists their application, PID and swap-chain
address. Select the game and its intended swap chain before comparing results.

```powershell
python ai-engine/src/session_analyzer.py game.csv --format presentmon --metric displayed --pid 1234 --swap-chain 0x124A --threshold-ms 30 --output report.json
```

| Metric | Supported fields |
| --- | --- |
| `presented` | `MsBetweenPresents` |
| `displayed` | `DisplayedTime` or `MsBetweenDisplayChange` |
| `cpu` | `FrameTime` or `MsBetweenAppStart` |

Compare runs using the same metric, thresholds and capture settings. The default
is `presented`. A capture that omits the requested measurement fails explicitly.
Blank, NA and zero durations are counted as unmeasured and excluded from
statistics. The report identifies the selected columns and the longest measured
frame. Rates use the span between the first and last measured frame timestamps.

For display measurements, the analyzer adds `DisplayLatency` or
`MsUntilDisplayed` when present to locate the display instant. For legacy
`MsBetweenDisplayChange` that instant marks the end of the measured interval.
For `DisplayedTime` it marks the beginning. Without an offset column the report
retains the row timestamp and records that the offset was unavailable.

The importer accepts `TimeInSeconds`, `TimeInMs`, `CPUStartTime` and
`CPUStartQPCTime`. For raw `CPUStartQPC` or `TimeInQPC` supply the originating
capture machine's counter frequency with `--qpc-frequency-hz`.

## Optional GPU and ETW context

Pass `--telemetry telemetry.csv --telemetry-offset-ms OFFSET --context-ms 250`.
Use a CSV with `timestamp_ms` and the available GPU/ETW fields shown here:

```csv
timestamp_ms,gpu_clock_mhz,gpu_temp_c,gpu_tgp_w,gpu_utilization_pct,vram_used_mb,dpc_latency_us,isr_latency_us,dpc_count,isr_count
1100,2300,67,190,80,4200,9.2,0.8,1200,42
```

The offset is added to telemetry time to map it to the selected PresentMon
clock. Use zero only when the clock and origin already match. For example,
with the same Windows boot and QPC origin, `--qpc_time_ms` can export PresentMon
timestamps in milliseconds. Existing NeuroPace pipe timestamps are relative to
the publisher's own start and require a separate alignment anchor. Convert
microseconds to milliseconds before applying the offset.

The report lists the nearest samples on either side of the longest frame,
counts samples in the requested window, and distinguishes absent telemetry
from telemetry outside the window. Leave unavailable fields blank. Timing
coincidence alone cannot establish a cause.
