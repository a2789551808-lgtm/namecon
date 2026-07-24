#pragma once
#include <string>
#include <cstdint>

// SDP 会话描述 - Offer 解析 + Answer 生成
class SdpParser {
public:
    bool parseOffer(const std::string& sdp);

    // 注入 SFU 的真实凭据（由 MediaServiceImpl 在生成 Answer 前调用）
    void setServerInfo(const std::string& ip, int port,
                       const std::string& ufrag, const std::string& pwd,
                       const std::string& fingerprint);

    std::string generateAnswer(const std::string& offerSdp);

    // 全局：解析出的视频/音频 PT（Router 用它判断 RTP 包是音频还是视频）
    static uint8_t videoPT;
    static uint8_t audioPT;

private:
    // 从浏览器 offer 解析到的
    std::string _iceUfrag;
    std::string _icePwd;
    std::string _fingerprint;
    std::string _audioPayloadType;
    std::string _videoPayloadType;
    std::string _audioSsrc;
    std::string _videoSsrc;

    // SFU 自身的真实凭据
    std::string _serverIp;
    int         _serverPort = 10000;
    std::string _serverUfrag;
    std::string _serverPwd;
    std::string _serverFingerprint;
};
