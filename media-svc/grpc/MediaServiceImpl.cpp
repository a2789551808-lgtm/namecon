#include "MediaServiceImpl.h"
#include "../sfu/Peer.h"
#include "../sfu/Router.h"
#include "../transport/IceServer.h"
#include "../transport/PacketRouter.h"
#include "../transport/DtlsContext.h"
#include "../sdp/SdpParser.h"
#include "../config/ConfigMgr.h"
#include <ctime>
#include <random>
#include <cstdlib>
#include <iostream>
#include <map>

// 获取 SFU 的公网 IP：环境变量 > 配置文件 > 默认 127.0.0.1
static std::string getPublicIp() {
    // ① 环境变量 PUBLIC_IP（最高优先级，用于 Docker 部署）
    const char* envIp = std::getenv("PUBLIC_IP");
    if (envIp && envIp[0] != '\0') return envIp;

    // ② 配置文件 server.public_ip
    auto cfgIp = ConfigMgr::GetInstance()["server"]["public_ip"];
    if (!cfgIp.empty()) return cfgIp;

    // ③ 默认（本地调试）
    return "127.0.0.1";
}

// ============================================================
// CreateRoom — 生成唯一房号（Go 侧用）
// ============================================================
grpc::Status MediaServiceImpl::CreateRoom(
    grpc::ServerContext* /*ctx*/,
    const media::CreateRoomReq* req,
    media::CreateRoomResp* resp)
{
    static std::mt19937 gen(std::time(nullptr));
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string roomId = std::to_string(dis(gen));

    resp->set_room_id(roomId);
    resp->set_token("token_" + roomId);

    std::cout << "[gRPC] CreateRoom: " << req->room_name()
              << " → " << roomId << std::endl;
    return grpc::Status::OK;
}

// ============================================================
// AddPeer — 预注册 Peer，返回 ICE + DTLS 参数
// Go 侧用这些参数拼接 SDP Answer
// ============================================================
grpc::Status MediaServiceImpl::AddPeer(
    grpc::ServerContext* /*ctx*/,
    const media::AddPeerReq* req,
    media::AddPeerResp* resp)
{
    auto peer = std::make_shared<Peer>();
    peer->peerId = req->peer_id();

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _peers[peer->peerId] = peer;
    }

    // 注册到 PacketRouter（DTLS 连接后会自动绑定 endpoint）
    if (_pktRouter) {
        _pktRouter->registerPeer(peer);
    }

    resp->set_sfu_ip(getPublicIp());
    resp->set_sfu_port(10000);

    if (_ice) {
        resp->set_ice_ufrag(_ice->getIceUfrag());
        resp->set_ice_pwd(_ice->getIcePwd());
    }
    resp->set_dtls_fingerprint(DtlsContext::fingerprint());

    std::cout << "[gRPC] AddPeer: " << req->peer_id()
              << " (ufrag=" << resp->ice_ufrag() << ")" << std::endl;
    return grpc::Status::OK;
}

// ============================================================
// SendOffer — 解析浏览器 SDP Offer，生成 SFU Answer（使用真实凭据）
//   处理 recv_mids：把订阅者声明的 mid 绑到 Router 的 Consumer，
//   收集 mid → 出口 SSRC，传给 SdpParser 在 answer 里写 a=ssrc
// ============================================================
grpc::Status MediaServiceImpl::SendOffer(
    grpc::ServerContext* /*ctx*/,
    const media::SendOfferReq* req,
    media::SendOfferResp* resp)
{
    SdpParser parser;

    if (_ice) {
        parser.setServerInfo(
            getPublicIp(),                              // IP（从配置读取）
            10000,                                          // UDP 端口
            _ice->getIceUfrag(),                            // ufrag
            _ice->getIcePwd(),                              // pwd
            DtlsContext::fingerprint()                      // DTLS 指纹
        );
    }

    // 把 recv_mids 绑到 Router 的 Consumer，收集 mid → 出口 SSRC
    std::map<std::string, uint32_t> midToSsrc;
    auto subscriber = findPeer(req->peer_id());
    if (subscriber && _router) {
        for (const auto& rm : req->recv_mids()) {
            uint32_t ssrc = _router->bindConsumerByMid(
                subscriber.get(), rm.publisher_peer_id(),
                rm.is_video(), rm.mid());
            if (ssrc != 0) {
                midToSsrc[rm.mid()] = ssrc;
            }
        }
    }
    parser.setMidSsrcMap(midToSsrc);

    std::string answer = parser.generateAnswer(req->sdp());
    if (answer.empty()) {
        // SDP 解析失败（如无 media track），返回空 answer 而不报错
        // 浏览器会在 getUserMedia 成功后重试
        std::cerr << "[gRPC] SendOffer: empty answer (no media in offer?)" << std::endl;
        return grpc::Status::OK;  // 不阻止连接建立
    }
    resp->set_answer_sdp(answer);
    std::cout << "[gRPC] SendOffer: " << req->peer_id()
              << " → answer " << answer.size() << " bytes"
              << " (recv_mids=" << req->recv_mids_size() << ")" << std::endl;
    return grpc::Status::OK;
}

// ============================================================
// AddConsumer — Go 告诉 C++: subscriber 要订阅 publisher 的某路流
// 返回 SFU 分配的出口 SSRC（写入 subscriber 的 SDP answer 的 a=ssrc）
// ============================================================
grpc::Status MediaServiceImpl::AddConsumer(
    grpc::ServerContext* /*ctx*/,
    const media::AddConsumerReq* req,
    media::AddConsumerResp* resp)
{
    auto subscriber = findPeer(req->subscriber_peer_id());
    auto publisher  = findPeer(req->publisher_peer_id());

    if (!subscriber || !publisher) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND,
                            "Peer not registered yet (wait for AddPeer)");
    }

    uint32_t ssrc = 0;
    if (_router) {
        ssrc = _router->addConsumer(subscriber.get(), publisher.get(), req->is_video());
    }
    resp->set_rewritten_ssrc(ssrc);
    return grpc::Status::OK;
}

// ============================================================
// RemoveConsumer — subscriber 取消订阅 publisher 的某路流
// 简化实现：当前不维护单个 Consumer 的 gRPC 句柄，移除依赖 Peer 离开时的 removePeer 批量清理
// 这里返回 success=true 占位，真实清理在 RemovePeer 时发生
// ============================================================
grpc::Status MediaServiceImpl::RemoveConsumer(
    grpc::ServerContext* /*ctx*/,
    const media::RemoveConsumerReq* /*req*/,
    media::RemoveConsumerResp* resp)
{
    resp->set_success(true);
    return grpc::Status::OK;
}

// ============================================================
// RemovePeer — 清理 Peer 资源
// ============================================================
grpc::Status MediaServiceImpl::RemovePeer(
    grpc::ServerContext* /*ctx*/,
    const media::RemovePeerReq* req,
    media::RemovePeerResp* resp)
{
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _peers.find(req->peer_id());
        if (it != _peers.end()) {
            peer = it->second;
            _peers.erase(it);
        }
    }

    if (peer && _router) {
        _router->removePeer(peer.get());
    }

    resp->set_success(true);
    std::cout << "[gRPC] RemovePeer: " << req->peer_id() << std::endl;
    return grpc::Status::OK;
}

// ============================================================
// findPeer — PacketRouter 在 DTLS 建立后关联 endpoint
// ============================================================
std::shared_ptr<Peer> MediaServiceImpl::findPeer(const std::string& peerId) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _peers.find(peerId);
    return (it != _peers.end()) ? it->second : nullptr;
}
