#include "comboaddress.hh"
#include "sclasses.hh"
#include <thread>
#include <signal.h>
#include "navmon.pb.h"
#include "fmt/format.h"
#include "fmt/printf.h"
#include <mutex>


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

std::mutex g_clientmut;
set<int> g_clients;

std::mutex g_dedupmut;

set<std::tuple<int, int, int, int>> g_dedup;

int g_store;

std::multimap<pair<uint32_t,uint32_t>, string> g_history;

void recvSession(int s, ComboAddress client)
{
  cerr<<"Receiving messages from "<<client.toStringWithPort()<<endl;
  for(;;) {
    string part=SRead(s, 4);
    if(part != "bert") {
      cerr << "Wrong magic!"<<endl;
      break;
    }
    string out=part;

    part = SRead(s, 2);
    out += part;
    
    uint16_t len;
    memcpy(&len, part.c_str(), 2);
    len = htons(len);

    part = SRead(s, len);
    out += part;
    
    NavMonMessage nmm;
    nmm.ParseFromString(part);
    //    cerr<<nmm.sourceid()<<" ";

    if(nmm.type() == NavMonMessage::GalileoInavType) {
      std::lock_guard<std::mutex> lg(g_dedupmut);
      if(g_dedup.count({nmm.gi().gnssid(), nmm.gi().gnsssv(), nmm.gi().gnsswn(), nmm.gi().gnsstow()})) {
        cerr<<"Dedupped message from "<< nmm.sourceid()<<" "<< fmt::format("{0} {1} {2} {3}", nmm.gi().gnssid(), nmm.gi().gnsssv(), nmm.gi().gnsswn(), nmm.gi().gnsstow()) << endl;
        continue;
      }
      cerr<<"New message from "<< nmm.sourceid()<<" "<< fmt::format("{0} {1} {2} {3}", nmm.gi().gnssid(), nmm.gi().gnsssv(), nmm.gi().gnsswn(), nmm.gi().gnsstow()) << endl;
      g_dedup.insert({nmm.gi().gnssid(), nmm.gi().gnsssv(), nmm.gi().gnsswn(), nmm.gi().gnsstow()});
    }
    else
      ; //      cerr<<"Not an inav message "<< (int) nmm.type()<<endl;

    g_history.insert({{nmm.localutcseconds(), nmm.localutcnanoseconds()}, out});
    for(const auto& fd : g_clients) {
      SWrite(fd, out);
    }
  }
}

void recvListener(Socket&& s, ComboAddress local)
{
  for(;;) {
    ComboAddress remote=local;
    int fd = SAccept(s, remote);
    std::thread t(recvSession, fd, remote);
    t.detach();
  }
}


void sendSession(int s, ComboAddress client)
{
  cerr<<"New downstream client "<<client.toStringWithPort() << endl;

  pair<uint32_t, uint32_t> start;
  start.first=time(0)-1800;
  start.second=0;

  int count =0;
  for(auto iter = g_history.lower_bound(start); iter != g_history.end(); ++iter) {
    SWriten(s, iter->second);
    ++count;
  }
  cerr<<"Wrote "<<count<<" historical messages"<<endl;
  
  g_clients.insert(s);
  char c;
  int res = read(s, &c, 1);
  g_clients.erase(s);
  close(s);
  cerr<<"Disconnect "<<client.toStringWithPort() << endl;
}

void sendListener(Socket&& s, ComboAddress local)
{
  for(;;) {
    ComboAddress remote=local;
    int fd = SAccept(s, remote);
    std::thread t(sendSession, fd, remote);
    t.detach();
  }

}


int main(int argc, char** argv)
{
  signal(SIGPIPE, SIG_IGN);

  g_store = open("permanent", O_CREAT | O_APPEND | O_WRONLY, 0666);
  if(g_store < 0) {
    cerr<<"Unable to open permanent storage file"<<endl;
    return(EXIT_FAILURE);
  }
    
  
  ComboAddress recvaddr("0.0.0.0", 29600);
  Socket receiver(recvaddr.sin4.sin_family, SOCK_STREAM, 0);
  SSetsockopt(receiver,SOL_SOCKET, SO_REUSEADDR, 1 );
  
  SBind(receiver, recvaddr);
  SListen(receiver, 128);

  thread recvThread(recvListener, std::move(receiver), recvaddr);
  recvThread.detach();

  ComboAddress sendaddr("0.0.0.0", 29601);
  Socket sender(sendaddr.sin4.sin_family, SOCK_STREAM);
  SSetsockopt(sender, SOL_SOCKET, SO_REUSEADDR, 1 );
  SBind(sender, sendaddr);
  SListen(sender, 128);
  
  thread sendThread(sendListener, std::move(sender), sendaddr);

  sendThread.detach();

  for(;;) {
    sleep(1);
  }

  
}
