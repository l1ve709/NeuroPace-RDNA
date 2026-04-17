const DiscordRPC = require('discord-rpc');
const clientId = '1230188684617674752'; 
DiscordRPC.register(clientId);
const rpc = new DiscordRPC.Client({ transport: 'ipc' });
const startTimestamp = new Date();
async function setActivity() {
    if (!rpc || !rpc.user) {
        return;
    }
    try {
        await rpc.setActivity({
            details: 'Predictive Latency Engine Active',
            state: 'Eliminating micro-stutter on RDNA3 GPUs',
            startTimestamp,
            largeImageKey: 'icon', 
            largeImageText: 'NeuroPace RDNA Control Center',
            smallImageKey: 'ai', 
            smallImageText: 'AI Frame Optimization',
            instance: false,
            buttons: [
                {
                    label: 'View GitHub Repo',
                    url: 'https://github.com/l1ve709/NeuroPace-RDNA'
                }
            ]
        });
    } catch (err) {
        console.error('[RPC] Failed to set Activity:', err.message);
    }
}
function initRPC() {
    rpc.on('ready', () => {
        console.log(`[RPC] Presenting Discord Rich Presence for ${rpc.user.username}`);
        setActivity();
        setInterval(() => {
            setActivity();
        }, 15e3);
    });
    rpc.login({ clientId }).catch(err => {
        console.error('[RPC] Could not connect to Discord RPC:', err.message);
    });
}
module.exports = { initRPC };
