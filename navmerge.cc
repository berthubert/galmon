#include "sclasses.hh"
#include <map>
#include "navmon.hh"
#include "navmon.pb.h"
#include <thread>
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

void recvSession(ComboAddress upstream)
{
  for(;;) {
    try {
      Socket sock(upstream.sin4.sin_family, SOCK_STREAM);
      cerr<<"Connecting to "<< upstream.toStringWithPort()<<" to source data..";
      SConnectWithTimeout(sock, upstream, 5);
      cerr<<" done"<<endl;

      for(int count=0;;++count) {
        string part=SRead(sock, 4);
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
        
        part = SRead(sock, 2);
        out += part;
        
        uint16_t len;
        memcpy(&len, part.c_str(), 2);
        len = htons(len);
        
        part = SRead(sock, len);  // XXXXX ???
        if(part.size() != len) {
          cerr<<"Mismatch, "<<part.size()<<", len "<<len<<endl;
          // XX AND THEN WHAT??
        }
        out += part;
      
        NavMonMessage nmm;
        nmm.ParseFromString(part);
        // writeToDisk(nmm.localutcseconds(), nmm.sourceid(), out);
        // do something with the message
        
        std::lock_guard<std::mutex> mut(g_mut);
        g_buffer.insert({{nmm.localutcseconds(), nmm.localutcnanoseconds()}, part});
      }
    }
    catch(std::exception& e) {
      cerr<<"Error in receiving thread: "<<e.what()<<endl;
      sleep(1);
    }
  }
  cerr<<"Thread for "<<upstream.toStringWithPort()<< " exiting"<<endl;
}

int main(int argc, char** argv)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  vector<string> destinations;
  vector<string> sources;

  bool doVERSION{false}, doSTDOUT{false};
  CLI::App app(program);
  app.add_option("--source", sources, "Connect to these IP address:port to source protobuf");
  app.add_option("--destination,-d", destinations, "Send output to this IPv4/v6 address");
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_flag("--stdout", doSTDOUT, "Emit output to stdout");

  CLI11_PARSE(app, argc, argv);
  
  if(doVERSION) {
    showVersion(program, g_gitHash);
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
  if(doSTDOUT)
    ns.addDestination(1);

  
  for(const auto& s : sources) {
    ComboAddress oneaddr(s, 29601);
    std::thread one(recvSession, oneaddr);
    one.detach();
  }

  time_t start=time(0);
  for(;;) {
    sleep(1);
    vector<string> tosend;
    {
      std::lock_guard<std::mutex> mut(g_mut);

      time_t now = time(0);
      if(now - start < 30) {
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
    cerr<<"Have "<<tosend.size()<<" messages to send, "<<g_buffer.size()<<" left in queue"<<endl;
    std::string buf;
    for(const auto& m : tosend) {
      ns.emitNMM(m);
    }
  }
}
