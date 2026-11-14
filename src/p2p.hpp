// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
namespace hls {
struct Response;
struct P2PState {
    using Clock=std::chrono::steady_clock;
    struct Message {std::uint64_t sequence=0;std::string from,type,sdp;};
    struct Peer {
        std::string id,key,path,version,ip,cookie,source_mount,source_version,source_path;
        std::uint64_t revision=0,size=0,received=0,sent=0,sequence=0,ack=0;
        Clock::time_point expires,rate_start;
        Clock::time_point activity=Clock::now();
        std::uint32_t wanted=0;
        unsigned messages_sent=0;
        bool relay=false,downloading=true,provider=false;
        std::size_t discovery=0;
        std::vector<std::pair<std::uint32_t,std::uint32_t>> have;
        std::deque<Message> messages;
    };
    struct HashPage {std::string body;Clock::time_point used;};
    struct Relay {std::string id,key,ip,cookie;std::vector<std::string> assigned,sources;std::uint64_t revision=0;Clock::time_point expires;};
    std::mutex mutex;
    std::map<std::string,Peer> peers;
    std::map<std::string,Relay> relays;
    std::map<std::string,HashPage> hashes;
    std::map<std::string,std::vector<std::function<void(Response)>>> pending;
    std::size_t signal_bytes=0,hash_bytes=0;
    std::atomic<std::uint64_t> reported_peer_bytes{0};
    void prune(std::uint64_t revision);
    void assign_relays(std::uint64_t revision);
};
}
