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

#include "CLI/CLI.hpp"
#include "version.hh"

static char program[]="navcat";

using namespace std;

extern const char* g_gitHash;

time_t parseTime(std::string_view in)
{
  time_t now=time(0);

  vector<string> formats({"%Y-%m-%d %H:%M", "%Y%m%d %H%M", "%H:%M", "%H%M"});
  for(const auto& f : formats) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    localtime_r(&now, &tm);
    tm.tm_isdst = -1;
    tm.tm_sec = 0;
    char* res = strptime(&in[0], f.c_str(), &tm);
    if(res && !*res) {
      cerr<<"Matched on "<<f<<endl;
      return mktime(&tm);
    }
  }
  
  throw runtime_error("Can only parse %Y-%m-%d %H:%M");
}


vector<uint64_t> getSources(string_view dirname)
{
  DIR *dir = opendir(&dirname[0]);
  if(!dir)
    unixDie("Listing metrics from statistics storage "+(string)dirname);
  struct dirent *result=0;
  vector<uint64_t> ret;
  for(;;) {
    errno=0;
    if(!(result = readdir(dir))) {
      closedir(dir);
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


void sendProtobuf(string_view dir, time_t startTime, time_t stopTime=0)
{
  timespec start;
  start.tv_sec = startTime;
  start.tv_nsec = 0;

  // so we have a ton of files, and internally these are not ordered
  map<string,uint32_t> fpos;
  vector<pair<timespec,string> > rnmms;

  for(;;) {
    cerr<<"Gathering data"<<endl;
    auto srcs = getSources(dir);
    rnmms.clear();
    for(const auto& src: srcs) {
      string fname = getPath(dir, start.tv_sec, src);
      FILE* fp = fopen(fname.c_str(), "r");
      if(!fp)
        continue;
      uint32_t offset= fpos[fname];
      if(fseek(fp, offset, SEEK_SET) < 0) {
        cerr<<"Error seeking: "<<strerror(errno) <<endl;
        fclose(fp);
        continue;
      }
      //      cerr <<"Seeked to position "<<fpos[fname]<<" of "<<fname<<endl;

      uint32_t looked=0;
      string msg;
      struct timespec ts;
      while(getRawNMM(fp, ts, msg, offset)) {
        // don't drop data that is only 5 seconds too old
        if(make_pair(ts.tv_sec + 5, ts.tv_nsec) >= make_pair(start.tv_sec, start.tv_nsec)) {
          rnmms.push_back({ts, msg});
        }
        ++looked;
      }
      //      cerr<<"Harvested "<<rnmms.size()<<" events out of "<<looked<<endl;
      fpos[fname]=offset;
      fclose(fp);
    }

    cerr<<"Sorting data"<<endl;
    sort(rnmms.begin(), rnmms.end(), [](const auto& a, const auto& b)
         {
           return std::tie(a.first.tv_sec, a.first.tv_nsec)
                < std::tie(b.first.tv_sec, b.first.tv_nsec);
         });
    cerr<<"Sending data"<<endl;
    for(const auto& nmm: rnmms) {
      if(nmm.first.tv_sec > stopTime)
        break;
      std::string buf="bert";
      uint16_t len = htons(nmm.second.size());
      buf.append((char*)(&len), 2);
      buf += nmm.second;
      //fwrite(buf.c_str(), 1, buf.size(), stdout);
      writen2(1, buf.c_str(), buf.size());
    }
    cerr<<"Done sending"<<endl;
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
  /*
  CLI::App app(program);

  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.allow_extras(true); // allow bare positional parameters
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }
  */
  signal(SIGPIPE, SIG_IGN);
  if(argc < 3) {
    cout<<"Syntax: navcat storage start stop"<<endl;
    cout<<"Example: ./navcat storage \"2020-01-01 00:00\" \"2020-01-02 00:00\" | ./navdump  "<<endl;
    return(EXIT_FAILURE);
  }
  time_t startTime = parseTime(argv[2]);
  time_t stopTime = parseTime(argv[3]);

  cerr<<"Emitting from "<<humanTime(startTime) << " to " << humanTime(stopTime) << endl;
  sendProtobuf(argv[1], startTime, stopTime);
  
}
