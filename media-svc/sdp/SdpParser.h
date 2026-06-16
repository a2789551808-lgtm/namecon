#pragma once
#include <string>

// SDP 会话描述 — Offer 解析 + Answer 生成
class SdpParser {
public:
    bool parseOffer(const std::string& sdp);

    // 注入 SFU 的真实凭据（由 MediaServiceImpl 在生成 Answer 前调用）
    void setServerInfo(const std::string& ip, int port,
                       const std::string& ufrag, const std::string& pwd,
                       const std::string& fingerprint);

    std::string generateAnswer(const std::string& offerSdp);

private:
    // 从浏览器 offer 解析到的
    std::string _iceUfrag;
    std::string _icePwd;
    std::string _fingerprint;
    std::string _audioPayloadType;   // 只保留认识的第一个音频 PT
    std::string _videoPayloadType;   // 只保留认识的第一个视频 PT
    std::string _audioSsrc;
    std::string _videoSsrc;

    // SFU 自身的真实凭据
    std::string _serverIp;
    int         _serverPort = 10000;
    std::string _serverUfrag;
    std::string _serverPwd;
    std::string _serverFingerprint;
};
