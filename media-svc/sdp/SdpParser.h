#pragma once
#include <string>
#include <cstdint>
#include <vector>

// SDP 会话描述 - Offer 解析 + Answer 生成（支持多 m= line，Unified Plan）
class SdpParser {
public:
    bool parseOffer(const std::string& sdp);

    void setServerInfo(const std::string& ip, int port,
                       const std::string& ufrag, const std::string& pwd,
                       const std::string& fingerprint);

    std::string generateAnswer(const std::string& offerSdp);

    // 全局：解析出的视频/音频 PT（Router 用它判断 RTP 包是音频还是视频）
    static uint8_t videoPT;
    static uint8_t audioPT;

private:
    struct MediaSection {
        std::string type;        // "audio" 或 "video"
        std::string mid;         // a=mid 值
        std::string direction;   // sendrecv / sendonly / recvonly
        std::string payloadType; // rtpmap 解析的 PT
        std::string codec;       // 编码名（opus/VP8）
    };

    std::vector<MediaSection> _sections;

    // 浏览器全局信息（取第一个 m= line 的）
    std::string _iceUfrag;
    std::string _icePwd;
    std::string _fingerprint;

    // SFU 自身的真实凭据
    std::string _serverIp;
    int         _serverPort = 10000;
    std::string _serverUfrag;
    std::string _serverPwd;
    std::string _serverFingerprint;
};
