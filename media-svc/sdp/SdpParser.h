#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <map>
#include <set>

// SDP 会话描述 - Offer 解析 + Answer 生成（支持多 m= line，Unified Plan）
class SdpParser {
public:
    bool parseOffer(const std::string& sdp);

    void setServerInfo(const std::string& ip, int port,
                       const std::string& ufrag, const std::string& pwd,
                       const std::string& fingerprint);

    std::string generateAnswer(const std::string& offerSdp);

    // 设置 mid -> 出口 SSRC 映射，generateAnswer 时为 recvonly section 写 a=ssrc
    void setMidSsrcMap(const std::map<std::string, uint32_t>& m) { _midSsrc = m; }

    // 全局：解析出的视频/音频 PT 集合（Router 用它判断 RTP 包是音频还是视频）
    // 浏览器实际发送的 PT 可能是 VP8(96) 也可能是其他(如123)，
    // 靠单个 PT 判断不可靠，改用集合覆盖 video section 的所有 PT
    static std::set<uint8_t> videoPTs;
    static std::set<uint8_t> audioPTs;
    // 兼容旧接口
    static uint8_t videoPT;
    static uint8_t audioPT;

private:
    struct MediaSection {
        std::string type;        // "audio" 或 "video"
        std::string mid;         // a=mid 值
        std::string direction;   // sendrecv / sendonly / recvonly / inactive
        std::string ptList;      // m= 行的 PT 列表原文（如 "96 97 98"）
        // 该 section 的 codec 相关行（rtpmap/rtcp-fb/fmtp），answer 原样镜像，
        // 保证浏览器能正确配置 send/recv parameters（含 RTX apt 关联、NACK/PLI 反馈）
        std::vector<std::string> codecLines;
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

    std::map<std::string, uint32_t> _midSsrc;  // mid → SFU 出口 SSRC
};
