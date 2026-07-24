#!/usr/bin/env python3
"""STUN Binding Request 测试 — 含 ice-ufrag + MESSAGE-INTEGRITY 凭据"""
import socket
import struct
import hmac
import hashlib
import subprocess
import re
import os
import time
import signal

STUN_MAGIC_COOKIE = 0x2112A442

def extract_credentials(log_output):
    """从 SFU 启动日志提取 ufrag + pwd"""
    m = re.search(r'\[IceServer\] ufrag=(\S+) pwd=(\S+)', log_output)
    if m:
        return m.group(1), m.group(2)
    return None, None


def build_stun_request(ufrag, pwd):
    """构造含 USERNAME + MESSAGE-INTEGRITY 的 STUN Binding Request"""
    msg_type   = 0x0001       # Binding Request
    msg_len    = 0            # 先填 0
    magic      = STUN_MAGIC_COOKIE
    tid        = b'\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c'  # 12 bytes

    # -- 构造 body (attributes) --
    body = b''

    # USERNAME: "server_ufrag:peer_ufrag"
    username = f"{ufrag}:test_peer".encode()
    body += struct.pack('!HH', 0x0006, len(username)) + username
    # 对齐到 4 字节
    while len(body) % 4 != 0:
        body += b'\x00'

    # MESSAGE-INTEGRITY: type(2) + len(2) + hmac(20) = 24 bytes
    mi_start = len(body)
    body += struct.pack('!HH', 0x0008, 20) + b'\x00' * 20

    # -- 组装完整包并计算 HMAC --
    # 消息头中 length = body 长度（到 MI 末尾）
    header = struct.pack('!HHI', msg_type, len(body), magic) + tid
    packet = header + body

    # MESSAGE-INTEGRITY 计算:
    #   1) 包中 HMAC 值清零
    #   2) HMAC-SHA1(key=pwd, data=packet)
    pkt = bytearray(packet)
    hmac_pos = 20 + mi_start + 4  # header(20) + attr_type(2) + attr_len(2)
    pkt[hmac_pos:hmac_pos+20] = b'\x00' * 20

    computed = hmac.new(pwd.encode(), bytes(pkt), hashlib.sha1).digest()
    pkt[hmac_pos:hmac_pos+20] = computed

    return bytes(pkt)


def send_and_check(packet, host='127.0.0.1', port=10000):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    sock.sendto(packet, (host, port))
    try:
        data, addr = sock.recvfrom(2048)
        msg_type = struct.unpack('!H', data[:2])[0]
        if msg_type == 0x0101:
            print(f"✅ STUN Binding Success Response! ({len(data)} bytes)")
            mi_present = b'\x00\x08' in data[20:]
            print(f"   MESSAGE-INTEGRITY in response: {mi_present}")
            return True
        else:
            print(f"❌ Unexpected type: 0x{msg_type:04x}")
    except socket.timeout:
        print("❌ No response (timeout)")
    return False


if __name__ == '__main__':
    # ① 启动 SFU
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    proc = subprocess.Popen(
        ["./media-svc/build/media-svc"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True
    )

    # ② 等待启动, 提取 ufrag/pwd
    log = ""
    ufrag, pwd = None, None
    t0 = time.time()
    while time.time() - t0 < 5:
        line = proc.stdout.readline()
        if not line:
            time.sleep(0.1)
            continue
        log += line
        print(line, end='')
        u, p = extract_credentials(line)
        if u:
            ufrag, pwd = u, p
        if 'io_context running' in line:
            break

    time.sleep(0.3)

    if not ufrag:
        print("\n❌ Could not extract ufrag/pwd from SFU output")
        proc.terminate()
        exit(1)

    print(f"\n--- Using ufrag={ufrag} pwd={pwd} ---\n")

    # ③ 测试 1: 无凭据的请求 → 应被拒绝（超时）
    print("Test 1: Request WITHOUT credentials...")
    bare = struct.pack('!HHI', 0x0001, 0, STUN_MAGIC_COOKIE) + b'\x01'*12
    send_and_check(bare)
    print()

    # ④ 测试 2: 含正确凭据的请求 → 应成功
    print("Test 2: Request WITH valid credentials...")
    pkt = build_stun_request(ufrag, pwd)
    send_and_check(pkt)

    # ⑤ 清理
    proc.send_signal(signal.SIGTERM)
    proc.wait()
