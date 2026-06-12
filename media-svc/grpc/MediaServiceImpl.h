#pragma once
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include "media/media.pb.h"
#include "media/media.grpc.pb.h"

struct Peer;
class Router;
class IceServer;
class PacketRouter;

// MediaService 实现 — 所有 RPC 方法
class MediaServiceImpl final : public media::MediaService::Service {
public:
    MediaServiceImpl() = default;

    // 注入外部依赖
    void setRouter(std::shared_ptr<Router> router)       { _router = std::move(router); }
    void setIceServer(std::shared_ptr<IceServer> ice)    { _ice = std::move(ice); }
    void setPacketRouter(std::shared_ptr<PacketRouter> pr) { _pktRouter = std::move(pr); }

    // === RPC 实现 ===
    grpc::Status CreateRoom(grpc::ServerContext*,
        const media::CreateRoomReq*, media::CreateRoomResp*) override;

    grpc::Status AddPeer(grpc::ServerContext*,
        const media::AddPeerReq*, media::AddPeerResp*) override;

    grpc::Status SendOffer(grpc::ServerContext*,
        const media::SendOfferReq*, media::SendOfferResp*) override;

    grpc::Status AddForwarding(grpc::ServerContext*,
        const media::AddForwardingReq*, media::AddForwardingResp*) override;

    grpc::Status RemovePeer(grpc::ServerContext*,
        const media::RemovePeerReq*, media::RemovePeerResp*) override;

    // 以下暂不实现
    grpc::Status DestroyRoom(grpc::ServerContext*, const media::DestroyRoomReq*, media::DestroyRoomResp*) override
    { return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO"); }
    grpc::Status SendIceCandidate(grpc::ServerContext*, const media::SendIceCandidateReq*, media::SendIceCandidateResp*) override
    { return grpc::Status(grpc::StatusCode::OK, ""); }  // ICE-lite 不需要处理
    grpc::Status MuteTrack(grpc::ServerContext*, const media::MuteTrackReq*, media::MuteTrackResp*) override
    { return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO"); }
    grpc::Status GetRoomStats(grpc::ServerContext*, const media::RoomStatsReq*, media::RoomStatsResp*) override
    { return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO"); }

    // === 内部接口（PacketRouter 回调） ===
    // 通过 peerId 查找已注册的 Peer
    std::shared_ptr<Peer> findPeer(const std::string& peerId);

private:
    std::shared_ptr<Router>       _router;
    std::shared_ptr<IceServer>    _ice;
    std::shared_ptr<PacketRouter> _pktRouter;

    // peerId → Peer (AddPeer 创建, 等 DTLS 连接后由 PacketRouter 关联 endpoint)
    std::mutex _mutex;
    std::unordered_map<std::string, std::shared_ptr<Peer>> _peers;
};
