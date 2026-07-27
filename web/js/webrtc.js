// webrtc.js - RTCPeerConnection 管理（重构为对象封装）
const WebRTC = {
    pc: null,
    localStream: null,
    remoteStream: null,
    audioContext: null,
    gainNode: null,
    mediaRecorder: null,
    recChunks: [],
    _remoteTrackCb: null,

    async init(sfuInfo) {
        console.log('[WebRTC] Init SFU=' + sfuInfo.sfu_ip + ':' + sfuInfo.sfu_port);
        this.pc = new RTCPeerConnection({
            iceServers: [{ urls: "stun:stun.l.google.com:19302" }]
        });

        // 获取摄像头麦克风
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
            if (localVideo) {
                localVideo.srcObject = this.localStream;
            }

            // 加入 PeerConnection
            this.localStream.getTracks().forEach(t => this.pc.addTrack(t, this.localStream));
        } catch (e) {
            console.warn('[WebRTC] getUserMedia failed:', e.message);
        }

        // 远端 track - 多流模式
        this.pc.ontrack = (event) => {
            console.log('[WebRTC] Track: ' + event.track.kind + ' id=' + event.track.id + ' mid=' + event.transceiver.mid);
            const stream = event.streams[0] || new MediaStream([event.track]);
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

        // 创建 Offer
        const offer = await this.pc.createOffer();
        await this.pc.setLocalDescription(offer);

        if (offer.sdp && offer.sdp.includes('m=audio') && offer.sdp.includes('m=video')) {
            console.log('[WebRTC] Sending offer (' + offer.sdp.length + ' bytes)');
            signaling.send('offer', { sdp: offer.sdp });
        } else {
            console.warn('[WebRTC] Offer empty - no media tracks?');
        }
    },

    async setAnswer(sdp) {
        if (!this.pc) return;
        try {
            await this.pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp }));
            console.log('[WebRTC] Remote description set');
        } catch (e) {
            console.error('[WebRTC] setRemoteDescription failed:', e);
        }
    },

    // 为新参与者添加接收通道（video + audio recvonly）
    addRecvTransceiver() {
        if (!this.pc) return;
        this.pc.addTransceiver('video', { direction: 'recvonly' });
        this.pc.addTransceiver('audio', { direction: 'recvonly' });
        console.log('[WebRTC] Added recvonly transceivers (video + audio)');
    },

    // 重协商：createOffer -> send
    async renegotiate() {
        if (!this.pc) return;
        const offer = await this.pc.createOffer();
        await this.pc.setLocalDescription(offer);
        console.log('[WebRTC] Renegotiate offer (' + offer.sdp.length + ' bytes)');
        signaling.send('offer', { sdp: offer.sdp });
    },

    toggleMic() {
        const track = this.localStream.getAudioTracks()[0];
        if (track) {
            track.enabled = !track.enabled;
            return track.enabled;
        }
        return false;
    },

    toggleCamera() {
        const track = this.localStream.getVideoTracks()[0];
        if (track) {
            track.enabled = !track.enabled;
            return track.enabled;
        }
        return false;
    },

    setMicVolume(value) {
        if (this.gainNode) {
            this.gainNode.gain.value = value / 100;
        }
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
        this.mediaRecorder.ondataavailable = (e) => {
            if (e.data.size > 0) this.recChunks.push(e.data);
        };
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
            const now = new Date();
            const ts = now.toISOString().replace(/[:.]/g, '-').slice(0, 19);
            a.href = url;
            a.download = `namecon_${ts}.webm`;
            a.click();
            URL.revokeObjectURL(url);
        };
    },

    onRemoteTrack(cb) {
        this._remoteTrackCb = cb;
    },

    cleanup() {
        if (this.pc) { this.pc.close(); this.pc = null; }
        if (this.localStream) { this.localStream.getTracks().forEach(t => t.stop()); }
        if (this.audioContext) { this.audioContext.close(); }
    }
};

// 监听 Answer 消息
signaling.on('answer', async (msg) => {
    console.log('[WebRTC] Got answer (' + msg.sdp.length + ' bytes)');
    await WebRTC.setAnswer(msg.sdp);
});
