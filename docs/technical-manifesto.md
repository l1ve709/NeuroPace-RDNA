# NeuroPace RDNA: The Engineering Reality

*A transparent, BS-free evaluation of what NeuroPace RDNA actually does, written for system engineers, kernel developers, and skeptics.*

---

If you are an engineer looking at the terms "AI Frame Optimizer" or "DPC Latency Mitigator," your immediate reaction is likely skepticism. Windows is not an RTOS (Real-Time Operating System), user-mode applications cannot dictate kernel scheduling deterministic loops, and you certainly cannot override GPU firmware from Ring-3.

You are correct. This document explains the actual architectural boundaries of NeuroPace RDNA, separating marketing terminology from our engineering reality.

## 1. What NeuroPace is NOT

To understand the system, we must first define its constraints:

- **It is NOT a Kernel Controller:** We do not manipulate `NDIS` (Network) interrupts, USB interrupts, or hook `ntoskrnl.exe`. Doing so would violate modern anti-cheat systems (Vanguard, EAC, BattlEye) and risk system stability (BSODs).
- **It is NOT a "GPU Overclocker":** We do not inject voltages or hard-lock the GPU clock state. The AMD GPU driver remains the final authority on power delivery and clock speeds.
- **It is NOT a Deterministic Background Loop:** A "200Hz loop" in Windows User-Mode is an *average* rate, not a hard deterministic cycle limit. Windows context switching and scheduler jitter mean our loop will naturally fluctuate (e.g., 2ms to 12ms per tick).

## 2. What NeuroPace ACTUALLY Is

A more scientifically accurate description of NeuroPace RDNA is a **User-Mode Adaptive Performance Orchestration Layer**. 

### The Methodology: Observation + Inference → Soft Control

We divide the problem into three decoupled stages:

#### A. Telemetry & Observation (The "Collect" Phase)
Instead of hooking DirectX / Vulkan present queues, we use two passive, entirely legal observation pipelines:
1. **ETW (Event Tracing for Windows):** A low-overhead kernel facility. We listen for `DPC/ISR` duration events. We don't control the interrupts; we simply observe when the OS kernel is experiencing an interrupt storm.
2. **AMD ADLX SDK:** We poll the GPU firmware for high-resolution metrics (TGP, Hotspot limits, Driver-level FPS) bypassing WMI overhead.

#### B. Lightweight Inference (The "Decide" Phase)
Heavy neural networks (LSTMs, Transformers) would incur a massive Context Switch cost and cache pollution, defeating the purpose of an optimizer. 
Instead, we use a highly constrained **Random Forest Model** compiled to C++ via `ONNX Runtime`. It evaluates a 28-feature rolling window in **<0.7 milliseconds** on the CPU. It doesn't "understand" the game; it simply correlates specific ETW latency spikes and Thermal conditions with an impending frame drop.

#### C. Policy Adjustment (The "Apply" Phase)
When the AI infers a high probability of a stutter on the *next* frame, we execute **soft mitigations (hints)** rather than hard control:
1. **Windows API Priority Hints:** We call `SetPriorityClass` and `SetThreadPriority` to temporarily elevate the game's execution context to `High/Realtime`. We also attempt `SetProcessAffinityMask` to isolate the core, but if the game's anti-cheat strips our handle rights (`Error 5: Access Denied`), we gracefully fallback to priority-only mitigation.
2. **AMD ADLX Power Policy Adjustment:** We do *not* force a clock speed. Instead, we use the ADLX API to dynamically expand the *Total Board Power (TBP) envelope* (e.g., granting a momentary +35W headroom limit). This acts as a policy hint to the AMD driver: *"You have thermal/power clearance; do not throttle the clock state during this heavy frame."*

## 3. The "0.5% CPU Overhead" Reality

Claiming low CPU *utilization* percentage can be misleading. A program utilizing 0% CPU can still ruin game performance if its wakeup intervals cause aggressive context switching, or if it competes for L3 Cache space with the game engine.

We mitigate this by:
- Caching data in lock-free rings.
- Utilizing Named Pipes (`PIPE_TYPE_BYTE | FILE_FLAG_OVERLAPPED`) rather than network sockets or REST APIs, achieving IPC transport with sub-millisecond overhead.
- Keeping the AI inference exclusively on mathematical trees rather than complex memory-bound models.

## Conclusion

NeuroPace RDNA does not magically rewrite the Windows NT scheduler, nor does it hardware-hack your AMD GPU. 

It is simply an extremely fast, carefully orchestrated daemon that reads the OS's own telemetry, recognizes the mathematical signature of an incoming pipeline stall, and politely asks the Windows Scheduler and the AMD Driver for maximum priority clearance before the present queue drops the frame.

It is coordination, not coercion. And in practice, that coordination is enough to transform a choppy 1% low frame-pacing graph into a pristine, readable line.
