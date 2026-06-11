#include "MediaServiceImpl.h"
#include <ctime>
#include <random>

grpc::Status MediaServiceImpl::CreateRoom(
    grpc::ServerContext* /*ctx*/,
    const media::CreateRoomReq* req,
    media::CreateRoomResp* resp)
{
    // 用当前时间戳做种子生成 6 位房间号
    std::mt19937 gen(std::time(nullptr));
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string roomId = std::to_string(dis(gen));

    // 生成简单 token
    std::string token = "token_" + roomId;

    resp->set_room_id(roomId);
    resp->set_token(token);

    std::cout << "[gRPC] CreateRoom: name=" << req->room_name()
              << " → room_id=" << roomId << std::endl;

    return grpc::Status::OK;
}

grpc::Status MediaServiceImpl::AddPeer(
    grpc::ServerContext* /*ctx*/,
    const media::AddPeerReq* req,
    media::AddPeerResp* resp)
{
    resp->set_sfu_ip("127.0.0.1");
    resp->set_sfu_port(10001);

    std::cout << "[gRPC] AddPeer: peer=" << req->peer_id()
              << " room=" << req->room_id() << std::endl;

    return grpc::Status::OK;
}
