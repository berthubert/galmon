#include "nmmsender.hh"
#include <random>
#include "navmon.hh"
#include <algorithm>

#include <netinet/tcp.h>
using namespace std;

void NMMSender::sendLoop(Destination* d, SocketCommunicator& sc, std::unique_ptr<ZStdCompressor>& zsc, Socket& s, map<uint32_t, string>& unacked, time_t connStartTime)
{
  bool hadMessage=false;
  int msgnum = 0;

  for(;;) { 
    uint32_t num;
    // read acks
    for(; zsc ;) { // only do this for compressed protocol
      try {
        readn2(s, &num, 4); // this will give us 4, or throw
        num = ntohl(num);
        unacked.erase(num);
      }
      catch(EofException& ee) {
        throw std::runtime_error("EOF while reading acks");
      }
      catch(std::exception& e) {
        if(errno != EAGAIN)
          unixDie("Reading acknowledgements in nmmsender");
        break;
      }
    }

          
    std::string msg;
    {
      std::lock_guard<std::mutex> mut(d->mut);
      if(!d->queue.empty()) {
        msg = d->queue.front();
      }
    }
    if(!msg.empty()) {
      hadMessage=true;
      if(zsc) {              
        uint32_t num = htonl(msgnum);
        string encap((const char*)&num, 4);
        encap += msg;
        zsc->give(encap.c_str(), encap.size());
        unacked[msgnum] = msg;
        msgnum++;

      }
      else
        sc.writen(msg);
      std::lock_guard<std::mutex> mut(d->mut);
      d->queue.pop_front();

    }
    else {
      if(zsc && hadMessage) {
        //              cerr << "Compressed to: "<< 100.0*zsc->d_outputBytes/zsc->d_inputBytes<<"%, buffered compressed: "<<zsc->outputBufferBytes()<<" out of " <<zsc->outputBufferCapacity()<<" bytes. Unacked: "<<unacked.size()<<endl;

        zsc->flush();

        if(time(0) - connStartTime > 10 && unacked.size() > 1000)
          throw std::runtime_error("Too many messages unacked ("+to_string(unacked.size())+"), recycling connection");


              
      }
      hadMessage = false;
      if(d_pleaseQuit) 
        return;
      usleep(100000);
#if defined(TCP_CORK)
      /* linux-only: has an implied 200ms timeout */
      SSetsockopt(s, IPPROTO_TCP, TCP_CORK, 1 );
#elif defined(TCP_NOPUSH)
      /*
       * freebsd/osx: buffers until buffer full/connection closed, so
       * we toggle it every other loop through
       */
      static bool push_toggle;
      if (push_toggle) {
        SSetsockopt(s, IPPROTO_TCP, TCP_NOPUSH, 0 );
        SSetsockopt(s, IPPROTO_TCP, TCP_NOPUSH, 1 );
      }
      push_toggle = !push_toggle;
#endif

    }
  }
}


// this does all kinds of resolving based on a *string* destination
void NMMSender::sendTCPThread(Destination* d)
{
  struct NameError{};
  for(;;) {
    ComboAddress chosen;
    map<uint32_t, string> unacked;
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

#if !defined(TCP_CORK) && defined(TCP_NOPUSH)
	/* start off "buffering" */
        SSetsockopt(s, IPPROTO_TCP, TCP_NOPUSH, 1 );
#endif

        time_t connStartTime = time(0);
        if (d_debug) { cerr<<humanTimeNow()<<" Connected to "<<d->dst<<" on "<<addr.toStringWithPort()<<endl; }
        auto emit = [&sc](const char*buf, uint32_t len) {
          sc.writen(string(buf, len));
        };
        std::unique_ptr<ZStdCompressor> zsc;
        if(d_compress) {
          sc.writen("RNIE00000000"); // the other magic value is "bert". hence.
          // the 00000000 is a placeholder for a "secret" we might implement later
          zsc = std::make_unique<ZStdCompressor>(emit, 9);
        }

        sendLoop(d, sc, zsc, s, unacked, connStartTime);
      }
    }
    catch(NameError&) {
      {
        std::lock_guard<std::mutex> mut(d->mut);
        if (d_debug) { cerr<<humanTimeNow()<<" There are now "<<d->queue.size()<<" messages queued for "<<d->dst<<", and "<<unacked.size()<<" unacknowledged"<<endl; }
      }
      sleep(30);
    }
    catch(std::exception& e) {
      if (d_debug) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst<<" via "<<chosen.toStringWithPort()<<" had error: "<<e.what()<<endl; }
      {
        std::lock_guard<std::mutex> mut(d->mut);
        if (d_debug) { cerr<<humanTimeNow()<<" There are now "<<d->queue.size()<<" messages queued for "<<d->dst<<", and "<<unacked.size()<<" unacknowledged"<<endl; }
      }
      sleep(1);
    }
    catch(...) {
      if (d_debug) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst <<" via "<<chosen.toStringWithPort()<<" had error"; }
      {
        std::lock_guard<std::mutex> mut(d->mut);
        if (d_debug) { cerr<<"There are now "<<d->queue.size()<<" messages queued for "<<d->dst<<", and "<<unacked.size()<<" unacknowledge via "<<chosen.toStringWithPort()<<endl; }
      }
      sleep(1);
    }
    std::lock_guard<std::mutex> mut(d->mut);
    if(!unacked.empty()) {
      cerr<<humanTimeNow()<< " Stuffing "<<unacked.size()<<" messages back into the queue"<<endl;
      for(auto iter= unacked.rbegin(); iter != unacked.rend(); ++iter) {
        d->queue.push_front(iter->second);
      }
      unacked.clear();
    }
  }
}


void NMMSender::emitNMM(const NavMonMessage& nmm)
{
  string out;
  nmm.SerializeToString(& out);
  emitNMM(out);
}

void NMMSender::emitNMM(const std::string& out)
{
  for(auto& d : d_dests) {
    d->emitNMM(out, d_compress);
  }
}

/* the listener design. The listener has a thread that waits for connections.
   the listener is a normal 'destination'.
   It consumes its queue, and forwards messages to any connections made to it.
*/

void NMMSender::acceptorThread(Destination *d)
try
{
  cerr<<"Start of acceptor thread"<<endl;
  ComboAddress ca(d->dst);
  Socket l(ca.sin4.sin_family, SOCK_STREAM);
  SSetsockopt(l, SOL_SOCKET, SO_REUSEADDR, 1 );

  SBind(l, ca);
  SListen(l, 128);

  cerr<<"Made a listener on "<<ca.toStringWithPort()<<endl;
  NMMSender ns;

  
  std::thread t(&NMMSender::forwarderThread, this, d, &ns);
  t.detach();
  
  for(;;) {
    ComboAddress remote=ca;
    int fd = SAccept(l, remote);

    cout<<"Had a new connection from "<<remote.toStringWithPort()<<" on fd "<<fd<<endl;
    auto nd = std::make_unique<Destination>();
    nd->dst="source";
    ns.d_dests.push_back(std::move(nd));

    std::thread t(&NMMSender::sendTCPListenerThread, &ns, ns.d_dests.rbegin()->get(), fd, remote);
    t.detach();
  }

}
catch(std::exception& e) {
  cerr<<"Acceptor thread dying: "<<e.what()<<endl;
}

void NMMSender::forwarderThread(Destination *d, NMMSender* there)
{
  //  cout<<"Forwarder thread launched, this " << (void*)this<<" -> "<<(void*)there<<endl;
  std::string msg;

  for(;;) {
    {
      std::lock_guard<std::mutex> mut(d->mut);
      while(!d->queue.empty()) {
        //        cerr<<"Forwarded a message to "<< (void*)there<<endl;
        msg = d->queue.front();
        there->emitNMM(msg);
        d->queue.pop_front();
      }
    }
    usleep(100000);
  }
}

void NMMSender::sendTCPListenerThread(Destination* d, int fd, ComboAddress addr)
{
  cerr<<"sendTCPListenerThread launched on fd "<<fd<<" for "<<addr.toStringWithPort()<<", d_compress "<<d_compress<<endl;
  try {
    Socket s(fd);
    SocketCommunicator sc(s);
    time_t connStartTime = time(0);
    if (d_debug) { cerr<<humanTimeNow()<<" Connected to "<<d->dst<<" on "<<addr.toStringWithPort()<<endl; }
    auto emit = [&sc](const char*buf, uint32_t len) {
      sc.writen(string(buf, len));
    };
    std::unique_ptr<ZStdCompressor> zsc;
    if(d_compress) {
      sc.writen("RNIE00000000"); // the other magic value is "bert". hence.
      // the 00000000 is a placeholder for a "secret" we might implement later
      zsc = std::make_unique<ZStdCompressor>(emit, 9);
    }

    map<uint32_t, string> unacked;

    //    cerr<<"Entering sendloop"<<endl;
    sendLoop(d, sc, zsc, s, unacked, connStartTime);
  }
  catch(std::exception& e) {
    if (d_debug) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst<<" via "<<addr.toStringWithPort()<<" had error: "<<e.what()<<endl; }
  }
  catch(...) {
    if (d_debug) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst <<" via "<< addr.toStringWithPort()<<" had error"; }
    
  }
  // need a lock here, but I think think this is the right one
  std::lock_guard<std::mutex> mut(d->mut);
  cerr<<"Done with serving client "<<addr.toStringWithPort()<<": "<<d_dests.size() <<endl;

  d_dests.erase(remove_if(d_dests.begin(), d_dests.end(), [d](const auto& a)
  {
    //    cerr<<(void*) a.get()<< " ==? " <<(void*) d <<endl;
    return a.get() == d;
  }), d_dests.end());
  //  cerr<<"Size now: "<<d_dests.size()<<endl;
  // some kind of cleanup
}



void NMMSender::Destination::emitNMM(const std::string& out, bool compressed)
{
  string msg;
  // this bit is exceptionally tricky. We support multiple output formats
  // and somehow we do work on that here. This is very stupid.
  if(!listener) {
    if(dst.empty() || !compressed)
      msg="bert";
    
    uint16_t len = htons(out.size());
    msg.append((char*)&len, 2);
  }
  msg.append(out);

  if(!dst.empty() || listener) {
    std::lock_guard<std::mutex> l(mut);
    queue.push_back(msg);
  }
  else
    writen2(fd, msg.c_str(), msg.size());
}

