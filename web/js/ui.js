// ui.js - 视频网格管理
const UI = {
    grid: null,
    tiles: new Map(),

    init() {
        this.grid = document.getElementById('videoGrid');
    },

    addLocalVideo() {
        if (!this.grid) this.init();
        if (this.tiles.has('local')) return;

        const tile = this.createTile('local', '我');
        const video = tile.querySelector('video');
        video.id = 'localVideo';
        video.autoplay = true;
        video.muted = true;
        video.playsinline = true;
        this.updateLayout();
    },

    addRemoteVideo(peerId, username) {
        if (!this.grid) this.init();
        if (this.tiles.has(peerId)) return;

        const tile = this.createTile(peerId, username || '远端');
        const video = tile.querySelector('video');
        video.id = 'remoteVideo';
        video.autoplay = true;
        video.playsinline = true;
        this.updateLayout();
    },

    removeVideo(peerId) {
        const tile = this.tiles.get(peerId);
        if (tile) {
            tile.container.remove();
            this.tiles.delete(peerId);
            this.updateLayout();
        }
    },

    createTile(id, label) {
        const container = document.createElement('div');
        container.className = 'video-tile';
        container.id = `tile-${id}`;

        const video = document.createElement('video');
        video.autoplay = true;
        video.playsinline = true;

        const labelEl = document.createElement('div');
        labelEl.className = 'label';
        labelEl.textContent = label;

        container.appendChild(video);
        container.appendChild(labelEl);
        this.grid.appendChild(container);

        this.tiles.set(id, { container, video, label: labelEl });
        return container;
    },

    updateLayout() {
        const count = this.tiles.size;
        this.grid.dataset.count = count;
    },

    updateLabel(peerId, username) {
        const tile = this.tiles.get(peerId);
        if (tile) tile.label.textContent = username;
    }
};
