const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const { EventEmitter } = require('events');
const PROJECT_ROOT = path.resolve(__dirname, '..', '..');
function resolveCommand(primary, fallback) {
    if (fs.existsSync(primary)) return primary;
    if (fallback && fs.existsSync(fallback)) return fallback;
    return primary;
}

function resolvePythonCommand() {
    return process.env.NEUROPACE_PYTHON || (process.platform === 'win32' ? 'py' : 'python3');
}
const TELEMETRY_EXE = resolveCommand(
    path.join(PROJECT_ROOT, 'bin', 'neuropace-telemetry.exe'),
    path.join(PROJECT_ROOT, 'telemetry', 'build', 'Release', 'neuropace-telemetry.exe')
);
const AI_ENGINE_SCRIPT = resolveCommand(
    path.join(PROJECT_ROOT, 'ai-engine', 'src', 'main.py'),
    path.join(PROJECT_ROOT, 'scripts', 'main.py')
);
const ACTUATOR_EXE = (() => {
    const candidates = [
        path.join(PROJECT_ROOT, 'actuator', 'build', 'Release', 'neuropace-actuator.exe'),
        path.join(PROJECT_ROOT, 'bin', 'neuropace-actuator.exe'),
        path.join(PROJECT_ROOT, 'releases', 'NeuroPace-RDNA-v0.1.0', 'bin', 'neuropace-actuator.exe'),
    ];
    return candidates.find(p => fs.existsSync(p)) || candidates[2];
})();
console.log('[PATH] Project root:', PROJECT_ROOT);
console.log('[PATH] Telemetry:', TELEMETRY_EXE, fs.existsSync(TELEMETRY_EXE) ? '(OK)' : '(MISSING)');
console.log('[PATH] AI Engine:', AI_ENGINE_SCRIPT, fs.existsSync(AI_ENGINE_SCRIPT) ? '(OK)' : '(MISSING)');
console.log('[PATH] Actuator:', ACTUATOR_EXE, fs.existsSync(ACTUATOR_EXE) ? '(OK)' : '(MISSING)');
const MODULE_DEFINITIONS = {
    telemetry: {
        label: 'Telemetry',
        description: 'ETW + ADLX Sensor Module',
        command: TELEMETRY_EXE,
        args: [],
        cwd: path.dirname(TELEMETRY_EXE),
        icon: 'sensor',
    },
    ai_engine: {
        label: 'AI Engine',
        description: 'ONNX Prediction Engine',
        command: resolvePythonCommand(),
        args: process.platform === 'win32' && !process.env.NEUROPACE_PYTHON
            ? ['-3', AI_ENGINE_SCRIPT]
            : [AI_ENGINE_SCRIPT],
        cwd: path.dirname(AI_ENGINE_SCRIPT),
        icon: 'brain',
    },
    actuator: {
        label: 'Actuator',
        description: 'CPU Scheduler + TGP Control',
        command: ACTUATOR_EXE,
        args: [],
        cwd: path.dirname(ACTUATOR_EXE),
        icon: 'gear',
        optional: false,
    },
};
class ProcessManager extends EventEmitter {
    constructor() {
        super();
        this.modules = {};
        for (const [id, def] of Object.entries(MODULE_DEFINITIONS)) {
            this.modules[id] = {
                id,
                ...def,
                status: 'stopped',
                pid: null,
                proc: null,
                uptime: null,
                startedAt: null,
                exitCode: null,
                logs: [],
                maxLogs: 500,
            };
        }
    }
    start(moduleId) {
        const mod = this.modules[moduleId];
        if (!mod) return { ok: false, error: `Unknown module: ${moduleId}` };
        if (mod.status === 'running' || mod.status === 'starting' || mod.status === 'stopping') {
            return { ok: false, error: `${mod.label} is ${mod.status}` };
        }
        if (path.isAbsolute(mod.command) && !fs.existsSync(mod.command)) {
            mod.status = 'error';
            mod.exitCode = null;
            this._addLog(moduleId, 'error', `Executable not found: ${mod.command}`);
            this._emit('status', moduleId);
            return { ok: false, error: `Executable not found: ${mod.command}` };
        }
        mod.status = 'starting';
        mod.exitCode = null;
        mod.logs = [];
        this._emit('status', moduleId);
        try {
            const proc = spawn(mod.command, mod.args || [], {
                cwd: mod.cwd,
                stdio: ['ignore', 'pipe', 'pipe'],
                windowsHide: true,
            });
            mod.proc = proc;
            mod.pid = proc.pid || null;
            mod.startedAt = Date.now();
            if (proc.pid) {
                mod.status = 'running';
                this._emit('status', moduleId);
                this._addLog(moduleId, 'info', `Process started (PID: ${proc.pid})`);
            }
            proc.stdout.on('data', (data) => {
                const lines = data.toString().split('\n').filter(l => l.trim());
                for (const line of lines) {
                    this._addLog(moduleId, 'info', line.trim());
                }
            });
            proc.stderr.on('data', (data) => {
                const lines = data.toString().split('\n').filter(l => l.trim());
                for (const line of lines) {
                    const level = line.includes('ERROR') || line.includes('error') ? 'error'
                                : line.includes('WARN') || line.includes('warning') ? 'warn'
                                : 'info';
                    this._addLog(moduleId, level, line.trim());
                }
            });
            proc.once('close', (code, signal) => {
                if (mod.proc !== proc) return;
                mod.exitCode = code;
                mod.status = code === 0 || mod.status === 'stopping' ? 'stopped' : 'error';
                mod.pid = null;
                mod.proc = null;
                mod.startedAt = null;
                const detail = signal ? `signal: ${signal}` : `code: ${code}`;
                this._addLog(moduleId, mod.status === 'stopped' ? 'info' : 'error', `Process exited (${detail})`);
                this._emit('status', moduleId);
            });
            proc.once('error', (err) => {
                if (mod.proc !== proc) return;
                mod.status = 'error';
                mod.pid = null;
                mod.proc = null;
                mod.startedAt = null;
                this._addLog(moduleId, 'error', `Spawn error: ${err.message}`);
                this._emit('status', moduleId);
            });
            return proc.pid
                ? { ok: true, pid: proc.pid }
                : { ok: false, error: `Failed to start ${mod.label}` };
        } catch (err) {
            mod.status = 'error';
            this._addLog(moduleId, 'error', `Failed to start: ${err.message}`);
            this._emit('status', moduleId);
            return { ok: false, error: err.message };
        }
    }
    stop(moduleId) {
        const mod = this.modules[moduleId];
        if (!mod) return { ok: false, error: `Unknown module: ${moduleId}` };
        if (mod.status !== 'running' || !mod.proc) return { ok: false, error: `${mod.label} not running` };
        try {
            const proc = mod.proc;
            const pid = proc.pid;
            mod.status = 'stopping';
            this._emit('status', moduleId);
            proc.kill();
            const forceTimer = setTimeout(() => {
                if (mod.proc !== proc || proc.exitCode !== null || !pid) return;
                try {
                    if (process.platform === 'win32') {
                        spawn('taskkill', ['/PID', String(pid), '/F', '/T'], {
                            stdio: 'ignore', windowsHide: true,
                        }).unref();
                    } else {
                        proc.kill('SIGKILL');
                    }
                    this._addLog(moduleId, 'warn', 'Process did not exit in time; force termination requested');
                } catch (error) {
                    this._addLog(moduleId, 'error', `Force termination failed: ${error.message}`);
                }
            }, 2000);
            forceTimer.unref();
            this._addLog(moduleId, 'info', 'Stop command sent');
            return { ok: true };
        } catch (err) {
            return { ok: false, error: err.message };
        }
    }
    async startAll() {
        const results = {};
        for (const id of Object.keys(this.modules)) {
            if (this.modules[id].optional) continue;
            results[id] = this.start(id);
            await new Promise(r => setTimeout(r, 1500));
        }
        return results;
    }
    stopAll() {
        const results = {};
        for (const id of Object.keys(this.modules)) {
            if (this.modules[id].status === 'running') {
                results[id] = this.stop(id);
            }
        }
        return results;
    }
    getAllStatus() {
        const result = {};
        for (const [id, mod] of Object.entries(this.modules)) {
            result[id] = {
                id,
                label: mod.label,
                description: mod.description,
                icon: mod.icon,
                status: mod.status,
                pid: mod.pid,
                uptime: mod.startedAt && ['running', 'stopping'].includes(mod.status)
                    ? Date.now() - mod.startedAt
                    : null,
                exitCode: mod.exitCode,
                optional: !!mod.optional,
            };
        }
        return result;
    }
    getLogs(moduleId, limit = 100) {
        const mod = this.modules[moduleId];
        if (!mod) return [];
        const safeLimit = Number.isFinite(Number(limit))
            ? Math.min(500, Math.max(1, Math.trunc(Number(limit))))
            : 100;
        return mod.logs.slice(-safeLimit);
    }
    _addLog(moduleId, level, message) {
        const mod = this.modules[moduleId];
        const entry = {
            timestamp: new Date().toISOString(),
            module: moduleId,
            level,
            message,
        };
        mod.logs.push(entry);
        if (mod.logs.length > mod.maxLogs) {
            mod.logs.shift();
        }
        this.emit('log', entry);
    }
    _emit(event, moduleId) {
        this.emit(event, { moduleId, modules: this.getAllStatus() });
    }
    shutdown() {
        for (const [id, mod] of Object.entries(this.modules)) {
            if (mod.proc && mod.proc.exitCode === null) {
                try { mod.proc.kill(); } catch (error) {
                    this._addLog(id, 'error', `Shutdown failed: ${error.message}`);
                }
            }
        }
    }
}
module.exports = { ProcessManager, resolveCommand, resolvePythonCommand };
