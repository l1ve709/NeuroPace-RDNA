# Anti-Cheat Compliance Documentation

## NeuroPace RDNA — Riot Vanguard & EAC Compatibility Statement

### Principle: 100% User-Mode, Zero Game Intrusion

NeuroPace RDNA operates entirely in **Ring-3 (User-Mode)** and does NOT:

- ❌ Load any kernel-mode driver (.sys)
- ❌ Read or write game process memory (`ReadProcessMemory` / `WriteProcessMemory`)
- ❌ Inject DLLs or hook any API calls
- ❌ Create code caves or modify game binaries
- ❌ Intercept or modify network packets
- ❌ Use `NtQuerySystemInformation` on game processes
- ❌ Attach a debugger to any game process

### What NeuroPace RDNA DOES use (all public, documented APIs):

| API | Purpose | Risk Level |
|-----|---------|------------|
| `StartTraceW` / `ProcessTrace` (ETW) | Read DPC/ISR latency from kernel events | ✅ None — OS monitoring API |
| `SetProcessAffinityMask` | Adjust CPU core assignment for game process | ✅ None — Task Manager equivalent |
| `SetThreadPriority` | Adjust thread scheduling priority | ✅ None — Task Manager equivalent |
| AMD ADLX SDK | Read/write GPU power and clock settings | ✅ None — AMD official SDK |
| `CreateNamedPipe` / `ConnectNamedPipe` | Inter-process communication | ✅ None — Standard IPC |

### Process Interaction Model

```
NeuroPace RDNA process ←→ Named Pipes ←→ NeuroPace AI Engine
                          ↕
                   Windows Scheduler
                          ↕
                    Game Process (untouched)
```

NeuroPace communicates with the **Windows Scheduler** (via public APIs), NOT with the game directly. The game's memory space is never accessed.

### Verification

To verify compliance:
1. Run Process Monitor (Sysinternals) alongside NeuroPace
2. Filter for game process PID
3. Confirm zero `ReadFile`/`WriteFile` operations targeting game memory
4. Confirm zero DLL injection events
