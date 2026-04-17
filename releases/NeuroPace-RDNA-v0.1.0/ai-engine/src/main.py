"""
TR Engineering Note:
This module is part of the NeuroPace RDNA system.
Designed for Ring-3 User-Mode execution. Keep optimizations clean and hardware-specific.
"""
from __future__ import annotations
import logging
import signal
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from config import AIEngineConfig, PipeConfig, FeatureConfig, PredictorConfig, get_default_model_path
from ipc_subscriber import IpcSubscriber
from feature_engineer import FeatureEngineer
from predictor import Predictor, ActionType
from ipc_action_publisher import IpcActionPublisher
def setup_logging(level: str = "INFO") -> None:
    logging.basicConfig(
        level=getattr(logging, level.upper(), logging.INFO),
        format="[%(name)s] %(levelname)s: %(message)s",
        handlers=[logging.StreamHandler(sys.stdout)],
    )
    print("NeuroPace RDNA AI Engine v0.1.0")
_shutdown_requested = False
def _signal_handler(signum: int, frame) -> None:
    global _shutdown_requested
    _shutdown_requested = True
    print("\n[MAIN] Shutdown signal received...")
def run(config: AIEngineConfig) -> int:
    """Main processing loop. Returns exit code."""
    global _shutdown_requested
    model_path = Path(config.predictor.model_path) if config.predictor.model_path else get_default_model_path()
    logger = logging.getLogger("neuropace.ai.main")
    logger.info("Initializing components...")
    subscriber = IpcSubscriber(config.pipes)
    feature_engineer = FeatureEngineer(config.features)
    predictor = Predictor(config.predictor)
    publisher = IpcActionPublisher(config.pipes)
    if model_path.exists():
        if predictor.load_model(model_path):
            logger.info("Model loaded successfully: %s", model_path.name)
        else:
            logger.error("Failed to load model — predictions will be disabled")
    else:
        logger.warning(
            "Model not found at %s — run training first. "
            "Engine will start without predictions.",
            model_path,
        )
    publisher.start()
    logger.info("Action/Prediction pipe servers started")
    logger.info("Connecting to telemetry stream...")
    print("=" * 56)
    print("  Press Ctrl+C to stop the AI Engine")
    print("=" * 56)
    print()
    frame_counter: int = 0
    prediction_counter: int = 0
    last_status_time = time.time()
    status_interval = 5.0  
    try:
        for frame in subscriber.stream():
            if _shutdown_requested:
                break
            frame_counter += 1
            features = feature_engineer.process(frame)
            if features is None:
                continue
            if frame_counter % config.predictor.prediction_interval_frames != 0:
                continue
            result = predictor.predict(features)
            prediction_counter += 1
            result_dict = result.to_dict()
            if result.action != ActionType.NO_ACTION:
                publisher.publish_action(result_dict)
                logger.debug(
                    "ACTION: %s (prob=%.3f, conf=%.3f, factors=%s)",
                    result.action,
                    result.frame_drop_probability,
                    result.confidence,
                    result.contributing_factors,
                )
            if config.enable_dashboard_output:
                publisher.publish_prediction(result_dict)
            now = time.time()
            if now - last_status_time >= status_interval:
                _print_status(
                    frame_counter,
                    prediction_counter,
                    predictor,
                    subscriber,
                    publisher,
                    result,
                )
                last_status_time = now
    except KeyboardInterrupt:
        pass
    logger.info("Shutting down...")
    subscriber.disconnect()
    publisher.stop()
    print()
    print("=" * 56)
    print("  NeuroPace AI Engine - Session Summary")
    print("=" * 56)
    print(f"  Frames processed:     {frame_counter:,}")
    print(f"  Predictions made:     {prediction_counter:,}")
    print(f"  Actions triggered:    {predictor.stats.total_actions_triggered:,}")
    print(f"  Avg inference time:   {predictor.stats.avg_inference_time_ms:.3f} ms")
    print(f"  Max inference time:   {predictor.stats.max_inference_time_ms:.3f} ms")
    print(f"  Subscriber reconnects: {subscriber.stats.reconnect_count}")
    print("=" * 56)
    return 0
def _print_status(
    frames: int,
    predictions: int,
    predictor: Predictor,
    subscriber: IpcSubscriber,
    publisher: IpcActionPublisher,
    last_result,
) -> None:
    """Print a concise status line to stdout."""
    prob = last_result.frame_drop_probability if last_result else 0.0
    action = last_result.action if last_result else "N/A"
    inf_ms = predictor.stats.avg_inference_time_ms
    act_conn = "[OK]" if publisher.action_connected else "[NO]"
    dash_conn = "[OK]" if publisher.prediction_connected else "[NO]"
    print(
        f"[STATUS] frames={frames:>8,} | preds={predictions:>6,} | "
        f"prob={prob:.3f} | action={action:<20s} | "
        f"inf={inf_ms:.2f}ms | "
        f"actuator={act_conn} dashboard={dash_conn}"
    )
def main() -> None:
    setup_logging("INFO")
    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)
    config = AIEngineConfig()
    exit_code = run(config)
    sys.exit(exit_code)
if __name__ == "__main__":
    main()
