const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { randomUUID } = require('node:crypto');
const { once } = require('node:events');
const path = require('node:path');
const { test } = require('node:test');
const { setTimeout: delay } = require('node:timers/promises');
const { WebSocket } = require('ws');

const root = path.resolve(__dirname, '../..');
const python = process.env.NEUROPACE_PYTHON || 'python';

test('mock telemetry, ONNX inference, dashboard and producer reconnect', {
    skip: process.platform !== 'win32',
    timeout: 40000,
}, async (t) => {
    const suffix = randomUUID();
    const pipe = name => '\\\\.\\pipe\\neuropace-test-' + suffix + '-' + name;
    const env = {
        ...process.env,
        NEUROPACE_PORT: '0',
        NEUROPACE_TELEMETRY_PIPE: pipe('telemetry'),
        NEUROPACE_PREDICTION_PIPE: pipe('prediction'),
        NEUROPACE_ACTION_PIPE: pipe('action'),
        PYTHONUNBUFFERED: '1',
    };
    const children = [];
    let socket;
    function start(command, script) {
        const child = spawn(command, [script], { cwd: root, env, windowsHide: true });
        const state = { child, output: '', stopped: false };
        children.push(state);
        child.once('error', error => { state.error = error; });
        for (const stream of [child.stdout, child.stderr]) {
            stream.on('data', chunk => { state.output = (state.output + chunk).slice(-8000); });
        }
        return state;
    }
    async function stop(state) {
        state.stopped = true;
        if (state.child.exitCode !== null || state.child.signalCode || !state.child.pid) return;
        const closed = once(state.child, 'close');
        state.child.kill();
        await closed;
    }
    t.after(async () => {
        socket?.terminate();
        await Promise.all(children.map(stop));
    });
    async function until(check, label) {
        for (let attempt = 0; attempt < 150; attempt++) {
            for (const state of children) {
                if (!state.stopped && (state.error || state.child.exitCode !== null)) {
                    assert.fail(String(state.error || state.output));
                }
            }
            const result = check();
            if (result) return result;
            await delay(100);
        }
        assert.fail(label + '\n' + children.map(state => state.output).join('\n'));
    }

    let mock = start(python, 'scripts/mock-telemetry.py');
    start(python, 'ai-engine/src/main.py');
    const dashboard = start(process.execPath, 'dashboard/server/index.js');
    const [, port] = await until(
        () => dashboard.output.match(/Control Center: http:\/\/localhost:(\d+)/),
        'Dashboard did not listen',
    );
    const origin = 'http://127.0.0.1:' + port;
    const request = await fetch(origin + '/api/modules');
    assert.equal(request.status, 200);
    assert.ok((await request.json()).ai_engine);
    socket = new WebSocket('ws://127.0.0.1:' + port, { origin });
    const received = new Map();
    socket.on('message', payload => {
        const message = JSON.parse(payload);
        received.set(message.type, message.data);
    });
    socket.on('error', error => t.diagnostic(error.message));
    const frame = await until(() => received.get('telemetry'), 'No telemetry');
    assert.ok(frame.gpu.gpu_clock_mhz > 0);
    const prediction = await until(() => received.get('prediction'), 'No ONNX inference');
    const probability = prediction.prediction.frame_drop_probability;
    assert.ok(Number.isFinite(probability) && probability >= 0 && probability <= 1);

    await stop(mock);
    await until(() => received.get('status')?.telemetry_connected === false, 'Producer disconnect not detected');
    const beforeRestart = received.get('prediction').timestamp_us;
    received.delete('telemetry');
    received.delete('prediction');
    start(python, 'scripts/mock-telemetry.py');
    await until(() => received.get('telemetry'), 'Telemetry did not recover');
    await until(
        () => received.get('prediction')?.timestamp_us > beforeRestart,
        'Inference did not recover',
    );
});
