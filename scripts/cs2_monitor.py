"""
NeuroPace RDNA — CS2 Real-Time Performance Monitor
Captures telemetry data every second and writes to CSV for analysis.
"""
import time
import json
import csv
import os
import subprocess
import sys
try:
    import websocket
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "websocket-client"])
    import websocket
OUTPUT_CSV = os.path.join(os.path.dirname(__file__), "cs2_session.csv")
WS_URL = "ws://localhost:3200"
samples = []
session_start = None
cs2_pid = None
FIELDS = [
    "timestamp", "elapsed_s",
    "gpu_clock_mhz", "gpu_temp_c", "hotspot_temp_c", "gpu_tgp_w",
    "vram_used_mb", "vram_total_mb",
    "dpc_latency_us", "dpc_avg_us", "dpc_max_us",
    "isr_latency_us", "isr_avg_us", "isr_max_us",
    "frame_time_ms", "fps",
    "ai_drop_probability", "ai_action", "ai_confidence",
]
def find_cs2():
    """Find CS2 process"""
    try:
        result = subprocess.run(
            ["powershell", "-Command", "Get-Process -Name 'cs2' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id"],
            capture_output=True, text=True, timeout=5
        )
        if result.stdout.strip():
            return int(result.stdout.strip().split()[0])
    except:
        pass
    return None
def on_message(ws, message):
    global session_start, cs2_pid
    try:
        msg = json.loads(message)
    except:
        return
    now = time.time()
    if session_start is None:
        session_start = now
    if msg.get("type") == "telemetry":
        data = msg.get("data", {})
        gpu = data.get("gpu", {})
        dpc = data.get("dpc_isr", {})
        ft = data.get("frame_time_ms", 0)
        fps_val = (1000.0 / ft) if ft > 0 else 0
        fps_val = gpu.get("fps", 0)
        fps_text = "SAFE" if fps_val == 0 else str(round(fps_val, 1))
        row = {
            "timestamp": time.strftime("%H:%M:%S"),
            "elapsed_s": round(now - session_start, 1),
            "gpu_clock_mhz": gpu.get("gpu_clock_mhz", 0),
            "gpu_temp_c": gpu.get("gpu_temp_c", 0),
            "hotspot_temp_c": gpu.get("hotspot_temp_c", 0),
            "gpu_tgp_w": gpu.get("gpu_tgp_w", 0),
            "vram_used_mb": gpu.get("vram_used_mb", 0),
            "vram_total_mb": gpu.get("vram_total_mb", 0),
            "dpc_latency_us": dpc.get("dpc_latency_us", 0),
            "dpc_avg_us": dpc.get("dpc_avg_us", 0),
            "dpc_max_us": dpc.get("dpc_max_us", 0),
            "isr_latency_us": dpc.get("isr_latency_us", 0),
            "isr_avg_us": dpc.get("isr_avg_us", 0),
            "isr_max_us": dpc.get("isr_max_us", 0),
            "frame_time_ms": round(ft, 2) if ft else 0,
            "fps": fps_val,
            "ai_drop_probability": 0,
            "ai_action": "N/A",
            "ai_confidence": 0,
        }
        samples.append(row)
        if len(samples) % 10 == 0:
            print(f"[{row['timestamp']}] GPU: {row['gpu_clock_mhz']}MHz {row['gpu_temp_c']}C {row['gpu_tgp_w']}W | "
                  f"Driver-FPS: {fps_text} | Samples: {len(samples)}")
        if len(samples) % 30 == 1:
            pid = find_cs2()
            if pid and pid != cs2_pid:
                cs2_pid = pid
                print(f"\n*** CS2 DETECTED! PID: {cs2_pid} ***\n")
    elif msg.get("type") == "prediction":
        pred = msg.get("data", {})
        if samples:
            samples[-1]["ai_drop_probability"] = round(
                pred.get("prediction", {}).get("frame_drop_probability", 0), 4
            )
            samples[-1]["ai_action"] = pred.get("action", "N/A")
            samples[-1]["ai_confidence"] = round(pred.get("confidence", 0), 4)
def on_error(ws, error):
    print(f"[WS Error] {error}")
def on_close(ws, code, msg):
    print(f"\n[Session Ended] Captured {len(samples)} samples")
    save_csv()
def on_open(ws):
    print("=" * 60)
    print("  NeuroPace RDNA — CS2 Performance Monitor")
    print("  Capturing real-time GPU telemetry...")
    print("  Press Ctrl+C to stop and generate report")
    print("=" * 60)
    print()
def save_csv():
    if not samples:
        print("No data to save.")
        return
    with open(OUTPUT_CSV, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(samples)
    print(f"[Saved] {len(samples)} samples -> {OUTPUT_CSV}")
    fps_values = [s["fps"] for s in samples if s["fps"] > 0]
    ft_values = [s["frame_time_ms"] for s in samples if s["frame_time_ms"] > 0]
    dpc_values = [s["dpc_latency_us"] for s in samples if s["dpc_latency_us"] > 0]
    temp_values = [s["gpu_temp_c"] for s in samples if s["gpu_temp_c"] > 0]
    if fps_values:
        fps_sorted = sorted(fps_values)
        p1_low = fps_sorted[max(0, len(fps_sorted) // 100)]
        print(f"\n{'='*50}")
        print(f"  SESSION REPORT")
        print(f"{'='*50}")
        print(f"  Duration:     {samples[-1]['elapsed_s']}s")
        print(f"  Samples:      {len(samples)}")
        print(f"  Avg FPS:      {sum(fps_values)/len(fps_values):.1f}")
        print(f"  1% Low FPS:   {p1_low:.1f}")
        print(f"  Min FPS:      {min(fps_values):.1f}")
        print(f"  Max FPS:      {max(fps_values):.1f}")
        if ft_values:
            print(f"  Avg FT:       {sum(ft_values)/len(ft_values):.2f}ms")
            print(f"  Max FT:       {max(ft_values):.2f}ms")
        if dpc_values:
            print(f"  Avg DPC:      {sum(dpc_values)/len(dpc_values):.1f}us")
            print(f"  Max DPC:      {max(dpc_values):.1f}us")
        if temp_values:
            print(f"  Peak Temp:    {max(temp_values)}C")
        print(f"{'='*50}")
if __name__ == "__main__":
    try:
        ws = websocket.WebSocketApp(
            WS_URL,
            on_open=on_open,
            on_message=on_message,
            on_error=on_error,
            on_close=on_close,
        )
        ws.run_forever()
    except KeyboardInterrupt:
        print("\n[Stopping...]")
        save_csv()
