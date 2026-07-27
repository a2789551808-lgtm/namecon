cmake_minimum_required(VERSION 3.20)
project(media-svc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === 依赖库 ===
find_package(Boost REQUIRED)  # header-only (asio)
find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SRTP2 REQUIRED libsrtp2)

# === Proto 生成的源文件 ===
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/media")
set(PROTO_SRCS
    ${PROTO_DIR}/media.pb.cc
    ${PROTO_DIR}/media.grpc.pb.cc
)
set(PROTO_HDRS
    ${PROTO_DIR}/media.pb.h
    ${PROTO_DIR}/media.grpc.pb.h
)

# === SFU 核心 ===
set(SFU_SRCS
    sfu/Router.cpp
    sfu/Peer.cpp
    sfu/RouteTable.cpp
)

# === 传输层 ===
set(TRANSPORT_SRCS
    transport/UdpServer.cpp
    transport/IceServer.cpp
    transport/DtlsContext.cpp
    transport/PacketRouter.cpp
    transport/SrtpContext.cpp
)

# === SDP 解析 ===
set(SDP_SRCS
    sdp/SdpParser.cpp
)

# === RTP / RTCP ===
set(RTP_SRCS
    rtp/RtpHeader.cpp
)

set(RTCP_SRCS
    rtcp/RtcpHandler.cpp
)

# === 配置 ===
set(CONFIG_SRCS
    config/ConfigMgr.cpp
)

# === gRPC 服务 ===
set(GRPC_SRCS
    grpc/MediaServiceImpl.cpp
    grpc/GrpcServer.cpp
)

# === AI 管线 ===
set(AI_SRCS
    ai/Pipeline.cpp
    ai/Vad.cpp
    ai/Transcript.cpp
)

# === 工具 ===
set(UTIL_SRCS
    utils/Logger.cpp
    utils/Stats.cpp
)

# === 核心启动 ===
set(CORE_SRCS
    core/MediaServer.cpp
)

# === 编译 ===
add_executable(media-svc
    main.cpp
    ${CORE_SRCS}
    ${PROTO_SRCS}
    ${SFU_SRCS}
    ${TRANSPORT_SRCS}
    ${SDP_SRCS}
    ${RTP_SRCS}
    ${RTCP_SRCS}
    ${GRPC_SRCS}
    ${CONFIG_SRCS}
    ${AI_SRCS}
    ${UTIL_SRCS}
)

target_include_directories(media-svc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${Protobuf_INCLUDE_DIRS}
    ${SRTP2_INCLUDE_DIRS}
)

target_link_libraries(media-svc
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
    protobuf::libprotobuf
    gRPC::grpc++
    spdlog::spdlog
    ${SRTP2_LIBRARIES}
)

# === Consumer 模型单元测试（替代旧 test_forward） ===
add_executable(test_consumer
    sfu/test_consumer.cpp
    sfu/Router.cpp
    sfu/Peer.cpp
    sfu/RouteTable.cpp
    sdp/SdpParser.cpp
    transport/UdpServer.cpp
    transport/SrtpContext.cpp
    rtp/RtpHeader.cpp
)
target_include_directories(test_consumer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SRTP2_INCLUDE_DIRS}
)
target_link_libraries(test_consumer
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SRTP2_LIBRARIES}
)

# === RtpHeader 单元测试 ===
add_executable(test_rtp_header
    rtp/test_rtp_header.cpp
    rtp/RtpHeader.cpp
)
target_include_directories(test_rtp_header PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# === SdpParser a=ssrc 单元测试 ===
add_executable(test_sdp_ssrc
    sdp/test_sdp_ssrc.cpp
    sdp/SdpParser.cpp
)
target_include_directories(test_sdp_ssrc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
