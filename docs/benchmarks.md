# Performance evaluation

No reproducible benchmark dataset is included. Earlier numerical improvement tables were unsupported by raw recordings and have been removed. Synthetic model validation does not demonstrate game performance improvement.

To evaluate a change, record individual present intervals using a suitable capture tool, preserve the raw data and exact tool version, and document hardware, drivers, game version, settings and background load. Alternate baseline and enabled runs, include repeated sessions, and report variability as well as averages.

Evaluate prediction separately using chronological, session-separated recordings with a defined future outcome and prediction horizon. Report calibration, false positives and false negatives. Measure CPU use, memory, wakeups and inference/IPC latency on the same machine.

The dashboard's reciprocal FPS interval is unsuitable for per-frame percentile analysis. The offline [session analyzer](session-analyzer.md) accepts measured frame intervals and reports observed statistics; it does not attribute causality.
