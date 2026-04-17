const { createApp, ref, reactive, computed, onMounted, onUnmounted, nextTick, watch } = Vue;
const app = createApp({
    setup() {
        const page = ref('overview');
        let ws = null;
        let reconnectTimer = null;
        const wsConnected = ref(false);
        const currentTime = ref('--:--:--');
        const modules = reactive({});
        const gpu = reactive({
            gpu_clock_mhz: 0, gpu_temp_c: 0, hotspot_temp_c: 0,
            gpu_tgp_w: 0, vram_used_mb: 0, vram_total_mb: 24576,
        });
        const dpc = reactive({
            dpc_latency_us: 0, dpc_avg_us: 0, dpc_max_us: 0,
            isr_latency_us: 0, isr_avg_us: 0, isr_max_us: 0,
        });
        const prediction = reactive({
            probability: 0, action: 'NO_ACTION', confidence: 0,
            factors: [], inferenceMs: 0,
        });
        const frameTime = ref(0);
        const fps = ref(0);
        const actionLog = ref([]);
        const pipeStatus = reactive({});
        const allLogs = ref([]);
        const logTab = ref('all');
        const logFilter = ref('all');
        const logAutoScroll = ref(true);
        const logViewer = ref(null);
        const MAX_LOGS = 1000;
        const filteredLogs = computed(() => {
            let logs = allLogs.value;
            if (logTab.value !== 'all') {
                logs = logs.filter(l => l.module === logTab.value);
            }
            if (logFilter.value !== 'all') {
                logs = logs.filter(l => l.level === logFilter.value);
            }
            return logs.slice(-300);
        });
        let charts = {};
        let renderLoopId = null;
        const allRunning = computed(() =>
            Object.values(modules).every(m => m.status === 'running' || m.optional)
        );
        const noneRunning = computed(() =>
            Object.values(modules).every(m => m.status !== 'running')
        );
        const gaugeColor = computed(() => {
            const p = prediction.probability;
            if (p > 0.7) return '#ff3b5c';
            if (p > 0.4) return '#ffb800';
            return '#00e676';
        });
        const gaugeDash = computed(() => {
            return `${prediction.probability * 236} 314`;
        });
        function connectWS() {
            const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
            ws = new WebSocket(`${proto}//${location.host}`);
            ws.onopen = () => { wsConnected.value = true; };
            ws.onclose = () => {
                wsConnected.value = false;
                reconnectTimer = setTimeout(connectWS, 2000);
            };
            ws.onerror = () => { ws.close(); };
            ws.onmessage = (e) => {
                try { handleMessage(JSON.parse(e.data)); } catch {}
            };
        }
        function handleMessage(msg) {
            switch (msg.type) {
                case 'init':
                    Object.assign(modules, msg.data.modules);
                    Object.assign(pipeStatus, msg.data.status);
                    break;
                case 'module_status':
                    Object.assign(modules, msg.data);
                    break;
                case 'module_log':
                    allLogs.value.push(msg.data);
                    if (allLogs.value.length > MAX_LOGS) allLogs.value.splice(0, 100);
                    if (logAutoScroll.value) {
                        nextTick(() => {
                            const el = logViewer.value;
                            if (el) el.scrollTop = el.scrollHeight;
                        });
                    }
                    break;
                case 'telemetry':
                    if (msg.data.gpu) Object.assign(gpu, msg.data.gpu);
                    if (msg.data.dpc_isr) Object.assign(dpc, msg.data.dpc_isr);
                    if (msg.data.frame_time_ms != null) {
                        frameTime.value = msg.data.frame_time_ms;
                        fps.value = msg.data.frame_time_ms > 0 ? 1000 / msg.data.frame_time_ms : 0;
                    }
                    if (charts.overview) charts.overview.push(msg.data.frame_time_ms || 0);
                    if (charts.frameTime) charts.frameTime.push(msg.data.frame_time_ms || 0);
                    if (charts.dpc && msg.data.dpc_isr) charts.dpc.push(msg.data.dpc_isr.dpc_latency_us || 0);
                    break;
                case 'prediction':
                    if (msg.data.prediction) {
                        prediction.probability = msg.data.prediction.frame_drop_probability || 0;
                        prediction.factors = msg.data.prediction.contributing_factors || [];
                    }
                    prediction.action = msg.data.action || 'NO_ACTION';
                    prediction.confidence = msg.data.confidence || 0;
                    prediction.inferenceMs = msg.data.inference_time_ms || 0;
                    if (msg.data.action && msg.data.action !== 'NO_ACTION') {
                        actionLog.value.unshift({
                            time: new Date().toLocaleTimeString('en-US', { hour12: false }),
                            action: msg.data.action,
                            probability: msg.data.prediction?.frame_drop_probability || 0,
                        });
                        if (actionLog.value.length > 50) actionLog.value.length = 50;
                    }
                    break;
                case 'status':
                    Object.assign(pipeStatus, msg.data);
                    break;
            }
        }
        async function startModule(id) {
            await fetch(`/api/modules/${id}/start`, { method: 'POST' });
        }
        async function stopModule(id) {
            await fetch(`/api/modules/${id}/stop`, { method: 'POST' });
        }
        async function restartModule(id) {
            await fetch(`/api/modules/${id}/stop`, { method: 'POST' });
            setTimeout(() => fetch(`/api/modules/${id}/start`, { method: 'POST' }), 1500);
        }
        async function startAll() {
            await fetch('/api/modules/start-all', { method: 'POST' });
        }
        async function stopAll() {
            await fetch('/api/modules/stop-all', { method: 'POST' });
        }
        function toggleModule(id) {
            if (modules[id]?.status === 'running') stopModule(id);
            else startModule(id);
        }
        function formatUptime(ms) {
            if (!ms) return '--';
            const s = Math.floor(ms / 1000);
            const m = Math.floor(s / 60);
            const h = Math.floor(m / 60);
            if (h > 0) return `${h}h ${m % 60}m`;
            if (m > 0) return `${m}m ${s % 60}s`;
            return `${s}s`;
        }
        function getMetricStatus(type) {
            switch (type) {
                case 'clock': return gpu.gpu_clock_mhz < 1800 ? 'status-danger' : gpu.gpu_clock_mhz < 2200 ? 'status-warn' : 'status-good';
                case 'temp': return gpu.gpu_temp_c > 90 ? 'status-danger' : gpu.gpu_temp_c > 80 ? 'status-warn' : 'status-good';
                case 'tgp': return gpu.gpu_tgp_w > 340 ? 'status-danger' : gpu.gpu_tgp_w > 300 ? 'status-warn' : 'status-good';
                default: return '';
            }
        }
        function clearLogs() {
            allLogs.value = [];
        }
        function renderCharts() {
            for (const c of Object.values(charts)) {
                if (c) c.render();
            }
            renderLoopId = requestAnimationFrame(renderCharts);
        }
        let clockTimer = null;
        onMounted(async () => {
            try {
                const res = await fetch('/api/modules');
                const data = await res.json();
                Object.assign(modules, data);
            } catch {}
            await nextTick();
            const overviewCanvas = document.getElementById('overviewChart');
            if (overviewCanvas) {
                charts.overview = new LineChart(overviewCanvas, {
                    maxPoints: 300, lineColor: '#00aeff',
                    fillGradientStart: 'rgba(0,174,255,.15)', fillGradientEnd: 'rgba(0,174,255,.01)',
                    thresholds: [
                        { value: 16.67, color: '#ffb800', label: '60fps', dashed: true },
                        { value: 6.94, color: '#00ffcc', label: '144fps', dashed: true },
                    ],
                });
            }
            const ftCanvas = document.getElementById('frameTimeChart');
            if (ftCanvas) {
                charts.frameTime = new LineChart(ftCanvas, {
                    maxPoints: 400, lineColor: '#00aeff',
                    fillGradientStart: 'rgba(0,174,255,.18)', fillGradientEnd: 'rgba(0,174,255,.01)',
                    thresholds: [
                        { value: 16.67, color: '#ffb800', label: '60fps', dashed: true },
                        { value: 11.11, color: '#00e676', label: '90fps', dashed: true },
                        { value: 6.94, color: '#00ffcc', label: '144fps', dashed: true },
                    ],
                });
            }
            const dpcCanvas = document.getElementById('dpcChart');
            if (dpcCanvas) {
                charts.dpc = new LineChart(dpcCanvas, {
                    maxPoints: 200, lineColor: '#a855f7',
                    fillGradientStart: 'rgba(168,85,247,.12)', fillGradientEnd: 'rgba(168,85,247,.01)',
                    thresholds: [{ value: 500, color: '#ff3b5c', label: 'Spike', dashed: true }],
                });
            }
            renderCharts();
            clockTimer = setInterval(() => {
                currentTime.value = new Date().toLocaleTimeString('en-US', { hour12: false });
            }, 1000);
            currentTime.value = new Date().toLocaleTimeString('en-US', { hour12: false });
            connectWS();
        });
        onUnmounted(() => {
            if (renderLoopId) cancelAnimationFrame(renderLoopId);
            if (clockTimer) clearInterval(clockTimer);
            if (reconnectTimer) clearTimeout(reconnectTimer);
            if (ws) ws.close();
            for (const c of Object.values(charts)) { if (c) c.destroy(); }
        });
        return {
            page, wsConnected, currentTime, modules,
            gpu, dpc, prediction, frameTime, fps, actionLog, pipeStatus,
            allRunning, noneRunning, gaugeColor, gaugeDash,
            allLogs, logTab, logFilter, logAutoScroll, logViewer, filteredLogs,
            startModule, stopModule, restartModule, startAll, stopAll, toggleModule,
            formatUptime, getMetricStatus, clearLogs,
        };
    },
});
app.mount('#app');
