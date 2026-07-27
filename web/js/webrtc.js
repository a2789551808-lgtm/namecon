// webrtc.js - RTCPeerConnection 管理（Consumer 模型：peerId 化 transceiver + inactive 池复用）
const WebRTC = {
    pc: null,
    localStream: null,
    audioContext: null,
    gainNode: null,
    mediaRecorder: null,
    recChunks: [],
    _rawVideoTrack: null,

    // === Consumer 模型状态 ===
    _pendingTransceivers: [],   // addTransceiver 后还没拿 mid 的: {transceiver, peerId, kind}
    _inactivePool: [],          // 离开者留下的 inactive transceiver: {transceiver, kind}
    _midPeerMap: {},            // mid → peerId（已绑定）
    _renegotiateQueue: [],      // 串行化队列
    _renegotiating: false,
    _answerResolver: null,      // 等待 answer 的 Promise resolve

    async init(sfuInfo) {
        console.log('[WebRTC] Init SFU=' + sfuInfo.sfu_ip + ':' + sfuInfo.sfu_port);
        this.pc = new RTCPeerConnection({
            iceServers: [{ urls: "stun:stun.l.google.com:19302" }]
        });

        try {
            const rawStream = await navigator.mediaDevices.getUserMedia({ video: true, audio: true });
            // 通过 GainNode 处理音频（用于音量调节）
            this.audioContext = new AudioContext();
            const source = this.audioContext.createMediaStreamSource(rawStream);
            this.gainNode = this.audioContext.createGain();
            this.gainNode.gain.value = 1.0;
            const dest = this.audioContext.createMediaStreamDestination();
            source.connect(this.gainNode);
            this.gainNode.connect(dest);

            // 组合：处理后的音频 + 原始视频
            this.localStream = new MediaStream();
            this.localStream.addTrack(dest.stream.getAudioTracks()[0]);
            this.localStream.addTrack(rawStream.getVideoTracks()[0]);
            this._rawVideoTrack = rawStream.getVideoTracks()[0];

            // 显示本地画面
            const localVideo = document.getElementById('localVideo');
            if (localVideo) localVideo.srcObject = this.localStream;

            // 加入 PeerConnection
            this.localStream.getTracks().forEach(t => this.pc.addTrack(t, this.localStream));
        } catch (e) {
            console.warn('[WebRTC] getUserMedia failed:', e.message);
        }

        // 远端 track：ontrack 时把 stream 绑到对应 video 元素
        this.pc.ontrack = (event) => {
            console.log('[WebRTC] Track: ' + event.track.kind + ' mid=' + event.transceiver.mid);
            const stream = event.streams[0] || new MediaStream([event.track]);
            // 找到第一个空 video 槽位（room.html 创建的 .remote-video）
            const remoteTiles = document.querySelectorAll('.video-tile:not(#tile-local) video');
            for (const tile of remoteTiles) {
                if (!tile.srcObject) {
                    tile.srcObject = stream;
                    tile.play().catch(e => console.warn('play:', e));
                    console.log('[WebRTC] Bound stream to video element (mid=' + event.transceiver.mid + ')');
                    break;
                }
            }
        };

        this.pc.oniceconnectionstatechange = () => {
            console.log('[WebRTC] ICE: ' + this.pc.iceConnectionState);
        };

        // 首次 createOffer（仅本地 sendrecv 音视频）
        const offer = await this.pc.createOffer();
        await this.pc.setLocalDescription(offer);
        if (offer.sdp && offer.sdp.includes('m=audio') && offer.sdp.includes('m=video')) {
            console.log('[WebRTC] Sending initial offer (' + offer.sdp.length + ' bytes)');
            this._sendOffer({ sdp: offer.sdp, recv_mids: [] });
        } else {
            console.warn('[WebRTC] Offer empty - no media tracks?');
        }
    },

    // === 添加接收 transceiver（peerId 化，优先复用 inactive 池） ===
    // peerId: 新加入者的 peerId；kind: 'video' | 'audio' | 'both'
    addRecvTransceiver(peerId, kind) {
        if (!this.pc) return;
        const kindList = kind === 'both' ? ['video', 'audio'] : [kind];
        for (const k of kindList) {
            let tc;
            // 优先从 inactive 池复用（避免 m= line 无限增长）
            const idx = this._inactivePool.findIndex(p => p.kind === k);
            if (idx >= 0) {
                tc = this._inactivePool[idx].transceiver;
                this._inactivePool.splice(idx, 1);
                tc.direction = 'recvonly';  // 复活
                console.log('[WebRTC] Reused inactive transceiver for ' + peerId + ' (' + k + ')');
            } else {
                tc = this.pc.addTransceiver(k, { direction: 'recvonly' });
            }
            this._pendingTransceivers.push({ transceiver: tc, peerId, kind: k });
        }
    },

    // === 串行重协商：队列避免并发冲突（B、C 同时加入时） ===
    async renegotiate() {
        this._renegotiateQueue.push(true);
        if (this._renegotiating) return;
        await this._runRenegotiateQueue();
    },

    async _runRenegotiateQueue() {
        while (this._renegotiateQueue.length > 0) {
            this._renegotiateQueue.shift();
            if (this._pendingTransceivers.length === 0) continue;  // 无待绑 transceiver
            this._renegotiating = true;
            try {
                const offer = await this.pc.createOffer();
                await this.pc.setLocalDescription(offer);

                // setLocalDescription 后 mid 才确定，组装 recv_mids 并绑 _midPeerMap
                const recvMids = this._pendingTransceivers.map(p => {
                    this._midPeerMap[p.transceiver.mid] = p.peerId;
                    return {
                        mid: p.transceiver.mid,
                        publisher_peer_id: p.peerId,
                        kind: p.kind,
                        is_video: p.kind === 'video'
                    };
                });
                this._pendingTransceivers = [];

                console.log('[WebRTC] Renegotiate offer (' + offer.sdp.length + ' bytes, ' + recvMids.length + ' recv_mids)');
                await this._sendOffer({ sdp: offer.sdp, recv_mids: recvMids });
            } finally {
                this._renegotiating = false;
            }
        }
    },

    // === 发 offer 等 answer（Promise 化，串行保证） ===
    _sendOffer(payload) {
        return new Promise((resolve) => {
            this._answerResolver = resolve;
            signaling.send('offer', payload);
        });
    },

    async setAnswer(sdp) {
        if (!this.pc) return;
        try {
            await this.pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp }));
            console.log('[WebRTC] Remote description set');
        } catch (e) {
            console.error('[WebRTC] setRemoteDescription failed:', e);
        }
        // resolve 等待中的 _sendOffer Promise，让 renegotiate 队列继续
        if (this._answerResolver) {
            const r = this._answerResolver;
            this._answerResolver = null;
            r();
        }
    },

    // === 参与者离开：把对应 transceiver 设 inactive 复用（避免频繁重协商） ===
    markTransceiverInactive(peerId) {
        if (!this.pc) return;
        const transceivers = this.pc.getTransceivers();
        for (const tc of transceivers) {
            if (this._midPeerMap[tc.mid] === peerId) {
                tc.direction = 'inactive';
                this._inactivePool.push({
                    transceiver: tc,
                    kind: tc.receiver.track ? tc.receiver.track.kind : 'video'
                });
                delete this._midPeerMap[tc.mid];
                console.log('[WebRTC] Marked transceiver mid=' + tc.mid + ' inactive for ' + peerId);
            }
        }
    },

    toggleMic() {
        const track = this.localStream.getAudioTracks()[0];
        if (track) { track.enabled = !track.enabled; return track.enabled; }
        return false;
    },
    toggleCamera() {
        const track = this.localStream.getVideoTracks()[0];
        if (track) { track.enabled = !track.enabled; return track.enabled; }
        return false;
    },
    setMicVolume(value) {
        if (this.gainNode) this.gainNode.gain.value = value / 100;
    },
    startRecording() {
        if (!this.localStream) return false;
        const mimeType = 'video/webm;codecs=vp8,opus';
        if (!MediaRecorder.isTypeSupported(mimeType)) {
            console.warn('MediaRecorder not supported');
            return false;
        }
        this.recChunks = [];
        this.mediaRecorder = new MediaRecorder(this.localStream, { mimeType });
        this.mediaRecorder.ondataavailable = (e) => { if (e.data.size > 0) this.recChunks.push(e.data); };
        this.mediaRecorder.start();
        return true;
    },
    stopRecording() {
        if (!this.mediaRecorder) return;
        this.mediaRecorder.stop();
        this.mediaRecorder.onstop = () => {
            const blob = new Blob(this.recChunks, { type: 'video/webm' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            const ts = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
            a.href = url; a.download = `namecon_${ts}.webm`; a.click();
            URL.revokeObjectURL(url);
        };
    },

    cleanup() {
        if (this.pc) { this.pc.close(); this.pc = null; }
        if (this.localStream) this.localStream.getTracks().forEach(t => t.stop());
        if (this.audioContext) this.audioContext.close();
        this._pendingTransceivers = [];
        this._inactivePool = [];
        this._midPeerMap = {};
        this._renegotiateQueue = [];
        this._renegotiating = false;
    }
};

// 监听 Answer 消息
signaling.on('answer', async (msg) => {
    console.log('[WebRTC] Got answer (' + msg.sdp.length + ' bytes)');
    await WebRTC.setAnswer(msg.sdp);
});
