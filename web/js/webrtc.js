// webrtc.js - RTCPeerConnection 管理（音频默认推流，视频按需开关）
const WebRTC = {
    pc: null,
    localStream: null,
    audioContext: null,
    gainNode: null,
    mediaRecorder: null,
    recChunks: [],

    // === 摄像头状态 ===
    _videoTrack: null,          // 当前视频 track（null=摄像头关闭）
    _videoSender: null,         // pc.addTrack 返回的 sender（removeTrack 用）
    _cameraEnabled: false,

    // === Consumer 模型状态 ===
    _pendingTransceivers: [],   // addTransceiver 后还没拿 mid 的: {transceiver, peerId, kind}
    _inactivePool: [],          // 离开者留下的 inactive transceiver: {transceiver, kind}
    _midPeerMap: {},            // mid -> peerId（已绑定）
    _renegotiateQueue: [],      // 串行化队列
    _renegotiating: false,
    _needsRenegotiate: false,   // addTrack/removeTrack 触发的重协商
    _answerResolver: null,      // 等待 answer 的 Promise resolve

    async init(sfuInfo) {
        console.log('[WebRTC] Init SFU=' + sfuInfo.sfu_ip + ':' + sfuInfo.sfu_port);
        this.pc = new RTCPeerConnection({
            iceServers: [{ urls: "stun:stun.l.google.com:19302" }]
        });

        try {
            // 只获取音频（摄像头默认关闭，点开摄像头按钮才开启）
            const rawStream = await navigator.mediaDevices.getUserMedia({ video: false, audio: true });
            // 通过 GainNode 处理音频（用于音量调节）
            this.audioContext = new AudioContext();
            const source = this.audioContext.createMediaStreamSource(rawStream);
            this.gainNode = this.audioContext.createGain();
            this.gainNode.gain.value = 1.0;
            const dest = this.audioContext.createMediaStreamDestination();
            source.connect(this.gainNode);
            this.gainNode.connect(dest);

            this.localStream = new MediaStream();
            this.localStream.addTrack(dest.stream.getAudioTracks()[0]);

            // 显示本地画面（此时只有音频，video 黑屏）
            const localVideo = document.getElementById('localVideo');
            if (localVideo) localVideo.srcObject = this.localStream;

            // 只把音频加入 PeerConnection（视频按需添加）
            this.pc.addTrack(dest.stream.getAudioTracks()[0], this.localStream);
        } catch (e) {
            console.warn('[WebRTC] getUserMedia failed:', e.message);
        }

        // 远端 track：ontrack 时把 track 绑到对应 peer 的 video 元素
        // 用 mid -> peerId 映射找到正确的 video 槽位，避免 renegotiate 后 track 丢失
        this.pc.ontrack = (event) => {
            console.log('[WebRTC] Track: ' + event.track.kind + ' mid=' + event.transceiver.mid
                       + ' dir=' + event.transceiver.direction);
            const mid = event.transceiver.mid;
            const peerId = this._midPeerMap[mid];

            // 只处理 recvonly transceiver 的 track。
            // sendrecv 是本地发送 transceiver（自己开摄像头），receiver 收的 track 是对方发来的，
            // 但开摄像头 renegotiate 后 mid 从 recvonly 变 sendrecv，浏览器重新触发 ontrack，
            // 此时绑到远端元素会覆盖原本正确的远端画面。所以 sendrecv 直接跳过。
            // （sendrecv 的接收由对应的 recvonly transceiver 已处理）
            if (event.transceiver.direction !== 'recvonly') {
                console.log('[WebRTC] Skip non-recvonly track (mid=' + mid + ' dir=' + event.transceiver.direction + ')');
                return;
            }

            // 找到对应的远端 video 元素
            const remoteTiles = document.querySelectorAll('.video-tile:not(#tile-local) video');
            // 优先按 peerId 匹配（tile-id 格式 tile-<peerId>），否则找空槽位
            let targetVideo = null;
            if (peerId) {
                const tile = document.getElementById('tile-' + peerId);
                if (tile) targetVideo = tile.querySelector('video');
            }
            if (!targetVideo) {
                for (const tile of remoteTiles) {
                    if (!tile.srcObject) { targetVideo = tile; break; }
                }
            }
            if (targetVideo) {
                // 把新 track 加入已有的 stream，或创建新 stream
                let stream = targetVideo.srcObject;
                if (!stream) {
                    stream = new MediaStream();
                    targetVideo.srcObject = stream;
                }
                // 移除同类型的旧 track，加入新 track
                const oldTracks = stream.getTracks().filter(t => t.kind === event.track.kind);
                oldTracks.forEach(t => stream.removeTrack(t));
                stream.addTrack(event.track);
                targetVideo.play().catch(e => console.warn('play:', e));
                console.log('[WebRTC] Bound ' + event.track.kind + ' track to video (mid=' + mid + ', peer=' + (peerId||'?') + ')');
            }
        };

        this.pc.oniceconnectionstatechange = () => {
            console.log('[WebRTC] ICE: ' + this.pc.iceConnectionState);
        };

        // 首次 createOffer（仅本地 sendrecv 音频，无视频）
        const offer = await this.pc.createOffer();
        await this.pc.setLocalDescription(offer);
        if (offer.sdp && offer.sdp.includes('m=audio')) {
            console.log('[WebRTC] Sending initial offer (' + offer.sdp.length + ' bytes)');
            await this._sendOffer({ sdp: offer.sdp, recv_mids: [] });
        } else {
            console.warn('[WebRTC] Offer empty - no media tracks?');
        }
    },

    // === 开启摄像头：getUserMedia(video) -> addTransceiver(sendrecv) -> renegotiate ===
    // 关键：用 addTransceiver 而非 addTrack。
    // addTrack 会复用已有的 recvonly transceiver（mid=1），把方向改成 sendrecv，
    // 破坏 mid<->Consumer 绑定，导致收不到对方视频。
    // addTransceiver 创建新的 sendrecv transceiver，保持 recvonly 不变。
    async enableCamera() {
        if (this._cameraEnabled) return true;
        if (!this.pc) return false;
        try {
            const stream = await navigator.mediaDevices.getUserMedia({ video: true, audio: false });
            this._videoTrack = stream.getVideoTracks()[0];
            // 用 addTransceiver 显式创建新的 sendrecv transceiver，不复用 recvonly
            this._videoSender = this.pc.addTransceiver(this._videoTrack, { direction: 'sendrecv' });
            this.localStream.addTrack(this._videoTrack);
            this._cameraEnabled = true;
            // 更新本地 video 元素显示摄像头画面
            const localVideo = document.getElementById('localVideo');
            if (localVideo) localVideo.srcObject = this.localStream;
            console.log('[WebRTC] Camera enabled, renegotiating');
            this._needsRenegotiate = true;
            await this.renegotiate();
            return true;
        } catch (e) {
            console.warn('[WebRTC] enableCamera failed:', e.message);
            return false;
        }
    },

    // === 关闭摄像头：transceiver.direction=inactive + track.stop()（停止捕获，灯灭）-> renegotiate ===
    async disableCamera() {
        if (!this._cameraEnabled) return false;
        // addTransceiver 返回的是 transceiver，用 direction=inactive 停止发送
        if (this._videoSender && this._videoSender.direction) {
            this._videoSender.direction = 'inactive';
        }
        if (this._videoTrack) {
            this._videoTrack.stop();  // 真正停止摄像头捕获（指示灯熄灭）
            this.localStream.removeTrack(this._videoTrack);
        }
        this._videoTrack = null;
        this._videoSender = null;
        this._cameraEnabled = false;
        // 更新本地 video 元素（只剩音频，画面黑屏）
        const localVideo = document.getElementById('localVideo');
        if (localVideo) localVideo.srcObject = this.localStream;
        console.log('[WebRTC] Camera disabled, renegotiating');
        this._needsRenegotiate = true;
        await this.renegotiate();
        return false;
    },

    // === 添加接收 transceiver（peerId 化，优先复用 inactive 池） ===
    addRecvTransceiver(peerId, kind) {
        if (!this.pc) return;
        const kindList = kind === 'both' ? ['video', 'audio'] : [kind];
        for (const k of kindList) {
            let tc;
            const idx = this._inactivePool.findIndex(p => p.kind === k);
            if (idx >= 0) {
                tc = this._inactivePool[idx].transceiver;
                this._inactivePool.splice(idx, 1);
                tc.direction = 'recvonly';
                console.log('[WebRTC] Reused inactive transceiver for ' + peerId + ' (' + k + ')');
            } else {
                tc = this.pc.addTransceiver(k, { direction: 'recvonly' });
            }
            this._pendingTransceivers.push({ transceiver: tc, peerId, kind: k });
        }
    },

    // === 串行重协商：队列避免并发冲突 ===
    async renegotiate() {
        this._renegotiateQueue.push(true);
        if (this._renegotiating) return;
        await this._runRenegotiateQueue();
    },

    async _runRenegotiateQueue() {
        while (this._renegotiateQueue.length > 0) {
            this._renegotiateQueue.shift();
            if (this._pendingTransceivers.length === 0 && !this._needsRenegotiate) continue;
            this._renegotiating = true;
            try {
                const offer = await this.pc.createOffer();
                await this.pc.setLocalDescription(offer);

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
                this._needsRenegotiate = false;

                console.log('[WebRTC] Renegotiate offer (' + offer.sdp.length + ' bytes, ' + recvMids.length + ' recv_mids)');
                await this._sendOffer({ sdp: offer.sdp, recv_mids: recvMids });
            } finally {
                this._renegotiating = false;
            }
        }
    },

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
        if (this._answerResolver) {
            const r = this._answerResolver;
            this._answerResolver = null;
            r();
        }
    },

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
    async toggleCamera() {
        if (this._cameraEnabled) {
            return await this.disableCamera();
        } else {
            return await this.enableCamera();
        }
    },
    setMicVolume(value) {
        if (this.gainNode) this.gainNode.gain.value = value / 100;
    },
    startRecording() {
        if (!this.localStream) return false;
        const mimeType = this._cameraEnabled ? 'video/webm;codecs=vp8,opus' : 'audio/webm;codecs=opus';
        if (!MediaRecorder.isTypeSupported(mimeType)) {
            console.warn('MediaRecorder not supported: ' + mimeType);
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
            const blob = new Blob(this.recChunks, { type: this.mediaRecorder.mimeType });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            const ts = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
            const ext = this._cameraEnabled ? 'webm' : 'weba';
            a.href = url; a.download = `namecon_${ts}.${ext}`; a.click();
            URL.revokeObjectURL(url);
        };
    },

    cleanup() {
        if (this._videoTrack) this._videoTrack.stop();
        if (this.pc) { this.pc.close(); this.pc = null; }
        if (this.localStream) this.localStream.getTracks().forEach(t => t.stop());
        if (this.audioContext) this.audioContext.close();
        this._pendingTransceivers = [];
        this._inactivePool = [];
        this._midPeerMap = {};
        this._renegotiateQueue = [];
        this._renegotiating = false;
        this._needsRenegotiate = false;
        this._videoTrack = null;
        this._videoSender = null;
        this._cameraEnabled = false;
    }
};

signaling.on('answer', async (msg) => {
    console.log('[WebRTC] Got answer (' + msg.sdp.length + ' bytes)');
    await WebRTC.setAnswer(msg.sdp);
});
