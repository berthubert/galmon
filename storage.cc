#include "storage.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <iostream>
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
  ret.push_back(to_string(tm.tm_mday));
  ret.push_back(to_string(tm.tm_hour)+".pb");
  return ret;
}

bool getNMM(int fd, NavMonMessage& nmm, uint32_t& offset)
{
  char bert[4];
  if(read(fd, bert, 4) != 4 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
    return false;
  }
    
  uint16_t len;
  if(read(fd, &len, 2) != 2)
    return false;
  len = htons(len);
  char buffer[len];
  if(read(fd, buffer, len) != len)
    return false;
    
  nmm.ParseFromString(string(buffer, len));
  offset += 4 + 2 + len;
  return true;
}

bool getNMM(FILE* fp, NavMonMessage& nmm, uint32_t& offset)
{
  char bert[4];
  if(fread(bert, 1, 4, fp) != 4 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
    return false;
  }
    
  uint16_t len;
  if(fread(&len, 1, 2, fp) != 2)
    return false;
  len = htons(len);
  char buffer[len];
  if(fread(buffer, 1, len, fp) != len)
    return false;
    
  nmm.ParseFromString(string(buffer, len));
  offset += 4 + 2 + len;
  return true;
}

bool getRawNMM(int fd, timespec& t, string& raw, uint32_t& offset)
{
  char bert[4];
  int res;
  if((res=read(fd, bert, 4)) != 4 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
    if(res != 4)
      return false;

    for(int s=0;; ++s ) {
      cerr<<"Skipping character hunting for good magic.. "<<s<<endl;
      bert[0] = bert[1];
      bert[1] = bert[2];
      bert[2] = bert[3];
      res = read(fd, bert + 3, 1);
      if(res != 1)
        return false;
      if(bert[0]=='b' && bert[1]=='e' && bert[2] =='r' && bert[3]=='t')
        break;
    }
  }
    
    
  uint16_t len;
  if(read(fd, &len, 2) != 2)
    return false;
  len = htons(len);
  char buffer[len];
  if(read(fd, buffer, len) != len)
    return false;

  NavMonMessage nmm;
  raw.assign(buffer, len);
  nmm.ParseFromString(raw);
  t.tv_sec = nmm.localutcseconds();
  t.tv_nsec = nmm.localutcnanoseconds();
  offset += 4 + 2 + len;
  return true;
}



bool getRawNMM(FILE* fp, timespec& t, string& raw, uint32_t& offset)
{
  char bert[4];
  int res;
  if((res=fread(bert, 1, 4, fp)) != 4 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
    if(res != 4) {
      //      cerr<<"EOF"<<endl;
      return false;
    }

    for(int s=0;; ++s ) {
      cerr<<"Skipping character hunting for good magic.. "<<s<<endl;
      bert[0] = bert[1];
      bert[1] = bert[2];
      bert[2] = bert[3];
      res = fread(bert+3, 1,1, fp);
      if(res != 1)
        return false;
      if(bert[0]=='b' && bert[1]=='e' && bert[2] =='r' && bert[3]=='t') {
        cerr<<"Resync!"<<endl;
        break;
      }
    }
  }
    
  uint16_t len;
  if(fread(&len, 1, 2, fp) != 2)
    return false;
  len = htons(len);
  char buffer[len];
  if(fread(buffer, 1, len, fp) != len)
    return false;
  NavMonMessage nmm;
  raw.assign(buffer, len);
  nmm.ParseFromString(raw);
  t.tv_sec = nmm.localutcseconds();
  t.tv_nsec = nmm.localutcnanoseconds();
  offset += 4 + 2 + len;
  return true;
}
