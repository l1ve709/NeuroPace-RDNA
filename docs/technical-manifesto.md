# Implementation boundaries

NeuroPace is an experimental user-mode application. ETW observes DPC/ISR execution durations; it does not reprioritize interrupts. ADLX supplies driver metrics; no wavefront, cache-miss or GPU queue instrumentation is implemented.

The Python engine computes 28 rolling features and runs an ONNX classifier trained on synthetic examples. Its output is an experimental recommendation. Threshold-based contributing factors are heuristics, not model explanations or causal diagnoses. Online training is disabled by default.

The scheduler uses process affinity and thread priority APIs. These changes are experimental and can reduce performance. The Windows scheduler, application and driver retain control. No real-time scheduling guarantee exists.

GPU power writes are disabled. ADLX SetPowerLimit expects a percentage, whereas the previous implementation supplied an assumed watt limit. A future backend requires capability/range queries, restoration of the actual original setting, and hardware validation.

No measured performance improvement, sub-millisecond transport guarantee, fixed CPU overhead or universal anti-cheat compatibility is asserted.
