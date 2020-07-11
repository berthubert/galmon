#pragma once
#include <string>
#include <deque>
#include <atomic>
#include "navmon.pb.h"
#include <thread>
#include <mutex>

class NMMSender
{
  struct Destination
  {
    int fd{-1};
    std::string dst;
    std::string fname;

    std::deque<std::string> queue;
    std::mutex mut;
    void emitNMM(const NavMonMessage& nmm, bool compress);
  };
  
public:
  void addDestination(int fd)
  {
    auto d = std::make_unique<Destination>();
    d->fd = fd;
    d_dests.push_back(std::move(d));
  }
  void addDestination(const std::string& dest)
  {
    auto d = std::make_unique<Destination>();
    d->dst = dest;
    d_dests.push_back(std::move(d));
  }

  void launch()
  {
    for(auto& d : d_dests) {
      if(!d->dst.empty()) {
        d_thread.emplace_back(std::move(std::make_unique<std::thread>(&NMMSender::sendTCPThread, this, d.get())));
      }
    }
  }
  
  void sendTCPThread(Destination* d);

  void emitNMM(const NavMonMessage& nmm);
  bool d_debug{false};
  bool d_compress{false}; // set BEFORE launch
  bool d_pleaseQuit{false};
  ~NMMSender()
  {
    if(!d_thread.empty()) {
      d_pleaseQuit = true;
      for(auto& t : d_thread)
        t->join();
    }
  }
  
private:
  std::vector<std::unique_ptr<Destination>> d_dests;
  std::vector<std::unique_ptr<std::thread>> d_thread;
};
