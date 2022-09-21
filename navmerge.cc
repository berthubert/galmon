#include "sclasses.hh"
#include <map>
#include "navmon.hh"
#include "navmon.pb.h"
#include <thread>
#include <signal.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include "nmmsender.hh"
#include "CLI/CLI.hpp"
#include "version.hh"

using namespace std;

static char program[]="navmerge";

/* ubxtool/rtcmtool/septool deliver data to one of several `navrecv` instances
   This means we need a 'merge' tool.

   The merge tool should be able to stream data from multiple `navnexus` instances 
      (that correspond to the `navrecv` instances)

   Currently, `navnexus` is really simple - it will send you a feed from x hours back, where 
   you don't get to pick x.

   The simplest navmerge implementation does nothing but connect to a few navnexus instances
   and it mixes them together.

   Every message "should" only appear on one of the upstreams, but you never know. 

   On initial connection, the different navnexuses may start up from a different time, currently.
   Let us state that This Should Not Happen.

   On initial connect, a navnexus might take dozens of seconds before it starts coughing up data. 

   Initial goal for navmerge is: only make sure realtime works. 

   Every upstream has a thread that loops trying to connect
   If a new message comes in, it is stored in a shared data structure
   If a new connect is made, set a "don't send" marker for a whole minute

   There is a sender thread that periodically polls this data structure
   Any data that is older than the previous high-water mark gets sent out & removed from structure
   However, transmission stops 10 seconds before realtime
   If a "don't send" marker is set, we don't do a thing

*/

multimap<pair<uint64_t, uint64_t>, string> g_buffer;
std::mutex g_mut;

// navmerge can also dedup its output, we keep track of recent messages here
// this means each Galileo message will only get set once
map<tuple<uint32_t, std::string, uint32_t, std::string, int16_t>, time_t> g_seen;

bool g_inavdedup{false};

/* Goal: do a number of TCP operations that have a combined timeout.
   maybe some helper:

   auto deadline = xSecondsFromNow(1.5);
   SConnectWithDeadline(sock, addr, deadline);
   resp=SReadWithDeadline(sock, 2, deadline);
   ..
   resp2=SReadWithDeadline(sock, num, deadline);


   //
   // not getting 'num' bytes -> error
   // exceeding timeout before getting 'num' bytes -> error


*/

auto xSecondsFromNow(double seconds)
{
  auto now = chrono::steady_clock::now();
  now += std::chrono::milliseconds((unsigned int)(seconds*1000));
  return now;
}

int msecLeft(const std::chrono::steady_clock::time_point& deadline)
{
  auto now = chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
}


string SReadWithDeadline(int sock, int num, const std::chrono::steady_clock::time_point& deadline)
{
  string ret;
  char buffer[1024];
  std::string::size_type leftToRead=num;
  
  for(; leftToRead;) {
    auto now = chrono::steady_clock::now();
    
    auto msecs = chrono::duration_cast<chrono::milliseconds>(deadline-now);
    if(msecs.count() <= 0) 
      throw std::runtime_error("Timeout");

    double toseconds = msecs.count()/1000.0;
    int res = waitForRWData(sock, true, &toseconds); // 0 = timeout, 1 = data, -1 error
    if(res == 0)
      throw std::runtime_error("Timeout");
    if(res < 0)
      throw std::runtime_error("Reading with deadline: "+string(strerror(errno)));

    auto chunk = sizeof(buffer) < leftToRead ? sizeof(buffer) : leftToRead;
    res = read(sock, buffer, chunk);
    if(res < 0)
      throw std::runtime_error(fmt::sprintf("Read from socket: %s", strerror(errno)));
    if(!res)
      throw std::runtime_error(fmt::sprintf("Unexpected EOF"));
    ret.append(buffer, res);
    leftToRead -= res;
  }
  return ret;
}

void recvSession(ComboAddress upstream)
{
  for(;;) {
    try {
      Socket sock(upstream.sin4.sin_family, SOCK_STREAM);
      cerr<<"Connecting to "<< upstream.toStringWithPort()<<" to source data..";
      SConnectWithTimeout(sock, upstream, 5);
      cerr<<" done"<<endl;

      for(int count=0;;++count) {
        auto deadline = xSecondsFromNow(600); // 
        string part=SReadWithDeadline(sock, 4, deadline);
        if(part.empty()) {
          cerr<<"EOF from "<<upstream.toStringWithPort()<<endl;
          break;
        }
        if(part != "bert") {
          cerr << "Message "<<count<<", wrong magic from "<<upstream.toStringWithPort()<<": "<<makeHexDump(part)<<endl;
          break;
        }
        if(!count)
          cerr<<"Receiving messages from "<<upstream.toStringWithPort()<<endl;
        string out=part;
        
        part = SReadWithDeadline(sock, 2, deadline);
        out += part;
        
        uint16_t len;
        memcpy(&len, part.c_str(), 2);
        len = htons(len);
        
        part = SReadWithDeadline(sock, len, deadline);  // XXXXX ???
        if(part.size() != len) {
          cerr<<"Mismatch, "<<part.size()<<", len "<<len<<endl;
          // XX AND THEN WHAT??
        }
        out += part;
        //        if(msecLeft(deadline)/1000.0 < 119)
        //          cerr<<"Done with "<<msecLeft(deadline)/1000.0<<" seconds left\n";
        NavMonMessage nmm;
        nmm.ParseFromString(part);

        if(g_inavdedup) {
          if(nmm.type() == NavMonMessage::GalileoInavType) {
            std::lock_guard<std::mutex> mut(g_mut);
            decltype(g_seen)::key_type tup(nmm.gi().gnsssv(), nmm.gi().contents(), nmm.gi().sigid(), nmm.gi().reserved1(),nmm.gi().has_ssp() ? nmm.gi().ssp() : -1);
            
            if(!g_seen.count(tup))
              g_buffer.insert({{nmm.localutcseconds(), nmm.localutcnanoseconds()}, part});
            g_seen[tup]=time(0);
          }
        }
        else {
          std::lock_guard<std::mutex> mut(g_mut);
          g_buffer.insert({{nmm.localutcseconds(), nmm.localutcnanoseconds()}, part});
        }
      }
    }
    catch(std::exception& e) {
      cerr<<"Error in receiving thread: "<<e.what()<<endl;
      sleep(1);
    }
  }
  cerr<<"Thread for "<<upstream.toStringWithPort()<< " exiting"<<endl;
}

static void cleanFilter()
{
  time_t lim = time(0) - 60;
  std::lock_guard<std::mutex> mut(g_mut);
  for(auto iter = g_seen.begin(); iter!= g_seen.end() ;) {
    if(iter->second < lim)
      iter = g_seen.erase(iter);
    else
      ++iter;
  }
}

int main(int argc, char** argv)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  vector<string> destinations;
  vector<string> sources;
  vector<string> listeners;

  bool doVERSION{false}, doSTDOUT{false};
  CLI::App app(program);
  app.add_option("--source", sources, "Connect to these IP address:port to source protobuf");
  app.add_option("--destination,-d", destinations, "Send output to this IPv4/v6 address");
  app.add_option("--listener,-l", listeners, "Make data available on this IPv4/v6 address");
  app.add_flag("--inavdedup", g_inavdedup, "Only pass on Galileo I/NAV, and dedeup");  
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_flag("--stdout", doSTDOUT, "Emit output to stdout");

  CLI11_PARSE(app, argc, argv);
  
  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  if(sources.empty()) {
    cerr<< "No sources defined. Exiting."<<endl;
    exit(0);
  }

  signal(SIGPIPE, SIG_IGN);
  NMMSender ns;

  ns.d_debug = true;
  for(const auto& s : destinations) {
    auto res=resolveName(s, true, true);
    if(res.empty()) {
      cerr<<"Unable to resolve '"<<s<<"' as destination for data, exiting"<<endl;
      exit(EXIT_FAILURE);
    }
    ns.addDestination(s); // ComboAddress(s, 29603));
  }
  for(const auto& l : listeners) {
    ComboAddress ca(l, 29604);
    cerr<<"Adding listener on "<<ca.toStringWithPort()<<endl;
    ns.addListener(l); // ComboAddress(s, 29603));
  }
  
  if(doSTDOUT)
    ns.addDestination(1);

  
  for(const auto& s : sources) {
    ComboAddress oneaddr(s, 29601);
    std::thread one(recvSession, oneaddr);
    one.detach();
  }

  ns.launch();
  
  time_t start=time(0);
  int counter=0;
  for(;;) {
    usleep(500000);
    vector<string> tosend;
    {
      std::lock_guard<std::mutex> mut(g_mut);

      time_t now = time(0);
      if(now - start < 30) { // was 30
        cerr<<"Have "<<g_buffer.size()<<" messages"<<endl;
        continue;
      }

      for(auto iter = g_buffer.begin(); iter != g_buffer.end(); ) {
        if(iter->first.first > (uint64_t)now - 5)
          break;
        
        tosend.push_back(iter->second);
        iter = g_buffer.erase(iter);
      }
    }
    // cerr<<"Have "<<tosend.size()<<" messages to send, "<<g_buffer.size()<<" left in queue"<<endl;
    std::string buf;
    for(const auto& m : tosend) {
      if(!((counter++) % 32768)) 
        cleanFilter();
      ns.emitNMM(m);
    }
  }
}
