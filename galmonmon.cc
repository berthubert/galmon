
#include "minicurl.hh"
#include <iostream>
#include "navmon.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include "ext/powerblog/h2o-pp.hh"
#include <variant>

#include "CLI/CLI.hpp"
#include "version.hh"

static char program[]="galmonmon";

using namespace std;

extern const char* g_gitHash;

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
  std::optional<string> reportState(string_view thing, string_view name, var_t state, const std::string& state_text="");
  std::optional<string> getState(string_view thing, string_view name);


  std::optional<string> getPrevState(string_view thing, string_view name);  

  struct State
  {
    var_t state;
    time_t since;
    string text;
  };

  std::optional<State> getFullState(string_view thing, string_view name);
  std::optional<State> getPrevFullState(string_view thing, string_view name);
  
private:
  struct Names
  {
    string offName, onName;
  };
  map<string, Names> names;


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

std::optional<StateKeeper::State> StateKeeper::getFullState(string_view thing, string_view name)
{
  if(states.count((string)thing) && states[(string)thing].count((string) name) && states[(string)thing][(string)name].live) {
    return states[(string)thing][(string)name].live;
  }
  return std::optional<StateKeeper::State>();
}


std::optional<string> StateKeeper::getPrevState(string_view thing, string_view name)
{
  if(states.count((string)thing) && states[(string)thing].count((string) name) && states[(string)thing][(string)name].prev) {
    return getName(name, states[(string)thing][(string)name].prev->state);
  }
  return std::optional<string>();
}

std::optional<StateKeeper::State> StateKeeper::getPrevFullState(string_view thing, string_view name)
{
  if(states.count((string)thing) && states[(string)thing].count((string) name) && states[(string)thing][(string)name].prev) {
    return states[(string)thing][(string)name].prev;
  }
  return std::optional<StateKeeper::State>();
}


std::optional<string> StateKeeper::reportState(string_view thing, string_view name, var_t newstate, const std::string& state_text)
{
  auto& state = states[(string)thing][(string)name];
  std::optional<string> ret;
  
  time_t now = time(0);
  
  if(!state.live) {  // we had no state yet
    state.live = State{newstate, now, state_text};
    state.provisional.reset(); // for good measure
    return ret;
  }
  else if(state.live->state == newstate) {  // confirmation of current state
    state.live->text = state_text; // update text perhaps
    state.provisional.reset();
    return ret;
  }
  else if(!state.provisional) {             // new provisional state
    state.provisional = State{newstate, now, state_text};
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
#if 0
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
#endif 
void sendTweet(const string& tweet)
{
  string etweet = tweet;
  //system((string("twurl -X POST /1.1/statuses/update.json -d \"media_ids=1215649475231997953&status=")+etweet+"\" >> twitter.log").c_str());
  if(system((string("twurl -X POST /1.1/statuses/update.json -d \"status=")+etweet+"\" >> twitter.log").c_str()) < 0) {
    cout<<"Problem tweeting!"<<endl;
  }
  return;
}

int main(int argc, char **argv)
{
  MiniCurl mc;
  MiniCurl::MiniCurlHeaders mch;
  //  string url="https://galmon.eu/svs.json";
  string url="http://[::1]:29599/";
  bool doVERSION{false};

  CLI::App app(program);

  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--url,-u", url, "URL of navparse process to retrieve status from");
  bool doTweet{false};
  app.add_flag("--tweet,-t", doTweet, "Actually send out tweets");

  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  g_sk.setBoolNames("health", "healthy", "unhealthy");
  g_sk.setBoolNames("eph-too-old", "ephemeris fresh", "ephemeris aged");
  g_sk.setBoolNames("silent", "observed", "not observed");

  std::variant<bool, string> tst;

  auto observers = nlohmann::json::parse(mc.getURL(url+"observers.json"));

  
  //  sendTweet("Galmonmon " +string(g_gitHash)+ " started, " + std::to_string(observers.size()) +" observers seen");
  cout<<("Galmonmon " +string(g_gitHash)+ " started, " + std::to_string(observers.size()) +" observers seen")<<endl;

  string meh="🤔";
  string unhappy="😬";
  string alert="🚨";
  
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

      res = mc.getURL(url+"sbas.json");
      j = nlohmann::json::parse(res);
      std::optional<string> sbasHealthChange;
      for(auto iter = j.begin();  iter != j.end(); ++iter)  {
        if(iter.value().count("health")) {
          string name = sbasName(atoi(iter.key().c_str()));
          sbasHealthChange = g_sk.reportState(name, "sbas-health", (string)iter.value()["health"]);
          //          cout<<"Setting state for "<< name <<" to "<< (string)iter.value()["health"] << endl;

          
          if(sbasHealthChange) {
            ostringstream out;
            out<<"✈️ augmentation system "<<name<<" health changed: "<<*sbasHealthChange;
            cout<<out.str()<<endl; 
            if(doTweet)
              sendTweet(out.str());
            
          }
        }
      }
      
      res = mc.getURL(url+"svs.json");
      j = nlohmann::json::parse(res);
      bool first=true;       
      for(const auto& sv : j) {
        if(!sv.count("gnssid") || !sv.count("fullName") || !sv.count("sigid")) {
          cout<<"Skipping "<< sv.count("gnssid") <<", "<< sv.count("fullName") <<", " <<sv.count("sigid") <<endl;
          continue;
        }
        int gnssid = sv["gnssid"], sigid = sv["sigid"];
        string fullName = sv["fullName"];
        
        if(!(gnssid == 2 && sigid==1) &&
           !(gnssid == 0 && sigid==0) &&
           !(gnssid == 3 && sigid==0) &&
           !(gnssid == 6 && sigid==0))
          continue;
      
        int numfresh=0;
        // we only track "received status" for GPS and Galileo
        bool notseen= gnssid ==0 || gnssid==2;
        if(!sv.count("healthissue") || !sv.count("eph-age-m") || !sv.count("sisa") || !sv.count("perrecv")) {
          //          cout<<"Skipping "<<fullName<<" in loop: "<<sv.count("healthissue")<<", "<<sv.count("eph-age-m") << ", "<<sv.count("perrecv")<<endl;
          continue;
        }
        
        for(const auto& recv : sv["perrecv"]) {
          if(!recv.count("last-seen-s")) {
            cout<<"Missing last-seen-s"<<endl;
            continue;
          }
          if((int)recv["last-seen-s"] < 60)
            numfresh++;
          if((int)recv["last-seen-s"] < 3600)
            notseen=false;
          
        }

        auto healthchange = g_sk.reportState(fullName, "health", sv["healthissue"]!=0);
        std::optional<string> tooOldChange;
        if(gnssid == 2)
          tooOldChange = g_sk.reportState(fullName, "eph-too-old", sv["eph-age-m"] > 105, fmt::sprintf("%.2f", (double)sv["eph-age-m"]));
        else 
          tooOldChange = g_sk.reportState(fullName, "eph-too-old", sv["eph-age-m"] > 140, fmt::sprintf("%.2f", (double)sv["eph-age-m"]));
      
        auto seenChange = g_sk.reportState(fullName, "silent", notseen);

        auto sisaChange = g_sk.reportState(fullName, "sisa", (double) sv["sisa-m"], (string)sv["sisa"]);

        double ephdisco = sv.count("latest-disco") ? (double)sv["latest-disco"] : -1.0;
        auto ephdiscochange = g_sk.reportState(fullName, "eph-disco", ephdisco);
        if(ephdisco == -1.0)
          ephdiscochange.reset();
        
        double timedisco = sv.count("time-disco") ? fabs((double)sv["time-disco"]) : 0.0;
        auto timediscochange = g_sk.reportState(fullName, "time-disco", timedisco);
        
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

        if(ephdiscochange) {
          cout<<fullName <<": ephemeris (orbit description) discontinuity of "<< fmt::sprintf("%.02f", ephdisco)<<" meters"<<endl;
        }
        if(timediscochange) {
          cout<<fullName <<": clock jump of "<< fmt::sprintf("%.02f", timedisco)<<" nanoseconds (= " <<fmt::sprintf("%.01f meters)", timedisco/2.99)<<endl;
        }
        */
        ostringstream out;

          
        if(healthchange)
          out<< *healthchange<<" ";
        if(tooOldChange) {
          out<< "Ephemeris age: "<<*tooOldChange<<", new value: "<< fmt::sprintf("%.02f", (double)sv["eph-age-m"])<<" minutes, old: ";
          out<< g_sk.getPrevFullState(fullName, "eph-too-old")->text <<" minutes";
        }
        if(seenChange)
          out<< *seenChange<<" ";

        if(ephdiscochange && (gnssid ==0 || gnssid == 2) && ephdisco > 1.45) {
          if(ephdisco > 10)
            out<<alert;
          else if(ephdisco > 5)
            out<<unhappy;
          else
            out<<meh;

          out<<" ephemeris (orbit description) discontinuity of "<< fmt::sprintf("%.02f", ephdisco)<<" meters"<<endl;
        }
        if(timediscochange && (gnssid == 2 && timedisco > 2.5)) {
          if(timedisco > 10)
            out<<alert;
          else if(timedisco > 5)
            out<<unhappy;
          else
            out<<meh;
          out<<" clock jump of "<< fmt::sprintf("%.02f", timedisco)<<" nanoseconds  (= " <<fmt::sprintf("%.01f meters)", timedisco/2.99)<<endl;
        }

        if(sisaChange) {
          ostringstream tmp;
          auto state = g_sk.getFullState(fullName, "sisa");
          auto prevState = g_sk.getPrevFullState(fullName, "sisa");
          tmp<< " SISA/URA reported ranging accuracy changed, new: "<<state->text<<", old: " << prevState->text;
          if(get<double>(state->state) > 3 || get<double>(prevState->state) > 3)
            out << tmp.str();
          else
            cout<<"Not reporting: "<<tmp.str()<<endl;
        }

        string tweet;
        if(!out.str().empty()) {
          if(gnssid == 0)
            tweet = "GPS";
          else if(gnssid == 2)
            tweet = "Galileo";
          else if(gnssid == 3)
            tweet ="BeiDou";
          else if(gnssid== 6)
            tweet = "GLONASS";
          tweet += " " + fullName +": ";
          tweet += out.str();


          if(doTweet)
            sendTweet(tweet);
          if(first)
            cout<<"\n";
          first=false;
          cout<<humanTimeNow() <<" " << tweet << endl;
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
