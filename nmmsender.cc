#include "nmmsender.hh"
#include "comboaddress.hh"
#include "swrappers.hh"
#include "sclasses.hh"
#include <random>
#include "navmon.hh"
#include <algorithm>

using namespace std;

void NMMSender::sendTCPThread(Destination* d)
{
  struct NameError{};
  for(;;) {
    ComboAddress chosen;
    try {
      vector<ComboAddress> addrs;
      for(;;) {
        addrs=resolveName(d->dst, true, true);
        if(!addrs.empty())
          break;
          
        cerr<<humanTimeNow()<<" Unable to resolve "<<d->dst<<", sleeping and trying again later"<<endl;
        throw NameError();
      }
          
      std::random_device rng;
      std::mt19937 urng(rng());
      std::shuffle(addrs.begin(), addrs.end(), urng);

      for(auto& addr: addrs)  {
        if(!addr.sin4.sin_port)
          addr.sin4.sin_port = ntohs(29603);
        chosen=addr;
        Socket s(addr.sin4.sin_family, SOCK_STREAM);
        SocketCommunicator sc(s);
        sc.setTimeout(3);
        sc.connect(addr);
        if (d_debug) { cerr<<humanTimeNow()<<" Connected to "<<d->dst<<" on "<<addr.toStringWithPort()<<endl; }
        for(;;) {
          std::string msg;
          {
            std::lock_guard<std::mutex> mut(d->mut);
            if(!d->queue.empty()) {
              msg = d->queue.front();
            }
          }
          if(!msg.empty()) {
            sc.writen(msg);
            std::lock_guard<std::mutex> mut(d->mut);
            d->queue.pop_front();
          }
          else usleep(100000);
        }
      }
    }
    catch(NameError&) {
      {
        std::lock_guard<std::mutex> mut(d->mut);
        if (d_debug) { cerr<<humanTimeNow()<<" There are now "<<d->queue.size()<<" messages queued for "<<d->dst<<endl; }
      }
      sleep(30);
    }
    catch(std::exception& e) {
      if (d_debug) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst<<" via "<<chosen.toStringWithPort()<<" had error: "<<e.what()<<endl; }
      {
        std::lock_guard<std::mutex> mut(d->mut);
        if (d_debug) { cerr<<humanTimeNow()<<" There are now "<<d->queue.size()<<" messages queued for "<<d->dst<<endl; }
      }
      sleep(1);
    }
    catch(...) {
      if (d_debug) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst <<" via "<<chosen.toStringWithPort()<<" had error"; }
      {
        std::lock_guard<std::mutex> mut(d->mut);
        if (d_debug) { cerr<<"There are now "<<d->queue.size()<<" messages queued for "<<d->dst<<" via "<<chosen.toStringWithPort()<<endl; }
      }
      sleep(1);
    }
  }
}


void NMMSender::emitNMM(const NavMonMessage& nmm)
{
  for(auto& d : d_dests) {
    d->emitNMM(nmm);
  }
}

void NMMSender::Destination::emitNMM(const NavMonMessage& nmm)
{
  string out;
  nmm.SerializeToString(& out);
  string msg("bert");
  
  uint16_t len = htons(out.size());
  msg.append((char*)&len, 2);
  msg.append(out);

  if(!dst.empty()) {
    std::lock_guard<std::mutex> l(mut);
    queue.push_back(msg);
  }
  else
    writen2(fd, msg.c_str(), msg.size());
}

