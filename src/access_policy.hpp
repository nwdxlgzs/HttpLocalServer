// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <chrono>
#include <algorithm>
namespace hls {
struct LoginAttempt {
    using Clock=std::chrono::steady_clock;
    unsigned failures=0,round=0;
    Clock::time_point until{},used{};
    unsigned wait(Clock::time_point now)const{
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(until-now).count();
        return ms>0?static_cast<unsigned>((ms+999)/1000):0;
    }
    unsigned failed(Clock::time_point now){
        used=now;
        if(wait(now))return wait(now); // Blocked attempts do not mutate the deadline or round.
        if(++failures<5)return 0;
        failures=0;round=std::min(11u,round+1);
        const auto seconds=std::min(86400u,120u<<(round-1));until=now+std::chrono::seconds(seconds);return seconds;
    }
};
}
