#pragma once
#include <grpcpp/grpcpp.h>
#include "media/media.pb.h"
#include "media/media.grpc.pb.h"

// MediaService 实现 — 继承 proto 生成的 Service 基类
class MediaServiceImpl final : public media::MediaService::Service {
public:
    grpc::Status CreateRoom(
        grpc::ServerContext* ctx,
        const media::CreateRoomReq* req,
        media::CreateRoomResp* resp) override;

    grpc::Status AddPeer(
        grpc::ServerContext* ctx,
        const media::AddPeerReq* req,
        media::AddPeerResp* resp) override;

    // 其他 RPC 方法暂返回 UNIMPLEMENTED
    grpc::Status DestroyRoom(grpc::ServerContext*, const media::DestroyRoomReq*, media::DestroyRoomResp*) override {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO");
    }
    grpc::Status RemovePeer(grpc::ServerContext*, const media::RemovePeerReq*, media::RemovePeerResp*) override {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO");
    }
    grpc::Status SendOffer(grpc::ServerContext*, const media::SendOfferReq*, media::SendOfferResp*) override {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO");
    }
    grpc::Status SendIceCandidate(grpc::ServerContext*, const media::SendIceCandidateReq*, media::SendIceCandidateResp*) override {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO");
    }
    grpc::Status MuteTrack(grpc::ServerContext*, const media::MuteTrackReq*, media::MuteTrackResp*) override {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO");
    }
    grpc::Status GetRoomStats(grpc::ServerContext*, const media::RoomStatsReq*, media::RoomStatsResp*) override {
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TODO");
    }
};
