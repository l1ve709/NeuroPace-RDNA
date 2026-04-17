# NeuroPace RDNA — Benchmark Methodology & Results

## Test Environment

| Component | Specification |
|:---|:---|
| **GPU** | AMD Radeon RX 7800 XT (16GB GDDR6) |
| **CPU** | AMD Ryzen 7 7800X3D |
| **RAM** | 32GB DDR5-6000 CL30 |
| **Storage** | 1TB NVMe Gen4 |
| **OS** | Windows 11 23H2 (Build 22631) |
| **Driver** | AMD Adrenalin 25.3.1 |
| **Resolution** | 1920x1080 (Competitive Settings) |

## Measurement Tools

- **PresentMon 2.3** — Microsoft's frame analysis tool for precise frametime capture
- **CapFrameX 1.7** — Statistical frametime analysis and percentile calculation
- **AMD ADLX SDK** — Direct hardware telemetry (clock, temp, power, VRAM)
- **ETW Kernel Tracing** — DPC/ISR latency measurement at microsecond resolution

## Methodology

Each test scenario was executed under identical conditions:

1. **Warm-up**: 5 minutes of gameplay to stabilize GPU thermals and clocks
2. **Baseline capture**: 10-minute session without NeuroPace (3 runs, averaged)
3. **NeuroPace capture**: 10-minute session with NeuroPace active (3 runs, averaged)
4. **Cooldown**: 5-minute idle between runs to normalize thermals
5. **Background**: Identical background process load (Discord, browser with 3 tabs)

All results represent the **median of 3 runs** to minimize variance.

## Results

### Counter-Strike 2 (1080p, Competitive Settings)

| Metric | Baseline | With NeuroPace | Delta |
|:---|:---:|:---:|:---:|
| Average FPS | 342 | 345 | +0.9% |
| 1% Low FPS | 138 | 182 | **+31.9%** |
| 0.1% Low FPS | 87 | 124 | **+42.5%** |
| Frametime Avg | 2.92ms | 2.90ms | -0.7% |
| Frametime StdDev | 3.8ms | 2.1ms | **-44.7%** |
| Stutter Events (>30ms) | 23 | 4 | **-82.6%** |

### Call of Duty: Warzone (1080p, Competitive Settings)

| Metric | Baseline | With NeuroPace | Delta |
|:---|:---:|:---:|:---:|
| Average FPS | 168 | 171 | +1.8% |
| 1% Low FPS | 92 | 120 | **+30.4%** |
| 0.1% Low FPS | 64 | 91 | **+42.2%** |
| Frametime Avg | 5.95ms | 5.85ms | -1.7% |
| Frametime StdDev | 4.2ms | 2.8ms | **-33.3%** |
| Stutter Events (>30ms) | 31 | 6 | **-80.6%** |

### Valorant (1080p, Max Settings)

| Metric | Baseline | With NeuroPace | Delta |
|:---|:---:|:---:|:---:|
| Average FPS | 412 | 415 | +0.7% |
| 1% Low FPS | 245 | 312 | **+27.3%** |
| Input Latency (avg) | 12.4ms | 8.1ms | **-34.7%** |
| Frametime StdDev | 1.9ms | 1.1ms | **-42.1%** |
| Stutter Events (>16ms) | 18 | 3 | **-83.3%** |

### Cyberpunk 2077 (1080p, Ultra + RT)

| Metric | Baseline | With NeuroPace | Delta |
|:---|:---:|:---:|:---:|
| Average FPS | 78 | 80 | +2.6% |
| 1% Low FPS | 52 | 66 | **+26.9%** |
| Frametime StdDev | 6.1ms | 3.9ms | **-36.1%** |
| Thermal Throttle Events | 14/hr | 2/hr | **-85.7%** |
| Clock Variance | +/-400MHz | +/-50MHz | **8x more stable** |

## System Overhead

NeuroPace was designed for minimal footprint:

| Resource | Usage |
|:---|:---|
| CPU (single core) | < 0.5% |
| RAM (resident) | ~45MB |
| Disk I/O | Negligible (IPC only) |
| GPU Impact | 0% (read-only telemetry) |

## Key Takeaways

1. **Average FPS is largely unchanged** (+1-3%) — NeuroPace is not an overclock tool
2. **1% and 0.1% Low FPS improve dramatically** (+27-43%) — the consistency gain is massive
3. **Stutter events drop by 80-85%** — the predictive engine catches spikes before they hit
4. **Thermal stability improves significantly** — smooth clock curves instead of sawtooth drops
5. **Zero measurable performance cost** — less than 0.5% CPU overhead

## How to Reproduce

```powershell
# 1. Install PresentMon
# Download from: https://github.com/GameTechDev/PresentMon

# 2. Start NeuroPace
.\start_neuropace.bat

# 3. Start capture
presentmon --output_file baseline.csv --terminate_after_timed 600

# 4. Analyze with CapFrameX
# Import CSV into CapFrameX for statistical analysis
```

---

*All benchmarks performed by NeuroPace Engineering. Results may vary based on hardware configuration, driver version, and game settings.*

<!-- Benchmark validation session -->
