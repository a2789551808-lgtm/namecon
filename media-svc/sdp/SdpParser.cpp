#include "SdpParser.h"
#include <sstream>
#include <iostream>

uint8_t SdpParser::videoPT = 96;
uint8_t SdpParser::audioPT = 111;
std::set<uint8_t> SdpParser::videoPTs;
std::set<uint8_t> SdpParser::audioPTs;

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
        // 容错：去除行首空白（部分 offer 在 CRLF 后带空格，浏览器 SDP 一般无此问题）
        size_t firstNWS = line.find_first_not_of(" \t");
        if (firstNWS == std::string::npos) continue;  // 空行跳过
        if (firstNWS > 0) line.erase(0, firstNWS);

        // m= 行：开始一个新的 media section
        if (line.compare(0, 2, "m=") == 0) {
            MediaSection sec;
            // m=<type> <port> <proto> <pt1> <pt2> ...
            std::istringstream ms(line.substr(2));
            std::string type, port, proto;
            ms >> type >> port >> proto;
            // 剩余部分为 PT 列表
            std::string rest;
            std::getline(ms, rest);
            // 去掉前导空格
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            sec.type = type;
            sec.ptList = rest;
            // 把该 section 的所有 PT 加入对应集合（video/audio）
            // 这样 Router 能靠集合判断 RTP 包类型，不受浏览器 PT 分配影响
            {
                std::istringstream pts(rest);
                int pt;
                while (pts >> pt) {
                    if (type == "video")      videoPTs.insert(static_cast<uint8_t>(pt));
                    else if (type == "audio") audioPTs.insert(static_cast<uint8_t>(pt));
                }
            }
            _sections.push_back(sec);
            current = &_sections.back();
            continue;
        }

        // 全局属性（取首次出现；真实浏览器 SDP 中 ice-ufrag/pwd/fingerprint
        // 常出现在 m= section 内部，因此无论是否已进入 section 都需检查）
        std::string val = extractAttr(line, "ice-ufrag");
        if (!val.empty()) { if (_iceUfrag.empty()) _iceUfrag = val; continue; }
        val = extractAttr(line, "ice-pwd");
        if (!val.empty()) { if (_icePwd.empty()) _icePwd = val; continue; }
        val = extractAttr(line, "fingerprint");
        if (!val.empty()) {
            if (_fingerprint.empty()) {
                size_t space = val.find(' ');
                _fingerprint = (space != std::string::npos) ? val.substr(space + 1) : val;
            }
            continue;
        }

        if (!current) continue;

        // 当前 section 的属性
        val = extractAttr(line, "mid");
        if (!val.empty()) { current->mid = val; continue; }

        // direction: a=sendrecv / a=sendonly / a=recvonly / a=inactive
        if (line == "a=sendrecv") { current->direction = "sendrecv"; continue; }
        if (line == "a=sendonly") { current->direction = "sendonly"; continue; }
        if (line == "a=recvonly") { current->direction = "recvonly"; continue; }
        if (line == "a=inactive") { current->direction = "inactive"; continue; }

        // 收集 codec 相关行：rtpmap / rtcp-fb / fmtp
        // answer 原样镜像这些行，保证浏览器能正确配置 send/recv parameters
        // （含 RTX 的 apt 关联、NACK/PLI/FIR 反馈机制）
        if (line.compare(0, 9, "a=rtpmap:") == 0) {
            current->codecLines.push_back(line);
            // 解析 rtpmap 更新全局 videoPT/audioPT（Router 用它判 RTP 包类型）
            // 取该 type 下首个 VP8/opus 的 PT 作为主 codec
            std::string rv = line.substr(9);
            size_t space = rv.find(' ');
            std::string pt = (space != std::string::npos) ? rv.substr(0, space) : rv;
            std::string codec = (space != std::string::npos) ? rv.substr(space + 1) : "";
            if (current->type == "video" &&
                (codec.find("VP8") != std::string::npos || codec.find("vp8") != std::string::npos)) {
                videoPT = static_cast<uint8_t>(std::stoi(pt));
            } else if (current->type == "audio" &&
                       (codec.find("opus") != std::string::npos || codec.find("Opus") != std::string::npos)) {
                audioPT = static_cast<uint8_t>(std::stoi(pt));
            }
            continue;
        }
        if (line.compare(0, 10, "a=rtcp-fb:") == 0) {
            current->codecLines.push_back(line);
            continue;
        }
        if (line.compare(0, 7, "a=fmtp:") == 0) {
            current->codecLines.push_back(line);
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
        const std::string& offerDir = sec.direction.empty() ? "sendrecv" : sec.direction;

        // SDP 方向协商（RFC 3264 §6.1）：
        //   offer sendrecv -> answer sendrecv
        //   offer sendonly -> answer recvonly
        //   offer recvonly -> answer sendonly  ← 浏览器想收，SFU 就发
        //   offer inactive -> answer inactive
        std::string answerDir;
        if (offerDir == "sendrecv")      answerDir = "sendrecv";
        else if (offerDir == "sendonly") answerDir = "recvonly";
        else if (offerDir == "recvonly") answerDir = "sendonly";
        else                             answerDir = "inactive";

        answer << "m=" << sec.type << " 9 UDP/TLS/RTP/SAVPF " << sec.ptList << "\r\n";
        answer << "c=IN IP4 0.0.0.0\r\n";
        answer << "a=ice-ufrag:" << srvUf << "\r\n";
        answer << "a=ice-pwd:" << srvPwd << "\r\n";
        answer << "a=candidate:1 1 UDP 2130706433 " << srvIp << " " << srvPort << " typ host\r\n";
        answer << "a=rtcp:9 IN IP4 0.0.0.0\r\n";
        answer << "a=fingerprint:sha-256 " << srvFp << "\r\n";
        answer << "a=setup:passive\r\n";
        answer << "a=rtcp-mux\r\n";
        answer << "a=mid:" << sec.mid << "\r\n";
        answer << "a=" << answerDir << "\r\n";

        // 镜像 offer 的所有 codec 相关行（rtpmap/rtcp-fb/fmtp）
        // 这样浏览器能正确配置 send parameters（RTX apt 关联、反馈机制等），
        // 避免 "Failed to set remote video description send parameters" 错误
        for (const auto& cl : sec.codecLines) {
            answer << cl << "\r\n";
        }

        // === recvonly section 写入 a=ssrc：告知浏览器该 transceiver 期望的出口 SSRC ===
        if (offerDir == "recvonly") {
            auto it = _midSsrc.find(sec.mid);
            if (it != _midSsrc.end()) {
                uint32_t ssrc = it->second;
                answer << "a=ssrc:" << ssrc << " cname:namecon\r\n";
                answer << "a=ssrc:" << ssrc << " msid:namecon namecon" << ssrc << "\r\n";
                answer << "a=ssrc:" << ssrc << " mslabel:namecon\r\n";
                answer << "a=ssrc:" << ssrc << " label:namecon" << ssrc << "\r\n";
            }
        }
    }

    return answer.str();
}
