import sys
from pathlib import Path
import numpy as np
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from feature_engineer import FeatureEngineer, FEATURE_NAMES, NUM_FEATURES
from config import FeatureConfig
def _make_frame(
    dpc: float = 50.0,
    isr: float = 15.0,
    clock: int = 2500,
    temp: int = 72,
    tgp: int = 300,
    vram_used: int = 8000,
    vram_total: int = 24576,
    util: float = 92.0,
    frame_time: float = 6.9,
    seq: int = 0,
) -> dict:
    """Create a minimal TelemetryFrame dict for testing."""
    return {
        "timestamp_us": seq * 10_000,
        "sequence_id": seq,
        "dpc_isr": {
            "dpc_latency_us": dpc,
            "dpc_avg_us": 45.0,
            "dpc_max_us": 120.0,
            "isr_latency_us": isr,
            "isr_avg_us": 16.0,
            "isr_max_us": 40.0,
            "dpc_count": 1000,
            "isr_count": 800,
        },
        "gpu": {
            "gpu_clock_mhz": clock,
            "mem_clock_mhz": 1250,
            "gpu_temp_c": temp,
            "hotspot_temp_c": temp + 12,
            "gpu_tgp_w": tgp,
            "vram_used_mb": vram_used,
            "vram_total_mb": vram_total,
            "gpu_utilization_pct": util,
            "fan_speed_rpm": 1500,
        },
        "frame_time_ms": frame_time,
        "fps_instant": 1000.0 / frame_time,
    }
class TestFeatureNames:
    def test_feature_count(self):
        assert NUM_FEATURES == 28
    def test_names_are_unique(self):
        assert len(FEATURE_NAMES) == len(set(FEATURE_NAMES))
    def test_static_access(self):
        names = FeatureEngineer.get_feature_names()
        assert names == FEATURE_NAMES
        assert FeatureEngineer.get_num_features() == 28
class TestWindowManagement:
    def test_not_ready_initially(self):
        fe = FeatureEngineer(FeatureConfig(min_window_size=10))
        assert not fe.is_ready
        assert fe.window_size == 0
    def test_returns_none_until_ready(self):
        config = FeatureConfig(min_window_size=5)
        fe = FeatureEngineer(config)
        for i in range(4):
            result = fe.process(_make_frame(seq=i))
            assert result is None, f"Should be None at frame {i}, window not ready"
        result = fe.process(_make_frame(seq=4))
        assert result is not None, "Should produce features at frame 4 (min_window=5)"
    def test_ready_after_min_frames(self):
        config = FeatureConfig(min_window_size=3)
        fe = FeatureEngineer(config)
        for i in range(3):
            fe.process(_make_frame(seq=i))
        assert fe.is_ready
        assert fe.window_size == 3
    def test_window_does_not_exceed_max(self):
        config = FeatureConfig(window_size=20, min_window_size=5)
        fe = FeatureEngineer(config)
        for i in range(50):
            fe.process(_make_frame(seq=i))
        assert fe.window_size == 20
    def test_reset_clears_state(self):
        config = FeatureConfig(min_window_size=3)
        fe = FeatureEngineer(config)
        for i in range(10):
            fe.process(_make_frame(seq=i))
        assert fe.is_ready
        fe.reset()
        assert not fe.is_ready
        assert fe.window_size == 0
class TestFeatureExtraction:
    def _get_features(self, frames: list[dict]) -> np.ndarray:
        """Helper: feed frames and return the last feature vector."""
        config = FeatureConfig(min_window_size=1, window_size=100)
        fe = FeatureEngineer(config)
        result = None
        for frame in frames:
            result = fe.process(frame)
        assert result is not None
        return result
    def test_feature_shape(self):
        frames = [_make_frame(seq=i) for i in range(5)]
        features = self._get_features(frames)
        assert features.shape == (NUM_FEATURES,)
        assert features.dtype == np.float64
    def test_instantaneous_dpc_values(self):
        frame = _make_frame(dpc=123.4, isr=45.6)
        features = self._get_features([frame])
        assert features[0] == pytest.approx(123.4)  
        assert features[3] == pytest.approx(45.6)   
    def test_instantaneous_gpu_values(self):
        frame = _make_frame(clock=2600, temp=80, tgp=320, util=95.5)
        features = self._get_features([frame])
        assert features[6] == 2600   
        assert features[7] == 80     
        assert features[9] == 320    
        assert features[10] == pytest.approx(95.5)  
    def test_vram_pressure(self):
        frame = _make_frame(vram_used=20000, vram_total=24576)
        features = self._get_features([frame])
        expected = 20000 / 24576
        assert features[13] == pytest.approx(expected, rel=1e-4)  
    def test_thermal_headroom(self):
        frame = _make_frame(temp=85)
        features = self._get_features([frame])
        assert features[14] == pytest.approx(95 - 85)  
    def test_dpc_spike_ratio(self):
        frame = _make_frame(dpc=200.0)  
        features = self._get_features([frame])
        expected = 200.0 / 45.0
        assert features[16] == pytest.approx(expected, rel=1e-2)  
    def test_temporal_deltas(self):
        frames = [
            _make_frame(dpc=50.0, clock=2500, seq=0),
            _make_frame(dpc=150.0, clock=2400, seq=1),  
        ]
        features = self._get_features(frames)
        assert features[17] == pytest.approx(100.0)    
        assert features[18] == pytest.approx(-100.0)   
    def test_window_statistics(self):
        frames = []
        for i in range(20):
            dpc = 50.0 if i < 15 else 800.0  
            frames.append(_make_frame(dpc=dpc, seq=i))
        features = self._get_features(frames)
        assert features[24] == 5.0  
    def test_no_nan_or_inf(self):
        frames = [_make_frame(seq=i) for i in range(20)]
        features = self._get_features(frames)
        assert not np.any(np.isnan(features)), "Features contain NaN"
        assert not np.any(np.isinf(features)), "Features contain Inf"
    def test_zero_division_safety(self):
        """Ensure no crash when GPU metrics are all zeros."""
        frame = _make_frame(
            dpc=0, isr=0, clock=0, temp=0, tgp=0,
            vram_used=0, vram_total=0, util=0, frame_time=0.1,
        )
        frame["gpu"]["vram_total_mb"] = 0
        config = FeatureConfig(min_window_size=1)
        fe = FeatureEngineer(config)
        features = fe.process(frame)
        assert features is not None
        assert not np.any(np.isnan(features))
        assert not np.any(np.isinf(features))
if __name__ == "__main__":
    pytest.main([__file__, "-v"])
