#pragma once
#include <string>

// SDP 会话描述 — Offer 解析 + Answer 生成
class SdpParser {
public:
    bool parseOffer(const std::string& sdp);
    std::string generateAnswer(const std::string& offerSdp);

private:
    std::string _iceUfrag;
    std::string _icePwd;
    std::string _fingerprint;
    std::string _audioPayloadType;
    std::string _videoPayloadType;
};
