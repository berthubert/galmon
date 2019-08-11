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
#include <unistd.h>
#include <stdexcept>
#include <sys/types.h>
#include "storage.hh"
#include <dirent.h>

using namespace std;

std::mutex g_clientmut;
set<int> g_clients;

std::mutex g_dedupmut;

set<std::tuple<int, int, int, int>> g_dedup;

std::string g_storage;

std::multimap<pair<uint32_t,uint32_t>, string> g_history;


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

void unixDie(const std::string& str)
{
  throw std::runtime_error(str+string(": ")+string(strerror(errno)));
}

vector<uint64_t> getSources()
{
  DIR *dir = opendir(g_storage.c_str());
  if(!dir)
    unixDie("Listing metrics from statistics storage "+g_storage);
  struct dirent *result=0;
  vector<uint64_t> ret;
  for(;;) {
    errno=0;
    if(!(result = readdir(dir))) {
      closedir(dir);
      if(errno)
        unixDie("Reading directory entry "+g_storage);
      else
        break;
    }
    if(result->d_name[0] != '.') {
      uint64_t src;
      if(sscanf(result->d_name, "%08lx", &src)==1)
        ret.push_back(src);
    }
  }

  sort(ret.begin(), ret.end());
  return ret;
}


int main(int argc, char** argv)
{
  signal(SIGPIPE, SIG_IGN);
  if(argc != 3) {
    cout<<"Syntax: navnexus storage listen-address"<<endl;
    return(EXIT_FAILURE);
  }
  g_storage=argv[1];
  for(;;) {
    auto srcs = getSources();
    for(const auto& s: srcs) {
      time_t t = time(0);
      
      cout<<s <<" -> "<<getPath(g_storage, t, s) << " & " << getPath(g_storage, t-3600, s) <<  endl;
    }
    sleep(5);
  }
    
#if 0
  ComboAddress sendaddr(argv[2], 29601);
  Socket sender(sendaddr.sin4.sin_family, SOCK_STREAM);
  SSetsockopt(sender, SOL_SOCKET, SO_REUSEADDR, 1 );
  SBind(sender, sendaddr);
  SListen(sender, 128);
  
  thread sendThread(sendListener, std::move(sender), sendaddr);

  sendThread.detach();
#endif   

  
}
