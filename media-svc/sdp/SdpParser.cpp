#include "SdpParser.h"
#include <sstream>
#include <iostream>

// 全局静态成员初始化
uint8_t SdpParser::videoPT = 96;  // 默认 VP8 PT=96
uint8_t SdpParser::audioPT = 111; // 默认 opus PT=111

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
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string val = extractAttr(line, "ice-ufrag");
        if (!val.empty()) { _iceUfrag = val; continue; }

        val = extractAttr(line, "ice-pwd");
        if (!val.empty()) { _icePwd = val; continue; }

        val = extractAttr(line, "fingerprint");
        if (!val.empty()) {
            size_t space = val.find(' ');
            _fingerprint = (space != std::string::npos) ? val.substr(space + 1) : val;
            continue;
        }

        val = extractAttr(line, "ssrc");
        if (!val.empty()) {
            size_t space = val.find(' ');
            std::string ssrcVal = (space != std::string::npos) ? val.substr(0, space) : val;
            if (_audioSsrc.empty()) _audioSsrc = ssrcVal;
            else if (_videoSsrc.empty()) _videoSsrc = ssrcVal;
            continue;
        }

        val = extractAttr(line, "rtpmap");
        if (!val.empty()) {
            size_t space = val.find(' ');
            std::string pt    = (space != std::string::npos) ? val.substr(0, space) : val;
            std::string codec = (space != std::string::npos) ? val.substr(space + 1) : "";
            if ((codec.find("opus") != std::string::npos || codec.find("Opus") != std::string::npos)
                && _audioPayloadType.empty()) {
                _audioPayloadType = pt;
                audioPT = static_cast<uint8_t>(std::stoi(pt));
            } else if ((codec.find("VP8") != std::string::npos || codec.find("vp8") != std::string::npos)
                    && _videoPayloadType.empty()) {
                _videoPayloadType = pt;
                videoPT = static_cast<uint8_t>(std::stoi(pt));
            }
            continue;
        }
    }
    return !_iceUfrag.empty() && !_fingerprint.empty();
}

void SdpParser::setServerInfo(const std::string& ip, int port,
                               const std::string& ufrag, const std::string& pwd,
                               const std::string& fingerprint) {
    _serverIp          = ip;
    _serverPort        = port;
    _serverUfrag       = ufrag;
    _serverPwd         = pwd;
    _serverFingerprint = fingerprint;
}

std::string SdpParser::generateAnswer(const std::string& offerSdp) {
    if (!parseOffer(offerSdp)) return "";

    std::string srvIp   = _serverIp.empty()   ? "127.0.0.1" : _serverIp;
    int         srvPort = _serverPort > 0     ? _serverPort  : 10000;
    std::string srvUf   = _serverUfrag.empty()? "sfu_default" : _serverUfrag;
    std::string srvPwd  = _serverPwd.empty()  ? "default_pwd" : _serverPwd;
    std::string srvFp   = _serverFingerprint.empty() ? "00:00:00:00:00" : _serverFingerprint;

    std::ostringstream answer;
    answer << "v=0\r\n";
    answer << "o=- 0 0 IN IP4 " << srvIp << "\r\n";
    answer << "s=NameCon\r\n";
    answer << "t=0 0\r\n";
    answer << "a=group:BUNDLE 0 1\r\n";
    answer << "a=msid-semantic: WMS\r\n";
    answer << "a=ice-lite\r\n";

    // 音频
    answer << "m=audio 9 UDP/TLS/RTP/SAVPF "
           << (_audioPayloadType.empty() ? "111" : _audioPayloadType) << "\r\n";
    answer << "c=IN IP4 0.0.0.0\r\n";
    answer << "a=ice-ufrag:" << srvUf << "\r\n";
    answer << "a=ice-pwd:" << srvPwd << "\r\n";
    answer << "a=candidate:1 1 UDP 2130706433 " << srvIp << " " << srvPort << " typ host\r\n";
    answer << "a=rtcp:9 IN IP4 0.0.0.0\r\n";
    answer << "a=fingerprint:sha-256 " << srvFp << "\r\n";
    answer << "a=setup:passive\r\n";
    answer << "a=rtcp-mux\r\n";
    answer << "a=mid:0\r\n";
    answer << "a=sendrecv\r\n";
    if (!_audioPayloadType.empty())
        answer << "a=rtpmap:" << _audioPayloadType << " opus/48000/2\r\n";

    // 视频
    answer << "m=video 9 UDP/TLS/RTP/SAVPF "
           << (_videoPayloadType.empty() ? "96" : _videoPayloadType) << "\r\n";
    answer << "c=IN IP4 0.0.0.0\r\n";
    answer << "a=ice-ufrag:" << srvUf << "\r\n";
    answer << "a=ice-pwd:" << srvPwd << "\r\n";
    answer << "a=candidate:1 1 UDP 2130706433 " << srvIp << " " << srvPort << " typ host\r\n";
    answer << "a=rtcp:9 IN IP4 0.0.0.0\r\n";
    answer << "a=fingerprint:sha-256 " << srvFp << "\r\n";
    answer << "a=setup:passive\r\n";
    answer << "a=rtcp-mux\r\n";
    answer << "a=mid:1\r\n";
    answer << "a=sendrecv\r\n";
    if (!_videoPayloadType.empty())
        answer << "a=rtpmap:" << _videoPayloadType << " VP8/90000\r\n";

    return answer.str();
}
