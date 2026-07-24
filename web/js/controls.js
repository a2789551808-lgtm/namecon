// controls.js - 底部控制栏逻辑
document.addEventListener('DOMContentLoaded', () => {
    // 麦克风按钮
    const micBtn = document.getElementById('micBtn');
    const micLabel = document.getElementById('micLabel');
    micBtn.addEventListener('click', () => {
        const enabled = WebRTC.toggleMic();
        micBtn.textContent = enabled ? '🎤' : '🔇';
        micLabel.textContent = enabled ? '麦克风' : '已静音';
        micBtn.classList.toggle('active', !enabled);
    });

    // 音量滑块
    const volumeSlider = document.getElementById('micVolume');
    const volIcon = document.getElementById('volIcon');
    volumeSlider.addEventListener('input', (e) => {
        const val = parseInt(e.target.value);
        WebRTC.setMicVolume(val);
        // 根据音量大小切换图标
        if (val == 0) volIcon.textContent = '🔇';
        else if (val < 50) volIcon.textContent = '🔉';
        else volIcon.textContent = '🔊';
    });

    // 摄像头按钮
    const camBtn = document.getElementById('camBtn');
    const camLabel = document.getElementById('camLabel');
    camBtn.addEventListener('click', () => {
        const enabled = WebRTC.toggleCamera();
        camBtn.textContent = enabled ? '📹' : '📷';
        camLabel.textContent = enabled ? '摄像头' : '已关闭';
        camBtn.classList.toggle('active', !enabled);
    });

    // 录制按钮
    const recBtn = document.getElementById('recBtn');
    const recLabel = document.getElementById('recLabel');
    let isRecording = false;
    recBtn.addEventListener('click', () => {
        if (!isRecording) {
            if (WebRTC.startRecording()) {
                isRecording = true;
                recBtn.textContent = '⏹';
                recLabel.textContent = '停止';
                recBtn.classList.add('recording');
            }
        } else {
            WebRTC.stopRecording();
            isRecording = false;
            recBtn.textContent = '⏺';
            recLabel.textContent = '录制';
            recBtn.classList.remove('recording');
        }
    });

    // 房间信息按钮
    const infoBtn = document.getElementById('infoBtn');
    const infoModal = document.getElementById('infoModal');
    infoBtn.addEventListener('click', () => {
        const roomId = localStorage.getItem('roomId') || '------';
        document.getElementById('modalRoomId').textContent = roomId;
        const count = document.getElementById('videoGrid').dataset.count || '1';
        document.getElementById('modalCount').textContent = `参会人数: ${count}`;
        infoModal.classList.add('show');
    });

    // 关闭弹窗
    const closeBtn = document.querySelector('.btn-close');
    if (closeBtn) {
        closeBtn.addEventListener('click', () => {
            infoModal.classList.remove('show');
        });
    }
    infoModal.addEventListener('click', (e) => {
        if (e.target === infoModal) infoModal.classList.remove('show');
    });

    // 挂断按钮
    document.getElementById('hangupBtn').addEventListener('click', () => {
        if (confirm('确定离开房间吗？')) {
            leaveRoom();
        }
    });
});

// 复制房间号
function copyRoomId() {
    const roomId = localStorage.getItem('roomId') || '';
    navigator.clipboard.writeText(roomId).then(() => {
        const btn = document.querySelector('.btn-copy');
        const orig = btn.textContent;
        btn.textContent = '已复制!';
        setTimeout(() => { btn.textContent = orig; }, 1500);
    });
}

function closeModal() {
    document.getElementById('infoModal').classList.remove('show');
}
