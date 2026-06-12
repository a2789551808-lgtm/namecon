#include "MediaServiceImpl.h"
#include "../sfu/Peer.h"
#include "../sfu/Router.h"
#include "../transport/IceServer.h"
#include "../transport/PacketRouter.h"
#include "../transport/DtlsContext.h"
#include "../sdp/SdpParser.h"
#include <ctime>
#include <random>
#include <iostream>

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

    resp->set_sfu_ip("127.0.0.1");
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
// SendOffer — 解析浏览器 SDP Offer，生成 SFU Answer
// ============================================================
grpc::Status MediaServiceImpl::SendOffer(
    grpc::ServerContext* /*ctx*/,
    const media::SendOfferReq* req,
    media::SendOfferResp* resp)
{
    SdpParser parser;
    std::string answer = parser.generateAnswer(req->sdp());
    if (answer.empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Failed to parse SDP offer");
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
