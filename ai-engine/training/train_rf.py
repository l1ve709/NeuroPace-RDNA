from __future__ import annotations
import sys
import time
import pickle
from pathlib import Path
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import (
    accuracy_score,
    precision_score,
    recall_score,
    f1_score,
    roc_auc_score,
    classification_report,
    confusion_matrix,
)
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from feature_engineer import FEATURE_NAMES, NUM_FEATURES
def load_dataset(csv_path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Load the synthetic telemetry CSV and return (X, y)."""
    df = pd.read_csv(csv_path)
    expected_cols = set(FEATURE_NAMES + ["frame_drop"])
    actual_cols = set(df.columns)
    missing = expected_cols - actual_cols
    if missing:
        raise ValueError(f"Missing columns in dataset: {missing}")
    X = df[FEATURE_NAMES].values.astype(np.float64)
    y = df["frame_drop"].values.astype(np.int64)
    return X, y
def train_model(X_train: np.ndarray, y_train: np.ndarray) -> RandomForestClassifier:
    """Train a Random Forest classifier optimized for frame drop detection."""
    model = RandomForestClassifier(
        n_estimators=200,
        max_depth=12,
        min_samples_split=10,
        min_samples_leaf=5,
        max_features="sqrt",
        class_weight="balanced",     
        random_state=42,
        n_jobs=-1,                   
        verbose=0,
    )
    print("  Training Random Forest (200 trees, max_depth=12)...")
    t_start = time.perf_counter()
    model.fit(X_train, y_train)
    t_end = time.perf_counter()
    print(f"  Training time: {t_end - t_start:.2f}s")
    return model
def evaluate_model(
    model: RandomForestClassifier,
    X_test: np.ndarray,
    y_test: np.ndarray,
) -> dict:
    """Evaluate the trained model and print detailed metrics."""
    y_pred = model.predict(X_test)
    y_proba = model.predict_proba(X_test)[:, 1]
    metrics = {
        "accuracy": accuracy_score(y_test, y_pred),
        "precision": precision_score(y_test, y_pred, zero_division=0),
        "recall": recall_score(y_test, y_pred, zero_division=0),
        "f1": f1_score(y_test, y_pred, zero_division=0),
        "roc_auc": roc_auc_score(y_test, y_proba) if len(np.unique(y_test)) > 1 else 0.0,
    }
    print("\n  -- Evaluation Results --------------------------------")
    print(f"  Accuracy:   {metrics['accuracy']:.4f}")
    print(f"  Precision:  {metrics['precision']:.4f}")
    print(f"  Recall:     {metrics['recall']:.4f}")
    print(f"  F1 Score:   {metrics['f1']:.4f}")
    print(f"  ROC AUC:    {metrics['roc_auc']:.4f}")
    print()
    cm = confusion_matrix(y_test, y_pred)
    print("  Confusion Matrix:")
    print(f"    TN={cm[0,0]:>5}  FP={cm[0,1]:>5}")
    print(f"    FN={cm[1,0]:>5}  TP={cm[1,1]:>5}")
    print()
    importances = model.feature_importances_
    indices = np.argsort(importances)[::-1]
    print("  Top 10 Feature Importances:")
    for rank, idx in enumerate(indices[:10], 1):
        print(f"    {rank:>2}. {FEATURE_NAMES[idx]:<28s} {importances[idx]:.4f}")
    return metrics
def export_to_onnx(
    model: RandomForestClassifier,
    output_path: Path,
) -> bool:
    """Export the trained sklearn model to ONNX format."""
    try:
        from skl2onnx import to_onnx
        from skl2onnx.common.data_types import FloatTensorType
        print(f"\n  Exporting to ONNX: {output_path.name}")
        initial_type = [("features", FloatTensorType([None, NUM_FEATURES]))]
        onnx_model = to_onnx(
            model,
            initial_types=initial_type,
            target_opset=17,
            options={id(model): {"zipmap": False}},  
        )
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, "wb") as f:
            f.write(onnx_model.SerializeToString())
        print(f"  ONNX model saved: {output_path}")
        print(f"  File size: {output_path.stat().st_size / 1024:.1f} KB")
        return True
    except Exception as e:
        print(f"  ONNX export failed: {e}")
        return False
def validate_onnx(
    sklearn_model: RandomForestClassifier,
    onnx_path: Path,
    X_test: np.ndarray,
    n_samples: int = 100,
) -> bool:
    """Validate ONNX model produces same predictions as sklearn."""
    try:
        import onnxruntime as ort
        session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
        input_name = session.get_inputs()[0].name
        X_subset = X_test[:n_samples].astype(np.float32)
        sk_proba = sklearn_model.predict_proba(X_subset)[:, 1]
        ort_outputs = session.run(None, {input_name: X_subset})
        ort_proba = ort_outputs[1][:, 1]  
        max_diff = float(np.max(np.abs(sk_proba - ort_proba)))
        mean_diff = float(np.mean(np.abs(sk_proba - ort_proba)))
        print(f"\n  -- ONNX Validation -----------------------------------")
        print(f"  Samples tested:   {n_samples}")
        print(f"  Max prob diff:    {max_diff:.6f}")
        print(f"  Mean prob diff:   {mean_diff:.6f}")
        times = []
        single_input = X_subset[:1]
        for _ in range(200):
            t_start = time.perf_counter()
            session.run(None, {input_name: single_input})
            times.append((time.perf_counter() - t_start) * 1000)
        times = np.array(times)
        print(f"  Inference latency (single sample):")
        print(f"    Mean:   {np.mean(times):.3f} ms")
        print(f"    Median: {np.median(times):.3f} ms")
        print(f"    P99:    {np.percentile(times, 99):.3f} ms")
        if max_diff > 0.01:
            print("  WARNING: ONNX predictions differ significantly from sklearn!")
            return False
        print("  ONNX validation: PASSED [OK]")
        return True
    except Exception as e:
        print(f"  ONNX validation failed: {e}")
        return False
def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    dataset_path = project_root / "training" / "dataset" / "synthetic_telemetry.csv"
    model_dir = project_root / "models"
    pkl_path = model_dir / "frame_drop_rf.pkl"
    onnx_path = model_dir / "frame_drop_rf.onnx"
    print("=" * 60)
    print("  NeuroPace RDNA — Random Forest Training Pipeline")
    print("=" * 60)
    if not dataset_path.exists():
        print(f"\n  Dataset not found: {dataset_path}")
        print("  Run generate_synthetic.py first.")
        sys.exit(1)
    print(f"\n  Loading dataset: {dataset_path.name}")
    X, y = load_dataset(dataset_path)
    print(f"  Samples: {len(y)}  Features: {X.shape[1]}")
    print(f"  Class distribution: neg={int((y == 0).sum())}, pos={int((y == 1).sum())} ({y.mean():.2%} positive)")
    X_train, X_test, y_train, y_test = train_test_split(
        X, y,
        test_size=0.2,
        stratify=y,
        random_state=42,
    )
    print(f"  Train: {len(y_train)}  Test: {len(y_test)}")
    model = train_model(X_train, y_train)
    metrics = evaluate_model(model, X_test, y_test)
    model_dir.mkdir(parents=True, exist_ok=True)
    with open(pkl_path, "wb") as f:
        pickle.dump(model, f)
    print(f"\n  sklearn model saved: {pkl_path.name} ({pkl_path.stat().st_size / 1024:.1f} KB)")
    if export_to_onnx(model, onnx_path):
        validate_onnx(model, onnx_path, X_test)
    print("\n" + "=" * 60)
    print("  Training pipeline complete!")
    print("=" * 60)
if __name__ == "__main__":
    main()
