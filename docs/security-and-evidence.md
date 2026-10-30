# Security and evidence limitations

The Control Center binds to loopback only. API requests require a loopback Host and reject foreign Origin headers; state-changing requests require the custom X-NeuroPace-Control header. This reduces cross-site request risk but **does not authenticate local processes**. Do not expose the dashboard to the network.

Actuator validation rejects negative TGP boosts and malformed confidence values. A non-null process handle does not imply safe access rights: audit OpenProcess/OpenThread access masks at acquisition sites before claiming comprehensive handle protection.

The included ML model is based on synthetic telemetry. Independent demonstration of future-frame predictive accuracy and causal FPS improvements remains outstanding. Evaluate on real time-aligned game sessions using session-grouped and chronological splits, shadow mode, calibration metrics and raw frametime recordings. No measured performance improvement is asserted by this PR.
