const net = require('net');
const { EventEmitter } = require('events');
class PipeBridge extends EventEmitter {
    constructor(pipeConfig) {
        super();
        this.pipePath = pipeConfig.path;
        this.label = pipeConfig.label;
        this.reconnectMs = pipeConfig.reconnectMs || 2000;
        this._client = null;
        this._connected = false;
        this._running = false;
        this._buffer = '';
        this._reconnectTimer = null;
        this.stats = {
            messagesReceived: 0,
            bytesReceived: 0,
            reconnectCount: 0,
            lastMessageTime: null,
        };
    }
    get connected() {
        return this._connected;
    }
    start() {
        this._running = true;
        this._connect();
        console.log(`[PIPE:${this.label}] Bridge started -> ${this.pipePath}`);
    }
    stop() {
        this._running = false;
        if (this._reconnectTimer) {
            clearTimeout(this._reconnectTimer);
            this._reconnectTimer = null;
        }
        this._disconnect();
        console.log(`[PIPE:${this.label}] Bridge stopped`);
    }
    _connect() {
        if (!this._running) return;
        try {
            this._client = net.connect({ path: this.pipePath }, () => {
                this._connected = true;
                this._buffer = '';
                console.log(`[PIPE:${this.label}] Connected`);
                this.emit('connected');
            });
            this._client.on('data', (chunk) => {
                this._onData(chunk);
            });
            this._client.on('error', (err) => {
                if (err.code === 'ENOENT') {
                } else if (err.code === 'ECONNREFUSED') {
                } else {
                    console.error(`[PIPE:${this.label}] Error: ${err.message}`);
                }
            });
            this._client.on('close', () => {
                if (this._connected) {
                    console.log(`[PIPE:${this.label}] Disconnected`);
                }
                this._connected = false;
                this.emit('disconnected');
                this._scheduleReconnect();
            });
            this._client.on('end', () => {
                this._connected = false;
                this._scheduleReconnect();
            });
        } catch (err) {
            this._scheduleReconnect();
        }
    }
    _disconnect() {
        if (this._client) {
            this._client.destroy();
            this._client = null;
        }
        this._connected = false;
        this._buffer = '';
    }
    _scheduleReconnect() {
        if (!this._running) return;
        if (this._reconnectTimer) return;
        this.stats.reconnectCount++;
        this._reconnectTimer = setTimeout(() => {
            this._reconnectTimer = null;
            this._disconnect();
            this._connect();
        }, this.reconnectMs);
    }
    _onData(chunk) {
        const str = chunk.toString('utf-8');
        this.stats.bytesReceived += chunk.length;
        this._buffer += str;
        let nlIndex;
        while ((nlIndex = this._buffer.indexOf('\n')) !== -1) {
            const line = this._buffer.substring(0, nlIndex).trim();
            this._buffer = this._buffer.substring(nlIndex + 1);
            if (line.length === 0) continue;
            try {
                const message = JSON.parse(line);
                this.stats.messagesReceived++;
                this.stats.lastMessageTime = Date.now();
                this.emit('message', message);
            } catch (e) {
            }
        }
        if (this._buffer.length > 500000) {
            this._buffer = '';
        }
    }
}
module.exports = { PipeBridge };
