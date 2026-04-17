const path = require('path');
const http = require('http');
const express = require('express');
const { WebSocketServer } = require('ws');
const { PipeBridge } = require('./pipe-bridge');
const { ProcessManager } = require('./process-manager');
const { initRPC } = require('./rpc');
const config = require('./config');
const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, '..', 'public')));
const server = http.createServer(app);
const pm = new ProcessManager();
app.get('/api/modules', (req, res) => {
    res.json(pm.getAllStatus());
});
app.post('/api/modules/:id/start', (req, res) => {
    const result = pm.start(req.params.id);
    res.json(result);
});
app.post('/api/modules/:id/stop', (req, res) => {
    const result = pm.stop(req.params.id);
    res.json(result);
});
app.post('/api/modules/start-all', async (req, res) => {
    const result = await pm.startAll();
    res.json(result);
});
app.post('/api/modules/stop-all', (req, res) => {
    const result = pm.stopAll();
    res.json(result);
});
app.get('/api/modules/:id/logs', (req, res) => {
    const limit = parseInt(req.query.limit) || 200;
    res.json(pm.getLogs(req.params.id, limit));
});
const wss = new WebSocketServer({ server, maxPayload: 1024 * 64 });
let wsClientCount = 0;
wss.on('connection', (ws) => {
    wsClientCount++;
    console.log(`[WS] Client connected (total: ${wsClientCount})`);
    ws.send(JSON.stringify({
        type: 'init',
        data: {
            modules: pm.getAllStatus(),
            status: getStatus(),
        },
    }));
    ws.on('close', () => {
        wsClientCount--;
    });
});
function broadcast(type, data) {
    const payload = JSON.stringify({ type, data });
    for (const client of wss.clients) {
        if (client.readyState === 1) {
            try { client.send(payload); } catch {}
        }
    }
}
pm.on('status', (data) => {
    broadcast('module_status', data.modules);
});
pm.on('log', (entry) => {
    broadcast('module_log', entry);
});
const telemetryBridge = new PipeBridge(config.pipes.telemetry);
const predictionBridge = new PipeBridge(config.pipes.prediction);
let telemetryFrameCounter = 0;
const DOWNSAMPLE = 3;
telemetryBridge.on('message', (data) => {
    telemetryFrameCounter++;
    if (telemetryFrameCounter % DOWNSAMPLE === 0) {
        broadcast('telemetry', data);
    }
});
predictionBridge.on('message', (data) => {
    broadcast('prediction', data);
});
function getStatus() {
    return {
        telemetry_connected: telemetryBridge.connected,
        prediction_connected: predictionBridge.connected,
        telemetry_stats: telemetryBridge.stats,
        prediction_stats: predictionBridge.stats,
        ws_clients: wsClientCount,
        uptime_s: Math.floor(process.uptime()),
    };
}
setInterval(() => {
    broadcast('status', getStatus());
}, config.ws.statusIntervalMs);
console.log('');
console.log('+----------------------------------------------------------+');
console.log('|                                                          |');
console.log('|   NeuroPace RDNA -- Control Center v0.2.0               |');
console.log('|   Full System GUI + Module Management                   |');
console.log('|                                                          |');
console.log('+----------------------------------------------------------+');
console.log('');
telemetryBridge.start();
predictionBridge.start();
initRPC();

server.listen(config.port, () => {
    console.log(`[HTTP] Control Center: http://localhost:${config.port}`);
    console.log(`[WS]   WebSocket:      ws://localhost:${config.port}`);
    console.log(`[API]  REST API:       http://localhost:${config.port}/api/modules`);
    console.log('');
    console.log('Press Ctrl+C to stop');
    console.log('');
});
process.on('SIGINT', () => {
    console.log('\n[MAIN] Shutting down...');
    pm.shutdown();
    telemetryBridge.stop();
    predictionBridge.stop();
    wss.close();
    server.close(() => {
        console.log('[MAIN] Control center stopped');
        process.exit(0);
    });
});
process.on('exit', () => {
    pm.shutdown();
});
