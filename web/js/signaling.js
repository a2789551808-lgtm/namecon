// signaling.js — WebSocket 信令管理
class SignalingClient {
    constructor() {
        this.ws = null;
        this.callbacks = {};
    }

    connect() {
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const url = `${proto}//${location.host}/ws`;
        this.ws = new WebSocket(url);

        this.ws.onmessage = (event) => {
            const msg = JSON.parse(event.data);
            const cb = this.callbacks[msg.type];
            if (cb) cb(msg);
        };

        this.ws.onclose = () => console.log('[WS] Disconnected');
    }

    on(type, callback) {
        this.callbacks[type] = callback;
    }

    send(type, payload = {}) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({ type, payload }));
        }
    }

    close() {
        if (this.ws) this.ws.close();
    }
}

const signaling = new SignalingClient();
