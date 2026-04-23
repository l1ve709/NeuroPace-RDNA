const path = require('path');
const http = require('http');
const express = require('express');
const { WebSocketServer } = require('ws');
const { PipeBridge } = require('./pipe-bridge');
const { ProcessManager } = require('./process-manager');
const config = require('./config');

const app = express();
app.use(express.json());

const publicPath = path.resolve(__dirname, '..', 'public');
console.log(`[HTTP] Serving static files from: ${publicPath}`);

// Debug middleware
app.use((req, res, next) => {
    console.log(`[HTTP] ${req.method} ${req.url}`);
    next();
});

// MIME Type fix for Windows
app.use((req, res, next) => {
    if (req.url.endsWith('.js')) {
        res.setHeader('Content-Type', 'application/javascript');
    }
    next();
});

app.use(express.static(publicPath));

// Client-side logging endpoint
app.post('/api/debug-log', (req, res) => {
    const { level, message, details } = req.body;
    console.log(`[CLIENT-${level ? level.toUpperCase() : 'INFO'}] ${message}`, details || '');
    res.sendStatus(200);
});

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
        console.log(`[WS] Client disconnected (total: ${wsClientCount})`);
    });
});

function broadcast(type, data) {
    const payload = JSON.stringify({ type, data });
    for (const client of wss.clients) {
        if (client.readyState === 1) { // OPEN
            try { client.send(payload); } catch (e) {}
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
}, config.ws.statusIntervalMs || 5000);

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

server.listen(config.port || 3200, () => {
    console.log(`[HTTP] Control Center: http://localhost:${config.port || 3200}`);
    console.log(`[WS]   WebSocket:      ws://localhost:${config.port || 3200}`);
    console.log(`[API]  REST API:       http://localhost:${config.port || 3200}/api/modules`);
    console.log('');
    console.log('Press Ctrl+C to stop');
    console.log('');
}).on('error', (err) => {
    if (err.code === 'EADDRINUSE') {
        console.error(`[FATAL] Port ${config.port || 3200} is already in use. Please close other dashboard instances.`);
        process.exit(1);
    } else {
        console.error(`[FATAL] Server error:`, err);
    }
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
