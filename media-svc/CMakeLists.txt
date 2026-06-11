cmake_minimum_required(VERSION 3.20)
project(media-svc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SRTP2 REQUIRED libsrtp2)

# TODO: 添加源文件和链接库
add_executable(media-svc main.cpp)
target_include_directories(media-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(media-svc
    Boost::system
    OpenSSL::SSL OpenSSL::Crypto
    gRPC::grpc++ gRPC::grpc++_reflection
    protobuf::libprotobuf
    spdlog::spdlog
    ${SRTP2_LIBRARIES}
)
