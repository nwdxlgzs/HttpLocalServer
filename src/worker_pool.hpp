#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace hls {
class WorkerPool {
    struct Slot { std::thread thread; bool finished=false; };
public:
    WorkerPool(unsigned minimum,std::size_t capacity,unsigned maximum=0,unsigned idle_seconds=15)
        :capacity_(capacity),minimum_(minimum),maximum_(std::max(minimum,maximum)),idle_seconds_(idle_seconds) {
        try { std::lock_guard<std::mutex> lock(mutex_); for(unsigned i=0;i<minimum_;++i) spawn_locked(); }
        catch(...) { stop(); throw; }
    }
    ~WorkerPool() { stop(); }
    bool submit(std::function<void()> job) {
        { std::lock_guard<std::mutex> lock(mutex_);
          if(stopping_ || jobs_.size()>=capacity_) return false;
          reap_locked(); jobs_.push_back(std::move(job));
          if(jobs_.size()>idle_) pressure_=std::chrono::steady_clock::now();
          if(jobs_.size()>idle_ && live_<maximum_) { try { spawn_locked(); } catch(...) {} }
        }
        ready_.notify_one(); return true;
    }
    unsigned count() const { std::lock_guard<std::mutex> lock(mutex_); return live_; }
    unsigned maximum() const { return maximum_; }
    void stop() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
        ready_.notify_all(); for(auto& slot:slots_) if(slot->thread.joinable()) slot->thread.join();
    }
private:
    void reap_locked() {
        for(auto it=slots_.begin();it!=slots_.end();) {
            if((*it)->finished) { if((*it)->thread.joinable()) (*it)->thread.join(); it=slots_.erase(it); }
            else ++it;
        }
    }
    void spawn_locked() {
        auto slot=std::make_shared<Slot>();
        slots_.push_back(slot); ++live_;
        try { slot->thread=std::thread([this,slot] {
            for(;;) {
                std::function<void()> job;
                { std::unique_lock<std::mutex> lock(mutex_); ++idle_;
                  if(!stopping_ && live_>minimum_ && jobs_.size()<idle_ && std::chrono::steady_clock::now()-pressure_>=std::chrono::seconds(idle_seconds_)) {
                      --idle_;--live_;slot->finished=true;ready_.notify_one();return;
                  }
                  const auto work=ready_.wait_for(lock,std::chrono::seconds(idle_seconds_),[this]{return stopping_||!jobs_.empty();}); --idle_;
                  if((stopping_ && jobs_.empty()) || (!work && live_>minimum_)) { --live_; slot->finished=true; return; }
                  if(jobs_.empty()) continue;
                  job=std::move(jobs_.front()); jobs_.pop_front(); }
                try { job(); } catch(...) {}
            }
        }); } catch(...) { --live_; slots_.pop_back(); throw; }
    }
    std::size_t capacity_; unsigned minimum_,maximum_,idle_seconds_;
    mutable std::mutex mutex_; std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_; std::vector<std::shared_ptr<Slot>> slots_;
    unsigned live_=0,idle_=0; bool stopping_=false;
    std::chrono::steady_clock::time_point pressure_=std::chrono::steady_clock::now();
};
}
