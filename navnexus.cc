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

#include "CLI/CLI.hpp"
#include "version.hh"

static char program[]="navnexus";

using namespace std;

extern const char* g_gitHash;

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

  timespec start;
  start.tv_sec = startTime;
  start.tv_nsec = 0;

  // so we have a ton of files, and internally these are not ordered
  map<string,uint32_t> fpos;
  vector<pair<timespec,string> > rnmms;
  for(;;) {
    auto srcs = getSources();
    rnmms.clear();
    for(const auto& src: srcs) {
      string fname = getPath(g_storage, start.tv_sec, src);
      int fd = open(fname.c_str(), O_RDONLY);
      if(fd < 0)
        continue;
      uint32_t offset= fpos[fname];
      if(lseek(fd, offset, SEEK_SET) < 0) {
        cout<<"Error seeking: "<<strerror(errno) <<endl;
        close(fd);
        continue;
      }
      //      cout <<"Seeked to position "<<fpos[fname]<<" of "<<fname<<endl;
      NavMonMessage nmm;

      uint32_t looked=0;
      string msg;
      struct timespec ts;

      while(getRawNMM(fd, ts, msg, offset)) {
        // don't drop data that is only 5 seconds too old
        if(make_pair(ts.tv_sec + 5, ts.tv_nsec) >= make_pair(start.tv_sec, start.tv_nsec)) {
          rnmms.push_back({ts, msg});
        }
        ++looked;
      }
      //      cout<<"Harvested "<<rnmms.size()<<" events out of "<<looked<<endl;
      fpos[fname]=offset;
      close(fd);
    }
    //    cout<<"Sorting.. ";
    //    cout.flush();
    sort(rnmms.begin(), rnmms.end(), [](const auto& a, const auto& b)
         {
           return std::tie(a.first.tv_sec, a.first.tv_nsec)
             < std::tie(b.first.tv_sec, b.first.tv_nsec);
         });
    //    cout<<"Sending.. ";
    //    cout.flush();
    for(const auto& nmm: rnmms) {
      std::string buf="bert";
      uint16_t len = htons(nmm.second.size());
      buf.append((char*)(&len), 2);
      buf += nmm.second;
      SWriten(clientfd, buf);
    }
    //    cout<<"Done"<<endl;
    if(3600 + start.tv_sec - (start.tv_sec % 3600) < time(0))
      start.tv_sec = 3600 + start.tv_sec - (start.tv_sec % 3600);
    else {
      if(!rnmms.empty())
        start = {rnmms.rbegin()->first.tv_sec, rnmms.rbegin()->first.tv_nsec};
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
  bool doVERSION{false};

  CLI::App app(program);
  string localAddress("127.0.0.1");
  int hours = 4;   
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--bind,-b", localAddress, "Address:port to bind to");
  app.add_option("--storage,-s", g_storage, "Location of storage files");  
  app.add_option("--hours", hours, "Number of hours of backlog to replay");
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  signal(SIGPIPE, SIG_IGN);
  
  ComboAddress sendaddr(localAddress, 29601);

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
