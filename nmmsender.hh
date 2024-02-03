#pragma once
#include <string>
#include <deque>
#include <map>
#include <atomic>
#include "navmon.pb.h"
#include <thread>
#include <mutex>
#include "zstdwrap.hh"
#include "comboaddress.hh"
#include "swrappers.hh"
#include "sclasses.hh"

class NMMSender
{
  struct Destination
  {
    int fd{-1};
    std::string dst;
    std::string fname;
    bool listener{false};
    std::deque<std::string> queue;
    std::mutex mut;
    void emitNMM(const std::string& out, bool compress);
    std::vector<Destination> clients;
  };
  
public:
  void addDestination(int fd)
  {
    auto d = std::make_unique<Destination>();
    d->fd = fd;
    std::lock_guard<std::mutex> l(d_destslock);
    d_dests.push_back(std::move(d));
  }
  void addDestination(const std::string& dest)
  {
    auto d = std::make_unique<Destination>();
    d->dst = dest;
    std::lock_guard<std::mutex> l(d_destslock);    
    d_dests.push_back(std::move(d));
  }
  void addListener(const std::string& dest)
  {
    auto d = std::make_unique<Destination>();
    d->dst = dest;
    d->listener = true;
    std::lock_guard<std::mutex> l(d_destslock);    
    d_dests.push_back(std::move(d));
  }

  void launch()
  {
    for(auto& d : d_dests) {
      if(d->listener) {
        d_thread.emplace_back(std::move(std::make_unique<std::thread>(&NMMSender::acceptorThread, this, d.get())));
      }
      else if(!d->dst.empty()) {
        d_thread.emplace_back(std::move(std::make_unique<std::thread>(&NMMSender::sendTCPThread, this, d.get())));
      }
    }
  }
  
  void sendTCPThread(Destination* d);
  void acceptorThread(Destination* d);
  void forwarderThread(Destination* d, NMMSender* there);
  void sendTCPListenerThread(Destination* d, int fd, ComboAddress remote);
  void sendLoop(Destination* d, SocketCommunicator& sc, std::unique_ptr<ZStdCompressor>& zsc, Socket& s, std::map<uint32_t, std::string>& unacked, time_t connStartTime);
  void emitNMM(const NavMonMessage& nmm);
  void emitNMM(const std::string& out);
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
  std::mutex d_destslock;
  std::vector<std::unique_ptr<Destination>> d_dests;
  std::vector<std::unique_ptr<std::thread>> d_thread;
};
