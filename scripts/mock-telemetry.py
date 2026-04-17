import json
import time
import signal
import sys
import threading
import numpy as np
import win32pipe
import win32file
import pywintypes
PIPE_NAME = r'\\.\pipe\neuropace-telemetry'
PUBLISH_RATE_HZ = 100
PUBLISH_INTERVAL = 1.0 / PUBLISH_RATE_HZ
shutdown = False
def signal_handler(sig, frame):
    global shutdown
    shutdown = True
    print("\n[MOCK] Shutdown requested...")
signal.signal(signal.SIGINT, signal_handler)
class MockTelemetryGenerator:
    """Generates realistic GPU telemetry data with occasional anomalies."""
    def __init__(self, seed=42):
        self.rng = np.random.default_rng(seed)
        self.seq = 0
        self.t_start = time.time()
        self.base_clock = 2500
        self.base_temp = 72.0
        self.base_tgp = 300
        self.base_dpc = 45.0
        self.anomaly_timer = 0
        self.anomaly_type = None
    def generate(self) -> dict:
        self.seq += 1
        t = time.time() - self.t_start
        if self.anomaly_timer <= 0 and self.rng.random() < 0.002:
            self.anomaly_type = self.rng.choice(['dpc_spike', 'thermal', 'vram'])
            self.anomaly_timer = int(self.rng.uniform(50, 200))  
        if self.anomaly_timer > 0:
            self.anomaly_timer -= 1
            if self.anomaly_timer == 0:
                self.anomaly_type = None
        dpc = self._gen_dpc()
        isr = self._gen_isr()
        gpu = self._gen_gpu()
        frame_time = self._gen_frame_time(dpc, gpu['gpu_clock_mhz'])
        return {
            "timestamp_us": int(t * 1_000_000),
            "sequence_id": self.seq,
            "dpc_isr": {
                "dpc_latency_us": round(dpc, 2),
                "dpc_avg_us": round(self.base_dpc + self.rng.normal(0, 2), 2),
                "dpc_max_us": round(max(dpc, 120 + self.rng.normal(0, 10)), 2),
                "isr_latency_us": round(isr, 2),
                "isr_avg_us": round(15 + self.rng.normal(0, 1), 2),
                "isr_max_us": round(max(isr, 40 + self.rng.normal(0, 5)), 2),
                "dpc_count": int(1000 + self.rng.normal(0, 30)),
                "isr_count": int(800 + self.rng.normal(0, 20)),
            },
            "gpu": gpu,
            "frame_time_ms": round(frame_time, 3),
            "fps_instant": round(1000.0 / max(frame_time, 0.1), 1),
        }
    def _gen_dpc(self) -> float:
        if self.anomaly_type == 'dpc_spike':
            return float(np.clip(self.base_dpc + self.rng.exponential(400), 200, 5000))
        return float(np.clip(self.base_dpc + self.rng.normal(0, 8), 5, 200))
    def _gen_isr(self) -> float:
        return float(np.clip(15 + self.rng.normal(0, 3), 2, 60))
    def _gen_gpu(self) -> dict:
        if self.anomaly_type == 'thermal':
            temp = int(np.clip(90 + self.rng.normal(0, 2), 87, 98))
            clock = int(np.clip(1900 + self.rng.normal(0, 50), 1600, 2100))
        else:
            temp = int(np.clip(self.base_temp + self.rng.normal(0, 1.5), 55, 85))
            clock = int(np.clip(self.base_clock + self.rng.normal(0, 20), 2200, 2700))
        if self.anomaly_type == 'vram':
            vram_used = int(np.clip(22000 + self.rng.normal(0, 500), 20000, 24500))
        else:
            vram_used = int(np.clip(8200 + self.rng.normal(0, 200), 4000, 16000))
        return {
            "gpu_clock_mhz": clock,
            "mem_clock_mhz": 1250,
            "gpu_temp_c": temp,
            "hotspot_temp_c": temp + 12,
            "gpu_tgp_w": int(np.clip(self.base_tgp + self.rng.normal(0, 10), 250, 355)),
            "vram_used_mb": vram_used,
            "vram_total_mb": 24576,
            "gpu_utilization_pct": round(float(np.clip(92 + self.rng.normal(0, 3), 50, 100)), 1),
            "fan_speed_rpm": int(np.clip(1400 + self.rng.normal(0, 50), 800, 3000)),
        }
    def _gen_frame_time(self, dpc: float, clock: int) -> float:
        base = 6.9
        if dpc > 500:
            base += dpc * 0.005
        if clock < 2200:
            base += (2500 - clock) * 0.003
        base += self.rng.normal(0, 0.3)
        return float(np.clip(base, 3.0, 60.0))
def pipe_client_handler(pipe_handle, client_id, generator):
    """Serve one pipe client with telemetry data."""
    global shutdown
    print(f"[MOCK] Client {client_id} connected")
    try:
        while not shutdown:
            frame = generator.generate()
            payload = json.dumps(frame, separators=(',', ':')) + '\n'
            try:
                win32file.WriteFile(pipe_handle, payload.encode('utf-8'))
            except pywintypes.error:
                break
            time.sleep(PUBLISH_INTERVAL)
    except Exception as e:
        print(f"[MOCK] Client {client_id} error: {e}")
    finally:
        try:
            win32pipe.DisconnectNamedPipe(pipe_handle)
            win32file.CloseHandle(pipe_handle)
        except pywintypes.error:
            pass
        print(f"[MOCK] Client {client_id} disconnected")
def main():
    global shutdown
    print("=" * 60)
    print("  NeuroPace RDNA -- Mock Telemetry Publisher")
    print(f"  Pipe: {PIPE_NAME}")
    print(f"  Rate: {PUBLISH_RATE_HZ} Hz")
    print("=" * 60)
    print("")
    generator = MockTelemetryGenerator()
    client_id = 0
    while not shutdown:
        try:
            pipe_handle = win32pipe.CreateNamedPipe(
                PIPE_NAME,
                win32pipe.PIPE_ACCESS_OUTBOUND,
                win32pipe.PIPE_TYPE_BYTE | win32pipe.PIPE_READMODE_BYTE | win32pipe.PIPE_WAIT,
                win32pipe.PIPE_UNLIMITED_INSTANCES,
                65536,
                0,
                0,
                None,
            )
        except pywintypes.error as e:
            print(f"[MOCK] CreateNamedPipe error: {e}")
            time.sleep(1)
            continue
        print(f"[MOCK] Waiting for client (pipe instance ready)...")
        try:
            win32pipe.ConnectNamedPipe(pipe_handle, None)
        except pywintypes.error as e:
            if e.winerror != 535:  
                if not shutdown:
                    print(f"[MOCK] ConnectNamedPipe error: {e}")
                try:
                    win32file.CloseHandle(pipe_handle)
                except pywintypes.error:
                    pass
                if shutdown:
                    break
                time.sleep(0.5)
                continue
        client_id += 1
        t = threading.Thread(
            target=pipe_client_handler,
            args=(pipe_handle, client_id, generator),
            daemon=True,
        )
        t.start()
    print("\n[MOCK] Mock telemetry stopped.")
if __name__ == '__main__':
    main()
