// api.js — REST API 调用
const api = {
    async createRoom(name) {
        const res = await fetch('/api/rooms', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ room_name: name })
        });
        return res.json();
    },

    async joinRoom(roomId, username) {
        const res = await fetch(`/api/rooms/${roomId}/join`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username })
        });
        return res.json();
    },

    async health() {
        const res = await fetch('/api/health');
        return res.json();
    }
};
