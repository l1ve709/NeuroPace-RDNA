module.exports = {
    port: Number(process.env.NEUROPACE_PORT ?? 3200),
    host: '127.0.0.1',
    pipes: {
        telemetry: {
            path: process.env.NEUROPACE_TELEMETRY_PIPE || String.raw`\\.\pipe\neuropace-telemetry`,
            label: 'Telemetry',
            reconnectMs: 2000,
        },
        prediction: {
            path: process.env.NEUROPACE_PREDICTION_PIPE || String.raw`\\.\pipe\neuropace-prediction`,
            label: 'AI Prediction',
            reconnectMs: 2000,
        },
    },
    ws: {
        statusIntervalMs: 1000,   
        maxClients: 10,
    },
};
