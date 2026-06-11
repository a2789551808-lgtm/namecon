#include "SdpParser.h"
#include <sstream>
#include <iostream>

// SDP 行格式: <type>=<value>
// 我们需要提取的关键字段:
//   a=ice-ufrag:xxx        → ice-ufrag
//   a=ice-pwd:xxx          → ice-pwd
//   a=fingerprint:sha-256 xx:xx:...  → fingerprint
//   a=rtpmap:111 opus/48000/2        → audio payload type
//   a=rtpmap:96 VP8/90000            → video payload type
//   a=ssrc:xxx cname:xxx             → ssrc
//   a=mid:0 / a=mid:1                → media mid (用于 bundle)

static std::string extractAttr(const std::string& line, const std::string& key) {
    std::string prefix = "a=" + key + ":";
    if (line.compare(0, prefix.size(), prefix) == 0) {
        return line.substr(prefix.size());
    }
    return "";
}

bool SdpParser::parseOffer(const std::string& sdp) {
    std::istringstream stream(sdp);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // ice-ufrag
        std::string val = extractAttr(line, "ice-ufrag");
        if (!val.empty()) { _iceUfrag = val; continue; }

        // ice-pwd
        val = extractAttr(line, "ice-pwd");
        if (!val.empty()) { _icePwd = val; continue; }

        // fingerprint
        val = extractAttr(line, "fingerprint");
        if (!val.empty()) {
            // "sha-256 AB:CD:..." → 去掉算法前缀
            size_t space = val.find(' ');
            _fingerprint = (space != std::string::npos) ? val.substr(space + 1) : val;
            continue;
        }

        // 音视频 payload type: a=rtpmap:111 opus/48000/2
        val = extractAttr(line, "rtpmap");
        if (!val.empty()) {
            size_t space = val.find(' ');
            std::string pt = (space != std::string::npos) ? val.substr(0, space) : val;
            std::string codec = (space != std::string::npos) ? val.substr(space + 1) : "";

            if (codec.find("opus") != std::string::npos || codec.find("Opus") != std::string::npos) {
                _audioPayloadType = pt;
            } else if (codec.find("VP8") != std::string::npos || codec.find("vp8") != std::string::npos
                    || codec.find("VP9") != std::string::npos || codec.find("H264") != std::string::npos
                    || codec.find("h264") != std::string::npos) {
                _videoPayloadType = pt;
            }
            continue;
        }
    }

    return !_iceUfrag.empty() && !_fingerprint.empty();
}

std::string SdpParser::generateAnswer(const std::string& offerSdp) {
    // 先解析 offer 拿到必要字段
    if (!parseOffer(offerSdp)) {
        return "";
    }

    // 生成 SFU 的 ice-ufrag 和 ice-pwd（每个 Peer 不同，这里简化为固定）
    std::string serverUfrag = "sfu_" + _iceUfrag.substr(0, 4);
    std::string serverPwd   = _icePwd;

    std::ostringstream answer;

    // 会话级描述
    answer << "v=0\r\n";
    answer << "o=- 0 0 IN IP4 127.0.0.1\r\n";
    answer << "s=NameCon\r\n";
    answer << "t=0 0\r\n";

    // Bundle (音视频共用端口)
    answer << "a=group:BUNDLE 0 1\r\n";
    answer << "a=msid-semantic: WMS\r\n";

    // ICE 参数
    answer << "a=ice-lite\r\n";                          // SFU 是 ICE-lite
    answer << "a=ice-ufrag:" << serverUfrag << "\r\n";
    answer << "a=ice-pwd:" << serverPwd << "\r\n";

    // DTLS 指纹 (由调用方填入实际值)
    answer << "a=fingerprint:sha-256 " << _fingerprint << "\r\n";
    answer << "a=setup:passive\r\n";                     // 浏览器主动发起 DTLS

    // 候选地址 (host candidate — 由调用方填入实际 IP/端口)
    answer << "a=candidate:1 1 UDP 2130706433 127.0.0.1 10000 typ host\r\n";

    // 音频媒体段
    answer << "m=audio 10000 UDP/TLS/RTP/SAVPF ";
    answer << (_audioPayloadType.empty() ? "111" : _audioPayloadType) << "\r\n";
    answer << "c=IN IP4 127.0.0.1\r\n";
    answer << "a=rtcp-mux\r\n";
    answer << "a=mid:0\r\n";
    answer << "a=recvonly\r\n";                          // 只收不发音频
    if (!_audioPayloadType.empty()) {
        answer << "a=rtpmap:" << _audioPayloadType << " opus/48000/2\r\n";
    }

    // 视频媒体段
    answer << "m=video 10000 UDP/TLS/RTP/SAVPF ";
    answer << (_videoPayloadType.empty() ? "96" : _videoPayloadType) << "\r\n";
    answer << "c=IN IP4 127.0.0.1\r\n";
    answer << "a=rtcp-mux\r\n";
    answer << "a=mid:1\r\n";
    answer << "a=recvonly\r\n";
    if (!_videoPayloadType.empty()) {
        answer << "a=rtpmap:" << _videoPayloadType << " VP8/90000\r\n";
    }

    return answer.str();
}
