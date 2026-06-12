#include "RouteTable.h"
#include "Peer.h"

Peer* RouteTable::lookup(uint32_t ssrc) const {
    auto it = _ssrcToPeer.find(ssrc);
    return (it != _ssrcToPeer.end()) ? it->second : nullptr;
}

void RouteTable::bind(uint32_t ssrc, Peer* peer) {
    _ssrcToPeer[ssrc] = peer;
}

void RouteTable::unbindPeer(Peer* peer) {
    for (auto it = _ssrcToPeer.begin(); it != _ssrcToPeer.end(); ) {
        if (it->second == peer) {
            it = _ssrcToPeer.erase(it);
        } else {
            ++it;
        }
    }
}
