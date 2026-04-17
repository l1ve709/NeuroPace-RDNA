const { spawn, execSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const { EventEmitter } = require('events');
const PROJECT_ROOT = path.resolve(__dirname, '..', '..');

// Detect paths: in release ZIP, binaries are in bin/, AI engine in ai-engine/src/
// In development, they might be elsewhere. We check both.
function resolveCommand(primary, fallback) {
    if (fs.existsSync(primary)) return primary;
    if (fallback && fs.existsSync(fallback)) return fallback;
    return primary; // Let it fail with a clear error
}

const TELEMETRY_EXE = resolveCommand(
    path.join(PROJECT_ROOT, 'bin', 'neuropace-telemetry.exe'),
    path.join(PROJECT_ROOT, 'releases', 'NeuroPace-RDNA-v0.1.0', 'bin', 'neuropace-telemetry.exe')
);
const AI_ENGINE_SCRIPT = resolveCommand(
    path.join(PROJECT_ROOT, 'ai-engine', 'src', 'main.py'),
    path.join(PROJECT_ROOT, 'scripts', 'main.py')
);
const ACTUATOR_EXE = resolveCommand(
    path.join(PROJECT_ROOT, 'bin', 'neuropace-actuator.exe'),
    path.join(PROJECT_ROOT, 'releases', 'NeuroPace-RDNA-v0.1.0', 'bin', 'neuropace-actuator.exe')
);

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
        command: 'python',
        args: [AI_ENGINE_SCRIPT],
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
        optional: true,
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
        if (mod.status === 'running') return { ok: false, error: `${mod.label} already running` };
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
            mod.pid = proc.pid;
            mod.startedAt = Date.now();
            mod.status = 'running';
            this._emit('status', moduleId);
            this._addLog(moduleId, 'info', `Process started (PID: ${proc.pid})`);
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
            proc.on('close', (code) => {
                mod.exitCode = code;
                mod.status = code === 0 ? 'stopped' : 'error';
                mod.pid = null;
                mod.proc = null;
                this._addLog(moduleId, code === 0 ? 'info' : 'error', `Process exited (code: ${code})`);
                this._emit('status', moduleId);
            });
            proc.on('error', (err) => {
                mod.status = 'error';
                mod.pid = null;
                mod.proc = null;
                this._addLog(moduleId, 'error', `Spawn error: ${err.message}`);
                this._emit('status', moduleId);
            });
            return { ok: true, pid: proc.pid };
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
        if (mod.status !== 'running') return { ok: false, error: `${mod.label} not running` };
        try {
            if (mod.proc) {
                const pid = mod.proc.pid;
                mod.proc.kill(); 
                setTimeout(() => {
                    if (mod.proc && !mod.proc.killed && pid) {
                        try {
                            execSync(`taskkill /PID ${pid} /F /T`, { stdio: 'ignore' });
                            this._addLog(moduleId, 'warn', 'Process forcefully terminated');
                        } catch (e) {
                        }
                    }
                }, 2000);
            }
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
                uptime: mod.startedAt ? Date.now() - mod.startedAt : null,
                exitCode: mod.exitCode,
                optional: !!mod.optional,
            };
        }
        return result;
    }
    getLogs(moduleId, limit = 100) {
        const mod = this.modules[moduleId];
        if (!mod) return [];
        return mod.logs.slice(-limit);
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
            if (mod.proc && !mod.proc.killed) {
                try { mod.proc.kill('SIGKILL'); } catch {}
            }
        }
    }
}
module.exports = { ProcessManager };
