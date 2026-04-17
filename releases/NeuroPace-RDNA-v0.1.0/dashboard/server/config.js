module.exports = {
    port: 3200,
    pipes: {
        telemetry: {
            path: String.raw`\\.\pipe\neuropace-telemetry`,
            label: 'Telemetry',
            reconnectMs: 2000,
        },
        prediction: {
            path: String.raw`\\.\pipe\neuropace-prediction`,
            label: 'AI Prediction',
            reconnectMs: 2000,
        },
    },
    ws: {
        statusIntervalMs: 1000,   
        maxClients: 10,
    },
};
