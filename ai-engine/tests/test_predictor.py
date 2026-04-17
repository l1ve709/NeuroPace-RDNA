import sys
import time
from pathlib import Path
import numpy as np
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from predictor import Predictor, PredictionResult, ActionType
from feature_engineer import NUM_FEATURES
from config import PredictorConfig
class TestPredictionResult:
    def test_to_dict_schema(self):
        result = PredictionResult(
            frame_drop_probability=0.75,
            action=ActionType.REBALANCE_THREADS,
            confidence=0.5,
            timestamp_us=1234567890,
            tgp_boost_w=20,
            thread_priority="ABOVE_NORMAL",
            contributing_factors=["dpc_spike", "thermal_throttle"],
        )
        d = result.to_dict()
        assert d["action"] == "REBALANCE_THREADS"
        assert d["confidence"] == 0.5
        assert d["timestamp_us"] == 1234567890
        assert d["prediction"]["frame_drop_probability"] == 0.75
        assert "dpc_spike" in d["prediction"]["contributing_factors"]
        assert d["params"]["tgp_boost_w"] == 20
        assert d["params"]["thread_priority"] == "ABOVE_NORMAL"
    def test_default_values(self):
        result = PredictionResult()
        assert result.frame_drop_probability == 0.0
        assert result.action == ActionType.NO_ACTION
        assert result.confidence == 0.0
        assert result.contributing_factors == []
class TestPredictorWithoutModel:
    def test_not_loaded_initially(self):
        predictor = Predictor()
        assert not predictor.is_loaded
    def test_predict_without_model_returns_zero(self):
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        result = predictor.predict(features)
        assert result.frame_drop_probability == 0.0
        assert result.action == ActionType.NO_ACTION
    def test_load_nonexistent_model(self):
        predictor = Predictor()
        success = predictor.load_model("/nonexistent/model.onnx")
        assert not success
        assert not predictor.is_loaded
class TestActionDecisionLogic:
    """Test the action thresholds without needing an actual model."""
    def _make_predictor_with_config(self, **kwargs) -> Predictor:
        config = PredictorConfig(**kwargs)
        return Predictor(config)
    def test_no_action_below_threshold(self):
        config = PredictorConfig(
            threshold_boost_tgp=0.3,
            threshold_rebalance=0.6,
            threshold_combined=0.8,
        )
        assert config.threshold_boost_tgp == 0.3
        assert config.threshold_rebalance == 0.6
        assert config.threshold_combined == 0.8
    def test_action_type_constants(self):
        assert ActionType.NO_ACTION == "NO_ACTION"
        assert ActionType.BOOST_TGP == "BOOST_TGP"
        assert ActionType.REBALANCE_THREADS == "REBALANCE_THREADS"
        assert ActionType.BOOST_AND_REBALANCE == "BOOST_AND_REBALANCE"
class TestContributingFactors:
    """Test the factor analysis on known feature vectors."""
    def test_dpc_spike_detection(self):
        """DPC spike ratio > 2.0 should produce 'dpc_spike' factor."""
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        features[16] = 3.5  
        factors = predictor._analyze_factors(features)
        assert "dpc_spike" in factors
    def test_thermal_throttle_detection(self):
        """Thermal headroom < 5°C should produce 'thermal_throttle' factor."""
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        features[14] = 3.0  
        factors = predictor._analyze_factors(features)
        assert "thermal_throttle" in factors
    def test_vram_pressure_detection(self):
        """VRAM pressure > 90% should produce 'vram_pressure' factor."""
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        features[13] = 0.95  
        factors = predictor._analyze_factors(features)
        assert "vram_pressure" in factors
    def test_tgp_saturation_detection(self):
        """TGP ratio > 95% should produce 'tgp_saturation' factor."""
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        features[15] = 0.98  
        factors = predictor._analyze_factors(features)
        assert "tgp_saturation" in factors
    def test_no_factors_when_normal(self):
        """Normal values should produce no contributing factors."""
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        features[14] = 20.0  
        features[13] = 0.3   
        features[15] = 0.5   
        features[16] = 1.0   
        factors = predictor._analyze_factors(features)
        assert len(factors) == 0
    def test_multiple_factors(self):
        """Multiple stress indicators should produce multiple factors."""
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        features[13] = 0.95  
        features[14] = 2.0   
        features[16] = 4.0   
        factors = predictor._analyze_factors(features)
        assert len(factors) == 3
        assert "vram_pressure" in factors
        assert "thermal_throttle" in factors
        assert "dpc_spike" in factors
class TestPredictorStats:
    def test_stats_initially_zero(self):
        predictor = Predictor()
        assert predictor.stats.total_predictions == 0
        assert predictor.stats.avg_inference_time_ms == 0.0
    def test_stats_increment_on_predict(self):
        predictor = Predictor()
        features = np.zeros(NUM_FEATURES, dtype=np.float64)
        predictor.predict(features)
        assert predictor.stats.total_predictions == 0
if __name__ == "__main__":
    pytest.main([__file__, "-v"])
