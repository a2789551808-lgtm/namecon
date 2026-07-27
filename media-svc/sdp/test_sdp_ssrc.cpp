#include "SdpParser.h"
#include <cassert>
#include <iostream>
#include <map>
#include <string>

int main() {
    // 一个含 2 条 recvonly video 的 Unified Plan offer（mid:2, mid:4）
    std::string offer =
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=-\r\n t=0 0\r\n"
        "a=group:BUNDLE 0 1 2 3 4\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n a=mid:0\r\n a=sendrecv\r\n a=rtpmap:111 opus/48000/2\r\n"
        "a=ice-ufrag:abc\r\n a=ice-pwd:def\r\n a=fingerprint:sha-256 AA:BB:CC\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n a=mid:1\r\n a=sendrecv\r\n a=rtpmap:96 VP8/90000\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n a=mid:2\r\n a=recvonly\r\n a=rtpmap:96 VP8/90000\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n a=mid:4\r\n a=recvonly\r\n a=rtpmap:96 VP8/90000\r\n";

    SdpParser p;
    p.setServerInfo("127.0.0.1", 10000, "uf", "pw", "AA:BB:CC");

    std::map<std::string, uint32_t> midSsrc = { {"2", 2001u}, {"4", 2003u} };
    p.setMidSsrcMap(midSsrc);

    std::string ans = p.generateAnswer(offer);
    assert(!ans.empty());
    assert(ans.find("a=ssrc:2001 cname:namecon") != std::string::npos);
    assert(ans.find("a=ssrc:2003 cname:namecon") != std::string::npos);
    // sendrecv 的 mid:1 不应有 a=ssrc
    auto mid1Pos = ans.find("a=mid:1");
    auto ssrc2001Pos = ans.find("a=ssrc:2001");
    assert(mid1Pos < ssrc2001Pos);  // mid:1 在 2001 之前出现即可，粗校验

    std::cout << "✅ SdpParser a=ssrc test passed\n";
    return 0;
}
