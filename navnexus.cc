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

std::string g_storage;

std::multimap<pair<uint32_t,uint32_t>, string> g_history;


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


void sendSession(int clientfd, ComboAddress client)
try
{
  cerr<<"New downstream client "<<client.toStringWithPort() << endl;

  pair<uint64_t, uint64_t> start = {0,0};
  start.first = time(0) - 1800;


  map<string,uint32_t> fpos;
  for(;;) {
    auto srcs = getSources();
    vector<NavMonMessage> nmms;
    for(const auto& s: srcs) {
      time_t t = time(0);
      
      cout<<s <<" -> "<<getPath(g_storage, t, s) << " & " << getPath(g_storage, t-3600, s) <<  endl;

      string fname = getPath(g_storage, t, s);
      int fd = open(fname.c_str(), O_RDONLY);
      if(fd < 0)
        continue;
      uint32_t offset= fpos[fname];
      if(lseek(fd, offset, SEEK_SET) < 0) {
        cout<<"Error seeking: "<<strerror(errno) <<endl;
        close(fd);
        continue;
      }
      cout <<"Seeked to position "<<fpos[fname]<<" of "<<fname<<endl;
      NavMonMessage nmm;

      uint32_t looked=0;
      while(getNMM(fd, nmm, offset)) {
        if(make_pair(nmm.localutcseconds(), nmm.localutcnanoseconds()) > start) {
          nmms.push_back(nmm);
        }
        ++looked;
      }
      cout<<"Harvested "<<nmms.size()<<" events out of "<<looked<<endl;
      fpos[fname]=offset;
      close(fd);
    }
    sort(nmms.begin(), nmms.end(), [](const auto& a, const auto& b)
         {
           return make_pair(a.localutcseconds(), b.localutcnanoseconds()) <
             make_pair(b.localutcseconds(), b.localutcnanoseconds());
         });

    for(const auto& nmm: nmms) {
      std::string out;
      nmm.SerializeToString(&out);
      std::string buf="bert";
      uint16_t len = htons(out.size());
      buf.append((char*)(&len), 2);
      buf+=out;
      SWriten(clientfd, buf);
    }
    if(!nmms.empty())
      start = {nmms.rbegin()->localutcseconds(), nmms.rbegin()->localutcnanoseconds()};
    sleep(1);
  }
}
 catch(std::exception& e) {
   cerr<<"Sender thread died: "<<e.what()<<endl;
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
  if(argc != 3) {
    cout<<"Syntax: navnexus storage listen-address"<<endl;
    return(EXIT_FAILURE);
  }
  g_storage=argv[1];
    
  ComboAddress sendaddr(argv[2], 29601);
  cout<<"Listening on "<<sendaddr.toStringWithPort()<<", storage: "<<g_storage<<endl;
  Socket sender(sendaddr.sin4.sin_family, SOCK_STREAM);
  SSetsockopt(sender, SOL_SOCKET, SO_REUSEADDR, 1 );
  SBind(sender, sendaddr);
  SListen(sender, 128);
  
  thread sendThread(sendListener, std::move(sender), sendaddr);

  sendThread.detach();

  for(;;) {
    sleep(5);
  }

  
}
