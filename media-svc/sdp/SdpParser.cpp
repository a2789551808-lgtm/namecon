#include "SdpParser.h"
#include <sstream>
#include <iostream>

uint8_t SdpParser::videoPT = 96;
uint8_t SdpParser::audioPT = 111;

static std::string extractAttr(const std::string& line, const std::string& key) {
    std::string prefix = "a=" + key + ":";
    if (line.compare(0, prefix.size(), prefix) == 0) {
        return line.substr(prefix.size());
    }
    return "";
}

bool SdpParser::parseOffer(const std::string& sdp) {
    _sections.clear();
    std::istringstream stream(sdp);
    std::string line;
    MediaSection* current = nullptr;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // m= 行：开始一个新的 media section
        if (line.compare(0, 2, "m=") == 0) {
            MediaSection sec;
            size_t space1 = line.find(' ');
            if (space1 != std::string::npos) {
                sec.type = line.substr(2, space1 - 2);
            }
            _sections.push_back(sec);
            current = &_sections.back();
            continue;
        }

        if (!current) {
            // 全局属性
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
            continue;
        }

        // 当前 section 的属性
        std::string val = extractAttr(line, "mid");
        if (!val.empty()) { current->mid = val; continue; }

        // direction: a=sendrecv / a=sendonly / a=recvonly / a=inactive
        if (line == "a=sendrecv") { current->direction = "sendrecv"; continue; }
        if (line == "a=sendonly") { current->direction = "sendonly"; continue; }
        if (line == "a=recvonly") { current->direction = "recvonly"; continue; }
        if (line == "a=inactive") { current->direction = "inactive"; continue; }

        val = extractAttr(line, "rtpmap");
        if (!val.empty()) {
            size_t space = val.find(' ');
            std::string pt = (space != std::string::npos) ? val.substr(0, space) : val;
            std::string codec = (space != std::string::npos) ? val.substr(space + 1) : "";
            current->payloadType = pt;
            current->codec = codec;
            // 更新全局 videoPT/audioPT
            if (codec.find("VP8") != std::string::npos || codec.find("vp8") != std::string::npos) {
                videoPT = static_cast<uint8_t>(std::stoi(pt));
            } else if (codec.find("opus") != std::string::npos || codec.find("Opus") != std::string::npos) {
                audioPT = static_cast<uint8_t>(std::stoi(pt));
            }
            continue;
        }
    }
    return !_iceUfrag.empty() && !_fingerprint.empty() && !_sections.empty();
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

    // SDP 头
    answer << "v=0\r\n";
    answer << "o=- 0 0 IN IP4 " << srvIp << "\r\n";
    answer << "s=NameCon\r\n";
    answer << "t=0 0\r\n";

    // BUNDLE：把所有 mid 列出来
    answer << "a=group:BUNDLE";
    for (const auto& sec : _sections) {
        answer << " " << sec.mid;
    }
    answer << "\r\n";

    answer << "a=msid-semantic: WMS\r\n";
    answer << "a=ice-lite\r\n";

    // 为每条 media section 生成 Answer
    for (const auto& sec : _sections) {
        std::string pt = sec.payloadType.empty() ?
            (sec.type == "audio" ? "111" : "96") : sec.payloadType;
        std::string codec = sec.codec.empty() ?
            (sec.type == "audio" ? "opus/48000/2" : "VP8/90000") : sec.codec;
        std::string dir = sec.direction.empty() ? "sendrecv" : sec.direction;

        answer << "m=" << sec.type << " 9 UDP/TLS/RTP/SAVPF " << pt << "\r\n";
        answer << "c=IN IP4 0.0.0.0\r\n";
        answer << "a=ice-ufrag:" << srvUf << "\r\n";
        answer << "a=ice-pwd:" << srvPwd << "\r\n";
        answer << "a=candidate:1 1 UDP 2130706433 " << srvIp << " " << srvPort << " typ host\r\n";
        answer << "a=rtcp:9 IN IP4 0.0.0.0\r\n";
        answer << "a=fingerprint:sha-256 " << srvFp << "\r\n";
        answer << "a=setup:passive\r\n";
        answer << "a=rtcp-mux\r\n";
        answer << "a=mid:" << sec.mid << "\r\n";
        answer << "a=" << dir << "\r\n";
        answer << "a=rtpmap:" << pt << " " << codec << "\r\n";
    }

    return answer.str();
}
