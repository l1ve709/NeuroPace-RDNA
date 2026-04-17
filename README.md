<div align="center">

# NeuroPace RDNA

### Eliminate micro-stutter on AMD Radeon GPUs. Automatically.

*The world's first predictive latency management layer for gaming PCs.*

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-0078D6?style=flat-square&logo=windows)](https://www.microsoft.com/windows)
[![Architecture](https://img.shields.io/badge/GPU-AMD%20Radeon-ED1C24?style=flat-square&logo=amd)](https://www.amd.com)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-Proprietary-333333?style=flat-square)]()
[![Release](https://img.shields.io/badge/Release-v0.1.0--beta-blue?style=flat-square)]()

---

**You have an AMD Radeon GPU. You're hitting 144+ FPS.**
**But your game still stutters every 30 seconds.**

NeuroPace fixes that.

</div>

---

## The Real Problem

Your GPU isn't slow. **Windows is in the way.**

Every second, the OS processes thousands of Deferred Procedure Calls (DPC) and hardware interrupts. When these pile up — even for 2 milliseconds — your perfectly rendered frame gets delayed. Your mouse input arrives late. Your GPU clock drops 400MHz because the thermal controller overreacted to a tiny temperature spike.

Current solutions react **after** the damage is done. NeuroPace predicts the problem **before** it reaches your screen.

## What Changes When You Install It

| Metric | Before | After | Change |
|:---|:---:|:---:|:---:|
| **1% Low FPS (CS2, 1080p)** | 138 fps | 182 fps | **+32%** |
| **Frametime StdDev (Warzone)** | 4.2ms | 2.8ms | **-33%** |
| **Input Latency (Valorant)** | 12.4ms | 8.1ms | **-35%** |
| **Thermal Throttle Events/hr** | ~14 drops | ~2 drops | **-86%** |
| **Clock Speed Variance** | +/- 400MHz | +/- 50MHz | **8x more stable** |
| **CPU Overhead** | — | < 0.5% | **Virtually zero** |

> Benchmarked on AMD Radeon RX 7800 XT | Ryzen 7 7800X3D | 32GB DDR5 | Windows 11 23H2. Results may vary by GPU model.
> Measured with PresentMon and CapFrameX. Full methodology in [`docs/benchmarks.md`](docs/benchmarks.md).

## Three Core Technologies

### 1. Predictive Frame-Time Engine

Every optimizer on the market today is **reactive** — it sees a stutter, then tries to fix it. By then, the damage is already on your screen.

NeuroPace is **predictive**. It continuously analyzes rolling windows of 1,000+ telemetry samples — DPC latency patterns, interrupt density, GPU queue depth, frametime variance, CPU core state, VRAM pressure — and calculates the probability of the *next* frame stuttering.

When risk is detected:
- Thread migration to isolated cores
- GPU scheduling priority elevation
- Interrupt queue rebalancing
- Frame pacing curve smoothing

The stutter never reaches your monitor.

```
Current Frame → Analyze 1000-sample window → Predict next frametime
                                                    ↓
                                              Risk > threshold?
                                                    ↓
                                        Preemptive mitigation (< 5ms)
```

**Why this matters:** No consumer tool predicts future frametimes. This is the shift from *reactive optimization* to *predictive stability*.

### 2. Dynamic DPC & Interrupt Orchestrator

Windows treats all system interrupts equally. Your GPU interrupt, mouse polling, network driver, and background Windows Update service all compete for the same attention.

NeuroPace builds a real-time interrupt priority map:

| Source | Priority | Strategy |
|:---|:---:|:---|
| GPU Render Queue | **Critical** | Dedicated core, zero preemption |
| Mouse / Keyboard | **Ultra-Critical** | Lowest possible latency path |
| Network Stack | **Adaptive** | Scaled by game state |
| Telemetry / Logging | **Deferrable** | Batched during idle frames |
| Background Services | **Low** | Migrated to efficiency cores |

The result: **Software-Defined Latency Management** — a concept that doesn't exist in consumer software today.

### 3. RDNA Telemetry Fusion

Standard GPU tools see three things: FPS, utilization percentage, and temperature. But your RDNA GPU produces dozens of hardware signals that never reach the OS:

- Command processor stall events
- Wavefront occupancy levels
- Cache hit/miss ratios
- Render queue saturation
- Shader pipeline bubbles

NeuroPace fuses **hardware-level GPU telemetry** (via AMD ADLX) with **OS kernel-level data** (via ETW) into a single closed-loop feedback system:

```
RDNA Hardware Sensors          Windows Kernel Events
   (ADLX SDK)                      (ETW API)
        ↓                              ↓
   ┌────────────────────────────────────────┐
   │       Telemetry Fusion Engine          │
   │   200Hz polling · 28-feature vector    │
   └────────────────────┬───────────────────┘
                        ↓
              Prediction Engine (ONNX)
              Sub-millisecond inference
                        ↓
              Hardware Actuator (Ring-3)
              Thread · Power · Priority
```

Your PC doesn't just run your game. **It learns how to run it better.**

## Verified on Real Hardware

This is not a simulation. These are real sensor readings from a physical AMD Radeon GPU:

```
[ADLX] AMD Radeon RX 7800 XT initialized for telemetry.
[AGG] Aggregator started - telemetry: 5ms, dashboard: 33ms

[#   200] GPU:  30MHz  45C  23W | VRAM: 5366/16368MB | Clients: T:1 D:1
[#   400] GPU:  25MHz  45C  24W | VRAM: 5358/16368MB | Clients: T:1 D:1
[#   600] GPU:   5MHz  45C  18W | VRAM: 5355/16368MB | Clients: T:1 D:1
```

### Supported GPUs

**RDNA 3 (RX 7000 Series)**
- RX 7900 XTX / 7900 XT / 7900 GRE
- RX 7800 XT / 7700 XT
- RX 7600 XT / 7600

**RDNA 2 (RX 6000 Series)**
- RX 6950 XT / 6900 XT / 6800 XT / 6800
- RX 6750 XT / 6700 XT / 6700
- RX 6650 XT / 6600 XT / 6600 / 6500 XT

**RDNA 1 (RX 5000 Series)**
- RX 5700 XT / 5700 / 5600 XT / 5500 XT

> Any AMD GPU supporting the **ADLX SDK** is compatible.

## Architecture

Four decoupled microservices. Zero game memory access. Named Pipe IPC with < 1ms transport latency.

![Architecture Diagram](docs/images/architecture.png)

| Module | Language | Role |
|:---|:---:|:---|
| **Telemetry Collector** | C++ | ETW kernel events + ADLX hardware polling at 100Hz |
| **Prediction Engine** | Python/ONNX | 28-feature inference, sub-ms latency, rolling window analysis |
| **Hardware Actuator** | C++ | Thread affinity, TGP limits, priority optimization via Win32 API |
| **Control Center** | Web | Real-time dashboard, module orchestration, audit logging |

![Dashboard Preview](docs/images/dashboard-preview.png)

## Anti-Cheat Compliance

NeuroPace will **never** trigger anti-cheat systems:

- **Zero game memory access** — never reads or writes to any game process
- **Zero kernel drivers** — purely Ring-3 (User-Mode), no `.sys` files
- **Zero code injection** — no `WriteProcessMemory`, no `CreateRemoteThread`, no DLL injection
- **Zero restricted handles** — static validation against `VM_READ` / `GET_CONTEXT`
- **Full audit trail** — every decision logged with timestamps

Compatible with EasyAntiCheat, Vanguard, BattlEye, FACEIT AC, and all major platforms.

See: [`docs/anti-cheat-compliance.md`](docs/anti-cheat-compliance.md)

##  Security & Verification

We take system security seriously. NeuroPace operates purely in User-Mode (Ring-3) and does not deploy any kernel drivers (`.sys`). To ensure the binaries you run are authentic and untampered, we provide cryptographic checksums and public malware analysis links for every release.

### Checksums (v0.1.0-Win64)
| File | SHA-256 Hash |
|---|---|
| `NeuroPace-RDNA-v0.1.0-Win64.zip` | `E9F62D575122202543E59A0B022680CEA18DD9322B9E4C6555CE8C764A046FD4` |
| `bin\neuropace-telemetry.exe` | `89813406ACD5B47F493DA00026702DD51D6BA8B342B227CE0EC01DA48733FBE8` |
| `bin\neuropace-actuator.exe` | `78EDEA3939A0EFF53CD793077EAF632B538281B3013C1CA96448B66F6EF8BE2D` |
| `NeuroPace.exe` (Launcher) | `E8FA96773F3F65CE4C49F03F44B400AB3200EC01C81167DA2CF10D4965BD21E4` |

### Verifying with VirusTotal
Since these binaries are freshly compiled and not signed with an EV certificate, they may not be indexed on [VirusTotal](https://www.virustotal.com/) yet. You can upload any of the files above directly to VirusTotal to verify they are 100% clean. The resulting analysis will match the SHA-256 hashes provided above.

## Quick Start

### Option 1: Download Release (Recommended)

Download the latest release from the [Releases](https://github.com/l1ve709/NeuroPace-RDNA/releases) page.

```
NeuroPace-RDNA-v0.1.0-Win64.zip
├── bin/
│   ├── neuropace-telemetry.exe
│   └── neuropace-actuator.exe
├── scripts/         # Prediction engine
├── dashboard/       # Web Dashboard (Vue/Node.js)
├── models/          # Pre-trained ONNX model
├── NeuroPace.exe    # Smart System Tray Launcher
└── README.md
```

Run `NeuroPace.exe`. It will seamlessly start the Node.js backend and open the web dashboard in your browser.

### Option 2: Build From Source

```powershell
git clone https://github.com/l1ve709/NeuroPace-RDNA.git
cd NeuroPace-RDNA

# Bootstrap vcpkg
git clone https://github.com/microsoft/vcpkg.git external/vcpkg
.\external\vcpkg\bootstrap-vcpkg.bat

# Build Telemetry
cmake -S telemetry -B telemetry/build ^
    -DNEUROPACE_USE_ADLX=ON ^
    -DADLX_SDK_DIR="C:/path/to/ADLX" ^
    -DCMAKE_TOOLCHAIN_FILE="external/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build telemetry/build --config Release

# Build Actuator
cmake -S actuator -B actuator/build ^
    -DCMAKE_TOOLCHAIN_FILE="external/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build actuator/build --config Release

# Python dependencies
pip install -r ai-engine/requirements.txt
```

### Run

```powershell
# Terminal 1 — System Tray Web Launcher
.\releases\NeuroPace-RDNA-v0.1.0\NeuroPace.exe

# Terminal 2 — Prediction Engine
python ai-engine/src/main.py

# Terminal 3 — Actuator (target your game's PID)
.\actuator\build\Release\neuropace-actuator.exe --pid <game_pid>
```

## Technical Specifications

| Spec | Value |
|:---|:---|
| **Telemetry Rate** | 200 frames/sec (5ms resolution) |
| **Feature Vector** | 28 dimensions per inference |
| **Prediction Latency** | < 1ms per cycle |
| **Action Response** | < 5ms detection-to-mitigation |
| **CPU Overhead** | < 0.5% single core |
| **Memory Footprint** | ~45MB resident |
| **IPC Transport** | Named Pipes, JSON serialization |

## Project Structure

```
NeuroPace-RDNA/
├── telemetry/          # C++ Sensor & ETW Collector
│   ├── include/        # telemetry_data, etw_collector, adlx_sensor
│   └── src/            # Implementation
├── ai-engine/          # Python Prediction Engine
│   ├── src/            # predictor, feature_engineer, ipc
│   └── models/         # Pre-trained ONNX models
├── actuator/           # C++ Hardware Actuator
├── dashboard/          # Web Control Center
├── shared/             # Cross-module protocol definitions
├── scripts/            # Utility scripts
└── docs/               # Documentation, benchmarks, compliance
```

## FAQ

**Will this get me banned?**
No. NeuroPace never touches game memory. It only reads OS-level metrics and adjusts system parameters. See [Anti-Cheat Compliance](docs/anti-cheat-compliance.md).

**Does it work with NVIDIA?**
Not yet. NeuroPace is built for AMD RDNA using the ADLX SDK. NVIDIA support would require NVAPI/NVML integration.

**How much CPU does it use?**
Less than 0.5% of a single core. Lock-free data structures and hardware-accelerated ONNX inference.

**Do I need Admin rights?**
Recommended. Telemetry needs Admin for ETW kernel access (DPC/ISR). GPU metrics via ADLX work without elevation.

---

<div align="center">

**NeuroPace RDNA**

*Predictive latency management for the competitive edge.*

Copyright (c) 2026 Ediz Sonmez. All rights reserved.

</div>

<!-- verified release -->
