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
    window_size: int = 200              
    min_window_size: int = 10           
    dpc_spike_threshold_us: float = 400.0  
    gpu_throttle_temp_c: int = 90
    gpu_max_tgp_w: int = 355
    gpu_max_safe_temp_c: int = 95
@dataclass(frozen=True)
class PredictorConfig:
    """Model inference parameters."""
    model_path: str = ""
    prediction_interval_frames: int = 1  
    threshold_boost_tgp: float = 0.25        
    threshold_rebalance: float = 0.55        
    threshold_combined: float = 0.75         
    tgp_boost_low_w: int = 20               
    tgp_boost_mid_w: int = 35               
    tgp_boost_high_w: int = 50              
@dataclass(frozen=True)
class OnlineLearningConfig:
    """Online / continual learning settings (for self-improvement during play)."""
    enabled: bool = True
    min_samples_to_retrain: int = 500    
    retrain_interval_sec: float = 300.0  
    label_high_load_tgp_w: int = 280     
    label_spike_dpc_us: float = 800.0    
    learning_rate: float = 0.05
    max_memory_samples: int = 5000       
@dataclass(frozen=True)
class TrainingConfig:
    """Model training / export settings."""
    output_dir: str = "models"
    model_name: str = "frame_drop_rf.onnx"
    n_estimators: int = 200              
    max_depth: int = 12                  
    min_samples_split: int = 4
    class_weight: str = "balanced"       
    cv_folds: int = 5
    test_size: float = 0.2
    random_state: int = 42
@dataclass(frozen=True)
class AIEngineConfig:
    """Top-level configuration aggregating all sub-configs."""
    pipes: PipeConfig = field(default_factory=PipeConfig)
    features: FeatureConfig = field(default_factory=FeatureConfig)
    predictor: PredictorConfig = field(default_factory=PredictorConfig)
    online_learning: OnlineLearningConfig = field(default_factory=OnlineLearningConfig)
    training: TrainingConfig = field(default_factory=TrainingConfig)
    log_level: str = "INFO"
    enable_dashboard_output: bool = True
def get_default_model_path() -> Path:
    """Resolve the default ONNX model path relative to the ai-engine directory."""
    return Path(__file__).resolve().parent.parent / "models" / "frame_drop_rf.onnx"
