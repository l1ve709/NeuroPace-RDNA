from __future__ import annotations
import logging
from collections import deque
from typing import Optional
import numpy as np
from config import FeatureConfig
logger = logging.getLogger("neuropace.ai.features")
FEATURE_NAMES: list[str] = [
    "dpc_latency_us",
    "dpc_avg_us",
    "dpc_max_us",
    "isr_latency_us",
    "isr_avg_us",
    "isr_max_us",
    "gpu_clock_mhz",
    "gpu_temp_c",
    "hotspot_temp_c",
    "gpu_tgp_w",
    "gpu_utilization_pct",
    "vram_used_mb",
    "fan_speed_rpm",
    "vram_pressure",            
    "thermal_headroom",         
    "tgp_ratio",                
    "dpc_spike_ratio",          
    "dpc_latency_delta",        
    "gpu_clock_delta",          
    "gpu_temp_delta",           
    "frame_time_delta",         
    "gpu_utilization_delta",    
    "dpc_std_window",           
    "frame_time_std_window",    
    "dpc_spike_count_window",   
    "gpu_clock_min_window",     
    "frame_time_p95_window",    
    "frame_time_p99_window",    
]
NUM_FEATURES: int = len(FEATURE_NAMES)  
def _safe_div(numerator: float, denominator: float, default: float = 0.0) -> float:
    """Division with zero-denominator protection."""
    if denominator == 0.0:
        return default
    return numerator / denominator
class FeatureEngineer:
    """
    Stateful feature extractor that maintains a sliding window of recent
    TelemetryFrame observations and computes a fixed-length feature vector.
    Usage (inference):
        fe = FeatureEngineer()
        for frame in telemetry_stream:
            features = fe.process(frame)
            if features is not None:
                prediction = model.predict(features)
    Usage (training):
        fe = FeatureEngineer()
        X, y = [], []
        for frame, label in labeled_data:
            features = fe.process(frame)
            if features is not None:
                X.append(features)
                y.append(label)
    """
    def __init__(self, config: FeatureConfig = FeatureConfig()) -> None:
        self._config = config
        self._window: deque[dict] = deque(maxlen=config.window_size)
        self._frame_count: int = 0
    @property
    def window_size(self) -> int:
        return len(self._window)
    @property
    def is_ready(self) -> bool:
        """True when the window has enough samples for reliable feature extraction."""
        return len(self._window) >= self._config.min_window_size
    def reset(self) -> None:
        """Clear the sliding window (e.g., on reconnection)."""
        self._window.clear()
        self._frame_count = 0
    def process(self, frame: dict) -> Optional[np.ndarray]:
        """
        Ingest a TelemetryFrame and return a feature vector.
        Args:
            frame: Parsed TelemetryFrame dict from the IPC subscriber.
        Returns:
            numpy array of shape (NUM_FEATURES,) with float64 values,
            or None if the window is not yet full enough.
        """
        self._window.append(frame)
        self._frame_count += 1
        if not self.is_ready:
            return None
        return self._extract_features(frame)
    def _extract_features(self, current: dict) -> np.ndarray:
        """Build the full feature vector from current frame + window history."""
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        dpc_isr = current.get("dpc_isr", {})
        gpu = current.get("gpu", {})
        features[0] = dpc_isr.get("dpc_latency_us", 0.0)
        features[1] = dpc_isr.get("dpc_avg_us", 0.0)
        features[2] = dpc_isr.get("dpc_max_us", 0.0)
        features[3] = dpc_isr.get("isr_latency_us", 0.0)
        features[4] = dpc_isr.get("isr_avg_us", 0.0)
        features[5] = dpc_isr.get("isr_max_us", 0.0)
        features[6] = gpu.get("gpu_clock_mhz", 0)
        features[7] = gpu.get("gpu_temp_c", 0)
        features[8] = gpu.get("hotspot_temp_c", 0)
        features[9] = gpu.get("gpu_tgp_w", 0)
        features[10] = gpu.get("gpu_utilization_pct", 0.0)
        features[11] = gpu.get("vram_used_mb", 0)
        features[12] = gpu.get("fan_speed_rpm", 0)
        vram_total = gpu.get("vram_total_mb", 1)
        features[13] = _safe_div(gpu.get("vram_used_mb", 0), vram_total)
        features[14] = self._config.gpu_max_safe_temp_c - gpu.get("gpu_temp_c", 0)
        features[15] = _safe_div(gpu.get("gpu_tgp_w", 0), self._config.gpu_max_tgp_w)
        features[16] = _safe_div(
            dpc_isr.get("dpc_latency_us", 0.0),
            max(dpc_isr.get("dpc_avg_us", 1.0), 1.0),
        )
        if len(self._window) >= 2:
            prev = self._window[-2]
            prev_dpc_isr = prev.get("dpc_isr", {})
            prev_gpu = prev.get("gpu", {})
            features[17] = (
                dpc_isr.get("dpc_latency_us", 0.0)
                - prev_dpc_isr.get("dpc_latency_us", 0.0)
            )
            features[18] = (
                gpu.get("gpu_clock_mhz", 0)
                - prev_gpu.get("gpu_clock_mhz", 0)
            )
            features[19] = (
                gpu.get("gpu_temp_c", 0)
                - prev_gpu.get("gpu_temp_c", 0)
            )
            features[20] = (
                current.get("frame_time_ms", 0.0)
                - prev.get("frame_time_ms", 0.0)
            )
            features[21] = (
                gpu.get("gpu_utilization_pct", 0.0)
                - prev_gpu.get("gpu_utilization_pct", 0.0)
            )
        window_list = list(self._window)  
        dpc_values = np.array(
            [f.get("dpc_isr", {}).get("dpc_latency_us", 0.0) for f in window_list],
            dtype=np.float64,
        )
        features[22] = float(np.std(dpc_values)) if len(dpc_values) > 1 else 0.0
        ft_values = np.array(
            [f.get("frame_time_ms", 0.0) for f in window_list],
            dtype=np.float64,
        )
        features[23] = float(np.std(ft_values)) if len(ft_values) > 1 else 0.0
        features[24] = float(np.sum(dpc_values > self._config.dpc_spike_threshold_us))
        clock_values = np.array(
            [f.get("gpu", {}).get("gpu_clock_mhz", 0) for f in window_list],
            dtype=np.float64,
        )
        features[25] = float(np.min(clock_values)) if len(clock_values) > 0 else 0.0
        if len(ft_values) >= 5:
            features[26] = float(np.percentile(ft_values, 95))
            features[27] = float(np.percentile(ft_values, 99))
        else:
            features[26] = float(np.max(ft_values)) if len(ft_values) > 0 else 0.0
            features[27] = features[26]
        return features
    @staticmethod
    def get_feature_names() -> list[str]:
        """Return the ordered list of feature names (for training/debugging)."""
        return FEATURE_NAMES.copy()
    @staticmethod
    def get_num_features() -> int:
        """Return the expected feature vector length."""
        return NUM_FEATURES
