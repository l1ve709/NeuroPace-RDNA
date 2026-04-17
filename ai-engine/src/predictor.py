from __future__ import annotations
import logging
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional
import numpy as np
from config import PredictorConfig
from feature_engineer import FEATURE_NAMES, NUM_FEATURES
logger = logging.getLogger("neuropace.ai.predictor")
class ActionType:
    NO_ACTION = "NO_ACTION"
    BOOST_TGP = "BOOST_TGP"
    REBALANCE_THREADS = "REBALANCE_THREADS"
    BOOST_AND_REBALANCE = "BOOST_AND_REBALANCE"
@dataclass
class PredictionResult:
    """Output of a single prediction cycle."""
    frame_drop_probability: float = 0.0
    action: str = ActionType.NO_ACTION
    confidence: float = 0.0
    timestamp_us: int = 0
    predicted_latency_spike_us: float = 0.0
    contributing_factors: list[str] = field(default_factory=list)
    tgp_boost_w: int = 0
    thread_priority: str = "NORMAL"
    inference_time_ms: float = 0.0
    def to_dict(self) -> dict:
        """Serialize to IPC-compatible dict (matches action_command.json schema)."""
        return {
            "timestamp_us": self.timestamp_us,
            "action": self.action,
            "confidence": round(self.confidence, 4),
            "prediction": {
                "frame_drop_probability": round(self.frame_drop_probability, 4),
                "predicted_latency_spike_us": round(self.predicted_latency_spike_us, 1),
                "contributing_factors": self.contributing_factors,
            },
            "params": {
                "tgp_boost_w": self.tgp_boost_w,
                "thread_priority": self.thread_priority,
            },
            "inference_time_ms": round(self.inference_time_ms, 3),
        }
@dataclass
class PredictorStats:
    """Runtime statistics."""
    total_predictions: int = 0
    total_actions_triggered: int = 0
    avg_inference_time_ms: float = 0.0
    max_inference_time_ms: float = 0.0
    _inference_times: list[float] = field(default_factory=list)
    def record_inference(self, time_ms: float) -> None:
        self._inference_times.append(time_ms)
        if len(self._inference_times) > 1000:
            self._inference_times = self._inference_times[-500:]
        self.avg_inference_time_ms = sum(self._inference_times) / len(self._inference_times)
        self.max_inference_time_ms = max(self.max_inference_time_ms, time_ms)
class Predictor:
    """
    ONNX Runtime-based frame drop predictor with action decision logic.
    Usage:
        predictor = Predictor(config)
        predictor.load_model("models/frame_drop_rf.onnx")
        features = feature_engineer.process(frame)
        result = predictor.predict(features)
        if result.action != ActionType.NO_ACTION:
            action_publisher.publish(result)
    """
    def __init__(self, config: PredictorConfig = PredictorConfig()) -> None:
        self._config = config
        self._session = None  
        self._input_name: str = ""
        self._model_loaded: bool = False
        self.stats = PredictorStats()
    @property
    def is_loaded(self) -> bool:
        return self._model_loaded
    def load_model(self, model_path: str | Path) -> bool:
        """
        Load an ONNX model from disk.
        Args:
            model_path: Path to the .onnx model file.
        Returns:
            True on success, False on failure.
        """
        model_path = Path(model_path)
        if not model_path.exists():
            logger.error("Model file not found: %s", model_path)
            return False
        try:
            import onnxruntime as ort
            sess_options = ort.SessionOptions()
            sess_options.inter_op_num_threads = 1
            sess_options.intra_op_num_threads = 2
            sess_options.graph_optimization_level = (
                ort.GraphOptimizationLevel.ORT_ENABLE_ALL
            )
            self._session = ort.InferenceSession(
                str(model_path),
                sess_options=sess_options,
                providers=["CPUExecutionProvider"],
            )
            inputs = self._session.get_inputs()
            outputs = self._session.get_outputs()
            if len(inputs) != 1:
                logger.error("Model must have exactly 1 input, got %d", len(inputs))
                return False
            self._input_name = inputs[0].name
            expected_shape = inputs[0].shape
            if len(expected_shape) == 2 and expected_shape[-1] != NUM_FEATURES:
                logger.error(
                    "Model expects %d features, but FeatureEngineer produces %d",
                    expected_shape[-1],
                    NUM_FEATURES,
                )
                return False
            self._model_loaded = True
            logger.info(
                "Model loaded: %s (input: %s %s, outputs: %d)",
                model_path.name,
                self._input_name,
                expected_shape,
                len(outputs),
            )
            return True
        except Exception as e:
            logger.error("Failed to load ONNX model: %s", e)
            self._model_loaded = False
            return False
    def predict(self, features: np.ndarray) -> PredictionResult:
        """
        Run inference on a feature vector and decide an action.
        Args:
            features: numpy array of shape (NUM_FEATURES,) from FeatureEngineer.
        Returns:
            PredictionResult with frame_drop_probability and recommended action.
        """
        result = PredictionResult()
        result.timestamp_us = int(time.time() * 1_000_000)
        if not self._model_loaded or self._session is None:
            return result
        input_array = features.reshape(1, -1).astype(np.float32)
        t_start = time.perf_counter()
        try:
            outputs = self._session.run(None, {self._input_name: input_array})
        except Exception as e:
            logger.error("ONNX inference failed: %s", e)
            return result
        t_end = time.perf_counter()
        result.inference_time_ms = (t_end - t_start) * 1000.0
        if len(outputs) >= 2:
            probabilities = outputs[1]
            if hasattr(probabilities, "shape") and len(probabilities.shape) == 2:
                result.frame_drop_probability = float(probabilities[0, 1])
            elif isinstance(probabilities, list) and len(probabilities) > 0:
                if isinstance(probabilities[0], dict):
                    result.frame_drop_probability = float(
                        probabilities[0].get(1, 0.0)
                    )
                else:
                    result.frame_drop_probability = float(probabilities[0][-1])
        elif len(outputs) == 1:
            result.frame_drop_probability = float(np.clip(outputs[0][0], 0.0, 1.0))
        prob = result.frame_drop_probability
        result.confidence = abs(prob - 0.5) * 2.0  
        if prob >= self._config.threshold_combined:
            result.action = ActionType.BOOST_AND_REBALANCE
            result.tgp_boost_w = self._config.tgp_boost_high_w
            result.thread_priority = "HIGH"
        elif prob >= self._config.threshold_rebalance:
            result.action = ActionType.REBALANCE_THREADS
            result.tgp_boost_w = self._config.tgp_boost_mid_w
            result.thread_priority = "ABOVE_NORMAL"
        elif prob >= self._config.threshold_boost_tgp:
            result.action = ActionType.BOOST_TGP
            result.tgp_boost_w = self._config.tgp_boost_low_w
            result.thread_priority = "NORMAL"
        else:
            result.action = ActionType.NO_ACTION
            result.tgp_boost_w = 0
            result.thread_priority = "NORMAL"
        result.contributing_factors = self._analyze_factors(features)
        current_dpc = features[0]  
        dpc_delta = features[17]   
        if dpc_delta > 0:
            result.predicted_latency_spike_us = current_dpc + dpc_delta * 5.0
        else:
            result.predicted_latency_spike_us = current_dpc
        self.stats.total_predictions += 1
        self.stats.record_inference(result.inference_time_ms)
        if result.action != ActionType.NO_ACTION:
            self.stats.total_actions_triggered += 1
        return result
    def _analyze_factors(self, features: np.ndarray) -> list[str]:
        """
        Identify the top contributing factors for a high frame-drop prediction.
        Uses simple threshold-based heuristics on individual features.
        """
        factors: list[str] = []
        if features[16] > 2.0:  
            factors.append("dpc_spike")
        if features[14] < 5.0:  
            factors.append("thermal_throttle")
        if features[13] > 0.90:  
            factors.append("vram_pressure")
        if features[15] > 0.95:  
            factors.append("tgp_saturation")
        if features[18] < -50:  
            factors.append("clock_instability")
        if features[23] > 3.0:  
            factors.append("frame_time_jitter")
        return factors
