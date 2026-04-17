from __future__ import annotations
import sys
from pathlib import Path
import numpy as np
import pandas as pd
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from feature_engineer import FeatureEngineer, FEATURE_NAMES, NUM_FEATURES
from config import FeatureConfig
def generate_normal_frame(t: float, rng: np.random.Generator) -> dict:
    """Generate a TelemetryFrame representing normal gaming operation."""
    dpc = 40.0 + rng.normal(0, 10)
    isr = 15.0 + rng.normal(0, 3)
    clock = 2500 + rng.normal(0, 20)
    temp = 72 + rng.normal(0, 1.5)
    return {
        "timestamp_us": int(t * 1_000_000),
        "sequence_id": int(t * 100),
        "dpc_isr": {
            "dpc_latency_us": max(5.0, dpc),
            "dpc_avg_us": 42.0 + rng.normal(0, 3),
            "dpc_max_us": 120 + rng.normal(0, 10),
            "isr_latency_us": max(2.0, isr),
            "isr_avg_us": 16.0 + rng.normal(0, 2),
            "isr_max_us": 45 + rng.normal(0, 5),
            "dpc_count": int(1000 + rng.normal(0, 50)),
            "isr_count": int(800 + rng.normal(0, 40)),
        },
        "gpu": {
            "gpu_clock_mhz": int(max(2200, clock)),
            "mem_clock_mhz": 1250,
            "gpu_temp_c": int(np.clip(temp, 55, 95)),
            "hotspot_temp_c": int(np.clip(temp + 12, 60, 110)),
            "gpu_tgp_w": int(np.clip(300 + rng.normal(0, 10), 250, 355)),
            "vram_used_mb": int(np.clip(8192 + rng.normal(0, 200), 4000, 23000)),
            "vram_total_mb": 24576,
            "gpu_utilization_pct": float(np.clip(92 + rng.normal(0, 3), 0, 100)),
            "fan_speed_rpm": int(np.clip(1400 + rng.normal(0, 50), 800, 3000)),
        },
        "frame_time_ms": float(np.clip(6.9 + rng.normal(0, 0.3), 4.0, 10.0)),
        "fps_instant": 0.0,  
    }
def generate_dpc_spike_frame(t: float, rng: np.random.Generator, severity: float = 1.0) -> dict:
    """Generate a frame with elevated DPC latency (pre-frame-drop signal)."""
    frame = generate_normal_frame(t, rng)
    spike = 500 + rng.exponential(300) * severity
    frame["dpc_isr"]["dpc_latency_us"] = float(np.clip(spike, 200, 8000))
    frame["dpc_isr"]["dpc_avg_us"] = float(np.clip(frame["dpc_isr"]["dpc_avg_us"] + spike * 0.05, 40, 500))
    frame["frame_time_ms"] = float(np.clip(frame["frame_time_ms"] + spike * 0.005, 6, 50))
    return frame
def generate_thermal_throttle_frame(t: float, rng: np.random.Generator) -> dict:
    """Generate a frame showing GPU thermal throttling."""
    frame = generate_normal_frame(t, rng)
    frame["gpu"]["gpu_temp_c"] = int(np.clip(90 + rng.normal(0, 2), 88, 100))
    frame["gpu"]["hotspot_temp_c"] = frame["gpu"]["gpu_temp_c"] + 15
    frame["gpu"]["gpu_clock_mhz"] = int(np.clip(1900 + rng.normal(0, 50), 1600, 2100))
    frame["gpu"]["gpu_tgp_w"] = int(np.clip(340 + rng.normal(0, 5), 330, 355))
    frame["frame_time_ms"] = float(np.clip(10 + rng.normal(0, 1.5), 8, 25))
    return frame
def generate_vram_pressure_frame(t: float, rng: np.random.Generator) -> dict:
    """Generate a frame with high VRAM usage."""
    frame = generate_normal_frame(t, rng)
    frame["gpu"]["vram_used_mb"] = int(np.clip(22500 + rng.normal(0, 300), 21000, 24500))
    frame["gpu"]["gpu_utilization_pct"] = float(np.clip(75 + rng.normal(0, 5), 50, 90))
    frame["frame_time_ms"] = float(np.clip(12 + rng.normal(0, 2), 8, 30))
    return frame
def generate_combined_stress_frame(t: float, rng: np.random.Generator) -> dict:
    """Generate a frame with multiple stress indicators."""
    frame = generate_dpc_spike_frame(t, rng, severity=0.7)
    frame["gpu"]["gpu_temp_c"] = int(np.clip(87 + rng.normal(0, 2), 83, 95))
    frame["gpu"]["hotspot_temp_c"] = frame["gpu"]["gpu_temp_c"] + 13
    frame["gpu"]["gpu_clock_mhz"] = int(np.clip(2100 + rng.normal(0, 40), 1900, 2300))
    frame["gpu"]["vram_used_mb"] = int(np.clip(20000 + rng.normal(0, 500), 18000, 24000))
    frame["frame_time_ms"] = float(np.clip(15 + rng.exponential(3), 10, 60))
    return frame
def generate_sequence(
    n_frames: int = 2000,
    scenario: str = "normal",
    seed: int = 42,
) -> list[tuple[dict, int]]:
    """
    Generate a labeled sequence of TelemetryFrames.
    Returns:
        List of (frame_dict, label) tuples.
        label = 1 if a frame drop is predicted in the next 50-100ms.
    """
    rng = np.random.default_rng(seed)
    frames: list[tuple[dict, int]] = []
    if scenario == "normal":
        for i in range(n_frames):
            t = i * 0.01  
            frame = generate_normal_frame(t, rng)
            frame["fps_instant"] = 1000.0 / max(frame["frame_time_ms"], 0.1)
            frames.append((frame, 0))
    elif scenario == "dpc_storms":
        for i in range(n_frames):
            t = i * 0.01
            if rng.random() < 0.05:
                frame = generate_dpc_spike_frame(t, rng)
                frames.append((frame, 1))
            else:
                frame = generate_normal_frame(t, rng)
                frames.append((frame, 0))
    elif scenario == "thermal_throttle":
        for i in range(n_frames):
            t = i * 0.01
            phase = (i % 500) / 500.0  
            if phase > 0.6:  
                frame = generate_thermal_throttle_frame(t, rng)
                frames.append((frame, 1))
            elif phase > 0.4:  
                frame = generate_normal_frame(t, rng)
                frame["gpu"]["gpu_temp_c"] = int(np.clip(82 + phase * 15, 80, 92))
                frame["gpu"]["hotspot_temp_c"] = frame["gpu"]["gpu_temp_c"] + 12
                frames.append((frame, 1))
            else:
                frame = generate_normal_frame(t, rng)
                frames.append((frame, 0))
    elif scenario == "vram_pressure":
        for i in range(n_frames):
            t = i * 0.01
            if rng.random() < 0.15:
                frame = generate_vram_pressure_frame(t, rng)
                frames.append((frame, 1))
            else:
                frame = generate_normal_frame(t, rng)
                frames.append((frame, 0))
    elif scenario == "combined":
        for i in range(n_frames):
            t = i * 0.01
            r = rng.random()
            if r < 0.03:
                frame = generate_combined_stress_frame(t, rng)
                frames.append((frame, 1))
            elif r < 0.06:
                frame = generate_dpc_spike_frame(t, rng, severity=0.5)
                frames.append((frame, 1))
            elif r < 0.08:
                frame = generate_thermal_throttle_frame(t, rng)
                frames.append((frame, 1))
            else:
                frame = generate_normal_frame(t, rng)
                frames.append((frame, 0))
    elif scenario == "mixed_realistic":
        state = "normal"
        state_timer = 0
        for i in range(n_frames):
            t = i * 0.01
            if state == "normal":
                state_timer += 1
                if state_timer > 300 and rng.random() < 0.02:  
                    state = rng.choice(["dpc_storm", "thermal", "vram", "combined"])
                    state_timer = 0
            elif state_timer > 50:  
                state = "recovery"
                state_timer = 0
            elif state == "recovery" and state_timer > 30:
                state = "normal"
                state_timer = 0
            state_timer += 1
            if state == "normal":
                frame = generate_normal_frame(t, rng)
                label = 0
            elif state == "dpc_storm":
                frame = generate_dpc_spike_frame(t, rng)
                label = 1
            elif state == "thermal":
                frame = generate_thermal_throttle_frame(t, rng)
                label = 1
            elif state == "vram":
                frame = generate_vram_pressure_frame(t, rng)
                label = 1
            elif state == "combined":
                frame = generate_combined_stress_frame(t, rng)
                label = 1
            elif state == "recovery":
                frame = generate_normal_frame(t, rng)
                frame["dpc_isr"]["dpc_latency_us"] += 30
                frame["frame_time_ms"] += 1.0
                label = 0
            else:
                frame = generate_normal_frame(t, rng)
                label = 0
            frame["fps_instant"] = 1000.0 / max(frame["frame_time_ms"], 0.1)
            frames.append((frame, label))
    lookahead = 10  
    labeled = []
    for i in range(len(frames)):
        frame, _ = frames[i]
        future_labels = [
            frames[j][1]
            for j in range(i + 1, min(i + 1 + lookahead, len(frames)))
        ]
        label = 1 if any(l == 1 for l in future_labels) else 0
        labeled.append((frame, label))
    return labeled
def extract_features_from_sequence(
    sequence: list[tuple[dict, int]],
) -> tuple[np.ndarray, np.ndarray]:
    """
    Run the FeatureEngineer over a sequence to extract (X, y) arrays.
    This ensures training uses the SAME feature extraction as inference.
    """
    fe = FeatureEngineer(FeatureConfig())
    X_list: list[np.ndarray] = []
    y_list: list[int] = []
    for frame, label in sequence:
        features = fe.process(frame)
        if features is not None:
            X_list.append(features)
            y_list.append(label)
    return np.array(X_list), np.array(y_list)
def main() -> None:
    output_dir = Path(__file__).resolve().parent / "dataset"
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / "synthetic_telemetry.csv"
    print("=" * 60)
    print("  NeuroPace RDNA — Synthetic Data Generator")
    print("=" * 60)
    scenarios = [
        ("normal", 3000, 1),
        ("dpc_storms", 2000, 2),
        ("thermal_throttle", 2000, 3),
        ("vram_pressure", 1500, 4),
        ("combined", 2000, 5),
        ("mixed_realistic", 5000, 6),
        ("mixed_realistic", 5000, 7),
        ("mixed_realistic", 5000, 8),
    ]
    all_X: list[np.ndarray] = []
    all_y: list[np.ndarray] = []
    for scenario, n_frames, seed in scenarios:
        print(f"  Generating: {scenario:<20s} ({n_frames:>5} frames, seed={seed})")
        sequence = generate_sequence(n_frames=n_frames, scenario=scenario, seed=seed)
        X, y = extract_features_from_sequence(sequence)
        all_X.append(X)
        all_y.append(y)
        n_pos = int(y.sum())
        n_neg = len(y) - n_pos
        print(f"    -> {len(y)} samples (pos={n_pos}, neg={n_neg}, ratio={n_pos/max(len(y),1):.2%})")
    X_all = np.vstack(all_X)
    y_all = np.concatenate(all_y)
    print(f"\n  Total: {len(y_all)} samples")
    print(f"  Positive (frame drop): {int(y_all.sum())} ({y_all.mean():.2%})")
    print(f"  Negative (normal):     {len(y_all) - int(y_all.sum())} ({1 - y_all.mean():.2%})")
    df = pd.DataFrame(X_all, columns=FEATURE_NAMES)
    df["frame_drop"] = y_all.astype(int)
    df.to_csv(output_path, index=False, float_format="%.4f")
    print(f"\n  Saved to: {output_path}")
    print(f"  File size: {output_path.stat().st_size / 1024:.1f} KB")
    print("=" * 60)
if __name__ == "__main__":
    main()
