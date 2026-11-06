import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from config import FeatureConfig
from feature_engineer import FeatureEngineer
from predictor import Predictor
from session_analyzer import Frame, analyze


def test_malformed_nested_telemetry_does_not_poison_window():
    engineer = FeatureEngineer(FeatureConfig(min_window_size=1))
    assert engineer.process({"gpu": None}) is None
    assert engineer.window_size == 0
    result = engineer.process({"gpu": {"gpu_temp_c": float("nan")}})
    assert np.all(np.isfinite(result))


def test_extreme_negative_logit_does_not_overflow():
    class Session:
        def run(self, *_args):
            return [np.array([[-10000.0]])]

    predictor = Predictor()
    predictor._session = Session()
    predictor._model_loaded = True
    assert predictor.predict(np.zeros(28)).frame_drop_probability == 0.0


def test_negative_timestamp_rejected_by_direct_analysis():
    with pytest.raises(ValueError):
        analyze([Frame(-1.0, 16.0)])
