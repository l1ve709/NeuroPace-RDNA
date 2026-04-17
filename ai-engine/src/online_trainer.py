"""
NeuroPace RDNA - Online Trainer
Continuously collects real game telemetry, auto-labels it, and
retrains the ONNX model every N minutes without stopping the engine.
"""
from __future__ import annotations
import logging
import threading
import time
from collections import deque
from pathlib import Path
from typing import Optional
import numpy as np
logger = logging.getLogger("neuropace.ai.online_trainer")
class SampleBuffer:
    """Thread-safe ring-buffer for labeled (features, label) pairs."""
    def __init__(self, max_size: int = 5000) -> None:
        self._buf: deque[tuple[np.ndarray, int]] = deque(maxlen=max_size)
        self._lock = threading.Lock()
    def add(self, features: np.ndarray, label: int) -> None:
        with self._lock:
            self._buf.append((features.copy(), label))
    def snapshot(self) -> tuple[np.ndarray, np.ndarray]:
        """Return (X, y) arrays for current buffer contents."""
        with self._lock:
            if not self._buf:
                return np.empty((0,)), np.empty((0,))
            X = np.stack([s[0] for s in self._buf]).astype(np.float32)
            y = np.array([s[1] for s in self._buf], dtype=np.int32)
        return X, y
    @property
    def size(self) -> int:
        with self._lock:
            return len(self._buf)
class OnlineTrainer:
    """
    Runs in a background thread. Observes every prediction cycle,
    auto-labels frames using hardware thresholds, and periodically
    retrains + exports a new ONNX model which the predictor hot-reloads.
    """
    def __init__(self, config, model_output_path: Path) -> None:
        self._cfg = config  
        self._model_path = model_output_path
        self._buffer = SampleBuffer(max_size=config.max_memory_samples)
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._last_retrain = time.time()
        self._retrain_count = 0
    def start(self) -> None:
        if not self._cfg.enabled:
            logger.info("[OnlineTrainer] Disabled by config.")
            return
        self._thread = threading.Thread(
            target=self._loop, daemon=True, name="OnlineTrainer"
        )
        self._thread.start()
        logger.info("[OnlineTrainer] Started. Will auto-retrain every %.0fs "
                    "when ≥%d samples collected.",
                    self._cfg.retrain_interval_sec,
                    self._cfg.min_samples_to_retrain)
    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=5.0)
    def observe(self, features: np.ndarray, gpu_tgp_w: float, dpc_latency_us: float) -> None:
        """
        Call this after every feature extraction.
        Auto-labels the frame as risky (1) or safe (0).
        """
        label = 1 if (
            gpu_tgp_w > self._cfg.label_high_load_tgp_w or
            dpc_latency_us > self._cfg.label_spike_dpc_us
        ) else 0
        self._buffer.add(features, label)
    def _loop(self) -> None:
        """Background retraining loop."""
        while not self._stop.wait(timeout=30.0):
            elapsed = time.time() - self._last_retrain
            n_samples = self._buffer.size
            if (n_samples >= self._cfg.min_samples_to_retrain and
                    elapsed >= self._cfg.retrain_interval_sec):
                self._retrain(n_samples)
    def _retrain(self, n_samples: int) -> None:
        """Train a RandomForest on buffered data and export to ONNX."""
        logger.info("[OnlineTrainer] Starting retraining on %d samples...", n_samples)
        X, y = self._buffer.snapshot()
        if len(X) == 0 or len(np.unique(y)) < 2:
            logger.warning("[OnlineTrainer] Not enough class diversity — skipping.")
            return
        try:
            from sklearn.ensemble import RandomForestClassifier
            from sklearn.model_selection import train_test_split
            from skl2onnx import convert_sklearn
            from skl2onnx.common.data_types import FloatTensorType
            X_train, X_test, y_train, y_test = train_test_split(
                X, y, test_size=0.2, random_state=42, stratify=y
            )
            clf = RandomForestClassifier(
                n_estimators=150,
                max_depth=10,
                min_samples_split=4,
                class_weight="balanced",
                n_jobs=-1,
                random_state=42,
            )
            clf.fit(X_train, y_train)
            test_acc = clf.score(X_test, y_test)
            logger.info("[OnlineTrainer] Training complete. Test accuracy: %.3f", test_acc)
            initial_type = [("float_input", FloatTensorType([None, X.shape[1]]))]
            onnx_model = convert_sklearn(clf, initial_types=initial_type,
                                        target_opset=17)
            tmp_path = self._model_path.with_suffix(".tmp.onnx")
            with open(tmp_path, "wb") as f:
                f.write(onnx_model.SerializeToString())
            tmp_path.replace(self._model_path)
            self._retrain_count += 1
            self._last_retrain = time.time()
            logger.info("[OnlineTrainer] Model updated (retrain #%d) → %s",
                        self._retrain_count, self._model_path.name)
        except ImportError as e:
            logger.warning("[OnlineTrainer] sklearn/skl2onnx not installed: %s. "
                           "Run: pip install scikit-learn skl2onnx", e)
        except Exception as e:
            logger.error("[OnlineTrainer] Retrain failed: %s", e)
    @property
    def stats(self) -> dict:
        return {
            "buffer_size": self._buffer.size,
            "retrain_count": self._retrain_count,
            "seconds_since_retrain": round(time.time() - self._last_retrain, 1),
        }
