#include "storage.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;


std::string getPath(std::string_view root, time_t s, uint64_t sourceid, bool create)
{
  auto comps = getPathComponents(root, s, sourceid);
  std::string path;
  for(unsigned int pos = 0; pos < comps.size() - 1 ; ++pos) {
    path += comps[pos] +"/";
    if(create)
      mkdir(path.c_str(), 0770);
  }
  path += comps[comps.size()-1]+".gnss";
  return path;
}


vector<string> getPathComponents(std::string_view root, time_t s, uint64_t sourceid)
{
  // path: source/year/month/day/hour.pb
  vector<string> ret;
  ret.push_back((string)root);
  ret.push_back(fmt::sprintf("%08x", sourceid));
  
  struct tm tm;
  gmtime_r(&s, &tm);

  ret.push_back(to_string(tm.tm_year+1900));
  ret.push_back(to_string(tm.tm_mon+1));
  ret.push_back(to_string(tm.tm_mday+1));
  ret.push_back(to_string(tm.tm_hour)+".pb");
  return ret;
}
