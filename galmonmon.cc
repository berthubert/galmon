
#include "minicurl.hh"
#include <iostream>
#include "navmon.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include "ext/powerblog/h2o-pp.hh"
#include <variant>
#include "githash.h"
#include "CLI/CLI.hpp"
#include "version.hh"

static char program[]="galmonmon";

using namespace std;

extern const char* g_gitHash;

void showVersion()
{
    _showVersion(program,g_gitHash)
}

/*
  Monitoring the satellites for sensible alerts.

  A satellite transitions state if:
  * galmon is up
  * galmon is up to date
  * We are seeing recent messages for the satellite
  * For more than 60 seconds this state is different than it was

  A satellite becomes 'unobserved' if:
  * galmon is up
  * galmon is up to date
  * We haven't seen a message since an hour

  A satellite becomes observed again if we have seen it for > 60 seconds.

At startup, every satellite assumes, without alert, the status we find for it, which is not a transition.

*/


class StateKeeper
{
public:
  typedef std::variant<bool, double, string> var_t;
  void setBoolNames(string_view name, string_view offName, string_view onName);
  std::optional<string> reportState(string_view thing, string_view name, var_t state, time_t now=0);
  std::optional<string> getState(string_view thing, string_view name);
  std::optional<string> getPrevState(string_view thing, string_view name);  

private:
  struct Names
  {
    string offName, onName;
  };
  map<string, Names> names;

  struct State
  {
    var_t state;
    time_t since;
  };

  struct ThingState
  {
    std::optional<State> prev, live, provisional;
  };

  map<string, map<string, ThingState> > states;
private:
  std::string getName(string_view name, var_t state)
  {
    if(auto ptr = std::get_if<bool>(&state)) {
      if(*ptr) {
        return names[(string)name].onName;
      }
      else
        return names[(string)name].offName;
    }
    if(auto ptr = std::get_if<string>(&state)) {
      return *ptr;
    }
    if(auto ptr = std::get_if<double>(&state)) {
      return to_string(*ptr);
    }
    return "?";
  }
};

void StateKeeper::setBoolNames(string_view name, string_view offName, string_view onName)
{
  names[(string)name].offName = offName;
  names[(string)name].onName = onName;
}

std::optional<string> StateKeeper::getState(string_view thing, string_view name)
{
  if(states.count((string)thing) && states[(string)thing].count((string) name) && states[(string)thing][(string)name].live) {
    return getName(name, states[(string)thing][(string)name].live->state);
  }
  return std::optional<string>();
}

std::optional<string> StateKeeper::getPrevState(string_view thing, string_view name)
{
  if(states.count((string)thing) && states[(string)thing].count((string) name) && states[(string)thing][(string)name].prev) {
    return getName(name, states[(string)thing][(string)name].prev->state);
  }
  return std::optional<string>();
}


std::optional<string> StateKeeper::reportState(string_view thing, string_view name, var_t newstate, time_t now)
{
  auto& state = states[(string)thing][(string)name];
  std::optional<string> ret;
  
  if(!now)
    now = time(0);
  
  if(!state.live) {  // we had no state yet
    state.live = State{newstate, now};
    state.provisional.reset(); // for good measure
    return ret;
  }
  else if(state.live->state == newstate) {  // confirmation of current state
    state.provisional.reset();
    return ret;
  }
  else if(!state.provisional) {             // new provisional state
    state.provisional = State{newstate, now};
    return ret;
  }
  else {                                         
    if(now - state.provisional->since > 60) { // provisional state has been confirmed
      state.prev = state.live;
      state.live = state.provisional;
      state.provisional.reset();
      return getName(name, state.live->state);
    }
  }
  return ret;
}


StateKeeper g_sk;

static std::string string_replace(const std::string& str, const std::string& match, 
        const std::string& replacement, unsigned int max_replacements = UINT_MAX)
{
    size_t pos = 0;
    std::string newstr = str;
    unsigned int replacements = 0;
    while ((pos = newstr.find(match, pos)) != std::string::npos
            && replacements < max_replacements)
    {
         newstr.replace(pos, match.length(), replacement);
         pos += replacement.length();
         replacements++;
    }
    return newstr;
}

void sendTweet(const string& tweet)
{
  string etweet = string_replace(tweet, "+", "%2b");
  system((string("twurl -X POST \"/1.1/statuses/update.json?status=")+etweet+"\" >> twitter.log").c_str());
}

int main(int argc, char **argv)
{
  MiniCurl mc;
  MiniCurl::MiniCurlHeaders mch;
  //  string url="https://galmon.eu/svs.json";
  string url="http://[::1]:10000/";
  bool doVERSION{false};

  CLI::App app(program);

  app.add_flag("--version", doVERSION, "show program version and copyright");

  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion();
    exit(0);
  }

  g_sk.setBoolNames("health", "healthy", "unhealthy");
  g_sk.setBoolNames("eph-too-old", "ephemeris fresh", "ephemeris too old");
  g_sk.setBoolNames("silent", "observed", "not observed");

  std::variant<bool, string> tst;

  auto observers = nlohmann::json::parse(mc.getURL(url+"observers.json"));
  
  cout<<("Galmonmon " +string(g_gitHash)+ " started, " + std::to_string(observers.size()) +" observers seen")<<endl;
  
  for(;;) {
    try {
      auto res = mc.getURL(url+"global.json");
      auto j = nlohmann::json::parse(res);
      if(!j.count("last-seen") || time(0) - (long) j["last-seen"] > 30) {
        cout<<"Galmon at "<<url<<" is not current"<<endl;
        sleep(10);
        continue;
      }
      /*
      if(auto iter = j.find("last-seen"); iter != j.end()) {
        cout<<"Galmon behind by " << (time(0) - (long) iter.value()) <<" seconds"<<endl;
      }
      */
      
      
      res = mc.getURL(url+"svs.json");
      j = nlohmann::json::parse(res);
      bool first=true;       
      for(const auto& sv : j) {

        int gnssid = sv["gnssid"];
        string fullName = sv["fullName"];
        
        if(!(gnssid == 2 && sv["sigid"]==1) &&
           !(gnssid == 0 && sv["sigid"]==0) &&
           !(gnssid == 3 && sv["sigid"]==0) &&
           !(gnssid == 6 && sv["sigid"]==0))
          continue;
      
        int numfresh=0;
        // we only track "received status" for GPS and Galileo
        bool notseen= gnssid ==0 || gnssid==2;
        if(!sv.count("sisa") && !sv.count("eph-age-m"))
          continue;
        if(sv.count("perrecv")) {
          for(const auto& recv : sv["perrecv"]) {
            if((int)recv["last-seen-s"] < 60)
              numfresh++;
            if((int)recv["last-seen-s"] < 3600)
              notseen=false;
            
          }
        }
      
        auto healthchange = g_sk.reportState(fullName, "health", sv["healthissue"]!=0);
        std::optional<string> tooOldChange;
        if(gnssid == 2)
          tooOldChange = g_sk.reportState(fullName, "eph-too-old", sv["eph-age-m"] > 120);
        else 
          tooOldChange = g_sk.reportState(fullName, "eph-too-old", sv["eph-age-m"] > 140);
      
        auto seenChange = g_sk.reportState(fullName, "silent", notseen);

        auto sisaChange = g_sk.reportState(fullName, "sisa", (string)sv["sisa"]);
        
        /*
          cout<<fullName<<": numfresh "<<numfresh << " healthissue "<<sv["healthissue"];
          cout<<" eph-age-m "<< fmt::sprintf("%.2f", (double)sv["eph-age-m"]);
          cout<<" not-seen "<<notseen;
          if(auto val = g_sk.getState(fullName, "eph-too-old"); val) {
          cout<<" eph-too-old \""<<*val<<"\"";
          }
          if(auto val = g_sk.getState(fullName, "health"); val) {
          cout<<" health \""<<*val<<"\"";
          }
        */
        if(healthchange || tooOldChange || seenChange || sisaChange) {
          if(first)
            cout<<"\n";
          first=false;
          ostringstream out;

          if(gnssid == 0)
            out<<"GPS";
          else if(gnssid == 2)
            out<<"Galileo";
          else if(gnssid == 3)
            out<<"BeiDou";
          else if(gnssid== 6)
            out<<"GLONASS";
          out<<" "<<fullName<<": ";
          
          if(healthchange)
            out<< *healthchange<<" ";
          if(tooOldChange) {
            out<< *tooOldChange<<", new value: "<< fmt::sprintf("%.02f", (double)sv["eph-age-m"])<<" minutes, old: ";
            out<< *g_sk.getPrevState(fullName, "eph-too-old");
          }
          if(seenChange)
            out<< *seenChange<<" ";
          if(sisaChange) {
            out<< "SISA/URA reported ranging accuracy new: "<<*sisaChange<<", old: " << *g_sk.getPrevState(fullName, "sisa");
          }
          if(out.str().find("200 cm") == string::npos || out.str().find("282 cm") == string::npos)
            sendTweet("TESTING: "+out.str());
          cout<<humanTimeNow()<<" CHANGE ";
          cout<<out.str()<<endl;
          
        }
      }
      cout<<".";
      cout.flush();
    }
    catch(std::exception& e) {
      cerr<<"\nError: "<<e.what()<<endl;
    }
    sleep(20);
  }
  
}
