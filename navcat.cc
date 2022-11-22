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
#include "navmon.hh"
// #include <execution>
#include "CLI/CLI.hpp"
#include "version.hh"

static char program[]="navcat";

using namespace std;

extern const char* g_gitHash;

// get all stations (numerical) from a directory
static vector<uint64_t> getSources(string_view dirname)
{
  shared_ptr<DIR> dir;
  if(auto ptr = opendir(&dirname[0]))
    dir=shared_ptr<DIR>(ptr, closedir);
  else
    unixDie("Listing metrics from statistics storage "+(string)dirname);
  struct dirent *result=0;
  vector<uint64_t> ret;
  for(;;) {
    errno=0;
    if(!(result = readdir(dir.get()))) {
      if(errno)
        unixDie("Reading directory entry "+(string)dirname);
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

static bool operator==(const timespec& a, const timespec& b)
{
  return a.tv_sec == b.tv_sec && a.tv_nsec && b.tv_nsec;
}

// send protobuf data from the named directories and stations, between start and stoptime
void sendProtobuf(const vector<string>& dirs, vector<uint64_t> stations, time_t startTime, time_t stopTime=0)
{
  timespec start;
  start.tv_sec = startTime;
  start.tv_nsec = 0;

  // so we have a ton of files, and internally these are not ordered
  vector<pair<timespec,string> > rnmms;

  for(;;) {
    rnmms.clear();
    for(const auto& dir : dirs) {
      cerr<<"Gathering data from "<<humanTime(start.tv_sec)<<" from "<<dir<<".. ";
          
      vector<uint64_t> srcs = stations.empty() ? getSources(dir) : stations;
      int count=0;
      for(const auto& src: srcs) {
        string fname = getPath(dir, start.tv_sec, src);
        FILE* ptr = fopen(fname.c_str(), "r");
        shared_ptr<FILE> fp;
        if(ptr) {
          fp = shared_ptr<FILE>(ptr, fclose);
        }
        else {
          fname+=".zst";
          struct stat statbuf;
          if(stat(fname.c_str(), &statbuf) == 0) {
            ptr = popen(("zstdcat "+fname).c_str(), "r");
            if(!ptr)
              continue;
            fp = shared_ptr<FILE>(ptr, pclose);
          }
          else
            continue;
        }
        
        string msg;
        struct timespec ts;
        unsigned int offset=0;
        while(getRawNMM(fp.get(), ts, msg, offset)) {
          // don't drop data that is only 5 seconds too old
          if(make_pair(ts.tv_sec + 5, ts.tv_nsec) >= make_pair(start.tv_sec, start.tv_nsec)) {
            rnmms.push_back({ts, msg});
            ++count;
          }
        }
        //      cerr<<"Harvested "<<rnmms.size()<<" events out of "<<looked<<endl;
        // fp will close itself
      }
      cerr<<" added "<<count<<endl;
    }
    //    cerr<<"Sorting data"<<endl;
    //    sort(execution::par, rnmms.begin(), rnmms.end(), [](const auto& a, const auto& b)
    sort(rnmms.begin(), rnmms.end(), [](const auto& a, const auto& b)
         {
           return std::tie(a.first.tv_sec, a.first.tv_nsec)
                < std::tie(b.first.tv_sec, b.first.tv_nsec);
         });

    auto newend=unique(rnmms.begin(), rnmms.end());
    cerr<<"Removed "<<rnmms.end() - newend <<" duplicates, ";
    
    rnmms.erase(newend, rnmms.end());
    cerr<<"sending data"<<endl;
    unsigned int count=0;
    for(const auto& nmm: rnmms) {
      if(nmm.first.tv_sec > stopTime)
        break;
      std::string buf="bert";
      uint16_t len = htons(nmm.second.size());
      buf.append((char*)(&len), 2);
      buf += nmm.second;
      //fwrite(buf.c_str(), 1, buf.size(), stdout);
      writen2(1, buf.c_str(), buf.size());
      ++count;
    }
    cerr<<"Done sending " << count<<" messages"<<endl;
    if(3600 + start.tv_sec - (start.tv_sec%3600) < stopTime)
      start.tv_sec = 3600 + start.tv_sec - (start.tv_sec%3600);
    else {
      break;
    }
  }
}


int main(int argc, char** argv)
{
  bool doVERSION{false};

  CLI::App app(program);
  string beginarg, endarg;
  vector<string> storages;
  int galwn{-1};
  app.add_option("--storage,-s", storages, "Locations of storage files");  
  vector<uint64_t> stations;
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--begin,-b", beginarg, "Begin time (2020-01-01 00:00, or 12:30)");
  app.add_option("--end,-e", endarg, "End time. Now if omitted");
  app.add_option("--stations", stations, "only send data from listed stations");
  app.add_option("--gal-wn", galwn, "Galileo week number to report on");
  CLI11_PARSE(app, argc, argv);

  
  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  time_t startTime, stopTime;
  if(galwn >=0) {
    startTime=utcFromGST(galwn, 0);
    stopTime=startTime + 7*86400;
  }
  else if(!beginarg.empty()) {
    startTime = parseTime(beginarg);
    stopTime = endarg.empty()  ? time(0) :  parseTime(endarg);
  }
  else {
    cerr<<"No time range specified, use -b or --gal-wn"<<endl;
    return 1;
  }

  cerr<<"Emitting from "<<humanTime(startTime) << " to " << humanTime(stopTime) << endl;
  if(!stations.empty()) {
    cerr<<"Restricting to stations:";
    for(const auto& s : stations)
      cerr<<" "<<s;
    cerr<<endl;
  }
  sendProtobuf(storages, stations, startTime, stopTime);
}
