#include "RouteTable.h"
#include "Peer.h"

Peer* RouteTable::lookup(uint32_t ssrc) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto it = _ssrcToPeer.find(ssrc);
    return (it != _ssrcToPeer.end()) ? it->second : nullptr;
}

void RouteTable::bind(uint32_t ssrc, Peer* peer) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _ssrcToPeer[ssrc] = peer;
}

void RouteTable::unbindPeer(Peer* peer) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    for (auto it = _ssrcToPeer.begin(); it != _ssrcToPeer.end(); ) {
        if (it->second == peer) {
            it = _ssrcToPeer.erase(it);
        } else {
            ++it;
        }
    }
}
