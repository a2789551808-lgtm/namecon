# NameCon 接口文档

> 版本: v1.0 | 日期: 2026-06-11

---

## 一、gRPC 接口 (Go ↔ C++)

### 1.1 服务定义

```protobuf
syntax = "proto3";
package media;

service MediaService {
  rpc CreateRoom      (CreateRoomReq)       returns (CreateRoomResp);
  rpc DestroyRoom     (DestroyRoomReq)      returns (DestroyRoomResp);
  rpc AddPeer         (AddPeerReq)          returns (AddPeerResp);
  rpc RemovePeer      (RemovePeerReq)       returns (RemovePeerResp);
  rpc SendOffer       (SendOfferReq)        returns (SendOfferResp);
  rpc SendIceCandidate(SendIceCandidateReq) returns (SendIceCandidateResp);
  rpc MuteTrack       (MuteTrackReq)        returns (MuteTrackResp);
  rpc GetRoomStats    (RoomStatsReq)        returns (RoomStatsResp);
}
```

### 1.2 接口详情

#### CreateRoom — 创建房间

```
rpc CreateRoom(CreateRoomReq) returns (CreateRoomResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_name | string | 房间名称 |
| **响应** | | |
| room_id | string | 6 位数字房间号，如 "261500" |
| token | string | 入会凭证，格式 "token_{room_id}" |

**C++ 实现**: `MediaServiceImpl::CreateRoom()` — 时间种子生成 6 位随机 ID

---

#### AddPeer — 添加参会者

```
rpc AddPeer(AddPeerReq) returns (AddPeerResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_id | string | 房间号 |
| peer_id | string | 用户标识 |
| token | string | 入会凭证 |
| **响应** | | |
| sfu_ip | string | SFU 服务器 IP，浏览器用来发 UDP |
| sfu_port | int32 | 分配的 UDP 端口号 |

---

#### DestroyRoom — 销毁房间

```
rpc DestroyRoom(DestroyRoomReq) returns (DestroyRoomResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_id | string | 房间号 |
| **响应** | | |
| success | bool | 是否成功 |

---

#### RemovePeer — 移除参会者

```
rpc RemovePeer(RemovePeerReq) returns (RemovePeerResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_id | string | 房间号 |
| peer_id | string | 用户标识 |
| **响应** | | |
| success | bool | 是否成功 |

---

#### SendOffer — 转发 SDP Offer

```
rpc SendOffer(SendOfferReq) returns (SendOfferResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_id | string | 房间号 |
| peer_id | string | 用户标识 |
| sdp | string | 浏览器生成的 SDP Offer 文本 |
| **响应** | | |
| answer_sdp | string | C++ 解析后生成的 SDP Answer |

---

#### SendIceCandidate — 转发 ICE Candidate

```
rpc SendIceCandidate(SendIceCandidateReq) returns (SendIceCandidateResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_id | string | 房间号 |
| peer_id | string | 用户标识 |
| candidate | string | ICE Candidate 字符串 |
| **响应** | | |
| success | bool | 是否成功 |

---

#### MuteTrack — 静音/关闭摄像头

```
rpc MuteTrack(MuteTrackReq) returns (MuteTrackResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_id | string | 房间号 |
| peer_id | string | 用户标识 |
| mute_audio | bool | 是否静音 |
| mute_video | bool | 是否关闭摄像头 |
| **响应** | | |
| success | bool | 是否成功 |

---

#### GetRoomStats — 查询房间状态

```
rpc GetRoomStats(RoomStatsReq) returns (RoomStatsResp)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| **请求** | | |
| room_id | string | 房间号 |
| **响应** | | |
| peer_count | int32 | 当前参会人数 |

---

## 二、REST API (浏览器 → Go)

### 2.1 房间管理

#### POST /api/rooms — 创建房间

```
Request:  { "room_name": "测试会议室" }
Response: { "room_id": "261500", "token": "token_261500" }
Status:   201 Created
```

#### GET /api/rooms/:id — 查询房间

```
Response: { "room_id": "261500", "room_name": "测试会议室", "participant_count": 3 }
Status:   200 OK
```

#### DELETE /api/rooms/:id — 结束房间 (房主)

```
Header:   Authorization: Bearer <token>
Response: { "success": true }
Status:   200 OK
```

### 2.2 参会者管理

#### POST /api/rooms/:id/join — 获取入会 Token

```
Request:  { "username": "张三" }
Response: { "token": "eyJhbG...", "peer_id": "peer_abc" }
Status:   200 OK
```

#### GET /api/rooms/:id/participants — 参与者列表

```
Response: { "participants": [{"peer_id": "...", "username": "张三"}, ...] }
Status:   200 OK
```

#### POST /api/rooms/:id/kick — 踢人 (房主)

```
Header:   Authorization: Bearer <token>
Request:  { "peer_id": "peer_abc" }
Response: { "success": true }
Status:   200 OK
```

### 2.3 AI 总结 (预留)

#### POST /api/rooms/:id/summary — 手动触发总结

```
Response: { "summary_id": "...", "status": "generating" }
Status:   202 Accepted
```

### 2.4 健康检查

#### GET /api/health

```
Response: { "status": "ok", "uptime": "2h30m" }
Status:   200 OK
```

---

## 三、WebSocket 协议 (浏览器 ↔ Go)

### 3.1 连接

```
ws://localhost:8081/ws?token=<jwt_token>
```

### 3.2 客户端 → 服务器

| type | payload | 说明 |
|------|---------|------|
| `join` | `{room_id, token}` | 加入房间 |
| `leave` | `{}` | 离开房间 |
| `offer` | `{sdp}` | SDP Offer |
| `ice-candidate` | `{candidate}` | ICE Candidate |
| `mute` | `{audio, video}` | 静音/关闭摄像头 |
| `request-summary` | `{}` | 手动触发 AI 总结 (预留) |

### 3.3 服务器 → 客户端

| type | payload | 说明 |
|------|---------|------|
| `joined` | `{peer_id, sfu_ip, sfu_port}` | 成功加入 |
| `peer-joined` | `{peer_id, username}` | 新成员加入 |
| `peer-left` | `{peer_id}` | 成员离开 |
| `answer` | `{sdp}` | SDP Answer |
| `ice-candidate` | `{candidate}` | ICE Candidate |
| `peer-muted` | `{peer_id, audio, video}` | 某人静音状态变化 |
| `summary` | `{title, overview, keyPoints, actionItems}` | AI 总结 (预留) |
| `error` | `{code, message}` | 错误信息 |

---

## 四、连接与端口

| 协议 | 端口 | 方向 | 说明 |
|------|------|------|------|
| HTTP | :8080 | 浏览器→Go | REST API |
| WebSocket | :8081 | 浏览器↔Go | 信令 |
| gRPC | :50051 | Go→C++ | 服务间通信 |
| UDP | 10000-20000 | 浏览器↔C++ | SRTP 加密音视频 |
