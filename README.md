# NeuroPace RDNA

**Advanced DPC Latency & GPU Optimization Engine**

NeuroPace RDNA is a high-performance system-level agent designed exclusively for AMD RDNA3 architectures. It operates purely in User-Mode (Ring-3) to dynamically mitigate DPC (Deferred Procedure Call) latency spikes and GPU thermal throttling that induce frame pacing variance in real-time workloads.

![Dashboard Preview](docs/images/dashboard-preview.png)

## Architecture

The system is decoupled into four microservices communicating over ultra-low-latency Named Pipes (IPC), ensuring no overhead to the target application context.

![Architecture Diagram](docs/images/architecture.png)

### Modules

1. **Telemetry Collector (C++)**
   - High-frequency polling engine capturing Event Tracing for Windows (ETW) kernel DPC/ISR events.
   - Polling AMD ADLX SDK for precise edge thermal and board power limits.

2. **AI Inference Engine (Python)**
   - Pre-trained ONNX Random Forest model deployed with ONNXRuntime.
   - Evaluates telemetry rolling windows (1000 samples) to probabilistically forecast latency spikes.

3. **Hardware Actuator (C++)**
   - System resource scheduler applying targeted mitigations based on AI inference confidence.
   - Features thread affinity rebalancing and dynamic TGP (Total Graphics Power) limit modifications via AMD ADLX.

4. **Control Center (Node.js/Vue 3)**
   - Unified observability and module orchestration platform.
   - Provides process lifetime management, live metric aggregation, and structured audit logs.

## Security & Compliance Model

NeuroPace is designed with rigid adherence to anti-cheat interoperability:
- Zero memory access to running user-space processes outside the daemon.
- Zero undocumented kernel-level system calls or driver dependencies.
- Static thread validation to ensure handles do not hold `VM_READ` or `GET_CONTEXT` permission boundaries.

## Build Requirements

- Windows 10 / 11 (64-bit)
- Visual Studio 2022 (MSVC Toolset)
- CMake 3.25+
- Node.js LTS (v18+)

See `docs/BUILDING.md` for specific vcpkg and CMake execution commands.

## License

Copyright © Ediz Sönmez 2026 NeuroPace Engineering. All rights reserved.
