from dataclasses import dataclass, field
from pathlib import Path
@dataclass(frozen=True)
class PipeConfig:
    """Named Pipe connection settings."""
    telemetry_pipe: str = r"\\.\pipe\neuropace-telemetry"
    action_pipe: str = r"\\.\pipe\neuropace-action"
    prediction_pipe: str = r"\\.\pipe\neuropace-prediction"
    read_buffer_size: int = 65536
    write_buffer_size: int = 65536
    max_pipe_instances: int = 4
    reconnect_interval_sec: float = 1.0
@dataclass(frozen=True)
class FeatureConfig:
    """Feature engineering parameters."""
    window_size: int = 100             
    min_window_size: int = 10          
    dpc_spike_threshold_us: float = 500.0   
    gpu_throttle_temp_c: int = 90      
    gpu_max_tgp_w: int = 355           
    gpu_max_safe_temp_c: int = 95      
@dataclass(frozen=True)
class PredictorConfig:
    """Model inference parameters."""
    model_path: str = ""               
    prediction_interval_frames: int = 5  
    threshold_boost_tgp: float = 0.3         
    threshold_rebalance: float = 0.6         
    threshold_combined: float = 0.8          
    tgp_boost_low_w: int = 10
    tgp_boost_mid_w: int = 20
    tgp_boost_high_w: int = 30
@dataclass(frozen=True)
class AIEngineConfig:
    """Top-level configuration aggregating all sub-configs."""
    pipes: PipeConfig = field(default_factory=PipeConfig)
    features: FeatureConfig = field(default_factory=FeatureConfig)
    predictor: PredictorConfig = field(default_factory=PredictorConfig)
    log_level: str = "INFO"
    enable_dashboard_output: bool = True
def get_default_model_path() -> Path:
    """Resolve the default ONNX model path relative to the ai-engine directory."""
    return Path(__file__).resolve().parent.parent / "models" / "frame_drop_rf.onnx"
