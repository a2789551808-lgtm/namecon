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

    std::string answer = parser.generateAnswer(req->sdp());
    if (answer.empty()) {
        // SDP 解析失败（如无 media track），返回空 answer 而不报错
        // 浏览器会在 getUserMedia 成功后重试
        std::cerr << "[gRPC] SendOffer: empty answer (no media in offer?)" << std::endl;
        return grpc::Status::OK;  // 不阻止连接建立
    }
    resp->set_answer_sdp(answer);
    std::cout << "[gRPC] SendOffer: " << req->peer_id()
              << " → answer " << answer.size() << " bytes" << std::endl;
    return grpc::Status::OK;
}

// ============================================================
// AddForwarding — Go 告诉 C++: fromPeer 的流转发给 toPeer
// ============================================================
grpc::Status MediaServiceImpl::AddForwarding(
    grpc::ServerContext* /*ctx*/,
    const media::AddForwardingReq* req,
    media::AddForwardingResp* resp)
{
    auto from = findPeer(req->from_peer_id());
    auto to   = findPeer(req->to_peer_id());

    if (!from || !to) {
        resp->set_success(false);
        return grpc::Status(grpc::StatusCode::NOT_FOUND,
                            "Peer not registered yet (wait for DTLS)");
    }

    if (_router) {
        _router->addForwarding(from.get(), to.get());
    }
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
