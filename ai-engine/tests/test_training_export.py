import importlib.util
from pathlib import Path

import numpy as np
import pytest


@pytest.mark.parametrize("export_ok", [False, True])
def test_failed_export_or_validation_keeps_installed_model(tmp_path, monkeypatch, export_ok):
    source = Path(__file__).resolve().parents[1] / "training" / "train_rf.py"
    spec = importlib.util.spec_from_file_location("train_rf", source)
    trainer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(trainer)

    training = tmp_path / "training"
    dataset = training / "dataset" / "synthetic_telemetry.csv"
    dataset.parent.mkdir(parents=True)
    dataset.touch()
    models = tmp_path / "models"
    models.mkdir()
    installed = models / "frame_drop_rf.onnx"
    installed.write_bytes(b"previous validated model")
    legacy = models / "frame_drop_rf.pkl"
    legacy.write_bytes(b"existing pickle")

    monkeypatch.setattr(trainer, "__file__", str(training / source.name))
    monkeypatch.setattr(trainer, "load_dataset", lambda _: (np.zeros((20, 28)), np.tile([0, 1], 10)))
    monkeypatch.setattr(trainer, "train_model", lambda *_: object())
    monkeypatch.setattr(trainer, "evaluate_model", lambda *_: None)

    def export(_model, path):
        path.write_bytes(b"unverified candidate")
        return export_ok

    monkeypatch.setattr(trainer, "export_to_onnx", export)
    monkeypatch.setattr(trainer, "validate_onnx", lambda *_: False)

    with pytest.raises(SystemExit) as failure:
        trainer.main()
    assert failure.value.code == 1
    assert installed.read_bytes() == b"previous validated model"
    assert legacy.read_bytes() == b"existing pickle"
    assert set(models.iterdir()) == {installed, legacy}
