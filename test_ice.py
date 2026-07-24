#!/usr/bin/env python3
"""直接测试 SFU 的 ICE + DTLS 链路"""
import socket
import struct
import hmac
import hashlib
import ssl
import time

HOST = '127.0.0.1'
PORT = 10000
MAGIC = 0x2112A442

# 1. 创建房间 → 拿到 ICE 参数
import urllib.request, json
def api(method, path, data=None):
    url = f'http://localhost:8080{path}'
    req = urllib.request.Request(url, method=method,
        data=json.dumps(data).encode() if data else None,
        headers={'Content-Type': 'application/json'})
    return json.loads(urllib.request.urlopen(req).read())

room = api('POST', '/api/rooms', {'room_name': 'pytest'})
print(f"Room: {room['room_id']}")
join = api('POST', f"/api/rooms/{room['room_id']}/join", {'username': 'pytest'})
print(f"Peer: {join['peer_id']}, ufrag={join['ice_ufrag']}, pwd={join['ice_pwd']}")

ufrag = join['ice_ufrag']
pwd   = join['ice_pwd']

# 2. 发 STUN Binding Request（含 USERNAME + MESSAGE-INTEGRITY）
username = f"{ufrag}:pyclient".encode()
tid = b'\x01' * 12

body = b''
# USERNAME
body += struct.pack('!HH', 0x0006, len(username)) + username
while len(body) % 4: body += b'\x00'
# MESSAGE-INTEGRITY
mi_start = len(body)
body += struct.pack('!HH', 0x0008, 20) + b'\x00' * 20

header = struct.pack('!HHI', 0x0001, len(body), MAGIC) + tid
packet = bytearray(header + body)

# HMAC-SHA1
hmac_pos = 20 + mi_start + 4
packet[hmac_pos:hmac_pos+20] = b'\x00' * 20
computed = hmac.new(pwd.encode(), bytes(packet), hashlib.sha1).digest()
packet[hmac_pos:hmac_pos+20] = computed

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(3)
sock.sendto(bytes(packet), (HOST, PORT))

try:
    data, addr = sock.recvfrom(2048)
    msg_type = struct.unpack('!H', data[:2])[0]
    print(f"STUN Response: 0x{msg_type:04x} {'✅' if msg_type == 0x0101 else '❌'}")
    # 检查 XOR-MAPPED-ADDRESS
    pos = 20
    while pos + 4 <= len(data):
        t, l = struct.unpack('!HH', data[pos:pos+4])
        if t == 0x0020:
            family = struct.unpack('!H', data[pos+5:pos+7])[0]
            if family == 0x01:
                xport = struct.unpack('!H', data[pos+6:pos+8])[0] ^ (MAGIC >> 16)
                xip = struct.unpack('!I', data[pos+8:pos+12])[0] ^ MAGIC
                ip = socket.inet_ntoa(struct.pack('!I', xip))
                print(f"  Mapped: {ip}:{xport}")
        pos += 4 + l
        if l % 4: pos += 4 - (l % 4)
except socket.timeout:
    print("❌ No STUN response (timeout)")

sock.close()
print("\nC++ ICE 测试完成 — 如果有 ✅ 说明 C++ 侧正常，问题在浏览器")