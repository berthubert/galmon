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
#include <inttypes.h>

using namespace std;

std::mutex g_clientmut;
set<int> g_clients;

std::string g_storage;

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
      if(sscanf(result->d_name, "%08" PRIx64, &src)==1)
        ret.push_back(src);
    }
  }

  sort(ret.begin(), ret.end());
  return ret;
}

void sendSession(int clientfd, ComboAddress client, time_t startTime, time_t stopTime=0)
try
{
  cerr<<"New downstream client "<<client.toStringWithPort() << endl;

  pair<uint64_t, uint64_t> start = {startTime, 0};

  // so we have a ton of files, and internally these are not ordered
  map<string,uint32_t> fpos;
  vector<NavMonMessage> nmms;
  for(;;) {
    auto srcs = getSources();
    nmms.clear();
    for(const auto& src: srcs) {
      string fname = getPath(g_storage, start.first, src);
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
        // don't drop data that is only 5 seconds too old
        if(make_pair(nmm.localutcseconds() + 5, nmm.localutcnanoseconds()) >= start) {
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
    if(3600 + start.first - (start.first%3600) < time(0))
      start.first = 3600 + start.first - (start.first%3600);
    else {
      if(!nmms.empty())
        start = {nmms.rbegin()->localutcseconds(), nmms.rbegin()->localutcnanoseconds()};
      sleep(1);
    }
  }
}
 catch(std::exception& e) {
   cerr<<"Sender thread died: "<<e.what()<<endl;
 }

void sendListener(Socket&& s, ComboAddress local, int hours)
{
  for(;;) {
    ComboAddress remote=local;
    int fd = SAccept(s, remote);
    std::thread t(sendSession, fd, remote, time(0) - hours  * 3600, 0);
    t.detach();
  }
}



int main(int argc, char** argv)
{
  signal(SIGPIPE, SIG_IGN);
  if(argc < 3) {
    cout<<"Syntax: navnexus storage listen-address [backlog-hours]"<<endl;
    return(EXIT_FAILURE);
  }
  g_storage=argv[1];
    
  ComboAddress sendaddr(argv[2], 29601);
  int hours = argc > 3 ? atoi(argv[3]) : 4;
  cout<<"Listening on "<<sendaddr.toStringWithPort()<<", backlog "<<hours<<" hours, storage: "<<g_storage<<endl;
  Socket sender(sendaddr.sin4.sin_family, SOCK_STREAM);
  SSetsockopt(sender, SOL_SOCKET, SO_REUSEADDR, 1 );
  SBind(sender, sendaddr);
  SListen(sender, 128);
  
  thread sendThread(sendListener, std::move(sender), sendaddr, hours);

  sendThread.detach();

  for(;;) {
    sleep(5);
  }

  
}
