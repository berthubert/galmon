#include <iostream>
#include "rinex.hh"
#include "navmon.hh"
#include <map>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>
#include <vector>
#include <deque>
#include <thread>
#include <future>
#include "ephemeris.hh"
#include <set>

using namespace std;

struct svstat
{
  int health{0};
  int napa{0};
  int stale{0};
};

template<typename T>
struct HanderOuter
{
  HanderOuter(const std::deque<T>& things) : d_things(things)
  {}

  
  bool getOne(T& str)
  {
    std::lock_guard<std::mutex> mut(d_mut);
    if(d_things.empty())
      return false;
    str = d_things.front();
    d_things.pop_front();
    return true;
  }
  std::deque<T> d_things;

  std::mutex d_mut;
  
};

typedef  map<SatID, map<time_t, svstat>> stat_t;

auto worker(HanderOuter<string>* ho)
{
  std::string file;
  auto stat = std::make_unique<stat_t>();
  while(ho->getOne(file)) {
    try {
      RINEXReader rr(file);
      RINEXEntry e;
  
      while(rr.get(e)) {
        if(e.gnss != 2 || e.sv == 14 || e.sv == 18)
          continue;
        SatID sid{(uint32_t)e.gnss, (uint32_t)e.sv, 0};
        auto& h=(*stat)[sid][e.t/3600];
        
        if(e.sisa < 0) {
          h.napa++;
        }
        
        if(e.health) {
          h.health++;
        }


        if(fabs(ephAge(e.tow, e.toe)) > 4*3600) {
          //          cout << humanTime(e.t)<<": "<<ephAge(e.tow, e.toe)<<" tow "<< e.tow<<" toe "<< e.toe << endl;
          h.stale++;
        }
          
      }
    }
    catch(std::exception& e) {
      cerr<<"Error processing file "<<file<<": "<<e.what()<<endl;
    }
  }
  return std::move(stat);
}

int main(int argc, char** argv)
{
  ifstream filefile(argv[1]);
  string fname;
  deque<string> files;
  while(getline(filefile, fname))
    files.push_back(fname);

  HanderOuter<string> ho(files);

  vector<std::future<std::unique_ptr<stat_t>>> futs;
  for(int n=0; n < 5; ++n)
    futs.push_back(std::async(worker, &ho));
  
  stat_t stat;
  for(auto& f : futs) {
    auto s = f.get();
    for(const auto& e : *s) {
      for(const auto& h : e.second) {
        auto& u = stat[e.first][h.first];
        u.napa += h.second.napa;
        u.health += h.second.health;
        u.stale += h.second.stale;
      }
    }
  }
  
  int totnapa{0}, tothours{0}, totstale{0}, totissue{0};
  int tothealth{0};
  ofstream dump("dump");
  set<time_t> hours;
  for(const auto& sv : stat) {
    cout<<makeSatPartialName(sv.first)<<": "<<sv.second.size()<<" hours, ";
    time_t start = 3600*sv.second.begin()->first;
    time_t stop = 3600*sv.second.rbegin()->first;
    cout<<humanTime(start) <<" - " << humanTime(stop) <<": ";
      
      
    int napa=0, health=0, stale=0, issue=0;
    for(const auto& h : sv.second) {
      hours.insert(h.first);
      if(sv.first.sv==33)
        dump<<humanTime(3600 * h.first)<<" napa "<<h.second.napa<<" health "<<h.second.health <<" stale "<<h.second.stale <<endl;
      if(h.second.napa)
        ++napa;
      if(h.second.health)
        ++health;
      if(h.second.stale)
        ++stale;

      if(h.second.napa || h.second.health || h.second.stale)
        ++issue;
    }

    totnapa += napa;
    tothealth += health;
    totstale += stale;
    totissue += issue;
    tothours += sv.second.size();
    cout<< fmt::sprintf("%.2f%% NAPA (%d), ", 100.0*napa/sv.second.size(), napa);
    cout<< fmt::sprintf("%.2f%% stale (%d), ", 100.0*stale/sv.second.size(), stale);
    cout<< fmt::sprintf("%.2f%% unhealthy (%d)\n", 100.0*health/sv.second.size(), health);
  }
  cout<<"All slots: ";
  cout<< fmt::sprintf("%.2f%% NAPA (%d), ", 100.0*totnapa/tothours, totnapa);
  cout<< fmt::sprintf("%.2f%% stale (%d), ", 100.0*totstale/tothours, totstale);
  cout<< fmt::sprintf("%.2f%% unhealthy (%d), ", 100.0*tothealth/tothours, tothealth);
  cout<< fmt::sprintf("%.2f%% issue (%d)\n", 100.0*totissue/tothours, totissue);

  int misnum=0;
  for(const auto& sv : stat) {
    cout<<makeSatPartialName(sv.first)<<": ";
      
    set<time_t> phours = hours;
    for(const auto& h : sv.second) {
      phours.erase(h.first);
    }
    for(const auto& missing : phours) {
      misnum++;
      cout<<" "<<humanTimeShort(missing*3600);
    }
    cout<<endl;
  }
  cout<<"Missing "<<misnum<<" SV-hours of data, or "<< misnum*100/(stat.size()*hours.size())<<"%\n";
  
}
