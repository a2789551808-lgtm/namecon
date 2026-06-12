// webrtc.js — RTCPeerConnection 管理
let pc = null;
let localStream = null;
let remoteStream = null;

async function initWebRTC(sfuInfo) {
    console.log('[WebRTC] Init SFU=' + sfuInfo.sfu_ip + ':' + sfuInfo.sfu_port);
    pc = new RTCPeerConnection({
        iceServers: [{ urls: "stun:stun.l.google.com:19302" }]
    });

    try {
        localStream = await navigator.mediaDevices.getUserMedia({ video: true, audio: true });
        document.getElementById('localVideo').srcObject = localStream;
        localStream.getTracks().forEach(t => pc.addTrack(t, localStream));
    } catch (e) {
        console.warn('[WebRTC] getUserMedia failed:', e.message);
    }

    // 创建空的远程流，预先绑定到 video 元素
    remoteStream = new MediaStream();
    const rv = document.getElementById('remoteVideo');
    rv.srcObject = remoteStream;
    rv.muted = true;

    pc.ontrack = (event) => {
        console.log('[WebRTC] Track: ' + event.track.kind + ' id=' + event.track.id);
        remoteStream.addTrack(event.track);
        // 重新绑定 srcObject 触发 Chrome 重新渲染
        rv.srcObject = remoteStream;
        if (event.track.kind === 'video') {
            rv.play().catch(e => console.warn('play:', e));
            setTimeout(() => {
                console.log('[WebRTC] Check: w=' + rv.videoWidth + ' h=' + rv.videoHeight + ' tracks=' + remoteStream.getTracks().length);
            }, 2000);
        }
    };

    pc.oniceconnectionstatechange = () => {
        console.log('[WebRTC] ICE: ' + pc.iceConnectionState);
    };
    pc.onicegatheringstatechange = () => {
        console.log('[WebRTC] ICE gathering: ' + pc.iceGatheringState);
    };
    pc.onicecandidate = (ev) => {
        if (ev.candidate) {
            console.log('[WebRTC] Local candidate: ' + ev.candidate.type + ' ' + ev.candidate.address);
        } else {
            console.log('[WebRTC] ICE gathering complete');
        }
    };

    // 即使没有本地 track，也创建 offer（信令层仍需建立连接）
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);

    // 只有在有 media section 时才发 offer
    if (offer.sdp && offer.sdp.includes('m=audio') && offer.sdp.includes('m=video')) {
        console.log('[WebRTC] Sending offer (' + offer.sdp.length + ' bytes)');
        signaling.send('offer', { sdp: offer.sdp });
    } else {
        console.warn('[WebRTC] Offer empty - no media tracks?');
    }
}

signaling.on('answer', async (msg) => {
    console.log('[WebRTC] Got answer (' + msg.sdp.length + ' bytes)');
    console.log('[WebRTC] signalingState before:', pc ? pc.signalingState : 'null');
    if (pc) {
        try {
            await pc.setRemoteDescription(new RTCSessionDescription({
                type: 'answer',
                sdp: msg.sdp
            }));
            console.log('[WebRTC] ✅ Remote description set, signalingState=' + pc.signalingState);
        } catch (e) {
            console.error('[WebRTC] ❌ setRemoteDescription failed:', e);
        }
    }
});

signaling.on('peer-joined', (msg) => {
    console.log('Peer joined:', msg.peer_id);
});

signaling.on('peer-left', (msg) => {
    console.log('Peer left:', msg.peer_id);
});
